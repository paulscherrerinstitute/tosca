#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <epicsExit.h>
#include <epicsMutex.h>
#include <ellLib.h>
#include <dbAccess.h>
#include <iocsh.h>

#include <regDev.h>

#include "symbolname.h"
#include "toscaMap.h"
#include "toscaDma.h"
#include "toscaIntr.h"
#include "toscaAddrStr.h"

typedef uint8_t __u8;
typedef uint32_t __u32;
typedef uint64_t __u64;
#include "vme_user.h"

#define VME_BLOCKTRANSFER (VME_SCT|VME_MBLT|VME_BLT|VME_2eVMEFast|VME_2eVME|VME_2eSST320|VME_2eSST267|VME_2eSST160)

#include <epicsExport.h>

#define TOSCA_DEBUG_NAME toscaRegDev
#include "toscaDebug.h"
epicsExportAddress(int, toscaRegDevDebug);

#define TOSCA_MAGIC 4009480706U /* crc("Tosca") */

struct regDevice
{
    unsigned int magic;
    const char* name;
    size_t baseaddr;
    volatile void* baseptr;
    unsigned int dmaReadLimit;
    unsigned int dmaWriteLimit;
    unsigned int addrspace;
    unsigned int dmaSpace;
    int swap;
    int ivec;
    IOSCANPVT ioscanpvt[256];
};

void toscaRegDevReport(regDevice *device, int level)
{
    printf("Tosca %s:0x%zx", toscaAddrSpaceToStr(device->addrspace), device->baseaddr);
    if (device->swap)
        printf(", swap=%s",
            device->swap == 2 ? "WS" : device->swap == 4 ? "DS" : device->swap == 8 ? "QS" : "??");
    if (!device->baseptr)
        printf(", DMA only");
    if (!device->dmaSpace)
        printf(", no DMA");
    if (device->dmaReadLimit > 1 || device->dmaWriteLimit > 1)
        printf(", DMA R/W limit=%d/%d", device->dmaReadLimit, device->dmaWriteLimit);
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
    const char* user)
{
    if (!device || device->magic != TOSCA_MAGIC)
    {
        debug("buggy device handle");
        return -1;
    }
    debugLvl(3,"device=%s offset=0x%zx dlen=%u, nelem=%zu [dmaReadLimit=%u] user=%s\n",
        device->name, offset, dlen, nelem, device->dmaReadLimit, user);
    if (!nelem || !dlen) return SUCCESS;

    if (device->dmaReadLimit && nelem >= device->dmaReadLimit)
    {
        char* fname;
        void* usr = (void*)user;

        assert(device->dmaSpace != 0);
        int status = toscaDmaRead(device->dmaSpace, device->baseaddr + offset, pdata, nelem*dlen,
            device->swap, 0, (toscaDmaCallback)callback, usr);
        if (callback != NULL && status == 0)
            return ASYNC_COMPLETION;
        if (status != 0) debugErrno("toscaDmaRead %s %s:0x%zx %s:0x%zx[0x%zx] swap=%d callback=%s(%p)",
            user, device->name, offset, toscaDmaTypeToStr(device->dmaSpace), device->baseaddr + offset, nelem*dlen,
            device->swap, fname=symbolName(callback,0), user), free(fname);
        return status;
    }
    if (device->baseptr == NULL)
    {
        debug("%s: %s is not memory mapped\n", user, device->name);
        return -1;
    }
    assert(pdata != NULL);
    if (device->swap)
        regDevCopy(device->swap, nelem*dlen/device->swap, device->baseptr + offset, pdata, NULL, REGDEV_DO_SWAP);
    else
        regDevCopy(dlen, nelem, device->baseptr + offset, pdata, NULL, REGDEV_NO_SWAP);
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
    const char* user)
{
    if (!device || device->magic != TOSCA_MAGIC)
    {
        debug("buggy device handle");
        return -1;
    }
    debugLvl(2, "device=%s offset=0x%zx dlen=%u, nelem=%zu [dmaWriteLimit=%u] pmask=%p user=%s",
        device->name, offset, dlen, nelem, device->dmaWriteLimit, pmask, user);
    if (!nelem || !dlen) return SUCCESS;

    if (pmask == NULL && device->dmaWriteLimit && nelem >= device->dmaWriteLimit)
    {
        char* fname;
        void* usr = (void*)user;

        assert(device->dmaSpace != 0);
        int status = toscaDmaWrite(pdata, device->dmaSpace, device->baseaddr + offset, nelem*dlen,
            device->swap, 0, (toscaDmaCallback)callback, usr);
        if (callback != NULL && status == 0)
            return ASYNC_COMPLETION;
        if (status != 0) debugErrno("toscaDmaWrite %s %s:0x%zx %s:0x%zx[0x%zx] swap=%d callback=%s(%p)",
            user, device->name, offset, toscaDmaTypeToStr(device->dmaSpace), device->baseaddr + offset, nelem*dlen,
            device->swap, fname=symbolName(callback,0), user), free(fname);
        return status;
    }

    /* TODO: check alignment of offset and nelem*dlen with device->swap */
    if (pmask && dlen != device->swap) /* mask with different dlen than swap */
    {
        int i;
        uint64_t mask;
        debug("mask, swap, regDevCopy");
        for (i = 0; i < 8/dlen; i++) memcpy((char*)&mask + dlen * i, pmask, dlen);
        pmask = &mask;
    }
    if (device->baseptr == NULL)
    {
        debug("%s: %s is not memory mapped\n", user, device->name);
        return -1;
    }
    assert(device->baseptr != NULL);
    assert(pdata != NULL);
    if (device->swap)
        regDevCopy(device->swap, nelem*dlen/device->swap, pdata, device->baseptr + offset, pmask, REGDEV_DO_SWAP);
    else
        regDevCopy(dlen, nelem, pdata, device->baseptr + offset, pmask, REGDEV_NO_SWAP);
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
    
    if(!interruptAccept) return;
    for (prio = 0; prio < 3; prio++) {
        io_scan_list *piosl = (io_scan_list *)pioscanpvt+prio;
        if (ellCount(&piosl->scan_list.list) > 0)
        {
            piosl->callback.callback(&piosl->callback);
        }
    }
}

