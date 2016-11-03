#include <stdlib.h>
#include <errno.h>

#include <sysfs.h>

#include "toscaPon.h"

#define TOSCA_DEBUG_NAME toscaPon
#include "toscaDebug.h"

const char* toscaPonAddrToRegname(int address)
{
    switch (address)
    {
        case 0x00: return "vendor";
        case 0x04: return "static_options";
        case 0x08: return "vmectl";
        case 0x0c: return "mezzanine";
        case 0x10: return "general";
        case 0x14: return "pciectl";
        case 0x18: return "user";
        case 0x1c: return "signature";
        case 0x20: return "cfgctl";
        case 0x24: return "cfgdata";
        case 0x40: return "bmrctl";
        default: return "unknown";
    }
};

int toscaPonFd(unsigned int address)
{
    static int fd[11] = {0};
    int reg;
    
    address &= ~3;
    if (address >= 0x28 && address != 0x40)
    {
        debug("address=0x%x -- not implemented", address);
        errno = EINVAL;
        return -1;
    }
    if (address == 40) reg = 10;
    else reg = address>>2;
    debug("address=0x%02x regname=%s", address, toscaPonAddrToRegname(address));
    if (!fd[reg])
    {
        char filename[50];
        sprintf(filename, "/sys/devices/*localbus/*.pon/%s", toscaPonAddrToRegname(address));
        fd[reg] = sysfsOpen(filename);
    }
    return fd[reg];
}

int toscaPonRead(int address)
{
    debug("address=0x%02x", address);
    int fd = toscaPonFd(address);
    if (fd < 0) return -1;
    return sysfsReadULong(fd);
}

int toscaPonWrite(int address, int value)
{
    debug("address=0x%02x value=0x%x", address, value);
    int fd = toscaPonFd(address);
    if (fd < 0) return -1;
    return sysfsWrite(fd, "%x", value);
}
