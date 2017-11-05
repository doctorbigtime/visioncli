#include <string>
#include <iostream>
#include <stdexcept>
#include <iomanip>
#include <vector>

#include <boost/circular_buffer.hpp>

#include <cassert>
#include <cstring>
#include <cerrno>

#include <linux/hiddev.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <dirent.h>


#define check(x) \
    if(0 > x) { \
        std::cout << #x << " failed: " << std::strerror(errno) << std::endl; \
        abort(); \
    }


template <typename T> 
std::string hexprint(T const& t)
{
    char nibs[] = {'0', '1', '2', '3', '4', '5', '6', '7'
                 , '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    std::ostringstream oss;
    auto p = reinterpret_cast<unsigned char const*>(&t);
    oss << "0x";
    for(auto i = 0; i < sizeof(T); ++i, ++p)
    {
        oss << nibs[(*p & 0xF0) >> 4];
        oss << nibs[(*p & 0xF)];
    }
    return oss.str();
}


struct Dir
{
    Dir(std::string const& path)
    {
        dp = ::opendir(path.c_str());
        if(NULL == dp)
            throw std::runtime_error("Could not open dir.");
    }
    template <typename Fun> 
    void apply(Fun&& fun)
    {
        dirent* p;
        while(p = ::readdir(dp))
        {
            if(!fun(p->d_type, std::string{p->d_name}))
                break;
        }
        ::closedir(dp);
    }
    DIR* dp;
};


#pragma push(pack, 1)
struct InputReport_v1
{
    char unknown_[0x34];
    unsigned short temps[5];
    char unknown__[2+0xd0];

    double temp(size_t index) const
    {
        assert(index < 5);
        return temps[index] / 100.0;
    }
};
#pragma pop(pack)


struct Vision
{
    Vision(int fd)
        : fd_(fd)
        , reports_(256)
    {
        hiddev_string_descriptor hdesc;
        hdesc.index = 1;
        check(::ioctl(fd, HIDIOCGSTRING, &hdesc));
        vendor_ = hdesc.value;
        hdesc.index = 2;
        check(::ioctl(fd, HIDIOCGSTRING, &hdesc));
        product_ = hdesc.value;
    }
    ~Vision() { ::close(fd_); }

    std::string describe() const
    {
        return vendor_ + " - " + product_;
    }

    static int constexpr ACV_VENDOR = 0xc70;
    static int constexpr ACV_PRODUCT = 0xf00c;

    static int is_vision(int fd)
    {
        hiddev_devinfo devinfo;
        if(0 > ::ioctl(fd, HIDIOCGDEVINFO, &devinfo))
        {
            return false;
        }
        if(devinfo.vendor == ACV_VENDOR && (devinfo.product&0xffff) == ACV_PRODUCT)
        {
            return true;
        }
        return false;
    }

    static Vision* findAndCreate()
    {
        int fd;
        std::string devusb = "/dev/usb";
        Dir dir(devusb);

        std::string path;
        dir.apply([&](int type, std::string const& name) -> bool {
            if(type != DT_CHR || name.substr(0, 6) != "hiddev")
                return true; //< continue
            path = devusb + '/' + name;
            fd = ::open(path.c_str(), O_RDONLY);
            if(0 > fd)
                return true; //< continue
            if(is_vision(fd)) 
                return false; //< break;
            // Not a VISION.
            ::close(fd);
            fd = -1;
            return true; //< continue;
        });
        if(fd != -1)
            return new Vision(fd);
        return nullptr;
    }

    using ReportBuffer = std::vector<char>;
    ReportBuffer* getInputReport()
    {
        hiddev_report_info rinfo;
        rinfo.report_type = HID_REPORT_TYPE_INPUT;
        rinfo.report_id   = 1;
        check(::ioctl(fd_, HIDIOCGREPORTINFO, &rinfo));

        ReportBuffer b;

        hiddev_field_info finfo;
        finfo.report_type = rinfo.report_type;
        finfo.report_id   = rinfo.report_id;
        finfo.field_index = 0;
        check(::ioctl(fd_, HIDIOCGFIELDINFO, &finfo));
        b.push_back(rinfo.report_id);

        for(auto j = 0; j < finfo.maxusage; ++j)
        {
            hiddev_usage_ref uref;
            std::memset(&uref, 0, sizeof(uref));
            uref.report_type = finfo.report_type;
            uref.report_id   = rinfo.report_id;
            uref.field_index = 0;
            uref.usage_index = j;
            check(::ioctl(fd_, HIDIOCGUCODE, &uref));
            check(::ioctl(fd_, HIDIOCGUSAGE, &uref));
            b.push_back(uref.value);
        }
        reports_.push_back(b);
        return &reports_.back();
    }

    int fd_;
    std::string vendor_, product_;
    boost::circular_buffer<ReportBuffer> reports_;
};