static IOSCANPVT toscaRegDevGetIoScanPvt(regDevice *device, size_t offset, unsigned int dlen, size_t nelm, int ivec, const char* user)
{
    debug("%s: ivec=0x%x", user, ivec);
    if (ivec == -1)
    {
        debug("%s: default ivec=0x%x", user, device->ivec);
        if (device->ivec == -1)
        {
            error("%s: No interrupt vector defined", user);
            return NULL;
        }
        ivec = device->ivec;
    }
    if (device->addrspace & (VME_A16|VME_A24|VME_A32|VME_A64))
    {
        if (ivec > 255)
        {
            error("%s: %s interrupt %d out of range 1-255\n",
                user, toscaAddrSpaceToStr(device->addrspace), ivec);
            return NULL;
        }
    }
    else
    if (device->addrspace & (TOSCA_USER1|TOSCA_USER2|TOSCA_SMEM))
    {
        if (ivec > 15)
        {
            error("%s: %s interrupt %d out of range 0-15\n",
                user, toscaAddrSpaceToStr(device->addrspace), ivec);
            return NULL;
        }
        if (device->addrspace & TOSCA_USER2)
            ivec += 16;
    }
    else
    {
        error("%s: I/O Intr not supported on %s",
            user, toscaAddrSpaceToStr(device->addrspace));
        return NULL;
    }

    if (device->ioscanpvt[ivec] == NULL)
    {
        debug("%s: init %s interrupt %d handling", user, toscaAddrSpaceToStr(device->addrspace), ivec);
        scanIoInit(&device->ioscanpvt[ivec]);
        
        if (toscaIntrConnectHandler(
            device->addrspace & (VME_A16|VME_A24|VME_A32|VME_A64) ? TOSCA_VME_INTR_ANY : TOSCA_USER1_INTR(ivec),
            ivec, toscaScanIoRequest, device->ioscanpvt[ivec]) != 0)
        {
            error("%s: toscaIntrConnectHandler(0x%x,...) failed", user, ivec);
            return NULL;
        }
    }
    else
        debug("%s: %s interrupt %d handling already active", user, toscaAddrSpaceToStr(device->addrspace), ivec);
    return device->ioscanpvt[ivec];
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

int toscaRegDevConfigure(const char* name, unsigned int addrspace, size_t address, size_t size, const char* flags)
{
    regDevice* device;
    int blockmode = 0;
    
    debug("toscaRegDevConfigure(name=%s, addrspace=0x%x(%s), address=0x%zx size=0x%zx, flags=\"%s\")",
        name, addrspace, toscaAddrSpaceToStr(addrspace), address, size, flags);
    
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
    device->magic = TOSCA_MAGIC;
    device->name = strdup(name);
    device->baseaddr = address;
    device->dmaReadLimit = 100;
    device->dmaWriteLimit = 2048;
    device->ivec = -1;

    device->addrspace = addrspace;
    if (addrspace & (TOSCA_USER1|TOSCA_USER2|TOSCA_CSR)) device->swap = 4;
    if (addrspace & (TOSCA_USER1|TOSCA_USER2|TOSCA_SMEM)) device->dmaSpace = addrspace;
    if (addrspace & VME_A32) device->dmaSpace  = VME_SCT;

    if (flags)
    {
        const char* p;
        const char* q = flags;
        int l;
        while (*q)
        {
            p = q;
            while (isspace(*p)) p++;
            q = p;
            while (*q && !isspace(*q)) q++;
            if (!*p) break;
            l = q-p;
            
            if (strncasecmp(p, "NS", l) == 0) { device->swap = 0; continue; }
            if (strncasecmp(p, "WS", l) == 0) { device->swap = 2; continue; }
            if (strncasecmp(p, "DS", l) == 0) { device->swap = 4; continue; }
            if (strncasecmp(p, "QS", l) == 0) { device->swap = 8; continue; }
#if __BYTE_ORDER == __LITTLE_ENDIAN
            if (strncasecmp(p, "WB", l) == 0) { device->swap = 2; continue; }
            if (strncasecmp(p, "DB", l) == 0) { device->swap = 4; continue; }
            if (strncasecmp(p, "QB", l) == 0) { device->swap = 8; continue; }
#else
            if (strncasecmp(p, "WL", l) == 0) { device->swap = 2; continue; }
            if (strncasecmp(p, "DL", l) == 0) { device->swap = 4; continue; }
            if (strncasecmp(p, "QL", l) == 0) { device->swap = 8; continue; }
#endif
            debug("swap = %d", device->swap);

            if (strncasecmp(p, "block", l) == 0)      { blockmode |= REGDEV_BLOCK_READ|REGDEV_BLOCK_WRITE; continue; }
            if (strncasecmp(p, "blockread", l) == 0)  { blockmode |= REGDEV_BLOCK_READ; continue; }
            if (strncasecmp(p, "blockwrite", l) == 0) { blockmode |= REGDEV_BLOCK_WRITE; continue; }

            debug("blockmode = %s%s", blockmode & 1 ? "read " : "", blockmode & 2 ? "write" : "");

            if (strncasecmp(p, "nodma", l) == 0)     { device->dmaSpace = 0; continue; }
            if (strncasecmp(p, "dmaonly", l) == 0)   { device->dmaReadLimit = device->dmaWriteLimit = 1; continue; }
            if (strncasecmp(p, "dmaReadLimit=", 13) == 0)  { device->dmaReadLimit  = toscaStrToSize(p+13); continue; }
            if (strncasecmp(p, "dmaWriteLimit=", 14) == 0) { device->dmaWriteLimit = toscaStrToSize(p+14); continue; }

            if (strncasecmp(p, "SCT", l) == 0)       { device->dmaSpace = VME_SCT; continue; }
            if (strncasecmp(p, "BLT", l) == 0)       { device->dmaSpace = VME_BLT; continue; }
            if (strncasecmp(p, "MBLT", l) == 0)      { device->dmaSpace = VME_MBLT; continue; }
            if (strncasecmp(p, "2eVME", l) == 0)     { device->dmaSpace = VME_2eVME; continue; }
            if (strncasecmp(p, "2eVMEFast", l) == 0) { device->dmaSpace = VME_2eVMEFast; continue; }
            if (strncasecmp(p, "2eSST160", l) == 0)  { device->dmaSpace = VME_2eSST160; continue; }
            if (strncasecmp(p, "2eSST233", l) == 0)  { device->dmaSpace = VME_2eSST267; continue; } /* typo in pev  */
            if (strncasecmp(p, "2eSST267", l) == 0)  { device->dmaSpace = VME_2eSST267; continue; }
            if (strncasecmp(p, "2eSST320", l) == 0)  { device->dmaSpace = VME_2eSST320; continue; }
            if (strncasecmp(p, "2eSST", l) == 0)     { device->dmaSpace = VME_2eSST320; continue; }

            if (strncasecmp(p, "intr=", 5) == 0) { device->ivec = strtol(p+5, NULL, 0); continue; } /* Better use V= in record */
        }
    }
    if (device->dmaSpace & VME_BLOCKTRANSFER && !(addrspace & VME_A32))
    {
        error("%s only possible on VME A32 address space",
            toscaDmaTypeToStr(device->dmaSpace));
        device->dmaSpace = 0;
        return -1;
    }
    if (device->dmaSpace && blockmode & REGDEV_BLOCK_READ)
    {
        device->dmaReadLimit = 1;  /* dmaonly */
        debug("block read: dma only");
    }
    if (device->dmaSpace && blockmode & REGDEV_BLOCK_WRITE)
    {
        device->dmaWriteLimit = 1; /* dmaonly */
        debug("block write: dma only");
    }

    if (device->dmaReadLimit != 1 || device->dmaWriteLimit != 1) /* not DMA only */
    {
        if ((device->baseptr = toscaMap(addrspace, address, size, 0)) == NULL)
        {
            error("error mapping Tosca %s:0x%zx[0x%zx]: %m", toscaAddrSpaceToStr(addrspace), address, size);
            return -1;
        }
    }
    if (!device->dmaSpace)
    {
        debug("no dma");
        device->dmaReadLimit = device->dmaWriteLimit = 0; /* nodma */
    }
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
    if (blockmode) regDevMakeBlockdevice(device, blockmode, REGDEV_NO_SWAP, NULL);

    return 0;
}

static const iocshFuncDef toscaRegDevConfigureDef =
    { "toscaRegDevConfigure", 4, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "addrspace:address", iocshArgString },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "flags", iocshArgArgv },
}};

