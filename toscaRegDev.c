#define __STDC_VERSION__ 199901L

#include <stdlib.h>
#include <string.h>
#include <endian.h>

#include <epicsTypes.h>
#include <epicsExit.h>
#include <dbAccess.h>
#include <iocsh.h>

#include <regDev.h>

#include "toscaDevLib.h"
#include "toscaMap.h"
#include "toscaDma.h"
#include "toscaIntr.h"

typedef uint64_t __u64;
typedef uint32_t __u32;
typedef uint8_t __u8;
#include "vme.h"
#include "vme_user.h"

#define VME_BLOCKTRANSFER (VME_SCT|VME_MBLT|VME_BLT|VME_2eVMEFast|VME_2eVME|VME_2eSST320|VME_2eSST267|VME_2eSST160)

#include <epicsExport.h>

#define TOSCA_DEBUG_NAME toscaRegDev
#include "toscaDebug.h"
epicsExportAddress(int, toscaRegDevDebug);

struct regDevice
{
    const char* name;
    size_t baseaddr;
    volatile void* baseptr;
    size_t dmaLimit;
    unsigned int blockmode;
    unsigned int blocksize;
    unsigned int aspace;
    unsigned int dmaSpace;
    int swap;
    int ivec;
    IOSCANPVT ioscanpvt[256];
};

void toscaRegDevReport(regDevice *device, int level)
{
    toscaMapAddr_t addr = toscaMapLookupAddr(device->baseptr);
    printf("Tosca %s:0x%llx", toscaAddrSpaceToStr(addr.aspace), addr.address);
    if (device->swap)
        printf(", swap=%s",
            device->swap == 2 ? "WS" : device->swap == 4 ? "DS" : device->swap == 8 ? "QS" : "??");
    if (device->blockmode)
        printf(", block mode");
    else
        if (device->dmaLimit <= 1) printf(", DMA=%s", device->dmaLimit ? "always" : "never");
        else printf(", DMA>=%zd elem", device->dmaLimit);
    printf("\n");
}

int toscaRegDevRead(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    int priority,
    regDevTransferComplete callback,
    char* user)
{
    if (!nelem || !dlen) return SUCCESS;

    if (nelem > device->dmaLimit)
    {
        /* TODO: asynchronous DMA transfer? */
        int status = toscaDmaRead(device->dmaSpace, device->baseaddr + offset, pdata, nelem*dlen, device->swap);
        if (status) debugErrno("toscaDmaRead %s %s:0x%zx %s:0x%zx[0x%zx] swap=%d",
            user, device->name, offset, toscaDmaTypeToStr(device->dmaSpace), device->baseaddr + offset, nelem*dlen, device->swap);
        return status;
    }

    assert(device->baseptr != NULL);
    assert(pdata != NULL);
    regDevCopy(dlen, nelem, device->baseptr + offset, pdata, NULL, device->swap ? REGDEV_DO_SWAP : REGDEV_NO_SWAP);
    return SUCCESS;
};

int toscaRegDevWrite(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    void* pmask,
    int priority,
    regDevTransferComplete callback,
    char* user)
{
    if (!nelem || !dlen) return SUCCESS;

    if (pmask == NULL && device->dmaSpace && nelem > device->dmaLimit)
    {
        /* TODO: asynchronous DMA transfer? */
        int status = toscaDmaWrite(pdata, device->dmaSpace, device->baseaddr + offset, nelem*dlen, device->swap);
        if (status) debugErrno("toscaDmaWrite %s %s:0x%zx %s:0x%zx[0x%zx] swap=%d",
            user, device->name, offset, toscaDmaTypeToStr(device->dmaSpace), device->baseaddr + offset, nelem*dlen, device->swap);
        return status;
    }

    /* TODO: check alignment of offset and nelem*dlen with device->swap */
    if (pmask && dlen != device->swap) /* mask with different dlen than swap */
    {
        int i;
        epicsUInt64 mask;
        debug("mask, swap, regDevCopy");
        for (i = 0; i < 8/dlen; i++) memcpy((char*)&mask + dlen * i, pmask, dlen);
        pmask = &mask;
    }

    assert(device->baseptr != NULL);
    assert(pdata != NULL);
    regDevCopy(device->swap, nelem*dlen/device->swap, pdata, device->baseptr + offset, pmask, device->swap ? REGDEV_DO_SWAP : REGDEV_NO_SWAP);
    return SUCCESS;
};

