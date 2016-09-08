#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

typedef uint64_t __u64;
typedef uint32_t __u32;
typedef uint8_t __u8;
#include "vme.h"
#include "vme_user.h"
#include "toscaMap.h"

#define TOSCA_DEBUG_NAME toscaMap
#include "toscaDebug.h"

pthread_mutex_t maplist_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK pthread_mutex_lock(&maplist_mutex)
#define UNLOCK pthread_mutex_unlock(&maplist_mutex)

/* Tosca tries to re-use mapping windows if possible.
 * Unfortunately mmap does not re-use mappings.
 * We need to keep our own list.
 */

static struct map {
    toscaMapInfo_t info;
    struct map *next;
} *maps;

static volatile uint32_t* tCsr = NULL;
static size_t tCsrSize = 0;

volatile void* toscaMap(unsigned int aspace, vmeaddr_t address, size_t size)
{
    struct map **pmap, *map;
    volatile void *ptr;
    size_t offset;
    int fd;
    char filename[50];

    debug("aspace=%#x(%s), address=%#llx, size=%#zx",
            aspace, toscaAddrSpaceToStr(aspace),
            address,
            size);

    if (aspace & (VME_A16 | VME_A24 | VME_A32 | VME_A64))
    {
        if (!(aspace & VME_SUPER)) aspace |= VME_USER;
        if (!(aspace & VME_PROG)) aspace |= VME_DATA;
    }
    LOCK;
    for (pmap = &maps; *pmap; pmap = &(*pmap)->next)
    {
        debug("check aspace=%#x(%s), address=%#llx, size=%#zx",
                (*pmap)->info.aspace, toscaAddrSpaceToStr((*pmap)->info.aspace),
                (*pmap)->info.address,
                (*pmap)->info.size);
        if (aspace == (*pmap)->info.aspace &&
            address >= (*pmap)->info.address &&
            address + size <= (*pmap)->info.address + (*pmap)->info.size)
        {
            debug("use existing window addr=%#llx size=%#zx offset=%#llx",
                    (*pmap)->info.address, (*pmap)->info.size,
                    (address - (*pmap)->info.address));
            {
                UNLOCK;
                return (*pmap)->info.ptr + (address - (*pmap)->info.address);
            }
        }
    }

    if (!(aspace & TOSCA_CSR)) /* handle TOSCA_CSR later */
    {
        /* Tosca requires windows aligned to 1 MiB
           Thus round down address to the full MiB and adjust size.
        */
        struct vme_master vme_window = {0};

        vme_window.vme_addr = address & ~0xffffful;
        vme_window.size = size + (address & 0xffffful);
        vme_window.aspace = aspace & 0x0fff;
        vme_window.cycle = aspace & 0xf000;
        vme_window.dwidth = VME_D32;
        vme_window.enable = 1;

        sprintf(filename, "/dev/bus/vme/m0");
        fd = open(filename, O_RDWR);
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            UNLOCK;
            return NULL;
        }

        if (ioctl(fd, VME_SET_MASTER, &vme_window) != 0)
        {
            debugErrno("ioctl VME_SET_MASTER");
            close(fd);
            UNLOCK;
            return NULL;
        }

        /* If the request fits into an existing window we get that one instead of the requested one.
           That window may have a different start adddress, e.g. aligned to 4 MiB.
        */
        if (ioctl(fd, VME_GET_MASTER, &vme_window) != 0)
        {
            debugErrno("ioctl VME_GET_MASTER");
            close(fd);
            UNLOCK;
            return NULL;
        }
        debug("window address=%#llx size=%#llx",
                vme_window.vme_addr, vme_window.size);

        /* Find the MMU pages in the window we need to map */
        offset = address - vme_window.vme_addr;   /* Location within window that maps to requested address */
        address = vme_window.vme_addr;            /* Start address of the fd. */
        if ((aspace & 0xfff) == VME_A16)
            size = VME_A16_MAX;                   /* Map only the small A16 space, not the big window to user space. */
        else
            size = vme_window.size ;              /* Map the whole window to user space. */
    }
    else /* TOSCA_CSR */
    {
        /* Handle TCSR in compatible way to other address spaces */
        struct stat filestat = {0};

        sprintf(filename, "/sys/bus/pci/drivers/tosca/0000:03:00.0/resource3");
        fd = open(filename, O_RDWR);
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            UNLOCK;
            return NULL;
        }
        fstat(fd, &filestat);
        tCsrSize = filestat.st_size;
        if (address + size > tCsrSize)
        {
            errno = EINVAL;
            debug("address or size too big");
            UNLOCK;
            return NULL;
        }
        size = tCsrSize;  /* Map whole TCSR space */
        offset = address; /* Location within fd that maps to requested address */
        address = 0;      /* This is the start address of the fd. */
    }

    ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    debug("mmap(NULL, size=%#zx, PROT_READ | PROT_WRITE, MAP_SHARED, %s, 0) = %p",
            size, filename, ptr);
    if (ptr == MAP_FAILED)
    {
        debugErrno("mmap");
        close(fd);
        UNLOCK;
        return NULL;
    }
    /* close(fd); */

    map = malloc(sizeof(struct map));
    if (!map)
    {
        debugErrno("malloc");
        UNLOCK;
        return NULL;
    }
    (map)->info.ptr = ptr;
    (map)->info.aspace = aspace;
    (map)->info.address = address;
    (map)->info.size = size;
    (map)->next = NULL;
    *pmap = map;
    UNLOCK;

    return ptr + offset;
}

