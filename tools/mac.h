#pragma once
#include <net/if.h>
#include <netinet/in.h>
#include <string>
#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>

inline char* getMac(std::string s)
{
    static struct ifreq ifr;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, s.c_str(), IFNAMSIZ - 1);

    ioctl(fd, SIOCGIFHWADDR, &ifr);
    close(fd);
    return (char*)ifr.ifr_hwaddr.sa_data;
}
