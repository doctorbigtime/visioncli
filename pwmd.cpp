#include "libvision.h"
#include <vector>
#include <map>
#include <cmath>
#include <cstring>
#include <functional>
#include <fstream>
#include <boost/filesystem.hpp>

#include <time.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>

namespace fs = boost::filesystem;
using boost::system::system_error;
using boost::system::system_category;

struct Log
{
    Log()
    {
        ::openlog("pwmd", LOG_CONS|LOG_NDELAY, LOG_DAEMON);
    }
    ~Log()
    {
        ::closelog();
    }
    struct EOM {};
    static EOM endl;
    #define LOGGER(Name, Level) \
    struct Name { \
        ~Name() { \
            flush(); \
        } \
        template <typename T> \
        Name& operator<<(T const& t) { \
            os_ << t; \
        } \
        void operator<<(Log::EOM) { \
            flush(); \
        } \
        void flush() { \
            if(os_.str().size()) ::syslog(Level, "%s", os_.str().c_str()); \
        } \
        std::ostringstream os_; \
    }; \

    LOGGER(debug, LOG_DEBUG)
    LOGGER(warn, LOG_WARNING)
    LOGGER(error, LOG_ERR)
    LOGGER(info, LOG_INFO)
};


struct PwmChannel
{
    using pwm_fun = std::function<int(double)>;

    static int fan_curve(double temp)
    {
        // some discontinuous values
        if(temp < 15) return 0;   //< off.
        if(temp < 30) return 40;  //< silent.
        if(temp > 42) return 255; //< full blast.

        // custom fit quadratic.
        auto pwm_pct = std::min(100.0, 58.94545 - 7.49697*temp + 0.2020202*(std::pow(temp,2)));
        //auto pwm_pct = 1.859406-0.4977863*temp+0.06597786*(std::pow(temp,2));
        Log::debug() << "fan - temp: " <<  temp << " -> pwm_pct: " << pwm_pct 
                     << " pwm: " << static_cast<int>(pwm_pct * 2.55) ;
        return static_cast<int>(pwm_pct * 2.55);
    }

    static int pump_curve(double temp)
    {
        if(temp < 35) return 76; //< always at least running 'silent'
        auto pwm_pct = std::min(100.0, 725.4167 - 43.92045*temp + 0.6912879*(std::pow(temp,2)));
        Log::debug() << "pump - temp: " <<  temp << " -> pwm_pct: " << pwm_pct 
                     << " pwm: " << static_cast<int>(pwm_pct * 2.55) ;
        return static_cast<int>(pwm_pct * 2.55);
    }

    // TODO: parameterize
    static pwm_fun getCurve(std::string const& name)
    {
        static std::map<std::string, pwm_fun> m = {
            std::make_pair("pump", &PwmChannel::pump_curve),
            std::make_pair("fan", &PwmChannel::fan_curve),
        };
        if(m.count(name) == 0)
            throw std::runtime_error("Can't find appropriate fan curve.");
        return m[name];
    }

    PwmChannel(std::string const& name, std::string const& type)
        : name(name)
        , type(type)
        , lastVal(-1)
    {
        fs::path hwmon("/sys/class/hwmon/hwmon0");
        auto path = hwmon / name;
        if(!fs::exists(path))
        {
            Log::error() << "Unknown pwm channel: " << name;
            throw std::runtime_error("Bad input");
        }
        fd = ::open(path.c_str(), O_WRONLY);
        if(0 > fd)
            throw system_error(errno, system_category(), "open(" + std::string{path.c_str()} + ")");
        enableManualControl(hwmon/name);
        pwmCurve = PwmChannel::getCurve(type);
    }
    ~PwmChannel()
    {
        ::close(fd);
    }
    void setTemperature(double temp)
    {
        setPwmVal(pwmCurve(temp));
    }
    void enableManualControl(fs::path path)
    {
        path += "_enable";
        auto f = ::open(path.c_str(), O_WRONLY);
        if(0 > f)
            throw system_error(errno, system_category(), "open(" + std::string{path.c_str()} + ")");
        if(2 != ::write(f, "1", 2))
            throw system_error(errno, system_category(), "write(" + std::string{path.c_str()} + ")");
        ::close(f);
    }
    void setPwmVal(int val)
    {
        if(lastVal != val)
        {
            std::string strval = std::to_string(val);
            Log::info() << name << "::write(" << fd << ", \"" << strval << "\", "
                << strval.size()+1
                << ");";
            if(0 != ::lseek(fd, 0, SEEK_SET))
                Log::error() << "Failed to seek to beginning of file on FD: " << fd << " - " << strerror(errno);
            if(strval.size()+1 != ::write(fd, strval.c_str(), strval.size()+1))
                Log::error() << "Failed to write to FD: " << fd << " - " << strerror(errno);
        }
        lastVal = val;
    }
    std::string name, type;
    int fd;
    int lastVal;
    pwm_fun pwmCurve;
};


