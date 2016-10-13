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
#include <endian.h>
#include <glob.h>
#include <libgen.h>

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

typedef uint64_t __u64;
typedef uint32_t __u32;
typedef uint8_t __u8;
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

static volatile uint32_t* tCsr;
static size_t tCsrSize;
static size_t sramSize;

#define TOSCA_PCI_DIR "/sys/bus/pci/drivers/tosca"

typedef union {
    struct {
        int dom:16;
        int bus:8;
        int dev:5;
        int func:3;
    };
    uint32_t addr;
} pciAddr;

pciAddr *toscaDevices;
int toscaNumDevices = -1;

pciAddr toscaPciFind(int card)
{
    
    if (toscaNumDevices == -1)
    {     
        #ifndef GLOB_ONLYDIR
        #define GLOB_ONLYDIR 0
        #endif

        glob_t globresults = {0};   
        int status;
        int i;
        
        debug("glob("TOSCA_PCI_DIR "/*:*:*.*)");
        status = glob(TOSCA_PCI_DIR "/*:*:*.*", GLOB_ONLYDIR, NULL, &globresults);
        toscaNumDevices = globresults.gl_pathc;
        if (status == 0)
        {
            toscaDevices = calloc(globresults.gl_pathc, sizeof(pciAddr));
            debug ("found %zd tosca devices", globresults.gl_pathc);
            for (i = 0; i < globresults.gl_pathc; i++)
            {
                int dom, bus, dev, func;
                debug ("found %s", globresults.gl_pathv[i]+sizeof(TOSCA_PCI_DIR));
                sscanf(globresults.gl_pathv[i]+sizeof(TOSCA_PCI_DIR),
                    "%x:%x:%x.%x", &dom, &bus, &dev, &func);
                toscaDevices[i].dom = dom;
                toscaDevices[i].bus = bus;
                toscaDevices[i].dev = dev;
                toscaDevices[i].func = func;
            }
            globfree(&globresults);
        }
        else
        {
            debug("no tosca devices found: %m");
        }
    }
    if (card >= toscaNumDevices)
        return (pciAddr) {.addr=-1};
    return toscaDevices[card];
}

const char* toscaAddrSpaceToStr(unsigned int aspace)
{
    switch (aspace & 0xfff)
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
        case TOSCA_SRAM:  return "SRAM";

        case VME_SLAVE:   return "VME_SLAVE";

        case 0: return "none";
        default:
        {
            static char buf[20];
            sprintf(buf, "0x%x", aspace);
            return buf;
        }
    }
}

toscaMapAddr_t toscaStrToAddr(const char* str)
{
    toscaMapAddr_t result = {0};
    char *s, *p;
    
    if (!str) return result;
    
    result.address = strtoul(str, &s, 0);
    if (*s == ':') {
        s++;
        result.aspace = ((unsigned int) result.address) << 16;
        result.address = 0;
    }
    p = strchr(s, ':');
    if (p) result.address = toscaStrToSize(p+1);
    
    if ((strncmp(s, "USR", 3) == 0 && (s+=3)) ||
        (strncmp(s, "USER", 4) == 0 && (s+=4)))
    {
        if (*s == '2')
        {
            result.aspace |= TOSCA_USER2;
            s++;
        }
        else
        {
            result.aspace |= TOSCA_USER1;
            if (*s == '1') s++;
        }
    }
    else
    if ((strncmp(s, "SH_MEM", 6) == 0 && (s+=6)) ||
        (strncmp(s, "SHMEM", 5) == 0 && (s+=5)) ||
        (strncmp(s, "SHM", 3) == 0  && (s+=3)))
        result.aspace |= TOSCA_SHM;
    else
    if (strncmp(s, "TCSR", 4) == 0 && (s+=4))
        result.aspace |= TOSCA_CSR;
    else
    if (strncmp(s, "SRAM", 4) == 0 && (s+=4))
        result.aspace |= TOSCA_SRAM;
    else
    {
        if (strncmp(s, "VME_", 4) == 0) s+=4;
        if ((strncmp(s, "CRCSR", 5) == 0 && (s+=5)) || 
            (strncmp(s, "CSR", 3) == 0 && (s+=3)))
            result.aspace |= VME_CRCSR;
        else
        if (strncmp(s, "SLAVE", 5) == 0 && (s+=5))
            result.aspace |= VME_SLAVE;
        else
        if (*s == 'A')
        {
            switch (strtol(++s, &s, 10))
            {
                case 16:
                    result.aspace |= VME_A16; break;
                case 24:
                    result.aspace |= VME_A24; break;
                case 32:
                    result.aspace |= VME_A32; break;
                default:
                    return (toscaMapAddr_t){0};
            }
            do
            {
                if (*s == '*') result.aspace |= VME_SUPER;
                else
                if (*s == '#') result.aspace |= VME_PROG;
                else
                break;
            } while (s++);
        }
    }
    if (*s == 0) return result;
    if (*s != ':') return (toscaMapAddr_t){0};
    return result;
}

