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
#include <glob.h>
#include <libgen.h>

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

typedef uint64_t __u64;
typedef uint32_t __u32;
typedef uint8_t __u8;
#include "vme_user.h"
#include "toscaMap.h"

#define TOSCA_DEBUG_NAME toscaMap
#include "toscaDebug.h"

/* Tosca tries to re-use mapping windows if possible.
 * Unfortunately mmap does not re-use mappings.
 * We need to keep our own list.
 */

struct map {
    toscaMapInfo_t info;
    struct map *next;
};

struct toscaDevice {
    unsigned int dom:16;
    unsigned int bus:8;
    unsigned int dev:5;
    unsigned int func:3;
    struct map *maps, *csr, *io, *sram;
    pthread_mutex_t maplist_mutex;
} static *toscaDevices;
 int toscaNumDevices = -1;

#define TOSCA_PCI_DIR "/sys/bus/pci/drivers/tosca"

void toscaInit()
{
    glob_t globresults;
    int status;
    size_t i;

    if (toscaNumDevices >= 0) return;
    debug("glob("TOSCA_PCI_DIR "/*:*:*.*/)");
    status = glob(TOSCA_PCI_DIR "/*:*:*.*/", 0, NULL, &globresults);
    if (status != 0)
    {
        toscaNumDevices = 0;
        error("no tosca devices found");
        return;
    }
    toscaNumDevices = globresults.gl_pathc;
    toscaDevices = calloc(globresults.gl_pathc, sizeof(struct toscaDevice));
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
        pthread_mutex_init(&toscaDevices[i].maplist_mutex, NULL);
    }
    globfree(&globresults);
}

int toscaOpen(unsigned int card, const char* resource)
{
    int fd;
    char filename[80];

    debug("card=%u", card);
    if (toscaNumDevices < 0) toscaInit();
    if (card >= toscaNumDevices)
    {
        debug("card=%u but only %u tosca devices found", card, toscaNumDevices);
        errno = ENODEV;
        return -1;
    }
    sprintf(filename, TOSCA_PCI_DIR "/%04x:%02x:%02x.%x/%s",
        toscaDevices[card].dom,
        toscaDevices[card].bus,
        toscaDevices[card].dev,
        toscaDevices[card].func,
        resource);
    fd = open(filename, O_RDWR);
    if (fd < 0)
        fd = open(filename, O_RDONLY);
    if (fd < 0)
        fd = open(filename, O_WRONLY);
    if (fd < 0)
        debugErrno("open %s", filename);
    return fd;
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
        case TOSCA_IO:    return "TIO";
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
    unsigned long card;
    char *s, *p;
    
    if (!str) return result;
    
    card = strtoul(str, &s, 0);
    if (*s == ':')
    {
        debug("card=%lu", card);
        s++;
        result.aspace = card << 16;
    }
    else
    {
        s = (char*) str;
    }
    p = strchr(s, ':');
    result.address = toscaStrToSize(p ? p+1 : s);
    debug("address %s = 0x%llx", p ? p+1 : s, (unsigned long long) result.address );

    debug("aspace %s", s);
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
    if (strncmp(s, "TIO", 4) == 0 && (s+=4))
        result.aspace |= TOSCA_IO;
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
    debug("aspace = 0x%x = %u:%s", result.aspace, result.aspace>>16, toscaAddrSpaceToStr(result.aspace));
    if (*s == 0) return result;
    return result;
}

