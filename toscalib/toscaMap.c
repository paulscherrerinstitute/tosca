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
#include "vme_user.h"
#include "toscaMap.h"
#include "toscaUtils.h"

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
    int setcmd, getcmd;
    const char *filename;

    debug("aspace=%#x(%s), address=%#llx, size=%#zx",
            aspace, toscaAddrSpaceToStr(aspace),
            address,
            size);
    
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
    if (aspace == TOSCA_CSR)
    {
        /* Handle TCSR in compatible way to other address spaces */
        struct stat filestat = {0};

        filename = "/sys/bus/pci/drivers/tosca/0000:03:00.0/resource3";
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
    else /* USER, SHM, VME, VME_SLAVE */
    {
        /* Tosca requires mapping windows aligned to 1 MiB
           Thus round down address to the full MiB.
           Adjust and round up size to the next full MiB.
        */
        struct vme_master vme_window = {0};

        if (!size)
        {
            debug("size is 0");
            errno = EINVAL;
            return NULL;
        }

        vme_window.enable = 1;
        vme_window.vme_addr = address & ~0xffffful;
        vme_window.size = (size + (address & 0xffffful) + 0xffffful) & ~0xffffful;
        
        if (aspace & VME_SLAVE)
        {
            filename = "/dev/bus/vme/s0";
            setcmd = VME_SET_SLAVE;
            getcmd = VME_GET_SLAVE;
            vme_window.aspace = VME_A32;
        }
        else
        {
            filename = "/dev/bus/vme/m0";
            setcmd = VME_SET_MASTER;
            getcmd = VME_GET_MASTER;
            vme_window.aspace = aspace & 0x0fff;
            vme_window.cycle = aspace & 0xf000;
        }
        
        fd = open(filename, O_RDWR);
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            UNLOCK;
            return NULL;
        }

        debug("ioctl(%d, VME_SET_%s, {enable=%d addr=0x%llx size=0x%llx aspace=0x%x cycle=0x%x, dwidth=0x%x})",
            fd, setcmd == VME_SET_MASTER ? "MASTER" : "SLAVE",
            vme_window.enable, vme_window.vme_addr, vme_window.size, vme_window.aspace, vme_window.cycle, vme_window.dwidth);
        if (ioctl(fd, setcmd, &vme_window) != 0)
        {
            debugErrno("ioctl(%d, VME_SET_%s, {enable=%d addr=0x%llx size=0x%llx aspace=0x%x cycle=0x%x, dwidth=0x%x})",
                fd, setcmd == VME_SET_MASTER ? "MASTER" : "SLAVE",
                vme_window.enable, vme_window.vme_addr, vme_window.size, vme_window.aspace, vme_window.cycle, vme_window.dwidth);
            close(fd);
            UNLOCK;
            return NULL;
        }

        if (!(aspace & VME_SLAVE)) /* reading back slave windows is buggy */
        {
        /* If the request fits into an existing master window,
           we may get that one instead of the requested one.
           That window may have a different start adddress.
        */
        if (ioctl(fd, getcmd, &vme_window) != 0)
        {
            debugErrno("ioctl(%d, VME_GET_%s)",
                fd, getcmd == VME_GET_MASTER ? "MASTER" : "SLAVE");
            close(fd);
            UNLOCK;
            return NULL;
        }
        debug("got window address=%#llx size=%#llx",
                vme_window.vme_addr, vme_window.size);
        }
        
        /* Find the MMU pages in the window we need to map */
        offset = address - vme_window.vme_addr;   /* Location within window that maps to requested address */
        address = vme_window.vme_addr;            /* Start address of the fd. */
        if ((aspace & 0xfff) == VME_A16)
            size = 0x10000;                       /* Map only the small A16 space, not the big window to user space. */
        else
            size = vme_window.size ;              /* Map the whole window to user space. */
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
    close(fd);

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
    switch (aspace & 0xFFF)
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

        case VME_SLAVE:   return "VME_SLAVE";

        case 0: return "none";
        default: return "invalid";
    }
}

