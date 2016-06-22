#define __STDC_VERSION__ 199901L

#include <stdlib.h>
#include <string.h>
#include <endian.h>

#include <epicsTypes.h>
#include <iocsh.h>

#include "regDev.h"
#include "toscaMap.h"
#include <epicsExport.h>

int toscaRegDevDebug;
epicsExportAddress(int, toscaRegDevDebug);
FILE* toscaRegDevDebugFile = NULL;

#define debug_internal(m, fmt, ...) if(m##Debug) fprintf(m##DebugFile?m##DebugFile:stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#define debugErrno(fmt, ...) debug(fmt " failed: %s", ##__VA_ARGS__, strerror(errno))
#define debug(fmt, ...) debug_internal(toscaRegDev, fmt, ##__VA_ARGS__)

struct regDevice
{
    volatile void* baseptr;
    unsigned long dmalimit;
    unsigned int blockmode;
    unsigned int blocksize;
    uint8_t swap;
    uint8_t intr;
};

void toscaDevReport(regDevice *device, int level)
{
    toscaMapAddr_t addr = toscaMapLookupAddr(device->baseptr);
    printf("Tosca %s:0x%llx", toscaAddrSpaceToStr(addr.aspace), addr.address);
    if (device->swap)
        printf(", swap=%s",
            device->swap == 2 ? "WS" : device->swap == 4 ? "DS" : device->swap == 8 ? "QS" : "??");
    if (device->blockmode)
        printf(", block mode");
    else
        if (device->dmalimit <= 1) printf(", DMA=%s", device->dmalimit ? "always" : "never");
        else printf(", DMA>=%ld elem", device->dmalimit);
    printf("\n");
}

int toscaDevRead(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    int priority,
    regDevTransferComplete callback,
    char* user)
{
    if (device->swap)
        regDevCopy(device->swap, nelem*dlen/device->swap, device->baseptr, pdata, NULL, REGDEV_DO_SWAP);
    else
        regDevCopy(dlen, nelem, device->baseptr, pdata, NULL, REGDEV_NO_SWAP);
    return SUCCESS;
};

int toscaDevWrite(
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
    if (device->swap)
    {
        if (pmask && dlen != device->swap) /* mask with different dlen than swap */
        {
            int i;
            epicsUInt64 mask;
            for (i = 0; i < 8/dlen; i++) memcpy((char*)&mask + dlen * i, pmask, dlen);
            regDevCopy(device->swap, nelem*dlen/device->swap, device->baseptr, pdata, &mask, REGDEV_DO_SWAP);
        }
        else
            regDevCopy(device->swap, nelem*dlen/device->swap, device->baseptr, pdata, pmask, REGDEV_DO_SWAP);
    }
    else
        regDevCopy(dlen, nelem, device->baseptr, pdata, pmask, REGDEV_NO_SWAP);
    return SUCCESS;
};

struct regDevSupport toscaDev = {
    .report = toscaDevReport,
    .read = toscaDevRead,
    .write = toscaDevWrite,
};

int toscaDevConfigure(const char* name, const char* resource, size_t address, size_t size, const char* flags)
{
    int aspace;
    volatile void* baseptr;
    regDevice* device;
    const char *p;
    
    if (!name)
    {
        iocshCmd("help toscaDevConfigure");
        printf("resources: USER1 (or USER), USER2, SHMEM, TCSR, CRCSR, A16, A24, A32\n"
               "   (add * for 'supervisory' and # for 'program' access to Axx modes)\n"
               "flags:\n"
               "   - swap: NS (none), WS (word), DS (double word) QS (quad word)\n"
               "           WL, WB, DL, DB, QB, QB (convert to/from little/big endian)\n"
               "       (Default for VME modes is NS, for others DL)\n"
               "   - DMA mode element minimum: DMA[=xxx]\n"
               "       (0:never, 1:always, default:1000)\n"
               "   - block mode: block\n"
               "       (records with PRIO=HIGH trigger transfer, implies DMA=1)\n"
               "   - VME block mode: BLT, MBLT, 2eVME, 2eSST[160|233|320]\n"
               "       (VME resource without block mode implies DMA=0)\n"
               "   - VME block size: bs=128|256|512\n"
               "       (Default is 512)\n"
               "   - VME interrupt vector: intr=1...255\n"
               "   - USER1|2 interrupt line: intr=0...15\n");
               
        return -1;
    }
    
    debug("toscaDevConfigure(name=%s, resource=%s, address=0x%zx size=0x%zx, flags=%s)",
        name, resource, address, size, flags);
    
    if (regDevFind(name))
    {
        fprintf(stderr, "toscaDevConfigure: name \"%s\" already in use\n", name);
        return -1;
    }

    if (!(aspace = toscaStrToAddrSpace(resource)))
    {
        fprintf(stderr, "toscaDevConfigure: unknown Tosca resource \"%s\"\n", resource);
        return -1;
    }

    if (!(baseptr = toscaMap(aspace, address, size)))
    {
        fprintf(stderr, "toscaDevConfigure: error mapping Tosca resource \"%s\" address 0x%zx size 0x%zx: %m\n", resource, address, size);
        return -1;
    }
    
    if (!(device = calloc(1, sizeof(regDevice))))
    {
        fprintf(stderr, "toscaDevConfigure: cannot allocate device structure: %m\n");
        return -1;
    }
    device->baseptr = baseptr;
    if (aspace & (TOSCA_USER1|TOSCA_USER2|TOSCA_SHMEM|TOSCA_CSR)) device->swap = 4;
    device->blocksize = 512;
    device->dmalimit = 1000;

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
        if (strstr(flags, "block")) device->blockmode = 1;
        if ((p = strstr(flags, "DMA=")))
        {
            device->dmalimit = strtol(flags+4, NULL, 0);
        }
        if ((p = strstr(flags, "intr=")))
        {
            device->intr = strtol(flags+5, NULL, 0);
        }
        if ((p = strstr(flags, "bs=")))
        {
            device->blocksize = strtol(flags+3, NULL, 0);
        }
    }
    
    if (regDevRegisterDevice(name, &toscaDev, device, size) != SUCCESS)
    {
        fprintf(stderr, "toscaDevConfigure: regDevRegisterDevice() failed\n");
        return -1;
    }
    
    return 0;
}