/* Instead of relaying the record processing to a
   callback thread do it directly in the interrupt
   handler thread to save one thread context switch.
*/
#include <callback.h>
typedef struct scan_list{
    epicsMutexId        lock;
    ELLLIST             list;
    short               modified;
} scan_list;
typedef struct io_scan_list {
    CALLBACK            callback;
    scan_list           scan_list;
    struct io_scan_list *next;
} io_scan_list;

void toscaScanIoRequest(IOSCANPVT pioscanpvt)
{
    int prio;
    
    for (prio = 0; prio < 3; prio++) {
        io_scan_list *piosl = &pioscanpvt[prio];
        if (ellCount(&piosl->scan_list.list) > 0)
        {
            piosl->callback.callback(&piosl->callback);
        }
    }
}

static IOSCANPVT toscaRegDevGetIoScanPvt(regDevice *device, size_t ivec)
{
    debug("ivec=0x%x", ivec);
    if (ivec == (size_t)-1)
    {
        if (device->blockmode)
        {
            debug("blockmode");
            if (device->ioscanpvt[0] == NULL)
            {
                scanIoInit(&device->ioscanpvt[0]);
            }
            return device->ioscanpvt[0];
        }

        debug("default ivec=0x%x", device->ivec);
        if (device->ivec == -1)
        {
            error("No interrupt vector defined");
            return NULL;
        }
        ivec = device->ivec;
    }
    if (device->aspace & (VME_A16|VME_A24|VME_A32|VME_A64))
    {
        if (ivec > 255)
        {
            error("%s interrupt %d out of range 1-255\n",
                toscaAddrSpaceToStr(device->aspace), ivec);
            return NULL;
        }
    }
    else
    if (device->aspace & (TOSCA_USER1|TOSCA_USER2|TOSCA_SHM))
    {
        if (ivec > 15)
        {
            error("%s interrupte %d out of range 0-15\n",
                toscaAddrSpaceToStr(device->aspace), ivec);
            return NULL;
        }
        ivec += (device->aspace & TOSCA_USER2) ? 0x110 : 0x100;
    }
    else
    {
        error("I/O Intr not supported on %s", toscaAddrSpaceToStr(device->aspace));
        return NULL;
    }

    if (device->ioscanpvt[ivec&0xff] == NULL)
    {
        debug("init %s interrupt %d handling", toscaAddrSpaceToStr(device->aspace), ivec&0xff);
        scanIoInit(&device->ioscanpvt[ivec&0xff]);
        if (toscaDevLibConnectInterrupt(ivec, toscaScanIoRequest, device->ioscanpvt[ivec&0xff]) != 0)
        {
            error("toscaDevLibConnectInterrupt(0x%x,...) failed", ivec);
            return NULL;
        }
    }
    else
        debug("%s interrupt %d handling already active", toscaAddrSpaceToStr(device->aspace), ivec&0xff);
    return device->ioscanpvt[ivec&0xff];
}

void* toscaRegDevDmaAlloc(regDevice *device, void* ptr, size_t size)
{
    if (ptr) free(ptr);
    if (size) return valloc(size); /* in principle we can use any memory, but page aligned is more efficient */
    return NULL;
}

struct regDevSupport toscaRegDev = {
    .report = toscaRegDevReport,
    .read = toscaRegDevRead,
    .write = toscaRegDevWrite,
    .getInScanPvt = toscaRegDevGetIoScanPvt,
    .getOutScanPvt = toscaRegDevGetIoScanPvt,
};