toscaMapInfo_t toscaMapForeach(int(*func)(toscaMapInfo_t info))
{
    struct map *map;

    for (map = maps; map; map = map->next)
    {
        if (func(map->info) != 0) break; /* loop until user func returns non 0 */
    }
    if (map) return map->info;           /* info of map where user func returned non 0 */
    else return (toscaMapInfo_t) { 0, 0, 0, NULL };
}

toscaMapInfo_t toscaMapFind(const volatile void* ptr)
{
    int toscaMapPtrCompare(toscaMapInfo_t info)
    {
        return ptr >= info.ptr && ptr < info.ptr + info.size;
    }
    return toscaMapForeach(toscaMapPtrCompare);
}

toscaMapAddr_t toscaMapLookupAddr(const volatile void* ptr)
{
    toscaMapInfo_t info = toscaMapFind(ptr);
    return (toscaMapAddr_t) { info.aspace, info.address + (ptr - info.ptr) };
}

const char* toscaAddrSpaceToStr(unsigned int aspace)
{
    switch (aspace & ~(VME_USER | VME_DATA))
    {
        case VME_A16:   return "A16";
        case VME_A24:   return "A24";
        case VME_A32:   return "A32";
        case VME_A64:   return "A64";
        case VME_CRCSR: return "CRCSR";

        case VME_A16 | VME_SUPER: return "A16*";
        case VME_A24 | VME_SUPER: return "A24*";
        case VME_A32 | VME_SUPER: return "A32*";
        case VME_A64 | VME_SUPER: return "A64*";

        case VME_A16 | VME_PROG: return "A16#";
        case VME_A24 | VME_PROG: return "A24#";
        case VME_A32 | VME_PROG: return "A32#";
        case VME_A64 | VME_PROG: return "A64#";

        case VME_A16 | VME_PROG | VME_SUPER: return "A16#*";
        case VME_A24 | VME_PROG | VME_SUPER: return "A24#*";
        case VME_A32 | VME_PROG | VME_SUPER: return "A32#*";
        case VME_A64 | VME_PROG | VME_SUPER: return "A64#*";

        case TOSCA_USER1: return "USER1";
        case TOSCA_USER2: return "USER2";
        case TOSCA_SHM:   return "SHM";
        case TOSCA_CSR:   return "TCSR";

        case 0: return "none";
        default: return "invalid";
    }
}

unsigned int toscaStrToAddrSpace(const char* str)
{
    unsigned int aspace;
    if (!str) return 0;
    switch (str[0])
    {
        case 'C':
            if (strcmp(str+1,"RCSR") == 0 || strcmp(str+1,"SR") == 0)
                return VME_CRCSR;
            return 0;
        case 'V':
            if (strcmp(str+1, "ME_CSR") == 0)
                return VME_CRCSR;
            if (strncmp(str+1, "ME_A", 4) != 0) return 0;
            str += 4;
        case 'A':
        {
            switch (strtol(str+1, NULL, 10))
            {
                case 16:
                    aspace = VME_A16;
                    break;
                case 24:
                    aspace = VME_A24;
                    break;
                case 32:
                    aspace = VME_A32;
                    break;
                default:
                    return 0;
            }
            switch (str[3])
            {
                case '*':
                    aspace |= VME_SUPER;
                    break;
                case '#':
                    aspace |= VME_PROG;
                    break;
                default:
                    return aspace;
            }
            switch (str[4])
            {
                case '*':
                    aspace |= VME_SUPER;
                    break;
                case '#':
                    aspace |= VME_PROG;
                    break;
            }
            return aspace;
        }
        case 'U':
            if (strncmp(str+1,"SER", 3) == 0 || strncmp(str+1,"SR", 2) == 0)
            {
                switch (str[strlen(str)-1])
                {
                    case '1':
                    case 'R':
                        return TOSCA_USER1;
                    case '2':
                        return TOSCA_USER2;
                }
            }
            return 0;
        case 'S':
            if (strcmp(str+1,"HM") == 0 || strcmp(str+1,"H_MEM") == 0 || strcmp(str+1,"HMEM") == 0)
                return TOSCA_SHM;
            return 0;
        case 'T':
            if (strcmp(str+1,"CSR") == 0)
                return TOSCA_CSR;
            return 0;
    }
    return 0;
}

uint32_t toscaCsrRead(unsigned int address)
{
    if (!tCsr && !(tCsr = toscaMap(TOSCA_CSR, 0, 0)) ) return -1;
    if (address >= tCsrSize) { errno = EINVAL; return -1; }
    return le32toh(tCsr[address>>2]);
}

int toscaCsrWrite(unsigned int address, uint32_t value)
{
    if (!tCsr && !(tCsr = toscaMap(TOSCA_CSR, 0, 0)) ) return -1;
    if (address >= tCsrSize) { errno = EINVAL; return -1; }
    tCsr[address>>2] = htole32(value);
    return 0;
}

int toscaCsrSet(unsigned int address, uint32_t value)
{
    if (!tCsr && !(tCsr = toscaMap(TOSCA_CSR, 0, 0)) ) return -1;
    if (address >= tCsrSize) { errno = EINVAL; return -1; }
    tCsr[address>>2] |= htole32(value);
    return 0;
}

int toscaCsrClear(unsigned int address, uint32_t value)
{
    if (!tCsr && !(tCsr = toscaMap(TOSCA_CSR, 0, 0)) ) return -1;
    if (address >= tCsrSize) { errno = EINVAL; return -1; }
    tCsr[address>>2] &= ~htole32(value);
    return 0;
}

toscaMapVmeErr_t toscaGetVmeErr()
{
    int addr = toscaCsrRead(0x418); /* last VME error address modulo 32 bit */
    int stat = toscaCsrRead(0x41C); /* last VME error access code */

    return (toscaMapVmeErr_t) { addr, {stat} };
}
