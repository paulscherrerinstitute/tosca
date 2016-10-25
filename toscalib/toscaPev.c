#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pevulib.h>
#include <pevxulib.h>

#include "toscaMap.h"
#include "toscaElb.h"
#include "i2c.h"

#define TOSCA_DEBUG_NAME pev
#include "toscaDebug.h"

struct pev_node* pev_init(uint card)
{
    debug("pev compatibility mode");
    return (void*) -1;
}

struct pevx_node* pevx_init(uint card)
{
    debug("pevx compatibility mode");
    return (void*) -1;
}

/** CSR ****************************************************/

int pevx_csr_rd(uint card, int address)
{
    if (address & 0x80000000)
        return toscaCsrRead((card << 16) | (address & 0x7FFFFFFF));
    else
        return toscaIoRead((card << 16) | address);
}

int pev_csr_rd(int address)
{
    return pevx_csr_rd(0, address);
}

int pevx_csr_wr(uint card, int address, int value)
{
    if (address & 0x80000000)
        return toscaCsrWrite((card << 16) | (address & 0x7FFFFFFF), value);
    else
        return toscaIoWrite((card << 16) | address, value);
}

void pev_csr_wr(int address, int value)
{
    pevx_csr_wr(0, address, value);
}

int pevx_csr_set(uint card, int address, int value)
{
    if (address & 0x80000000)
        return toscaCsrSet((card << 16) | (address & 0x7FFFFFFF), value);
    else
        return toscaIoSet((card << 16) | address, value);
}

void pev_csr_set(int address, int value)
{
    pevx_csr_set(0, address, value);
}

/** ELB AND SRAM *******************************************/

static uint32_t *sramPtr(size_t address)
{
    static volatile void* sram = NULL;
    if (address > 0x2000)
    {
        errno = EFAULT;
        return NULL;
    }
    if (!sram)
    {
        sram = toscaMap(TOSCA_SRAM, 0, 0);
        if (!sram) return NULL;
    }
    return (uint32_t*) ((size_t) sram + address);
}

int pevx_elb_rd(uint card, int address)
{
    if (address >= 0xe000) /* sram */
    {
        uint32_t* ptr = sramPtr(address - 0xe000);
        if (!ptr) return -1;
        return *ptr;
    }
    else
    {
        return toscaElbRead(address);
    }
}

int pev_elb_rd(int address)
{
    return pevx_elb_rd(0, address);
}

int pevx_elb_wr(uint card, int address, int value)
{
    if (address >= 0xe000) /* sram */
    {
        uint32_t* ptr = sramPtr(address - 0xe000);
        if (!ptr) return -1;
        *ptr = value;
        return 0;
    }
    else
    {
        return toscaElbWrite(address, value);
    }
}

int pev_elb_wr(int address, int value)
{
    return pevx_elb_wr(0, address, value);
}

/** SMON ***************************************************/

int pev_smon_rd(int address)
{
    return toscaSmonRead(address);
}

void pev_smon_wr(int address, int value)
{
    toscaSmonWrite(address, value);
}

/** BMR ****************************************************/

static int pev_bmr_fd(unsigned int bmr, unsigned int address)
{
    static int fd[4] = {0};

    if (bmr > 3)
    {
        errno = EINVAL;
        return -1;
    }
    if (!fd[bmr])
        fd[bmr] = i2cOpen("/sys/devices/*localbus/*000a0.pon-i2c/i2c-*", bmr == 3 ? 0x24 : 0x53 + bmr * 8);
    if (fd[bmr] < 0)
        errno = ENODEV;
    return fd[bmr];
}

int pev_bmr_read(unsigned int bmr, unsigned int address, unsigned int *value, unsigned int count)
{
    int fd;
    debug("bmr=%u address=0x%x, count=%u", bmr, address, count);
    fd = pev_bmr_fd(bmr, address);
    if (fd < 0) return -1;
    return i2cRead(fd, address, count, value);
}

int pev_bmr_write(unsigned int bmr, unsigned int address, unsigned int value, unsigned int count)
{
    int fd;
    debug("bmr=%u address=0x%x, value=0x%x  count=%u", bmr, address, value, count);
    fd = pev_bmr_fd(bmr, address);
    if (fd < 0) return -1;
    return i2cWrite(fd, address, count, value);
}

float pev_bmr_conv_11bit_u(unsigned short value)
{
    unsigned short l;
    short h;

    l = value & 0x7ff;
    h = value >> 11;
    h |= 0xffe0;
    h = ~h + 1;
    return(((float)l/(1 << h)));
}

float pev_bmr_conv_11bit_s(unsigned short value)
{
    short h,l;

    l = value & 0x7ff;
    if( l & 0x400) l |= 0xf800;
    h = value >> 11;
    h |= 0xffe0;
    h = ~h + 1;
    return(((float)l/(1 << h)));
}

float pev_bmr_conv_16bit_u(unsigned short value)
{
    return(((float)value/(1 << 13)));
}

