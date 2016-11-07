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
    int fd;
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

void toscaInit() __attribute__((__constructor__));
void toscaInit()
{
    glob_t globresults;
    int status;
    size_t i;

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
        debugErrno("open %s", filename);
    return fd;
}

const char* toscaAddrSpaceToStr(unsigned int aspace)
{
    switch (aspace & (0xffff & ~VME_SWAP))
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
        case TOSCA_SMEM:  return "SMEM";
        case TOSCA_CSR:   return "TCSR";
        case TOSCA_IO:    return "TIO";
        case TOSCA_SRAM:  return "SRAM";

        case VME_SLAVE:   return "SLAVE";

        case 0: return "none";
        default:
        {
            static char buf[20];
            sprintf(buf, "0x%x", aspace);
            return buf;
        }
    }
}

volatile void* toscaMap(unsigned int aspace, vmeaddr_t address, size_t size, vmeaddr_t res_address)
{
    struct map **pmap, *map;
    volatile void *baseptr;
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

    if (card >= toscaNumDevices)
    {
        debug("card %u does not exist", card);
        errno = ENODEV;
        return NULL;
    }

    /* Quick access to TCSR, CIO and SRAM */
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
        return map->info.baseptr + address;
    }

    /* Lookup can be lock free because we only ever append to list. */
    pmap = &toscaDevices[card].maps;
