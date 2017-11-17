#include <stdlib.h>
#include <errno.h>

#include <epicsTypes.h>
#include <iocsh.h>
#include <epicsStdioRedirect.h>
#include <regDev.h>
#include "toscaReg.h"
#include <epicsExport.h>

#define TOSCA_EXTERN_DEBUG
#define TOSCA_DEBUG_NAME toscaReg
#include "toscaDebug.h"

struct regDevice
{
    int fmc;
    unsigned int base;
};

void toscaSbcDevReport(regDevice *device, int level __attribute__((unused)))
{
    printf("Tosca Serial Bus to FMC %d 0x%x\n", device->fmc, device->base);
}

int toscaSbcDevRead(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    int priority __attribute__((unused)),
    regDevTransferComplete callback __attribute__((unused)),
    const char* user)
{
    size_t i;
    epicsUInt32 value;
    
    debugLvl(2, "%s %s(FMC%d):0x%zx dlen=%d nelm=%zd", user, regDevName(device), device->fmc, offset, dlen, nelem);
    if (dlen == 0) return 0; /* any way to check online status ? */
    offset += device->base;
    for (i = 0; i < nelem; i++)
    {
        value = toscaSbcRead(device->fmc, offset+i);
        if (errno)
        {
            debugErrno("toscaSbcRead(%d,0x%zx)", device->fmc, offset+i);
            return errno;
        }
        switch (dlen)
        {
            case 1: ((epicsUInt8*)pdata)[i] = (epicsUInt8)value; break;
            case 2: ((epicsUInt16*)pdata)[i] = (epicsUInt16)value; break;
            case 4: ((epicsUInt32*)pdata)[i] = (epicsUInt32)value; break;
            default: return -1;
        }
    }
    return 0;
}

int toscaSbcDevWrite(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    void* pmask,
    int priority __attribute__((unused)),
    regDevTransferComplete callback __attribute__((unused)),
    const char* user)
{
    size_t i;
    epicsUInt32 value;
    epicsUInt32 mask = 0xffffffff;
    
    if (pmask)
    {
        switch (dlen)
        {
            case 1: mask &= *(epicsUInt8*)pmask; break;
            case 2: mask &= *(epicsUInt16*)pmask; break;
            case 4: mask = *(epicsUInt32*)pmask; break;
            default: return -1;
        }
    }
    debugLvl(2, "%s %s(FMC%d):0x%zx dlen=%d nelm=%zd mask=0x%x", user, regDevName(device), device->fmc, offset, dlen, nelem, mask);
    offset += device->base;
    for (i = 0; i < nelem; i++)
    {
        switch (dlen)
        {
            case 1: value = ((epicsUInt8*)pdata)[i]; break;
            case 2: value = ((epicsUInt16*)pdata)[i]; break;
            case 4: value = ((epicsUInt32*)pdata)[i]; break;
            default: return -1;
        }
        toscaSbcWriteMasked(device->fmc, offset+i, mask, value);
        if (errno)
        {
            debugErrno("toscaSbcWriteMasked(%d, 0x%zx, 0x%x, 0x%x)", device->fmc, offset+i, mask, value);
            return errno;
        }
    }
    return 0;
}

struct regDevSupport toscaSbcDevRegDev = {
    .report = toscaSbcDevReport,
    .read = toscaSbcDevRead,
    .write = toscaSbcDevWrite,
};

int toscaSbcDevConfigure(const char* name, unsigned int fmc, unsigned int addr, unsigned int size)
{
    regDevice *device = NULL;
    
    if (!name || !name[0])
    {
        iocshCmd("help toscaSbcDevConfigure");
        return -1;
    }
    if (fmc < 1 || fmc > 2)
    {
        fprintf(stderr, "fmc_slot must be 1 or 2\n");
        return -1;
    }
    device = malloc(sizeof(regDevice));
    if (!device)
    {
        fprintf(stderr, "malloc regDevice failed: %m\n");
        return -1;
    }
    if (addr + size > 0x40000000)
    {
        fprintf(stderr, "address overflow addr + size > 0x40000000\n");
        return -1;
    }
    device->fmc = fmc;
    device->base = addr;
    errno = 0;
    if (regDevRegisterDevice(name, &toscaSbcDevRegDev, device, size) != SUCCESS)
    {
        if (errno) fprintf(stderr, "regDevRegisterDevice failed: %m\n");
        free(device);
        return -1;
    }
    if (regDevInstallWorkQueue(device, 100) != SUCCESS)
    {
        fprintf(stderr, "regDevInstallWorkQueue() failed: %m\n");
        return -1;
    }
    return 0;
}