struct Daemon
{
    Daemon(int argc, char** argv)
        : vision(Vision::findAndCreate())
        , lastTemp(-1)
        , log()
    {
        if(::geteuid())
        {
            std::cout << "Warning: Not starting as root"
                      << ", it is likely that this will fail." 
                      << std::endl;
        }
        if(!vision)
            throw std::runtime_error("Could not find VISION device!");
        Log::info() << "Found device: " << vision->describe();
        while(--argc)
        {
            std::string channel_type = *++argv;
            std::string channel = channel_type;
            std::string type = "fan";
            auto equal = channel_type.find_first_of('=');
            if(equal != std::string::npos)
            {
                type = channel_type.substr(equal+1);
                channel = channel_type.substr(0, equal);
            }
            pwm_channels.push_back(std::make_shared<PwmChannel>(channel, type));
            Log::info() << "Opened channel " << pwm_channels.back()->name
                << " on FD: " << pwm_channels.back()->fd;
        }
        if(pwm_channels.empty())
        {
            throw std::runtime_error("Error: No PWM channels given.");
        }
        daemonize();
    }

    static void signal_handler(int signal)
    {
        Log::info() << "Caught signal " << ::strsignal(signal);
        Daemon::exiting = true;
    }

    void daemonize()
    {
        pid_t child = ::fork();
        if(0 > child)
            throw system_error(errno, system_category(), "fork()");
        if(child > 0)
            ::exit(0);
        ::umask(0);
        ::chdir("/");
        ::close(0);
        ::close(1);
        ::close(2);
        {
            std::ofstream ofs("/var/run/pwmd.pid");
            if(!ofs.good())
                throw system_error(errno, system_category(), "open(/var/run/pwmd.pid)");
            ofs << ::getpid() << std::endl;
        }
        ::signal(SIGHUP, &Daemon::signal_handler);
        ::signal(SIGINT, &Daemon::signal_handler);
        ::signal(SIGTERM, &Daemon::signal_handler);
    }

    ~Daemon()
    {
        delete vision;
    }

    void run()
    {
        do
        {
            time_t now = ::time(nullptr);
            auto report = vision->getInputReport();
            auto ir = reinterpret_cast<InputReport_v1 const*>(&report->front());
            auto temp = ir->temp(4);
            //Log::debug() << now << " - polled: " << temp;
            auto diff = temp - lastTemp;
            if(diff)
            {
                if(std::abs(diff) > 0.5 && lastTemp > 0)
                {
                    Log::warn() << "Temperature jump > 0.5 degrees (" << lastTemp 
                                << " -> " << temp << ")";
                    //goto pause;
                }
                for(auto& channel : pwm_channels)
                    channel->setTemperature(temp);
                lastTemp = temp;
            }
            pause:
            ::sleep(1);
        } while(!Daemon::exiting);
    }
    static bool exiting;
    Vision* vision;
    double lastTemp;
    std::vector<std::shared_ptr<PwmChannel>> pwm_channels;
    Log log;
};

bool Daemon::exiting = false;


auto main(int argc, char**argv) -> int
{
    try
    {
        Daemon pwmd{argc, argv};
        pwmd.run();
    }
    catch(std::exception const& e)
    {
        std::cerr << "Failed to start daemon: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
