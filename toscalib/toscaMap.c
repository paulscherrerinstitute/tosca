#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <endian.h>
#include <pthread.h>
#include <stdint.h>

typedef uint64_t __u64;
typedef uint32_t __u32;
typedef uint8_t __u8;
#include "vme.h"
#include "vme_user.h"
#include "toscaMap.h"

pthread_mutex_t maplist_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK pthread_mutex_lock(&maplist_mutex);
#define UNLOCK pthread_mutex_unlock(&maplist_mutex);

int toscaMapDebug;
FILE* toscaMapDebugFile = NULL;

#define debug_internal(m, fmt, ...) if(m##Debug) fprintf(m##DebugFile?m##DebugFile:stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#define debugErrno(fmt, ...) debug(fmt " failed: %s", ##__VA_ARGS__, strerror(errno))
#define debug(fmt, ...) debug_internal(toscaMap, fmt, ##__VA_ARGS__)

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

static volatile void* toscaMapInternal(int aspace, vmeaddr_t address, size_t size)
{
    struct map **pmap;
    volatile void *ptr;
    size_t offset;
    int fd;
    char filename[50];

    debug("aspace=%#x(%s), address=%#llx, size=%#zx",
            aspace, toscaAddrSpaceStr(aspace),
            address,
            size);

    aspace &= ~(VME_USER | VME_DATA);

    for (pmap = &maps; *pmap; pmap = &(*pmap)->next)
    {
        debug("check aspace=%#x(%s), address=%#llx, size=%#zx",
                (*pmap)->info.aspace, toscaAddrSpaceStr((*pmap)->info.aspace),
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
                return (*pmap)->info.ptr + (address - (*pmap)->info.address);
            }
        }
    }

    if (aspace != TOSCA_CSR) /* handle TOSCA_CSR later */
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
            return NULL;
        }

        if (ioctl(fd, VME_SET_MASTER, &vme_window) != 0)
        {
            debugErrno("ioctl VME_SET_MASTER");
            close(fd);
            return NULL;
        }

        /* If the request fits into an existing window we get that one instead.
           That window may have a different start adddress, e.g. aligned to 4 MiB.
        */
        if (ioctl(fd, VME_GET_MASTER, &vme_window) != 0)
        {
            debugErrno("ioctl VME_GET_MASTER");
            close(fd);
            return NULL;
        }
        debug("window address=%#llx size=%#llx",
                vme_window.vme_addr, vme_window.size);

        /* Find the MMU pages in the window we need to map */
        offset = address - vme_window.vme_addr;   /* Location within window that mapps to requested address */
        address = vme_window.vme_addr;            /* This is the start address of the fd. */
        if ((aspace & 0xfff) == VME_A16)
            size = VME_A16_MAX;
        else
            size = vme_window.size ;              /* Map the whole window to user space. */
    }
    else /* TOSCA_CSR */
    {
        /* Handle TCSR in compatible way */
        struct stat filestat = {0};

        sprintf(filename, "/sys/bus/pci/drivers/tosca/0000:03:00.0/resource3");
        fd = open(filename, O_RDWR);
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            return NULL;
        }
        fstat(fd, &filestat);
        tCsrSize = filestat.st_size;
        if (address + size > tCsrSize)
        {
            errno = EINVAL;
            debug("address or size too big");
            return NULL;
        }
        size = tCsrSize;                          /* Whole TCSR space */
        offset = address;                         /* Location within fd that mapps to requested address */
        address = 0;                              /* This is the start address of the fd. */
    }

    ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    debug("mmap(NULL, size=%#zx, PROT_READ | PROT_WRITE, MAP_SHARED, %s, 0) = %p",
            size, filename, ptr);
    if (ptr == MAP_FAILED)
    {
        debugErrno("mmap");
        close(fd);
        return NULL;
    }
    close(fd);

    *pmap = malloc(sizeof(struct map));
    if (!*pmap)
    {
        debugErrno("malloc");
        return NULL;
    }
    (*pmap)->info.ptr = ptr;
    (*pmap)->info.aspace = aspace;
    (*pmap)->info.address = address;
    (*pmap)->info.size = size;
    (*pmap)->next = NULL;

    return ptr + offset;
}

/* wrapper to lock access to map list */
volatile void* toscaMap(int aspace, vmeaddr_t address, size_t size)
{
    volatile void*ptr;
    LOCK
    ptr = toscaMapInternal(aspace, address,size);
    UNLOCK;
    return ptr;
}

toscaMapInfo_t toscaMapForeach(int(*func)(toscaMapInfo_t info))
{
    struct map *map;

    LOCK
    for (map = maps; map; map = map->next)
    {
        if (func(map->info) != 0) break;
    }
    UNLOCK
    if (map) return map->info;
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

const char* toscaAddrSpaceStr(int aspace)
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
        case TOSCA_SHMEM: return "SHMEM";
        case TOSCA_CSR:   return "TCSR";

        default: return "invalid";
    }
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

toscaMapVmeErr_t toscaMapGetVmeErr()
{
    int addr = toscaCsrRead(0x418); /* last VME error address modulo 32 bit */
    int stat = toscaCsrRead(0x41C); /* last VME error access code */

    return (toscaMapVmeErr_t) { addr, {stat} };
}
