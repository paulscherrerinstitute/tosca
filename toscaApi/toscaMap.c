#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <glob.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#define open(path,flags) ({int _fd=open(path,(flags)&~O_CLOEXEC); if ((flags)&O_CLOEXEC) fcntl(_fd, F_SETFD, fcntl(_fd, F_GETFD)|FD_CLOEXEC); _fd; })
#endif

#include "sysfs.h"
#include "toscaMap.h"
typedef uint64_t __u64;
typedef uint32_t __u32;
typedef uint8_t __u8;
#include "vme_user.h"

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

static struct toscaDevice {
    unsigned int dom:16;
    unsigned int bus:8;
    unsigned int dev:5;
    unsigned int func:3;
    struct map *maps, *csr, *io, *sram;
    pthread_mutex_t maplist_mutex;
    unsigned int type;  /* 0x1210, 0x1211 = Tosca, 0x1001 = Althea */
    unsigned int bridgenum;
    unsigned int driververs;
} *toscaDevices;

/* resources of different boards:

IFC1210: device 0 (0x1210): TCSR, TIO, USER1, SHM1, VME
IFC1211: device 0 (0x1211): TCSR, TIO, SHM1, VME
         device 1 (0x1001): TCSR, TIO, USER1, USER2, SHM1, SHM2
IFC1410: device 0 (0x1410): TCSR, TIO, USER1, USER2, SHM1, SHM2

*/

unsigned int numDevices = 0;

unsigned int toscaNumDevices()
{
    return numDevices;
}

unsigned int toscaListDevices()
{
    unsigned int i;
    for (i = 0; i < numDevices; i++)
    {
        printf("%d %04x:%02x:%02x.%d %04x bridgenum=%d driververs=%d\n",
            i, toscaDevices[i].dom, toscaDevices[i].bus, toscaDevices[i].dev, toscaDevices[i].func,
            toscaDevices[i].type, toscaDevices[i].bridgenum, toscaDevices[i].driververs);
    }
    return i;
}

unsigned int toscaDeviceType(unsigned int device)
{
    if (device < numDevices)
        return toscaDevices[device].type;
    errno = ENODEV;
    return 0;
}

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
        numDevices = 0;
        error("no tosca devices found");
        return;
    }
    numDevices = globresults.gl_pathc;
    toscaDevices = calloc(globresults.gl_pathc, sizeof(struct toscaDevice));
    debug("found %zd tosca device%s", globresults.gl_pathc, globresults.gl_pathc==1?"":"s");
    for (i = 0; i < globresults.gl_pathc; i++)
    {
        unsigned int dom, bus, dev, func;
        int fd;
        char filename[50];

        sscanf(globresults.gl_pathv[i]+sizeof(TOSCA_PCI_DIR),
            "%x:%x:%x.%x", &dom, &bus, &dev, &func);
        toscaDevices[i].dom = dom;
        toscaDevices[i].bus = bus;
        toscaDevices[i].dev = dev;
        toscaDevices[i].func = func;
        pthread_mutex_init(&toscaDevices[i].maplist_mutex, NULL);

        sprintf(filename, "%s/device", globresults.gl_pathv[i]);
        fd = open(filename, O_RDONLY|O_CLOEXEC);
        if (fd >= 0)
        {
            toscaDevices[i].type = sysfsReadULong(fd);
            close(fd);
        }
        sprintf(filename, "%s/ToscaBridgeNr", globresults.gl_pathv[i]);
        fd = open(filename, O_RDONLY|O_CLOEXEC);
        if (fd >= 0)
        {
            toscaDevices[i].bridgenum = sysfsReadULong(fd);
            close(fd);
            /* If we have ToscaBridgeNr it is a newer driver with new device paths. */
            toscaDevices[i].driververs = 1;
        }
        else
            toscaDevices[i].bridgenum = -1;
        debug ("found %04x at %s #%d", toscaDevices[i].type, globresults.gl_pathv[i], toscaDevices[i].bridgenum);
    }
    globfree(&globresults);
}