static void toscaRegDevConfigureFunc(const iocshArgBuf *args)
{
    toscaMapAddr_t addr;
    size_t size;
    int i, l=0;
    char flags[80] = "";

    if (!args[0].sval)
    {
        iocshCmd("help toscaRegDevConfigure");
        printf("addrspace: USER1 (or USER), USER2, SMEM, TCSR, CRCSR, A16, A24, A32\n"
               "   add * for 'supervisory' and # for 'program' access to Axx modes\n"
               "   address and size can use k,M,G suffix for powers of 1024\n"
               "flags:\n"
               "   - swap: NS (none), WS (word), DS (double word) QS (quad word)\n"
               "           WL, WB, DL, DB, QB, QB (convert to/from little/big endian)\n"
               "           (Default for TCSR and USER* is DL, for others NS)\n"
               "   - DMA read element minimum: dmaReadLimit= \n"
               "           (0=nodma, 1=dmaonly, default:100)\n"
               "   - DMA write element minimum: dmaWriteLimit= \n"
               "           (0=nodma, 1=dmaonly, default:2k)\n"
               "   - block mode: block, blockread, blockwrite\n"
               "           (records with PRIO=HIGH trigger transfer)\n"
               "   - VME block transfer: SCT, BLT, MBLT, 2eVME, 2eSST[160|267|320]\n"
               "   - VME default interrupt vector: intr=1...255\n"
               "   - USER[1|2] default interrupt line: intr=0...15\n"
               "           (better use V=... in record link)\n"
        );
        return;
    }
    
    addr = toscaStrToAddr(args[1].sval);
    if (!addr.addrspace)
    {
        error("invalid address space %s", args[1].sval);
        return;
    }
    size = toscaStrToSize(args[2].sval);

    for (i = 1; i < args[3].aval.ac && l < sizeof(flags); i++)
        l += sprintf(flags+l, "%.*s ", (int)sizeof(flags)-l, args[3].aval.av[i]);
    if (l) flags[l-1] = 0;

    if (toscaRegDevConfigure(args[0].sval, addr.addrspace, addr.address, size, flags) != 0)
    {
        if (!interruptAccept) epicsExit(-1);
    }
}

static void toscaRegDevRegistrar(void)
{
    iocshRegister(&toscaRegDevConfigureDef, toscaRegDevConfigureFunc);
}

epicsExportRegistrar(toscaRegDevRegistrar);
