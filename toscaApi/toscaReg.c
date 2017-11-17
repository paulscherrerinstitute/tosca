#include <pthread.h>
#include <errno.h>

#include <endian.h>
#ifndef le32toh
#if  __BYTE_ORDER == __LITTLE_ENDIAN
#define le32toh(x) (x)
#define htole32(x) (x)
#else
#include <byteswap.h>
#define le32toh(x) __bswap_32(x)
#define htole32(x) __bswap_32(x)
#endif
#endif

#include "sysfs.h"
#include "toscaMap.h"
#include "toscaReg.h"

#define TOSCA_DEBUG_NAME toscaReg
#include "toscaDebug.h"

#if __GNUC__ * 100 + __GNUC_MINOR__ < 401
/* We have no atomic read-modify-write commands before GCC 4.1 */
pthread_mutex_t csr_mutex = PTHREAD_MUTEX_INITIALIZER;
#define __sync_fetch_and_or(p,v)  pthread_mutex_lock(&csr_mutex); *p |= v; pthread_mutex_unlock(&csr_mutex)
#define __sync_fetch_and_and(p,v) pthread_mutex_lock(&csr_mutex); *p &= v; pthread_mutex_unlock(&csr_mutex)
#endif

unsigned int toscaCsrRead(unsigned int address)
{
    return toscaRead((address & 0xffff0000) | TOSCA_CSR, (address & 0xffff));
}

unsigned int toscaCsrWrite(unsigned int address, unsigned int value)
{
    return toscaWrite((address & 0xffff0000) | TOSCA_CSR, (address & 0xffff), value);
}

unsigned int toscaCsrSet(unsigned int address, unsigned int bitsToSet)
{
    return toscaSet((address & 0xffff0000) | TOSCA_CSR, (address & 0xffff), bitsToSet);
}

unsigned int toscaCsrClear(unsigned int address, unsigned int bitsToClear)
{
    return toscaClear((address & 0xffff0000) | TOSCA_CSR, (address & 0xffff), bitsToClear);
}

unsigned int toscaIoRead(unsigned int address)
{
    return toscaRead((address & 0xffff0000) | TOSCA_IO, (address & 0xffff));
}

unsigned int toscaIoWrite(unsigned int address, unsigned int value)
{
    return toscaWrite((address & 0xffff0000) | TOSCA_IO, (address & 0xffff), value);
}

unsigned int toscaIoSet(unsigned int address, unsigned int bitsToSet)
{
    return toscaSet((address & 0xffff0000) | TOSCA_IO, (address & 0xffff), bitsToSet);
}

unsigned int toscaIoClear(unsigned int address, unsigned int bitsToClear)
{
    return toscaClear((address & 0xffff0000) | TOSCA_IO, (address & 0xffff), bitsToClear);
}

unsigned int toscaRead(unsigned int addrspace, unsigned int address)
{
    errno = 0;
    volatile uint32_t* ptr = toscaMap(addrspace, address, 4, 0);
    debug("address=0x%02x ptr=%p", address, ptr);
    if (!ptr) return (unsigned int)-1;
    return le32toh(*ptr);
}

unsigned int toscaWrite(unsigned int addrspace, unsigned int address, unsigned int value)
{
    errno = 0;
    volatile uint32_t* ptr = toscaMap(addrspace, address, 4, 0);
    debug("address=0x%02x value=0x%x ptr=%p", address, value, ptr);
    if (!ptr) return (unsigned int)-1;
    *ptr = htole32(value);
    return le32toh(*ptr);
}

unsigned int toscaSet(unsigned int addrspace, unsigned int address, unsigned int bitsToSet)
{
    errno = 0;
    volatile uint32_t* ptr = toscaMap(addrspace, address, 4, 0);
    debug("address=0x%02x bitsToSet=0x%x ptr=%p", address, bitsToSet, ptr);
    if (!ptr) return (unsigned int)-1;
    __sync_fetch_and_or(ptr, htole32(bitsToSet));
    return le32toh(*ptr);
}