int toscaOpen(unsigned int device, const char* resource)
{
    int fd;
    char filename[80];

    debug("device=%u resource=%s", device, resource);
    if (device >= numDevices)
    {
        error("device=%u but only %u tosca devices found", device, numDevices);
        errno = ENODEV;
        return -1;
    }
    sprintf(filename, TOSCA_PCI_DIR "/%04x:%02x:%02x.%x/%s",
        toscaDevices[device].dom,
        toscaDevices[device].bus,
        toscaDevices[device].dev,
        toscaDevices[device].func,
        resource);
    fd = open(filename, O_RDWR|O_CLOEXEC);
    if (fd < 0)
        debugErrno("open %s", filename);
    return fd;
}

const char* toscaAddrSpaceToStr(unsigned int addrspace)
{
    if (addrspace & VME_SLAVE) addrspace &= VME_SLAVE|VME_A16|VME_A24|VME_A32|VME_A64|VME_CRCSR;
    switch (addrspace & (0xffff & ~VME_SWAP))
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
        case TOSCA_SMEM1: return "SMEM1";
        case TOSCA_SMEM2: return "SMEM2";
        case TOSCA_CSR:   return "TCSR";
        case TOSCA_IO:    return "TIO";
        case TOSCA_SRAM:  return "SRAM";
        
        case VME_SLAVE:           return "SLAVE";
        case VME_SLAVE|VME_A16:   return "SLAVE16";
        case VME_SLAVE|VME_A24:   return "SLAVE24";
        case VME_SLAVE|VME_A32:   return "SLAVE32";
        case VME_SLAVE|VME_A64:   return "SLAVE64";
        case VME_SLAVE|VME_CRCSR: return "SLAVECRCSR";

        case 0: return "RAM";
        default: return "invalid";
    }
}

ssize_t toscaStrToSize(const char* str)
{
    char *q;
    if (!str) return 0;
    uint64_t size = strToSize(str, &q);
    if (*q)
    {
        errno = EINVAL;
        return -1;
    }
    if (size & ~(uint64_t)(size_t) -1)
    {
        error("%s too big for %u bit", str, (int)sizeof(size_t)*8);
        errno = EFAULT;
        return -1;
    }
    return size;
}