static const iocshFuncDef toscaDevConfigureDef =
    { "toscaDevConfigure", 5, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "resource", iocshArgString },
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "size", iocshArgInt },
    &(iocshArg) { "flags", iocshArgArgv },
}};

static void toscaDevConfigureFunc (const iocshArgBuf *args)
{
    int i, l=0;
    char flags[40] = "";
    for (i = 1; i < args[4].aval.ac && l < sizeof(flags); i++)
        l += sprintf(flags+l, "%.*s ", sizeof(flags)-l, args[4].aval.av[i]);
    if (l) flags[l-1] = 0;
    toscaDevConfigure(args[0].sval, args[1].sval, args[2].ival, args[3].ival, flags);
}

/* pev compatibility mode */

static const iocshArg * const pevConfigureArgs[] = {
    &(iocshArg) { "card", iocshArgInt },
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "resource", iocshArgString },
    &(iocshArg) { "offset", iocshArgInt },
    &(iocshArg) { "protocol", iocshArgString },
    &(iocshArg) { "intrVec", iocshArgInt },
    &(iocshArg) { "mapSize", iocshArgInt },
    &(iocshArg) { "blockMode", iocshArgInt },
    &(iocshArg) { "swap", iocshArgString },
    &(iocshArg) { "vmePktSize", iocshArgInt },
};

static const iocshFuncDef pevConfigureDef =
    { "pevConfigure", 10, pevConfigureArgs };
static const iocshFuncDef pevAsynConfigureDef =
    { "pevAsynConfigure", 10, pevConfigureArgs };

static void pevConfigureFunc(const iocshArgBuf *args)
{
    char flags[40] = "";
    int l = 0;
    const char *resource;
    int aspace;

    if (args[0].ival != 0)
    {
        printf ("Only card 0 is supported\n");
        return;
    }
    resource = args[2].sval;
    aspace = toscaStrToAddrSpace(resource);
    if (aspace) resource = toscaAddrSpaceToStr(aspace);
    
    if (args[5].ival) /* intrVec */
        l += sprintf(flags+l, "intr=%d ", args[5].ival);
    if (args[7].ival && l < sizeof(flags) - 6) /* blockMode */
        l += sprintf(flags+l, "block ");
    if (args[4].sval && l < sizeof(flags)) /* protocol */
    {
        if (strstr(args[4].sval, "BLT") || strstr(args[4].sval, "2e"))
            l += sprintf(flags+l, "%.*s ", sizeof(flags)-1-l, args[4].sval);
    }
    if (args[8].sval && l < sizeof(flags)) /* swap */
        l += sprintf(flags+l, "%.*s ", sizeof(flags)-1-l, args[8].sval);
    if (args[9].ival && l < sizeof(flags)-10) /* vmePktSize */
        l += sprintf(flags+l, "bs=%d ", args[9].ival);
    if (l) flags[l-1] = 0;
    
    printf("Compatibility mode! pevConfigure call replaced by:\ntoscaDevConfigure %s %s 0x%x 0x%x %s\n",
                      args[1].sval, resource, args[3].ival, args[6].ival, flags);
    toscaDevConfigure(args[1].sval, resource, args[3].ival, args[6].ival, flags);
}

static void toscaDevRegistrar(void)
{
    iocshRegister(&toscaDevConfigureDef, toscaDevConfigureFunc);
    iocshRegister(&pevConfigureDef, pevConfigureFunc);
    iocshRegister(&pevAsynConfigureDef, pevConfigureFunc);
}

epicsExportRegistrar(toscaDevRegistrar);