volatile void* toscaMap(unsigned int aspace, vmeaddr_t address, size_t size, vmeaddr_t res_address)
{
    struct map **pmap, *map;
    volatile void *ptr;
    size_t offset, mapsize;
    int fd;
    unsigned int card = aspace >> 16;
    unsigned int setcmd, getcmd;
    char filename[80];

    debug("card=%u aspace=0x%x(%s), address=0x%llx, size=0x%zx",
        card, aspace,
        toscaAddrSpaceToStr(aspace),
        (unsigned long long) address,
        size);

    if (toscaNumDevices < 0) toscaInit();
    if (card >= toscaNumDevices)
    {
        debug("card %u does not exist", card);
        errno = ENODEV;
        return NULL;
    }
    /* quick access to TCSR, CIO and SRAM */
    if (((aspace & TOSCA_CSR) && (map = toscaDevices[card].csr) != NULL) ||
        ((aspace & TOSCA_IO) && (map = toscaDevices[card].io) != NULL) ||
        ((aspace & TOSCA_SRAM) && (map = toscaDevices[card].sram) != NULL))
    {
        if (address + size > map->info.size)
        {
            debug("address 0x%llx + size 0x%zx exceeds %s size 0x%llx",
                (unsigned long long) address, size,
                toscaAddrSpaceToStr(aspace), (unsigned long long) map->info.size);
            return NULL;
        }
        return map->info.ptr + address;
    }
    pthread_mutex_lock(&toscaDevices[card].maplist_mutex);
    for (pmap = &toscaDevices[card].maps; (map = *pmap) != NULL; pmap = &map->next)
    {
        debug("%u:%s:0x%llx[0x%zx] check aspace=0x%x(%s), address=0x%llx, size=0x%zx",
            card, toscaAddrSpaceToStr(aspace), (unsigned long long) address, size,
            map->info.aspace,
            toscaAddrSpaceToStr(map->info.aspace),
            (unsigned long long) map->info.address,
            map->info.size);
        if (aspace == map->info.aspace &&
            address >= map->info.address &&
            address + size <= map->info.address + map->info.size)
        {
            pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
            debug("%u:%s:0x%llx[0x%zx] use existing window addr=0x%llx size=0x%zx offset=0x%llx",
                card, toscaAddrSpaceToStr(aspace), (unsigned long long) address, size,
                (unsigned long long) map->info.address, map->info.size,
                (unsigned long long) (address - map->info.address));
            return map->info.ptr + (address - map->info.address);
        }
    }
    if (aspace & (TOSCA_CSR | TOSCA_IO))
    {
        struct stat filestat;
        
        debug("creating new %s mapping", aspace & TOSCA_CSR ? "TCSR" : "TIO");
        fd = toscaOpen(card, aspace & TOSCA_CSR ? "resource3" : "resource4");
        if (fd < 0)
        {
            pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
            return NULL;
        }
        fstat(fd, &filestat);
        mapsize = filestat.st_size;  /* Map whole address space */
        offset = address;            /* Location within fd that maps to requested address */
        address = 0;                 /* This is the start address of the fd. */
    }
    else if (aspace & TOSCA_SRAM)
    {
        glob_t globresults;
        int n;
        char* uiodev;
        char buffer[24];

        debug("creating new SRAM mapping");        
        if (card != 0)
        {
            pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
            debug("access to SRAM only on local card");
            errno = EINVAL;
            return NULL;
        }

        sprintf(filename, "/sys/bus/platform/devices/*.sram/uio/uio*/maps/map0/size");
        debug("glob(%s)", filename);
        if (glob(filename, GLOB_ONLYDIR, NULL, &globresults) != 0)
        {
            pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
            debug("cannot find SRAM device");
            errno = ENODEV;
            return NULL;
        }
        fd = open(globresults.gl_pathv[0], O_RDONLY);
        n = read(fd, buffer, sizeof(buffer)-1);
        close(fd);
        if (n >= 0) buffer[n] = 0;
        mapsize = strtoul(buffer, NULL, 0); /* Map whole sram */
        offset = address;
        address = 0;
        uiodev = strstr(globresults.gl_pathv[0], "/uio/") + 5;
        *strchr(uiodev, '/') = 0;
        debug ("found SRAM device %s size %s", uiodev, buffer);
        sprintf(filename, "/dev/%s", uiodev);
        fd = open(filename, O_RDWR);
        globfree(&globresults);
        if (fd < 0)
        {
            pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
            debugErrno("open %s", filename);
            return NULL;
        }
    }
    else /* USER, SHM, VME, VME_SLAVE */
    {
        /* Tosca requires mapping windows aligned to 1 MiB
           Thus round down address to the full MiB.
           Adjust and round up size to the next full MiB.
        */
        struct vme_slave vme_window = {0};

        debug("creating new %s mapping", toscaAddrSpaceToStr(aspace));        
        if (size == 0) size = 1;
        vme_window.enable = 1;
        vme_window.vme_addr = address & ~0xffffful; /* round down to 1 MB alignment */
        vme_window.size = (size + (address & 0xffffful) + 0xffffful) & ~0xffffful; /* round up to full MB boundaries */
        vme_window.aspace = aspace & 0x0fff;
        vme_window.cycle = aspace & 0xf000;
        vme_window.aspace = aspace & 0x0fff;
        vme_window.cycle = aspace & 0xf000;

        if (aspace & VME_SLAVE)
        {
            sprintf(filename, "/dev/bus/vme/s%u", card);
            setcmd = VME_SET_SLAVE;
            getcmd = 0;            /* reading back slave windows is buggy */
            vme_window.resource_offset = res_address;
        }
        else
        {
            sprintf(filename, "/dev/bus/vme/m%u", card);
            setcmd = VME_SET_MASTER;
            getcmd = VME_GET_MASTER;
        }

        fd = open(filename, O_RDWR);
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
            return NULL;
        }

        debug("ioctl(%d, VME_SET_%s, {enable=%d addr=0x%llx size=0x%llx aspace=0x%x cycle=0x%x, resource_offset=0x%x})",
            fd, setcmd == VME_SET_MASTER ? "MASTER" : "SLAVE",
            vme_window.enable,
            (unsigned long long) vme_window.vme_addr,
            (unsigned long long) vme_window.size,
            vme_window.aspace,
            vme_window.cycle,
            vme_window.resource_offset);
        if (ioctl(fd, setcmd, &vme_window) != 0)
        {
            debugErrno("ioctl(%d, VME_SET_%s, {enable=%d addr=0x%llx size=0x%llx aspace=0x%x cycle=0x%x, resource_offset=0x%x})",
                fd, setcmd == VME_SET_MASTER ? "MASTER" : "SLAVE",
                vme_window.enable,
                (unsigned long long) vme_window.vme_addr,
                (unsigned long long) vme_window.size,
                vme_window.aspace,
                vme_window.cycle,
                vme_window.resource_offset);
            close(fd);
            pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
            return NULL;
        }

        if (getcmd)
        {
            /* If the request fits into an existing window,
               we may get that one instead of the requested one.
               That window may have a different start adddress.
            */
            if (ioctl(fd, getcmd, &vme_window) != 0)
            {
                debugErrno("ioctl(%d, VME_GET_%s)",
                    fd, getcmd == VME_GET_MASTER ? "MASTER" : "SLAVE");
                close(fd);
                pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
                return NULL;
            }
            debug("got window address=0x%llx size=0x%llx",
                (unsigned long long) vme_window.vme_addr,
                (unsigned long long) vme_window.size);
        }
        
        /* Find the MMU pages in the window we need to map */
        offset = address - vme_window.vme_addr;   /* Location within window that maps to requested address */
        address = vme_window.vme_addr;            /* Start address of the fd. */
        if (aspace & VME_A16)
            mapsize = 0x10000;                    /* Map only the small A16 space, not the big window to user space. */
        else
            mapsize = vme_window.size ;           /* Map the whole window to user space. */
    }

    ptr = mmap(NULL, mapsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    debug("mmap(NULL, size=0x%zx, PROT_READ | PROT_WRITE, MAP_SHARED, %s, 0) = %p",
            size, filename, ptr);
    if (ptr == MAP_FAILED)
    {
        debugErrno("mmap");
        close(fd);
        pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
        return NULL;
    }
    close(fd);

    map = malloc(sizeof(struct map));
    if (!map)
    {
        debugErrno("malloc");
        pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
        return NULL;
    }
    (map)->info.ptr = ptr;
    (map)->info.aspace = aspace;
    (map)->info.address = address;
    (map)->info.size = mapsize;
    (map)->next = NULL;
    *pmap = map;
    
    if (aspace & TOSCA_CSR) toscaDevices[card].csr = map;
    else if (aspace & TOSCA_IO) toscaDevices[card].io = map;
    else if (aspace & TOSCA_SRAM) toscaDevices[card].sram = map;
    pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
    
    if (offset + size > mapsize)
    {
        debug("address 0x%llx + size 0x%zx exceeds %s address space size 0x%llx",
            (unsigned long long) address + offset, size,
            toscaAddrSpaceToStr(aspace), (unsigned long long) mapsize);
        return NULL;
    }
    return ptr + offset;
}