unsigned int toscaStrToAddrSpace(const char* str, const char** end)
{
    unsigned int addrspace = 0;
    unsigned long device = 0;
    const char *s;
    
    if (!str | !*str)
    {
fault:
        errno = EINVAL;
        if (end) *end = str;
        return 0;
    }
    
    device = strtoul(str, (char**)&s, 0);
    if (*s == ':')
        s++;
    else
    {
        device = 0;
        s = str;
    }
    if (strncasecmp(s, "TOSCA_", 6) == 0) s+=6;
    
    if ((strncasecmp(s, "USR", 3) == 0 && (s+=3)) ||
        (strncasecmp(s, "USER", 4) == 0 && (s+=4)))
    {
        if (*s == '2')
        {
            addrspace |= TOSCA_USER2;
            s++;
        }
        else
        {
            addrspace |= TOSCA_USER1;
            if (*s == '1') s++;
        }
    }
    else
    if ((strncasecmp(s, "SH_MEM", 6) == 0 && (s+=6)) ||
        (strncasecmp(s, "SHMEM", 5) == 0 && (s+=5)) ||
        (strncasecmp(s, "SMEM", 4) == 0 && (s+=4)) ||
        (strncasecmp(s, "SHM", 3) == 0 && (s+=3)))
    {
        if (*s == '2')
        {
            addrspace |= TOSCA_SMEM2;
            s++;
        }
        else
        {
            addrspace |= TOSCA_SMEM1;
            if (*s == '1') s++;
        }
    }
    else
    if ((strncasecmp(s, "TCSR", 4) == 0 && (s+=4)) ||
        (strncasecmp(s, "CSR", 9) == 0 && (s+=9)))
        addrspace |= TOSCA_CSR;
    else
    if ((strncasecmp(s, "TIO", 3) == 0 && (s+=3)) ||
        (strncasecmp(s, "IO", 8) == 0 && (s+=8)))
        addrspace |= TOSCA_IO;
    else
    if ((strncasecmp(s, "SRAM", 4) == 0 && (s+=4)))
        addrspace |= TOSCA_SRAM;
    else
    {
        if (strncasecmp(s, "VME_", 4) == 0) s+=4;
        if (strncasecmp(s, "CRCSR", 5) == 0 && (s+=5))
            addrspace |= VME_CRCSR;
        else
        if (strncasecmp(s, "SLAVE", 5) == 0 && (s+=5))
        {
            addrspace |= VME_SLAVE;
            switch (strtol(s, (char**)&s, 10))
            {
                case 16:
                    addrspace |= VME_A16; break;
                case 24:
                    addrspace |= VME_A24; break;
                case 0:
                case 32:
                    addrspace |= VME_A32; break;
                case 64:
                    addrspace |= VME_A64; break;
                default:
                    goto fault;
            }
        }
        else
        if (*s == 'A' || *s == 'a')
        {
            switch (strtol(++s, (char**)&s, 10))
            {
                case 16:
                    addrspace |= VME_A16; break;
                case 24:
                    addrspace |= VME_A24; break;
                case 32:
                    addrspace |= VME_A32; break;
                case 64:
                    addrspace |= VME_A64; break;
                default:
                    goto fault;
            }
            do
            {
                if (*s == '*') addrspace |= VME_SUPER;
                else
                if (*s == '#') addrspace |= VME_PROG;
                else
                break;
            } while (s++);
        }
    }
    if (*s != 0 && *s !=':')
        goto fault;
    if (*s == ':') s++;
    if (addrspace != 0)
        addrspace |= device << 16;
    else if (device != 0)
        goto fault;
    if (end) *end = s;
    return addrspace;
}

toscaMapAddr_t toscaStrToAddr(const char* str, const char** end)
{
    toscaMapAddr_t result = {0,0};
    const char *s;
    
    if (!str)
    {
        errno = EINVAL;
        if (end) *end = str;
        return (toscaMapAddr_t) {0,0};
    }
    result.addrspace = toscaStrToAddrSpace(str, &s);
    result.address = strToSize(s, (char**)&s);
    if (end) (*end = s);
    else
    if (*s != 0)
    {
        errno = EINVAL;
        return (toscaMapAddr_t){0,0};
    }
    return result;
}