unsigned int toscaStrToAddrSpace(const char* str)
{
    unsigned int aspace;
    if (!str) return 0;
    if (strncmp(str, "VME_", 4) == 0) str+=4;
    switch (str[0])
    {
        case 'C':
            if (strcmp(str+1,"RCSR") == 0 || strcmp(str+1,"SR") == 0)
                return VME_CRCSR;
            return 0;
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
            if (strcmp(str+1,"LAVE") == 0)
                return VME_SLAVE;
            return 0;
        case 'T':
            if (strcmp(str+1,"CSR") == 0)
                return TOSCA_CSR;
            return 0;
    }
    return 0;
}

int toscaMapVMESlave(unsigned int aspace, vmeaddr_t res_address, size_t size, vmeaddr_t vme_address, int swap)
{
    const char* filename;
    const char* res;
    FILE* file;
    
    switch (aspace)
    {
        case TOSCA_USER1: res = "usr"; break;
        case TOSCA_SHM:   res = "shm"; break;
        case VME_A32:     res = "vme"; break;
        default:
            errno = EAFNOSUPPORT;
            debug("invalid address space");
            return -1;
    }
    if (vme_address >= 512<<20)
    {
        debug("vme address too high (>=512M)");
        errno = EADDRNOTAVAIL;
        return -1;
    }
    if (size > 512<<20)
    {
        debug("size too large (>256M)");
        errno = EFBIG;
        return -1;
    }
    filename = "/sys/class/vme_user/bus!vme!s0/add_slave_window";
    file = fopen(filename, "w");
    if (file == NULL)
    {
        debugErrno("fopen %s", filename);
        return -1;
    }
    debug("0x%llx:0x%zx:0x%llx:%s:%c > '%s'",  vme_address, size, res_address, res, swap ? 'y' : 'n', filename);
    fprintf(file, "0x%llx:0x%zx:0x%llx:%s:%c", vme_address, size, res_address, res, swap ? 'y' : 'n');
    if (fclose(file) == -1)
    {
        debugErrno("add slave window");
        errno = EAGAIN;
        if (toscaCheckSlaveMaps(vme_address, size) > 0)
        {
            debug("overlap with existing slave window");
            errno = EADDRINUSE;
        }
        return -1;
    }

    filename = "/sys/bus/pci/drivers/tosca/0000:03:00.0/enableVMESlave";
    file = fopen(filename, "w");
    if (file == NULL)
    {
        debugErrno("fopen %s", filename);
        return -1;
    }
    fprintf(file, "1");
    if (fclose(file) == -1)
    {
        debugErrno("enable slave window");
        return -1;
    }
    return 0;
}

int toscaCheckSlaveMaps(vmeaddr_t addr, size_t size)
{
    FILE* file;
    size_t slaveBase=-1;
    size_t vmeOffs, mapSize, mode, resOffs;
    const char* filename = "/sys/bus/pci/devices/0000:03:00.0/slavemaps";
    const char* res;
    int overlap = 0;
    char buf[SIZE_STRING_BUFFER_SIZE];
    
    file = fopen(filename, "r");
    if (file == NULL)
    {
        debugErrno("fopen %s", filename);
        return -1;
    }
    fscanf(file, "%*[^0]%zi", &slaveBase);
    fscanf(file, "%*[^0]");
    while (fscanf(file, "%zi %zi %zi %zi", &vmeOffs, &mapSize, &mode, &resOffs) > 0)
    {
        if (size==0 || (addr+size > vmeOffs && addr < vmeOffs+mapSize))
        {
            overlap = 1;
            switch (mode & 0xf000)
            {
                case 0x0000: res="MEM"; break;
                case 0x1000: res="A32"; break;
                case 0x2000: res="SHM"; break;
                case 0x4000: res="USER1"; break;
                default: res="???"; break;
            }
            if (size == 0)
                printf("VME_SLAVE:0x%zx [%s] %s:0x%zx%s\n",
                    slaveBase+vmeOffs, sizeToStr(mapSize, buf),
                    res, resOffs, mode & 0x40 ? " SWAP" : "");
        }
    }
    fclose(file);
    return overlap;
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