toscaMapInfo_t toscaMapForeach(int(*func)(toscaMapInfo_t info, void* usr), void* usr)
{
    struct map *map;
    int card;

    for (card = 0; card < toscaNumDevices; card++)
    {
        for (map = toscaDevices[card].maps; map; map = map->next)
        {
            if (func(map->info, usr) != 0) break; /* loop until user func returns non 0 */
        }
        if (map) return map->info;           /* info of map where user func returned non 0 */
    }
    return (toscaMapInfo_t) {0};
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
    int card;
    toscaMapForeach((int(*)(toscaMapInfo_t, void*)) toscaMapPrintInfo, file);
    for (card = 0; card < toscaNumDevices; card++)
        toscaCheckSlaveMaps(card, 0, 0);
}

int toscaMapVMESlave(unsigned int aspace, vmeaddr_t res_address, size_t size, vmeaddr_t vme_address, int swap)
{
    const char* res;
    FILE* file;
    unsigned int card = aspace >> 16;;
    char filename[60];
    
    if (toscaNumDevices < 0) toscaInit();
    if (card >= toscaNumDevices)
    {
        debug("card %u does not exist", card);
        errno = ENODEV;
        return -1;
    }
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
    if (fprintf(file, "0x%llx:0x%zx:0x%llx:%s:%c",
        (unsigned long long) vme_address,
        size,
        (unsigned long long) res_address,
        res,
        swap ? 'y' : 'n') == -1)
    {
        debugErrno("fprintf %s", filename);
    }
    if (fclose(file) == -1)
    {
        toscaMapInfo_t overlap;
        debug("add slave window failed: %m");
        errno = EAGAIN;
        overlap = toscaCheckSlaveMaps(card, vme_address, size);
        if (overlap.aspace || overlap.address)
        {
            if (overlap.aspace == aspace && overlap.address == res_address)
            {
                error("slave window already mapped");
                return 0;
            }                
            debug("overlap with existing slave window %s:0x%llx",
                toscaAddrSpaceToStr(overlap.aspace),
                (unsigned long long) overlap.address);
            errno = EADDRINUSE;
        }
        return -1;
    }
    file = fdopen(toscaOpen(card, "enableVMESlave"), "w");
    if (file == NULL)
        return -1;
    fprintf(file, "1");
    fclose(file);
    return 0;
}