volatile void* toscaMap(unsigned int addrspace, uint64_t address, size_t size, uint64_t res_address)
{
    struct map **pmap, *map;
    volatile void *baseptr;
    size_t offset, mapsize;
    int fd = -1;
    unsigned int device;
    unsigned int setcmd, getcmd;
    char filename[80];

    device = addrspace >> 16;

    debug("addrspace=0x%x(%u:%s%s%s%s):0x%"PRIx64"[0x%zx]",
        addrspace, device,
        toscaAddrSpaceToStr(addrspace),
        addrspace & VME_SLAVE ? "->" : "",
        addrspace & VME_SLAVE ? toscaAddrSpaceToStr(addrspace & ~(VME_SLAVE|VME_A16|VME_A24|VME_A32|VME_A64|VME_CRCSR)) : "",
        addrspace & VME_SWAP ? " SWAP" : "",
        address,
        size);

    if (device >= numDevices)
    {
        debug("device %u does not exist", device);
        errno = ENODEV;
        return NULL;
    }

    /* Quick access to TCSR, CIO and SRAM (we have max one full range map of each) */
    if (((addrspace == TOSCA_CSR) && (map = toscaDevices[device].csr) != NULL) ||
        ((addrspace == TOSCA_IO) && (map = toscaDevices[device].io) != NULL) ||
        ((addrspace == TOSCA_SRAM) && (map = toscaDevices[device].sram) != NULL))
    {
        if (address + size > map->info.size)
        {
            debug("address 0x%"PRIx64" + size 0x%zx exceeds %s size 0x%zx",
                address, size,
                toscaAddrSpaceToStr(addrspace), map->info.size);
            return NULL;
        }
        return map->info.baseptr + address;
    }

    /* Lookup can be lock free because we only ever append to list. */
    pmap = &toscaDevices[device].maps;
check_existing_maps:
    while ((map = *pmap) != NULL)
    {
        debug("%u:%s:0x%"PRIx64"[0x%zx] check addrspace=0x%x(%s):0x%"PRIx64"[0x%zx]",
            device, toscaAddrSpaceToStr(addrspace), address, size,
            map->info.addrspace,
            toscaAddrSpaceToStr(map->info.addrspace),
            map->info.baseaddress,
            map->info.size);
        if (addrspace == map->info.addrspace &&
            address >= map->info.baseaddress &&
            address + size <= map->info.baseaddress + map->info.size)
        {
            if ((addrspace & 0xfe0) > VME_SLAVE)
            {
                /* Existing VME slave to Tosca resource: Check resource address. */
                if (res_address != (uint64_t)(size_t) map->info.baseptr + (address - map->info.baseaddress))
                {
                    debug("overlap with existing SLAVE map to %s:0x%"PRIx64,
                        toscaAddrSpaceToStr(map->info.addrspace & ~VME_SLAVE),
                            map->info.baseaddress);
                    errno = EADDRINUSE;
                    return NULL;
                }
            }
            debug("%u:%s:0x%"PRIx64"[0x%zx] use existing map %s:0x%"PRIx64"[0x%zx]+0x%"PRIx64,
                device, toscaAddrSpaceToStr(addrspace), address, size,
                toscaAddrSpaceToStr(map->info.addrspace), map->info.baseaddress, map->info.size,
                (address - map->info.baseaddress));
            return map->info.baseptr + (address - map->info.baseaddress);
        }
        pmap = &(*pmap)->next;
    }

    /* No matching map found. Serialize creating new maps. */
    pthread_mutex_lock(&toscaDevices[device].maplist_mutex);
    if (*pmap != NULL)
    {
        /* New maps have been added while we were sleeping, maybe the one we need? */
        pthread_mutex_unlock(&toscaDevices[device].maplist_mutex);
        goto check_existing_maps;
    }

    debug("creating new %s mapping", toscaAddrSpaceToStr(addrspace));
    /* TOSCA_CSR shares a bit with TOSCA_SMEM2 due to my bad design decision for older Tosca API
       but I do not want to break binary compatibility. */
    if (addrspace & (TOSCA_CSR | TOSCA_IO) && !(addrspace & 0x2000))
    {
        struct stat filestat;
        
        fd = toscaOpen(device, addrspace & TOSCA_CSR ? "resource3" : "resource4");
        if (fd < 0) goto fail;
        fstat(fd, &filestat);
        mapsize = filestat.st_size;  /* Map whole address space. */
        offset = address;            /* Location within fd that maps to requested address. */
        address = 0;                 /* This is the start address of the fd. */
        if (toscaMapDebug)
        {
            sprintf(filename, TOSCA_PCI_DIR "/%04x:%02x:%02x.%x/%s",
                toscaDevices[device].dom,
                toscaDevices[device].bus,
                toscaDevices[device].dev,
                toscaDevices[device].func,
                addrspace & TOSCA_CSR ? "resource3" : "resource4");
        }
    }
    else if (addrspace & TOSCA_SRAM)
    {
        glob_t globresults;
        ssize_t n;
        char* uiodev;
        char buffer[24];

        if (device != 0)
        {
            debug("access to SRAM only on local device");
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
        fd = open(globresults.gl_pathv[0], O_RDONLY|O_CLOEXEC);
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            goto fail;
        }
        n = read(fd, buffer, sizeof(buffer)-1);
        if (n < 0)
        {
            debugErrno("cannot read %s", filename);
            goto fail;
        }
        close(fd);
        if (n >= 0) buffer[n] = 0;
        mapsize = strtoul(buffer, NULL, 0); /* Map whole SRAM. */
        offset = address;
        address = 0;
        uiodev = strstr(globresults.gl_pathv[0], "/uio/") + 5;
        *strchr(uiodev, '/') = 0;
        debug ("found SRAM device %s size %s", uiodev, buffer);
        sprintf(filename, "/dev/%s", uiodev);
        fd = open(filename, O_RDWR|O_CLOEXEC);
        globfree(&globresults);
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            goto fail;
        }
    }
    else if (addrspace & (VME_A16|VME_A24|VME_A32|VME_A64|VME_CRCSR|VME_SLAVE|TOSCA_USER1|TOSCA_USER2|TOSCA_SMEM1|TOSCA_SMEM2))
    {
        struct vme_slave vme_window = {0,0,0,0,0,0};
        
        if (size == (size_t)-1)
        {
            error("invalid size");
            errno = EINVAL;
            goto fail;
        }
        if (size == 0) size = 1;
        /* Tosca requires mapping windows aligned to 1 MiB.
           Thus round down address to the full MiB and adjust size.
           Do not round up size to the the next full MiB! This makes A16 fail.
        */
        vme_window.enable   = 1;
        vme_window.vme_addr = address & ~0xfffffLL; /* Round down to 1 MB alignment */
        vme_window.size     = size + (address & 0xfffffLL); /* and adjust size. */
        vme_window.aspace   = addrspace & 0x0fff;
        vme_window.cycle    = addrspace & (0xf000 & ~VME_SWAP);

        if (addrspace & VME_SLAVE)
        {
            if ((addrspace & 0xfe0) > VME_SLAVE && (res_address ^ address) & 0xfffffLL)
            {
                error("slave address 0x%"PRIx64" not aligned with resource address 0x%"PRIx64,
                    address, res_address);
                errno = EFAULT;
                goto fail;
            }
            if ((res_address + size) > 0x10000000LL)
            {
                error("resource address 0x%"PRIx64" + size 0x%zx exceeds 256 MB",
                    address, size);
                errno = EFAULT;
                goto fail;
            }
            if (addrspace & ~(VME_SLAVE|TOSCA_USER1|TOSCA_USER2|TOSCA_SMEM1|TOSCA_SMEM2|VME_A32|VME_SWAP))
            {
                /* mapping to USER2 succeeds but crashes the kernel when accessing the VME range :-P */
                error("slave map only possible on A32 to memory, USER[1|2] or SMEM[1|2]");
                errno = EINVAL;
                goto fail;
            }
            vme_window.resource_offset = res_address & ~0xfffffLL;
            vme_window.aspace = addrspace & (TOSCA_USER1|TOSCA_USER2|TOSCA_SMEM1|TOSCA_SMEM2);
            if (!vme_window.aspace) vme_window.aspace = VME_A32;
            if (addrspace & VME_SWAP) vme_window.cycle |= VME_LE_TO_BE;
            vme_window.size += 0xfffffLL; /* Expand slave maps to MB boundary. */
            vme_window.size &= ~0xfffffLL;
            if ((addrspace & 0xfe0) == VME_SLAVE && vme_window.size > 0x400000LL) /* more than 4 MB to RAM*/
            {
                error("slave windows size 0x%"PRIx64" exceeds 4 MB",
                    vme_window.size);
                errno = ENOMEM;
                goto fail;
            }
            if (toscaDevices[device].driververs > 0)
                sprintf(filename, "/dev/bus/vme/s%u-%u", toscaDevices[device].bridgenum, 0);
            else
                sprintf(filename, "/dev/bus/vme/s%u", device);
            setcmd = VME_SET_SLAVE;
            getcmd = 0; /* Reading back slave windows is buggy. */
        }
        else
        {
            if (((addrspace & VME_A16) && address + size > 0x10000LL) ||
                ((addrspace & (VME_A24|VME_CRCSR)) && address + size > 0x1000000LL))
            {
                error("address 0x%"PRIx64" + size 0x%zx exceeds %s address space size",
                    address, size,
                    toscaAddrSpaceToStr(addrspace));
                errno = EFAULT;
                goto fail;
            }

            if (toscaDevices[device].driververs > 0)
                sprintf(filename, "/dev/bus/vme/m%u-%u", toscaDevices[device].bridgenum, 0);
            else
                sprintf(filename, "/dev/bus/vme/m%u", device);
            setcmd = VME_SET_MASTER;
            getcmd = VME_GET_MASTER;
            vme_window.aspace = addrspace & 0x0fff;
        }

        fd = open(filename, O_RDWR|O_CLOEXEC);
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            goto fail;
        }

        if (ioctl(fd, setcmd, &vme_window) != 0)
        {
            if (setcmd == VME_SET_SLAVE && errno == ENODEV)
            {
                debug("overlap with existing SLAVE map");
                errno = EADDRINUSE;
            }
            debugErrno("ioctl(%d (%s), VME_SET_%s, {enable=%d addr=0x%"PRIx64" size=0x%"PRIx64" addrspace=0x%x cycle=0x%x, resource_offset=0x%x})",
                fd, filename, setcmd == VME_SET_MASTER ? "MASTER" : "SLAVE",
                vme_window.enable,
                vme_window.vme_addr,
                vme_window.size,
                vme_window.aspace,
                vme_window.cycle,
                vme_window.resource_offset);
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
                goto fail;
            }
            debug("got window address=0x%"PRIx64" size=0x%"PRIx64,
                vme_window.vme_addr,
                vme_window.size);
        }
        res_address = vme_window.resource_offset;
        
        /* Find the MMU pages in the window we need to map. */
        offset = address - vme_window.vme_addr;   /* Location within window that maps to requested address */
        address = vme_window.vme_addr;            /* Start address of the fd. */
        if (addrspace & VME_A16)
            mapsize = 0x10000;                    /* Map only the small A16 space, not the big window to user space. */
        else
            mapsize = vme_window.size ;           /* Map the whole window to user space. */
    }
    else
    {
        error("invalid address space");
        errno = EINVAL;
        goto fail;
    }

    if ((addrspace & 0xfe0) > VME_SLAVE)
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
    map->info.addrspace = addrspace | (device << 16);
    map->info.baseaddress = address;
    map->info.size = mapsize;
    map->info.baseptr = baseptr;
    map->next = NULL;
    *pmap = map;

    pthread_mutex_unlock(&toscaDevices[device].maplist_mutex);
    
    if ((addrspace & 0xfe0) > VME_SLAVE)
    {
        /* VME_SLAVE windows to Tosca resources have no pointer to return */
        return NULL;
    }
        
    if (addrspace == TOSCA_CSR) toscaDevices[device].csr = map;
    else if (addrspace == TOSCA_IO) toscaDevices[device].io = map;
    else if (addrspace == TOSCA_SRAM) toscaDevices[device].sram = map;
    
    if (offset + size > mapsize)
    {
        debug("address 0x%"PRIx64" + size 0x%zx exceeds %s address space size 0x%zx",
            address + offset, size,
            toscaAddrSpaceToStr(addrspace), mapsize);
        errno = EFAULT;
        return NULL;
    }
    return baseptr + offset;

fail:
    if (fd >= 0)
    {
        int e = errno;
        close(fd);
        errno = e;
    }
    pthread_mutex_unlock(&toscaDevices[device].maplist_mutex);
    return NULL;
}

toscaMapInfo_t toscaMapForeach(int(*func)(toscaMapInfo_t info, void* usr), void* usr)
{
    struct map *map;
    unsigned int device;

    for (device = 0; device < numDevices; device++)
    {
        for (map = toscaDevices[device].maps; map; map = map->next)
        {
            if (func(map->info, usr) != 0) break; /* loop until user func returns non 0 */
        }
        if (map) return map->info;        /* info of map where user func returned non 0 */
    }
    return (toscaMapInfo_t) {0,0,0,0};
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
    return (toscaMapAddr_t) { .addrspace = info.addrspace, .address = info.baseaddress + (ptr - info.baseptr) };
}