unsigned int toscaClear(unsigned int addrspace, unsigned int address, unsigned int bitsToClear)
{
    errno = 0;
    volatile uint32_t* ptr = toscaMap(addrspace, address, 4, 0);
    debug("address=0x%02x bitsToClear=0x%x ptr=%p", address, bitsToClear, ptr);
    if (!ptr) return (unsigned int)-1;
    __sync_fetch_and_and(ptr, ~htole32(bitsToClear));
    return le32toh(*ptr);
}


/* Access to Virtex-6 System Monitor via toscaCsr */

pthread_mutex_t smon_mutex = PTHREAD_MUTEX_INITIALIZER;
#define CSR_SMON_REG 0x40

unsigned int toscaSmonRead(unsigned int address)
{
    errno = 0;
    unsigned int value;
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_CSR, CSR_SMON_REG, 12, 0);
    debug("address=0x%02x ptr=%p", address, ptr);
    if (!ptr) return (unsigned int)-1;
    address &= 0xffff;
    if (address >= 0x80) { errno = EINVAL; return (unsigned int)-1; }
    pthread_mutex_lock(&smon_mutex);
    ptr[0] = htole32(address);
    (void) ptr[0]; /* read back to flush write */
    /* check status 0x48 here ? */
    value = le32toh(ptr[1]);
    pthread_mutex_unlock(&smon_mutex);
    return value;
}

unsigned int toscaSmonWriteMasked(unsigned int address, unsigned int mask, unsigned int value)
{
    errno = 0;
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_CSR, CSR_SMON_REG, 12, 0);
    debug("address=0x%02x mask=0x%x value=0x%x ptr=%p", address, mask, value, ptr);
    if (!ptr) return (unsigned int)-1;
    address &= 0xffff;
    if (address < 0x40) { errno = EACCES; return (unsigned int)-1; }
    if (address >= 0x80) { errno = EINVAL; return (unsigned int)-1; }
    pthread_mutex_lock(&smon_mutex);
    ptr[0] = htole32(address);
    (void) ptr[0]; /* read back to flush write */
    /* check status 0x48 here ? */
    if (mask != 0xffffffff)
    {
        value &= mask;
        value |= le32toh(ptr[1]) & ~mask;
    }
    ptr[1] = htole32(value);
    value = le32toh(ptr[1]); /* read back to flush write */
    pthread_mutex_unlock(&smon_mutex);
    return value;
}

unsigned int toscaSmonWrite(unsigned int address, unsigned int value)
{
    return toscaSmonWriteMasked(address, 0xffffffff, value);
}

unsigned int toscaSmonSet(unsigned int address, unsigned int bitsToSet)
{
    return toscaSmonWriteMasked(address, bitsToSet, 0xffffffff);
}

unsigned int toscaSmonClear(unsigned int address, unsigned int bitsToClear)
{
    return toscaSmonWriteMasked(address, bitsToClear, 0);
}


/* Read (and clear) VME error status. Error is latched and not overwritten until read. */

#define CSR_VMEERR_REG 0x418

toscaMapVmeErr_t toscaGetVmeErr(unsigned int device)
{
    volatile uint32_t* vmeerr = toscaMap((device<<16)|TOSCA_CSR, CSR_VMEERR_REG, 8, 0);
    if (!vmeerr) return (toscaMapVmeErr_t) { .address = -1 };
    return (toscaMapVmeErr_t) { .address = le32toh(vmeerr[0]), {.status = le32toh(vmeerr[1])} };
}


/* Access to PON registers via ELB */

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
    unsigned int reg;
    
    address &= ~3;
    if (address >= 0x28 && address != 0x40)
    {
        debug("address=0x%x -- not implemented", address);
        errno = EINVAL;
        return -1;
    }
    if (address == 40) reg = 10;
    else reg = address>>2;
    if (!fd[reg])
    {
        char filename[50];
        sprintf(filename, "/sys/devices/{,*/}*localbus/*.pon/%s", toscaPonAddrToRegname(address));
        fd[reg] = sysfsOpen(filename);
    }
    debug("address=0x%02x regname=%s fd=%d", address, toscaPonAddrToRegname(address), fd[reg]);
    return fd[reg];
}

unsigned int toscaPonRead(unsigned int address)
{
    debug("address=0x%02x", address);
    int fd = toscaPonFd(address);
    if (fd < 0) return (unsigned int)-1;
    return sysfsReadULong(fd);
}

