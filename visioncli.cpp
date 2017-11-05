#include "libvision.h"

auto main(int argc, char**argv) -> int
{
    char c;
    bool verbose = false;
    int sensor_index = -1;

    while((c = getopt(argc, argv, "vt:")) != EOF)
    {
        switch(c)
        {
            case 'v':
                verbose = true;
                break;
            case 't':
                if(::strcmp(optarg,"all") == 0)
                    sensor_index = -1;
                else sensor_index = atoi(optarg);
                break;
            default:
                break;
        }
    }

    Vision* vision = Vision::findAndCreate();
    if(!vision)
    {
        std::cerr << "Could not find a vision device." << std::endl;    
        return -1;
    }

    if(verbose)
        std::cout << vision->describe() << std::endl;
    auto report = vision->getInputReport();
    auto ir = reinterpret_cast<InputReport_v1*>(&report->front());
    if(0 > sensor_index || verbose)
    {
        for(unsigned int i = 0; i < 5; ++i)
            std::cout << "Temp [" << i << "]: " << ir->temp(i) << std::endl;
    }
    else std::cout << ir->temp(sensor_index) << std::endl;

    if(verbose)
        ::write(2, &report->front(), report->size());
    return 0;
}
