#include <pthread.h>
#include <errno.h>

#include <endian.h>
#ifndef le32toh
#if  __BYTE_ORDER == __LITTLE_ENDIAN
#define le32toh(x) (x)
#define htole32(x) (x)
#else
#include <byteswap.h>
#define le32toh(x) __bswap_32 (x)
#define htole32(x) __bswap_32 (x)
#endif
#endif

#include "sysfs.h"
#include "toscaMap.h"
#include "toscaReg.h"

#define TOSCA_DEBUG_NAME toscaReg
#include "toscaDebug.h"

unsigned int toscaCsrRead(unsigned int address)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_CSR, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    return le32toh(*ptr);
}

unsigned int toscaCsrWrite(unsigned int address, unsigned int value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_CSR, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    *ptr = htole32(value);
    return le32toh(*ptr);
}

unsigned int toscaCsrSet(unsigned int address, unsigned int value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_CSR, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    __sync_fetch_and_or(ptr, htole32(value));
    return le32toh(*ptr);
}

unsigned int toscaCsrClear(unsigned int address, unsigned int value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_CSR, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    return __sync_fetch_and_and(ptr, ~htole32(value));
}

unsigned int toscaIoRead(unsigned int address)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_IO, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    return le32toh(*ptr);
}

unsigned int toscaIoWrite(unsigned int address, unsigned int value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_IO, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    *ptr = htole32(value);
    return le32toh(*ptr);
}

unsigned int toscaIoSet(unsigned int address, unsigned int value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_IO, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    __sync_fetch_and_or(ptr, htole32(value));
    return le32toh(*ptr);
}

unsigned int toscaIoClear(unsigned int address, unsigned int value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_IO, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    __sync_fetch_and_and(ptr, ~htole32(value));
    return le32toh(*ptr);
}

pthread_mutex_t smon_mutex = PTHREAD_MUTEX_INITIALIZER;
#define CSR_SMON 0x40

unsigned int toscaSmonRead(unsigned int address)
{
    unsigned int value;
    volatile uint32_t* smon = toscaMap((address & 0xffff0000)|TOSCA_CSR, CSR_SMON, 12, 0);
    if (!smon) return -1;
    address &= 0xffff;
    if (address >= 0x80) { errno = EINVAL; return -1; }
    pthread_mutex_lock(&smon_mutex);
    smon[0] = htole32(address);
    (void) smon[0]; /* read back to flush write */
    /* check status 0x48 here ? */
    value = le32toh(smon[1]);
    pthread_mutex_unlock(&smon_mutex);
    return value;
}

unsigned int toscaSmonWrite(unsigned int address, unsigned int value)
{
    volatile uint32_t* smon = toscaMap((address & 0xffff0000)|TOSCA_CSR, CSR_SMON, 12, 0);
    if (!smon) return -1;
    address &= 0xffff;
    if (address < 0x40) { errno = EACCES; return -1; }
    if (address >= 0x80) { errno = EINVAL; return -1; }
    pthread_mutex_lock(&smon_mutex);
    smon[0] = htole32(address);
    (void) smon[0]; /* read back to flush write */
    /* check status 0x48 here ? */
    smon[1] = htole32(value);
    value = le32toh(smon[1]);
    pthread_mutex_unlock(&smon_mutex);
    return value;
}

unsigned int toscaSmonStatus()
{
    volatile uint32_t* smon = toscaMap(TOSCA_IO, CSR_SMON, 12, 0);
    return le32toh(smon[3]);
}

#define CSR_VMEERR 0x418

toscaMapVmeErr_t toscaGetVmeErr(unsigned int card)
{
    volatile uint32_t* vmeerr = toscaMap((card<<16)|TOSCA_CSR, CSR_VMEERR, 8, 0);
    if (!vmeerr) return (toscaMapVmeErr_t) { .address = -1 };
    return (toscaMapVmeErr_t) { .address = vmeerr[0], {.status = vmeerr[1]} };
}

const char* toscaPonAddrToRegname(unsigned int address)
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

unsigned int toscaPonRead(unsigned int address)
{
    debug("address=0x%02x", address);
    int fd = toscaPonFd(address);
    if (fd < 0) return -1;
    return sysfsReadULong(fd);
}

unsigned int toscaPonWrite(unsigned int address, unsigned int value)
{
    debug("address=0x%02x value=0x%x", address, value);
    int fd = toscaPonFd(address);
    if (fd < 0) return -1;
    sysfsWrite(fd, "%x", value);
    return sysfsReadULong(fd);
}