int toscaRegDevConfigure(const char* name, const char* resource, size_t address, size_t size, const char* flags)
{
    regDevice* device;
    const char *p;
    
    if (name == NULL)
    {
        iocshCmd("help toscaRegDevConfigure");
        printf("resources: USER1 (or USER), USER2, SHM, TCSR, CRCSR, A16, A24, A32\n"
               "   (add * for 'supervisory' and # for 'program' access to Axx modes)\n"
               "flags:\n"
               "   - swap: NS (none), WS (word), DS (double word) QS (quad word)\n"
               "           WL, WB, DL, DB, QB, QB (convert to/from little/big endian)\n"
               "       (Default for VME modes is NS, for others DL)\n"
               "   - DMA mode element minimum: DMA[=xxx]\n"
               "       (0:never, 1:always, default:100)\n"
               "   - block mode: block\n"
               "       (records with PRIO=HIGH trigger transfer, implies DMA=1)\n"
               "   - VME block mode: SCT, BLT, MBLT, 2eVME, 2eSST[160|233|320]\n"
               "       (VME resource without block mode implies DMA=0)\n"
               "   - VME block size: bs=128|256|512\n"
               "       (Default is 512)\n"
               "   - VME interrupt vector: intr=1...255\n"
               "   - USER[1|2] interrupt line: intr=0...15\n"
        );
               
        return -1;
    }
    
    debug("toscaRegDevConfigure(name=%s, resource=%s, address=0x%zx size=0x%zx, flags=\"%s\")",
        name, resource, address, size, flags);
    
    if (regDevFind(name))
    {
        error("name \"%s\" already in use", name);
        return -1;
    }

    if ((device = calloc(1, sizeof(regDevice))) == NULL)
    {
        error("cannot allocate device structure: %m");
        return -1;
    }

    if ((device->aspace = toscaStrToAddrSpace(resource)) == 0)
    {
        error("unknown Tosca resource \"%s\"", resource);
        return -1;
    }

    device->name = strdup(name);
    device->baseaddr = address;
    if (device->aspace & (TOSCA_USER1|TOSCA_USER2|TOSCA_SHM|TOSCA_CSR)) device->swap = 4;
    device->blocksize = 512;
    device->dmaLimit = 100;
    device->ivec = -1;

    if (device->aspace & (TOSCA_USER1|TOSCA_USER2|TOSCA_SHM))
        device->dmaSpace = device->aspace;
    if (device->aspace & VME_A32)
        device->dmaSpace  = VME_SCT;

    if (flags)
    {
        if (strstr(flags, "NS")) device->swap = 0;
        if (strstr(flags, "WS")) device->swap = 2;
        if (strstr(flags, "DS")) device->swap = 4;
        if (strstr(flags, "QS")) device->swap = 8;
#if __BYTE_ORDER == __LITTLE_ENDIAN
        if (strstr(flags, "WB")) device->swap = 2;
        if (strstr(flags, "DB")) device->swap = 4;
        if (strstr(flags, "QB")) device->swap = 8;
#else
        if (strstr(flags, "WL")) device->swap = 2;
        if (strstr(flags, "DL")) device->swap = 4;
        if (strstr(flags, "QL")) device->swap = 8;
#endif
        debug("swap = %d", device->swap);

        if (strstr(flags, "block"))
        {
            device->blockmode = 1;
//          blockmode not yet implemented
//            device->dmaLimit = 1; /* always */
        }
        debug("blockmode = %d", device->blockmode);

        if ((p = strstr(flags, "NODMA")))
            device->dmaSpace = 0;
        else if ((p = strstr(flags, "SCT")))
            device->dmaSpace = VME_SCT;
        else if ((p = strstr(flags, "MBLT")))
            device->dmaSpace = VME_MBLT;
        else if ((p = strstr(flags, "BLT")))
            device->dmaSpace = VME_BLT;
        else if ((p = strstr(flags, "2eVMEFast")))
            device->dmaSpace = VME_2eVMEFast;
        else if ((p = strstr(flags, "2eVME")))
            device->dmaSpace = VME_2eVME;
        else if ((p = strstr(flags, "2eSST320")))
            device->dmaSpace = VME_2eSST320;
        else if ((p = strstr(flags, "2eSST267")))
            device->dmaSpace = VME_2eSST267;
        else if ((p = strstr(flags, "2eSST233")))  /* this mode was a typo in the pev driver */
            device->dmaSpace = VME_2eSST267;
        else if ((p = strstr(flags, "2eSST160")))
            device->dmaSpace = VME_2eSST160;

        if ((p = strstr(flags, "DMA=")))
        {
            if (!device->dmaSpace)
                error("flag DMA= given but device has no DMA space");            
            device->dmaLimit = strtol(p+4, NULL, 0);
            debug("set DME limit to %d", device->dmaLimit);
        }
        debug("dmaLimit = %d", device->dmaLimit);

        if ((p = strstr(flags, "intr=")))          /* backward compatibility. Better specify V=ivec in the record */
            device->ivec = strtol(p+5, NULL, 0);
        debug("default interrupt = %d", device->dmaLimit);

        if ((p = strstr(flags, "bs=")))
        {
            if (device->dmaSpace & VME_BLOCKTRANSFER)
                error("flag bs= makes only sense with VME block transfer");
            device->blocksize = strtol(flags+3, NULL, 0);
            debug("VME block transfer size = %d", device->blocksize);
        }
    }
    if (device->dmaSpace & VME_BLOCKTRANSFER && !(device->aspace & VME_A32))
    {
        error("%s only possible on VME A32 address space",
            toscaDmaTypeToStr(device->dmaSpace));
        device->dmaSpace = 0;
        return -1;
    }

    if (device->dmaLimit != 1) /* not always DMA */
    {
        if ((device->baseptr = toscaMap(device->aspace, address, size)) == NULL)
        {
            error("error mapping Tosca resource \"%s\" address 0x%zx size 0x%zx: %m", resource, address, size);
            return -1;
        }
        debug("baseptr = %p", device->baseptr);
    }
    else
        debug("DMA only");

    if (!device->dmaSpace) device->dmaLimit = 0;
    if (!device->dmaSpace && !device->baseptr)
    {
        error("device has neither DMA nor memory map");
        return -1;
    }
    
    if (regDevRegisterDevice(name, &toscaRegDev, device, size) != SUCCESS)
    {
        error("regDevRegisterDevice() failed");
        return -1;
    }
    
    regDevRegisterDmaAlloc(device, toscaRegDevDmaAlloc);

    return 0;
}

static const iocshFuncDef toscaRegDevConfigureDef =
    { "toscaRegDevConfigure", 5, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "resource", iocshArgString },
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "size", iocshArgInt },
    &(iocshArg) { "flags", iocshArgArgv },
}};

static void toscaRegDevConfigureFunc (const iocshArgBuf *args)
{
    int i, l=0;
    char flags[40] = "";
    for (i = 1; i < args[4].aval.ac && l < sizeof(flags); i++)
        l += sprintf(flags+l, "%.*s ", sizeof(flags)-l, args[4].aval.av[i]);
    if (l) flags[l-1] = 0;
    if (toscaRegDevConfigure(args[0].sval, args[1].sval, args[2].ival, args[3].ival, flags) != 0)
    {
        if (!interruptAccept) epicsExit(-1);
    }
}

static void toscaRegDevRegistrar(void)
{
    iocshRegister(&toscaRegDevConfigureDef, toscaRegDevConfigureFunc);
}

epicsExportRegistrar(toscaRegDevRegistrar);