volatile void* toscaMap(unsigned int aspace, vmeaddr_t address, size_t size)
{
    struct map **pmap, *map;
    volatile void *ptr;
    size_t offset;
    int fd;
    unsigned int card = aspace >> 16;
    unsigned int setcmd, getcmd;
    char filename[80];

    debug("aspace=%#x(%s), address=%#llx, size=%#zx",
            aspace,
            toscaAddrSpaceToStr(aspace),
            (unsigned long long) address,
            size);
            
    LOCK;
    for (pmap = &maps; *pmap; pmap = &(*pmap)->next)
    {
        debug("check aspace=%#x(%s), address=%#llx, size=%#zx",
                (*pmap)->info.aspace,
                toscaAddrSpaceToStr((*pmap)->info.aspace),
                (unsigned long long) (*pmap)->info.address,
                (*pmap)->info.size);
        if (aspace == (*pmap)->info.aspace &&
            address >= (*pmap)->info.address &&
            address + size <= (*pmap)->info.address + (*pmap)->info.size)
        {
            UNLOCK;
            debug("use existing window addr=%#llx size=%#zx offset=%#llx",
                    (unsigned long long) (*pmap)->info.address, (*pmap)->info.size,
                    (unsigned long long) (address - (*pmap)->info.address));
            return (*pmap)->info.ptr + (address - (*pmap)->info.address);
        }
    }
    if (aspace & TOSCA_CSR)
    {
        /* Handle TCSR in compatible way to other address spaces */
        struct stat filestat = {0};
        pciAddr pciaddr;
        
        pciaddr = toscaPciFind(card);
        sprintf(filename, TOSCA_PCI_DIR "/%04x:%02x:%02x.%x/resource3",
            pciaddr.dom, pciaddr.bus, pciaddr.dev, pciaddr.func);
        fd = open(filename, O_RDWR);
        if (fd < 0)
        {
            UNLOCK;
            debugErrno("open %s", filename);
            return NULL;
        }
        fstat(fd, &filestat);
        tCsrSize = filestat.st_size; /* Map whole TCSR space */
        if (address + size > tCsrSize)
        {
            UNLOCK;
            close(fd);
            debug("address or size too big");
            errno = EINVAL;
            return NULL;
        }
        size = tCsrSize;  /* Map whole TCSR space */
        offset = address; /* Location within fd that maps to requested address */
        address = 0;      /* This is the start address of the fd. */
    }
    else if (aspace & TOSCA_SRAM)
    {
        glob_t globresults = {0};            

        if (card != 0)
        {
            UNLOCK;
            debug("access to sram only on local card");
            errno = EINVAL;
            return NULL;
        }
        sprintf(filename, "/sys/bus/platform/devices/*.sram/uio/uio*");
        debug("glob(%s)", filename);
        if (glob(filename, GLOB_ONLYDIR, NULL, &globresults) != 0)
        {
            UNLOCK;
            debug("cannot find sram device");
            errno = ENODEV;
            return NULL;
        }
        sprintf(filename, "/dev/%s", basename(globresults.gl_pathv[0]));
        sramSize = 0x2000; /* should we read that from /sys/.../uioX/maps/mapY/size ? */
        globfree(&globresults);
        if (address + size > sramSize)
        {
            UNLOCK;
            debug("address or size too big");
            errno = EINVAL;
            return NULL;
        }
        fd = open(filename, O_RDWR);
        if (fd < 0)
        {
            UNLOCK;
            debugErrno("open %s", filename);
            return NULL;
        }
        size = sramSize;
        offset = address;
        address = 0;
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
            sprintf(filename, "/dev/bus/vme/s%u", card);
            setcmd = VME_SET_SLAVE;
            getcmd = VME_GET_SLAVE;
            vme_window.aspace = VME_A32;
        }
        else
        {
            sprintf(filename, "/dev/bus/vme/m%u", card);
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
            vme_window.enable,
            (unsigned long long) vme_window.vme_addr,
            (unsigned long long) vme_window.size,
            vme_window.aspace,
            vme_window.cycle,
            vme_window.dwidth);
        if (ioctl(fd, setcmd, &vme_window) != 0)
        {
            debugErrno("ioctl(%d, VME_SET_%s, {enable=%d addr=0x%llx size=0x%llx aspace=0x%x cycle=0x%x, dwidth=0x%x})",
                fd, setcmd == VME_SET_MASTER ? "MASTER" : "SLAVE",
                vme_window.enable,
                (unsigned long long) vme_window.vme_addr,
                (unsigned long long) vme_window.size,
                vme_window.aspace,
                vme_window.cycle,
                vme_window.dwidth);
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
            (unsigned long long) vme_window.vme_addr,
            (unsigned long long) vme_window.size);
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

toscaMapInfo_t toscaMapForeach(int(*func)(toscaMapInfo_t info, void* usr), void* usr)
{
    struct map *map;

    for (map = maps; map; map = map->next)
    {
        if (func(map->info, usr) != 0) break; /* loop until user func returns non 0 */
    }
    if (map) return map->info;           /* info of map where user func returned non 0 */
    else return (toscaMapInfo_t) { 0, 0, 0, NULL };
}

int toscaMapPtrCompare(toscaMapInfo_t info, void* ptr)
{
    return ptr >= info.ptr && ptr < info.ptr + info.size;
}

toscaMapInfo_t toscaMapFind(const volatile void* ptr)
{
    return toscaMapForeach(toscaMapPtrCompare, (void*)ptr);
}

toscaMapAddr_t toscaMapLookupAddr(const volatile void* ptr)
{
    toscaMapInfo_t info = toscaMapFind(ptr);
    return (toscaMapAddr_t) { info.aspace, info.address + (ptr - info.ptr) };
}

int toscaMapPrintInfo(toscaMapInfo_t info, FILE* file)
{
    if (!file) file = stdout;
    char buf[SIZE_STRING_BUFFER_SIZE];
    fprintf(file, "%5s:0x%-8llx [%s]\t%p\n",
        toscaAddrSpaceToStr(info.aspace),
        (unsigned long long)info.address,
        toscaSizeToStr(info.size, buf),info.ptr);
    return 0;
}

void toscaMapShow(FILE* file)
{
    toscaMapForeach((int(*)(toscaMapInfo_t, void*)) toscaMapPrintInfo, file);
    toscaCheckSlaveMaps(0,0);
}

int toscaMapVMESlave(unsigned int aspace, vmeaddr_t res_address, size_t size, vmeaddr_t vme_address, int swap)
{
    const char* res;
    FILE* file;
    unsigned int card = aspace >> 16;;
    pciAddr pciaddr;
    char filename[80];
    
    switch (aspace & 0xffff)
    {
        case TOSCA_USER1: res = "usr"; break;
        case TOSCA_SHM:   res = "shm"; break;
        case VME_A32:     res = "vme"; break;
        default:
            errno = EAFNOSUPPORT;
            debug("invalid address space 0x%x", aspace & 0xffff);
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
    sprintf(filename, "/sys/class/vme_user/bus!vme!s%u/add_slave_window", card);
    file = fopen(filename, "w");
    if (file == NULL)
    {
        debugErrno("fopen %s", filename);
        return -1;
    }
    debug("0x%llx:0x%zx:0x%llx:%s:%c > '%s'", 
        (unsigned long long) vme_address,
        size,
        (unsigned long long) res_address,
        res,
        swap ? 'y' : 'n',
        filename);
    fprintf(file, "0x%llx:0x%zx:0x%llx:%s:%c",
        (unsigned long long) vme_address,
        size,
        (unsigned long long) res_address,
        res,
        swap ? 'y' : 'n');
    if (fclose(file) == -1)
    {
        toscaMapAddr_t overlap;
        debugErrno("add slave window");
        errno = EAGAIN;
        overlap = toscaCheckSlaveMaps(vme_address, size);
        if (overlap.aspace || overlap.address)
        {
            if (overlap.aspace == aspace && overlap.address == res_address)
            {
                debug("slave window already mapped");
                return 0;
            }                
            debug("overlap with existing slave window %s:0x%llx",
                toscaAddrSpaceToStr(overlap.aspace), overlap.address);
            errno = EADDRINUSE;
        }
        return -1;
    }
    pciaddr = toscaPciFind(card);
    sprintf(filename, TOSCA_PCI_DIR "/%04x:%02x:%02x.%x/enableVMESlave",
        pciaddr.dom, pciaddr.bus, pciaddr.dev, pciaddr.func);
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

toscaMapAddr_t toscaCheckSlaveMaps(vmeaddr_t addr, size_t size)
{
    FILE* file;
    unsigned long long slaveBase=-1, vmeOffs, resOffs;
    size_t mapSize;
    unsigned int mode;
    const char* res;
    toscaMapAddr_t overlap = (toscaMapAddr_t) {0,0};
    unsigned int card = 0;
    pciAddr pciaddr;
    char buf[SIZE_STRING_BUFFER_SIZE];
    char filename[60];
    
    while (1)
    {
        pciaddr = toscaPciFind(card);
        if (pciaddr.addr == -1) break;
        sprintf(filename, TOSCA_PCI_DIR "/%04x:%02x:%02x.%x/slavemaps",
            pciaddr.dom, pciaddr.bus, pciaddr.dev, pciaddr.func);
        file = fopen(filename, "r");
        if (file == NULL)
        {
            debugErrno("fopen %s", filename);
            return overlap;
        }
        fscanf(file, "%*[^0]%lli", &slaveBase);
        fscanf(file, "%*[^0]");
        while (fscanf(file, "%lli %zi %i %lli", &vmeOffs, &mapSize, &mode, &resOffs) > 0)
        {
            if (size==0 || (addr+size > vmeOffs && addr < vmeOffs+mapSize))
            {
                overlap.address = resOffs;
                switch (mode & 0xf000)
                {
                    case 0x0000: res=""; break;
                    case 0x1000: res="A32:";   overlap.aspace = VME_A32;     break;
                    case 0x2000: res="SHM:";   overlap.aspace = TOSCA_SHM;   break;
                    case 0x4000: res="USER1:"; overlap.aspace = TOSCA_USER1; break;
                    default: res="???"; break;
                }
                if (size == 0)
                {
                    if (card > 0) printf("%u:", card);
                    printf("SLAVE:0x%-8llx [%s]\t%s0x%llx%s\n",
                        slaveBase+vmeOffs, toscaSizeToStr(mapSize, buf),
                        res, resOffs, mode & 0x40 ? " SWAP" : "");
                }
            }
        }
        fclose(file);
        card++;
    }
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

size_t toscaStrToSize(const char* str)
{
    char* p;
    if (!str) return 0;
    size_t size = strtoul(str, &p, 0);
    switch (*p)
    {
        case 'k':
        case 'K':
            size *= 1UL<<10; break;
        case 'M':
            size *= 1UL<<20; break;
        case 'G':
            size *= 1UL<<30; break;
#if __WORDSIZE > 32
        case 'T':
            size *= 1ULL<<40; break;
        case 'P':
            size *= 1ULL<<50; break;
        case 'E':
            size *= 1ULL<<60; break;
#endif
    }
    return size;
}

char* toscaSizeToStr(size_t size, char* str)
{
    int l = 0;
    l = sprintf(str, "0x%zx", size);
    if (size < 0x400) return str;
    l += sprintf(str+l, "=");
#if __WORDSIZE > 32
    if (size >= 1ULL<<60)
        l += sprintf(str+l, "%zuE", size>>50);
    size &= (1ULL<<60)-1;
    if (size >= 1ULL<<50)
        l += sprintf(str+l, "%zuP", size>>40);
    size &= (1ULL<<50)-1;
    if (size >= 1ULL<<40)
        l += sprintf(str+l, "%zuT", size>>30);
    size &= (1ULL<<40)-1;
#endif
    if (size >= 1UL<<30)
        l += sprintf(str+l, "%zuG", size>>30);
    size &= (1UL<<30)-1;
    if (size >= 1UL<<20)
        l += sprintf(str+l, "%zuM", size>>20);
    size &= (1UL<<20)-1;
    if (size >= 1UL<<10)
        l += sprintf(str+l, "%zuK", size>>10);
    size &= (1UL<<10)-1;
    if (size > 0)
        l += sprintf(str+l, "%zu", size);
    return str;
}