toscaMapInfo_t toscaCheckSlaveMaps(unsigned int card, vmeaddr_t addr, size_t size)
{
    FILE* file;
    unsigned long long slaveBase = -1, vmeOffs, resOffs;
    size_t mapSize;
    unsigned int mode;
    const char* res;
    toscaMapInfo_t overlap = (toscaMapInfo_t) {0};
    char buf[SIZE_STRING_BUFFER_SIZE];
    
    while (1)
    {
        file = fdopen(toscaOpen(card, "slavemaps"), "r");
        if (file == NULL)
            return overlap;
        fscanf(file, "%*[^0]%lli", &slaveBase);
        fscanf(file, "%*[^0]");
        while (fscanf(file, "%lli %zi %i %lli", &vmeOffs, &mapSize, &mode, &resOffs) > 0)
        {
            if (size==0 || (addr+size > vmeOffs && addr < vmeOffs+mapSize))
            {
                overlap.address = resOffs;
                overlap.size = mapSize;
                overlap.ptr = (void*)(size_t)vmeOffs;
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
        if (++card >= toscaNumDevices) break;
    }
    debug("overlap=%s:0x%llx", toscaAddrSpaceToStr(overlap.aspace), overlap.address);
    return overlap;
}

uint32_t toscaCsrRead(unsigned int address)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_CSR, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    return le32toh(*ptr);
}

int toscaCsrWrite(unsigned int address, uint32_t value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_CSR, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    *ptr = htole32(value);
    return 0;
}

int toscaCsrSet(unsigned int address, uint32_t value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_CSR, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    *ptr |= htole32(value);
    return 0;
}

int toscaCsrClear(unsigned int address, uint32_t value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_CSR, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    *ptr &= ~htole32(value);
    return 0;
}

uint32_t toscaIoRead(unsigned int address)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_IO, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    return le32toh(*ptr);
}