check_existing_maps:
    for (; (map = *pmap) != NULL; pmap = &(*pmap)->next)
    {
        debug("%u:%s:0x%llx[0x%zx] check aspace=0x%x(%s), address=0x%llx, size=0x%zx",
            card, toscaAddrSpaceToStr(aspace), (unsigned long long) address, size,
            map->info.aspace,
            toscaAddrSpaceToStr(map->info.aspace),
            (unsigned long long) map->info.baseaddress,
            map->info.size);
        if (aspace == map->info.aspace &&
            address >= map->info.baseaddress &&
            address + size <= map->info.baseaddress + map->info.size)
        {
            if ((aspace & 0xfff) > VME_SLAVE)
            {
                /* Existing VME slave to Tosca resource: Check resource address. */
                if (res_address != (vmeaddr_t)(size_t) map->info.baseptr + (address - map->info.baseaddress))
                {
                    errno = EADDRINUSE;
                    return NULL;
                }
            }
            debug("%u:%s:0x%llx[0x%zx] use existing window addr=0x%llx size=0x%zx offset=0x%llx",
                card, toscaAddrSpaceToStr(aspace), (unsigned long long) address, size,
                (unsigned long long) map->info.baseaddress, map->info.size,
                (unsigned long long) (address - map->info.baseaddress));
            return map->info.baseptr + (address - map->info.baseaddress);
        }
    }

    /* No matching map found. Serialize creating new maps. */
    pthread_mutex_lock(&toscaDevices[card].maplist_mutex);
    if (*pmap)
    {
        /* New maps were added while we were sleeping, maybe the one we need? */
        pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
        goto check_existing_maps;
    }

    if (aspace & (TOSCA_CSR | TOSCA_IO))
    {
        struct stat filestat;
        
        debug("creating new %s mapping", aspace & TOSCA_CSR ? "TCSR" : "TIO");
        fd = toscaOpen(card, aspace & TOSCA_CSR ? "resource3" : "resource4");
        if (fd < 0) goto fail;
        fstat(fd, &filestat);
        mapsize = filestat.st_size;  /* Map whole address space. */
        offset = address;            /* Location within fd that maps to requested address. */
        address = 0;                 /* This is the start address of the fd. */
        if (toscaMapDebug)
        {
            sprintf(filename, TOSCA_PCI_DIR "/%04x:%02x:%02x.%x/%s",
                toscaDevices[card].dom,
                toscaDevices[card].bus,
                toscaDevices[card].dev,
                toscaDevices[card].func,
                aspace & TOSCA_CSR ? "resource3" : "resource4");
        }
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
            debug("access to SRAM only on local card");
            errno = EINVAL;
            goto fail;
        }

        sprintf(filename, "/sys/bus/platform/devices/*.sram/uio/uio*/maps/map0/size");
        debug("glob(%s)", filename);
        if (glob(filename, GLOB_ONLYDIR, NULL, &globresults) != 0)
        {
            debug("cannot find SRAM device");
            errno = ENODEV;
            goto fail;
        }
        fd = open(globresults.gl_pathv[0], O_RDONLY);
        n = read(fd, buffer, sizeof(buffer)-1);
        close(fd);
        if (n >= 0) buffer[n] = 0;
        mapsize = strtoul(buffer, NULL, 0); /* Map whole SRAM. */
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
            debugErrno("open %s", filename);
            goto fail;
        }
    }
    else /* USER, SMEM, VME, VME_SLAVE */
    {
        /* Tosca requires mapping windows aligned to 1 MiB.
           Thus round down address to the full MiB and adjust size.
           Do not round up size to the the next full MiB! This makes A16 fail.
        */
        struct vme_slave vme_window = {0};
        
        if (size == -1) goto fail;
        if ((address + size) & 0xffffffff00000000ull)
        {
            error("address out of 32 bit range");
            errno = EFAULT;
            goto fail;
        }
        if (((aspace & VME_A16) && address + size > 0x10000) ||
            ((aspace & (VME_A24|VME_CRCSR)) && address + size > 0x1000000))
        {
            error("address 0x%llx + size 0x%zx exceeds %s address space",
                (unsigned long long) address, size,
                toscaAddrSpaceToStr(aspace));
            errno = EFAULT;
            goto fail;
        }
        debug("creating new %s mapping", toscaAddrSpaceToStr(aspace));        
        if (size == 0) size = 1;
        vme_window.enable   = 1;
        vme_window.vme_addr = address & ~0xffffful; /* Round down to 1 MB alignment */
        vme_window.size     = size + (address & 0xffffful); /* and adjust size. */
        vme_window.aspace   = aspace & 0x0fff;
        vme_window.cycle    = aspace & (0xf000 & ~VME_SWAP);

        if (aspace & VME_SLAVE)
        {
            sprintf(filename, "/dev/bus/vme/s%u", card);
            setcmd = VME_SET_SLAVE;
            getcmd = 0; /* Reading back slave windows is buggy. */
            vme_window.resource_offset = res_address - (address & 0xffffful);
            vme_window.aspace = aspace & (0x0fff & ~VME_SLAVE);
            if (!vme_window.aspace) vme_window.aspace = VME_A32;
            if (aspace & VME_SWAP) vme_window.cycle |= VME_LE_TO_BE;
            vme_window.size += 0xffffful; /* Expand slave maps to MB boundary. */
            vme_window.size &= ~0xffffful;
        }
        else
        {
            sprintf(filename, "/dev/bus/vme/m%u", card);
            setcmd = VME_SET_MASTER;
            getcmd = VME_GET_MASTER;
            vme_window.aspace = aspace & 0x0fff;
        }

        fd = open(filename, O_RDWR);
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            goto fail;
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
            goto fail;
        }

        if (getcmd)
        {
            /* If the request fits into an existing window,
               we may get that one instead of the requested one
               which may have a different base adddress.
            */
            if (ioctl(fd, getcmd, &vme_window) != 0)
            {
                debugErrno("ioctl(%d, VME_GET_%s)",
                    fd, getcmd == VME_GET_MASTER ? "MASTER" : "SLAVE");
                close(fd);
                goto fail;
            }
            debug("got window address=0x%llx size=0x%llx",
                (unsigned long long) vme_window.vme_addr,
                (unsigned long long) vme_window.size);
        }
        res_address = vme_window.resource_offset;
        
        /* Find the MMU pages in the window we need to map. */
        offset = address - vme_window.vme_addr;   /* Location within window that maps to requested address */
        address = vme_window.vme_addr;            /* Start address of the fd. */
        if (aspace & VME_A16)
            mapsize = 0x10000;                    /* Map only the small A16 space, not the big window to user space. */
        else
            mapsize = vme_window.size ;           /* Map the whole window to user space. */
    }

    if ((aspace & 0xfff) > VME_SLAVE)
    {
        /* VME_SLAVE windows to Tosca resources do not use mmap. */
        baseptr = (void*)(size_t) res_address;
    }
    else
    {
        baseptr = mmap((void*)(size_t) res_address, mapsize, PROT_READ | PROT_WRITE, res_address ? MAP_PRIVATE | MAP_FIXED : MAP_SHARED, fd, 0);
        debug("mmap(%p, size=0x%zx, PROT_READ | PROT_WRITE, %s, %s, 0) = %p",
            (void*)(size_t) res_address, mapsize, res_address ? "MAP_PRIVATE | MAP_FIXED" : "MAP_SHARED", filename, baseptr);
        if (baseptr == MAP_FAILED || baseptr == NULL)
        {
            debugErrno("mmap");
            close(fd);
            goto fail;
        }
    }

    /* Fill in map info and append to list. */
    map = malloc(sizeof(struct map));
    if (!map)
    {
        debugErrno("malloc");
        goto fail;
    }
    map->info.aspace = aspace;
    map->info.baseaddress = address;
    map->info.size = mapsize;
    map->info.baseptr = baseptr;
    map->next = NULL;
    *pmap = map; /* Pointer assignment is atomic. */

    pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
    
    if (aspace & TOSCA_CSR) toscaDevices[card].csr = map;
    else if (aspace & TOSCA_IO) toscaDevices[card].io = map;
    else if (aspace & TOSCA_SRAM) toscaDevices[card].sram = map;
    
    if (offset + size > mapsize)
    {
        debug("address 0x%llx + size 0x%zx exceeds %s address space size 0x%llx",
            (unsigned long long) address + offset, size,
            toscaAddrSpaceToStr(aspace), (unsigned long long) mapsize);
        errno = EFAULT;
        return NULL;
    }
    return baseptr + offset;

fail:
    pthread_mutex_unlock(&toscaDevices[card].maplist_mutex);
    return NULL;
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
    return ptr >= info.baseptr && ptr < info.baseptr + info.size;
}

toscaMapInfo_t toscaMapFind(const volatile void* ptr)
{
    return toscaMapForeach(toscaMapPtrCompare, (void*)ptr);
}

toscaMapAddr_t toscaMapLookupAddr(const volatile void* ptr)
{
    toscaMapInfo_t info = toscaMapFind(ptr);
    return (toscaMapAddr_t) { info.aspace, info.baseaddress + (ptr - info.baseptr) };
}