unsigned int toscaPonWrite(unsigned int address, unsigned int value)
{
    debug("address=0x%02x value=0x%x", address, value);
    int fd = toscaPonFd(address);
    if (fd < 0) return (unsigned int)-1;
    sysfsWrite(fd, "%x", value);
    return sysfsReadULong(fd);
}

unsigned int toscaPonWriteMasked(unsigned int address, unsigned int mask, unsigned int value)
{
    debug("address=0x%02x value=0x%x", address, value);
    int fd = toscaPonFd(address);
    if (fd < 0) return (unsigned int)-1;
    sysfsWrite(fd, "%x", (value & mask) | (sysfsReadULong(fd) & ~mask));
    return sysfsReadULong(fd);
}

unsigned int toscaPonSet(unsigned int address, unsigned int bitsToSet)
{
    return toscaPonWriteMasked(address, bitsToSet, 0xffffffff);
}

unsigned int toscaPonClear(unsigned int address, unsigned int bitsToClear)
{
    return toscaPonWriteMasked(address, bitsToClear, 0);
}


/* Access to FMCs via TSCR Serial Bus Controller registers */


#define CSR_SERIAL_BUS_CONTROLER 0x120c /* 2 regs: address and value */
#define FMC_MAX 2

unsigned int toscaSbcWriteMasked(unsigned int fmc, unsigned int reg, unsigned int mask, unsigned int value)
{
    static pthread_mutex_t sbc_mutex[FMC_MAX] = {PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER};
    static volatile uint32_t* csr = (void*)-1;
    int tries_left = 1000;
    int addr;

    if (fmc-1 >= FMC_MAX)
    {
        errno = ENODEV;
        return -1;
    }
    errno = 0;
    if (csr == (void*)-1) csr = toscaMap((toscaDeviceType(0) == 0x1211 ? 0x10000 : 0)|TOSCA_CSR, 0, 0, 0);
    if (!csr) return (unsigned int)-1;
    debug("fmc=%i, reg=0x%x, mask=0x%x, value=0x%x", fmc, reg, mask, value);
    addr = (CSR_SERIAL_BUS_CONTROLER + --fmc * 0x100)/4;
    pthread_mutex_lock(&sbc_mutex[fmc]);
    if (mask != 0xffffffff)
    {
        csr[addr] = htole32(reg | 0x8000000); /* read cmd */
        while ((le32toh(csr[addr]) & 0x80000000) && tries_left--); /* wait for read complete */
        value &= mask;
        value |= le32toh(csr[addr+1]) & ~mask;
    }
    if (mask != 0)
    {
        csr[addr+1] = htole32(value);
        (void) csr[addr+1]; /* read back to flush */
        csr[addr] = htole32(reg | 0xc000000); /* write cmd */
        while ((le32toh(csr[addr]) & 0x80000000) && tries_left--); /* wait for write complete */
        
        /* read back value */
        csr[addr] = htole32(reg | 0x8000000); /* read cmd */
        while ((le32toh(csr[addr]) & 0x80000000) && tries_left--); /* wait for read complete */
        value = le32toh(csr[addr+1]);
    }
    debug("fmc=%i, reg=0x%x, readback=0x%x", fmc, reg, value);
    debugLvl(3, "tries_left: %i", tries_left);
    pthread_mutex_unlock(&sbc_mutex[fmc]);
    if (tries_left == 0)
    {
        errno = EIO;
        return (unsigned int)-1;
    }
    return value;
}

unsigned int toscaSbcWrite(unsigned int fmc, unsigned int reg, unsigned int value)
{
    return toscaSbcWriteMasked(fmc, reg, 0xffffffff, value);
}

unsigned int toscaSbcSet(unsigned int fmc, unsigned int reg, unsigned int bitsToSet)
{
    return toscaSbcWriteMasked(fmc, reg, bitsToSet, 0xffffffff);
}

unsigned int toscaSbcClear(unsigned int fmc, unsigned int reg, unsigned int bitsToClear)
{
    return toscaSbcWriteMasked(fmc, reg, bitsToClear, 0);
}

unsigned int toscaSbcRead(unsigned int fmc, unsigned int reg)
{
    return toscaSbcWriteMasked(fmc, reg, 0, 0);
}