int toscaIoWrite(unsigned int address, uint32_t value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_IO, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    *ptr = htole32(value);
    return 0;
}

int toscaIoSet(unsigned int address, uint32_t value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_IO, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    *ptr |= htole32(value);
    return 0;
}

int toscaIoClear(unsigned int address, uint32_t value)
{
    volatile uint32_t* ptr = toscaMap((address & 0xffff0000)|TOSCA_IO, (address & 0xffff), 4, 0);
    if (!ptr) return -1;
    *ptr &= ~htole32(value);
    return 0;
}

pthread_mutex_t smon_mutex = PTHREAD_MUTEX_INITIALIZER;
#define CSR_SMON 0x40

uint16_t toscaSmonRead(unsigned int address)
{
    uint16_t value;
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

int toscaSmonWrite(unsigned int address, uint16_t value)
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
    pthread_mutex_unlock(&smon_mutex);
    return 0;
}

uint32_t toscaSmonStatus()
{
    volatile uint32_t* smon = toscaMap(TOSCA_IO, CSR_SMON, 12, 0);
    return le32toh(smon[3]);
}

#define CSR_VMEERR 0x418

toscaMapVmeErr_t toscaGetVmeErr(unsigned int card)
{
    volatile uint32_t* vmeerr = toscaMap((card<<16)|TOSCA_CSR, CSR_VMEERR, 8, 0);
    if (!vmeerr) return (toscaMapVmeErr_t) { .address = -1, .status = 0 };
    return (toscaMapVmeErr_t) { .address = vmeerr[0], .status = vmeerr[1] };
}

vmeaddr_t toscaStrToSize(const char* str)
{
    const char* p = str;
    vmeaddr_t size = 0, val;
    if (!str) return 0;
    while (1)
    {
        val = strtoull(p, (char**)&p, 0);
        switch (*p++)
        {
            case 'e':
            case 'E':
                size += val <<= 60; break;
            case 'p':
            case 'P':
                size += val <<= 50; break;
            case 't':
            case 'T':
                size += val <<= 40; break;
            case 'g':
            case 'G':
                size += val <<= 30; break;
            case 'm':
            case 'M':
                size += val <<= 20; break;
            case 'k':
            case 'K':
                size += val << 10; break;
            default:
                size += val; return size;
        }
    }
}

char* toscaSizeToStr(vmeaddr_t size, char* str)
{
    unsigned long long val = size;
    int l = 0;
    l = sprintf(str, "0x%llx", val);
    l += sprintf(str+l, "=");
    if (val >= 1ULL<<60)
        l += sprintf(str+l, "%lluE", val>>50);
    val &= (1ULL<<60)-1;
    if (val >= 1ULL<<50)
        l += sprintf(str+l, "%lluP", val>>40);
    val &= (1ULL<<50)-1;
    if (val >= 1ULL<<40)
        l += sprintf(str+l, "%lluT", val>>30);
    val &= (1ULL<<40)-1;
    if (val >= 1UL<<30)
        l += sprintf(str+l, "%lluG", val>>30);
    val &= (1UL<<30)-1;
    if (val >= 1UL<<20)
        l += sprintf(str+l, "%lluM", val>>20);
    val &= (1UL<<20)-1;
    if (val >= 1UL<<10)
        l += sprintf(str+l, "%lluK", val>>10);
    val &= (1UL<<10)-1;
    if (val > 0)
        l += sprintf(str+l, "%llu", val);
    return str;
}