static const iocshFuncDef toscaSbcDevConfigureDef =
    { "toscaSbcDevConfigure", 4, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "fmc_slot(1|2)", iocshArgInt },
    &(iocshArg) { "addr", iocshArgInt },
    &(iocshArg) { "size", iocshArgInt },
}};

static void toscaSbcDevConfigureFunc(const iocshArgBuf *args)
{
    toscaSbcDevConfigure(args[0].sval, args[1].ival, args[2].ival, args[3].ival);
}

static const iocshFuncDef toscaSbcReadDef =
    { "toscaSbcRead", 2, (const iocshArg *[]) {
    &(iocshArg) { "fmc_slot(1|2)", iocshArgInt },
    &(iocshArg) { "register", iocshArgInt },
}};

static void toscaSbcReadFunc(const iocshArgBuf *args)
{
    unsigned int fmc = args[0].ival;
    unsigned int reg = args[1].ival;
    unsigned int val;
    val = toscaSbcRead(fmc, reg);
    if (errno)
        fprintf(stderr, "%m\n");
    else
        printf("0x%x\n", val);
}

static const iocshFuncDef toscaSbcWriteDef =
    { "toscaSbcWrite", 3, (const iocshArg *[]) {
    &(iocshArg) { "fmc_slot(1|2)", iocshArgInt },
    &(iocshArg) { "register_address", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaSbcWriteFunc(const iocshArgBuf *args)
{
    unsigned int fmc = args[0].ival;
    unsigned int reg = args[1].ival;
    unsigned int val = args[2].ival;
    toscaSbcWrite(fmc, reg, val);
    if (errno)
        fprintf(stderr, "%m\n");
}

static const iocshFuncDef toscaSbcWriteMaskedDef =
    { "toscaSbcWriteMasked", 4, (const iocshArg *[]) {
    &(iocshArg) { "fmc_slot(1|2)", iocshArgInt },
    &(iocshArg) { "register_address", iocshArgInt },
    &(iocshArg) { "mask", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaSbcWriteMaskedFunc(const iocshArgBuf *args)
{
    unsigned int fmc = args[0].ival;
    unsigned int reg = args[1].ival;
    unsigned int mask= args[2].ival;
    unsigned int val = args[3].ival;
    toscaSbcWriteMasked(fmc, reg, mask, val);
    if (errno)
        fprintf(stderr, "%m\n");
    else
        printf("0x%x\n", val);
}

static const iocshFuncDef toscaSbcSetDef =
    { "toscaSbcSet", 3, (const iocshArg *[]) {
    &(iocshArg) { "fmc_slot(1|2)", iocshArgInt },
    &(iocshArg) { "register_address", iocshArgInt },
    &(iocshArg) { "bitsToSet", iocshArgInt },
}};

static void toscaSbcSetFunc(const iocshArgBuf *args)
{
    unsigned int fmc = args[0].ival;
    unsigned int reg = args[1].ival;
    unsigned int val = args[2].ival;
    val = toscaSbcSet(fmc, reg, val);
    if (errno)
        fprintf(stderr, "%m\n");
    else
        printf("0x%x\n", val);
}

static const iocshFuncDef toscaSbcClearDef =
    { "toscaSbcClear", 3, (const iocshArg *[]) {
    &(iocshArg) { "fmc_slot(1|2)", iocshArgInt },
    &(iocshArg) { "register_address", iocshArgInt },
    &(iocshArg) { "bitsToClear", iocshArgInt },
}};

static void toscaSbcClearFunc(const iocshArgBuf *args)
{
    unsigned int fmc = args[0].ival;
    unsigned int reg = args[1].ival;
    unsigned int val = args[2].ival;
    val = toscaSbcClear(fmc, reg, val);
    if (errno)
        fprintf(stderr, "%m\n");
    else
        printf("0x%x\n", val);
}

static void toscaSbcRegistrar(void)
{
    iocshRegister(&toscaSbcDevConfigureDef, toscaSbcDevConfigureFunc);
    iocshRegister(&toscaSbcReadDef, toscaSbcReadFunc);
    iocshRegister(&toscaSbcWriteDef, toscaSbcWriteFunc);
    iocshRegister(&toscaSbcWriteMaskedDef, toscaSbcWriteMaskedFunc);
    iocshRegister(&toscaSbcSetDef, toscaSbcSetFunc);
    iocshRegister(&toscaSbcClearDef, toscaSbcClearFunc);
}

epicsExportRegistrar(toscaSbcRegistrar);
