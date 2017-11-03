#include <stdlib.h>
#include <errno.h>

#include <epicsTypes.h>

#include <regDev.h>

#include <iocsh.h>
#include <epicsStdioRedirect.h>
#include <epicsExport.h>

#include "toscaReg.h"

#define TOSCA_EXTERN_DEBUG
#define TOSCA_DEBUG_NAME toscaReg
#include "toscaDebug.h"

struct regDevice
{
    int dummy;
};

void toscaPonDevReport(regDevice *device __attribute__((unused)), int level __attribute__((unused)))
{
    printf("Tosca PON\n");
}

int toscaPonDevRead(
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
    
    if (dlen == 0) return 0; /* any way to check online status ? */
    if (dlen != 4)
    {
        error("%s %s: only 4 bytes supported", user, regDevName(device));
        return -1;
    }
    if (offset & 3)
    {
        error("%s %s: offset must be multiple of 4", user, regDevName(device));
        return -1;
    }
    for (i = 0; i < nelem; i++)
    {
        ((epicsUInt32*)pdata)[i] = toscaPonRead(offset+(i<<2));
    }
    return 0;
}

int toscaPonDevWrite(
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
    
    if (dlen != 4)
    {
        error("%s %s: only 4 bytes supported", user, regDevName(device));
        return -1;
    }
    if (offset & 3)
    {
        error("%s %s: offset must be multiple of 4", user, regDevName(device));
        return -1;
    }
    if (pmask && *(epicsUInt32*)pmask != 0xffffffff)
    {
        epicsUInt32 mask = *(epicsUInt32*)pmask;
        for (i = 0; i < nelem; i++)
        {
            toscaPonWriteMasked(offset+(i<<2), mask, ((epicsUInt32*)pdata)[i]);
        }
    }
    else
        for (i = 0; i < nelem; i++)
        {
            toscaPonWrite(offset+(i<<2), ((epicsUInt32*)pdata)[i]);
        }
    return 0;
}

struct regDevSupport toscaPonDevRegDev = {
    .report = toscaPonDevReport,
    .read = toscaPonDevRead,
    .write = toscaPonDevWrite,
};

int toscaPonDevConfigure(const char* name)
{
    regDevice *device = NULL;
    
    if (!name || !name[0])
    {
        printf("usage: toscaPonDevConfigure name\n");
        return -1;
    }
    device = malloc(sizeof(regDevice));
    if (!device)
    {
        fprintf(stderr, "malloc regDevice failed: %m\n");
        return -1;
    }
    errno = 0;
    if (regDevRegisterDevice(name, &toscaPonDevRegDev, device, 0x44) != SUCCESS)
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

static const iocshFuncDef toscaPonReadDef =
    { "toscaPonRead", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgString },
}};

static void toscaPonReadFunc(const iocshArgBuf *args)
{
    unsigned int addr;
    if (!args[0].sval)
    {
        for (addr = 0; addr <= 0x40; addr+=4)
        {
            if (addr == 0x28) addr = 0x40;
            printf("0x%02x %-14s 0x%08x\n",
                addr, toscaPonAddrToRegname(addr), toscaPonRead(addr));
        }
        return;
    }
    addr = strtol(args[0].sval, NULL, 0) & ~3;
    printf("%-14s 0x%08x\n",
        toscaPonAddrToRegname(addr), toscaPonRead(addr));
}

static const iocshFuncDef toscaPonWriteDef =
    { "toscaPonWrite", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaPonWriteFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaPonWrite(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaPonWriteMaskedDef =
    { "toscaPonWriteMasked", 3, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "mask", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaPonWriteMaskedFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaPonWriteMasked(args[0].ival, args[1].ival,  args[2].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaPonSetDef =
    { "toscaPonSet", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaPonSetFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaPonSet(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaPonDevConfigureDef =
    { "toscaPonDevConfigure", 1, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
}};

static const iocshFuncDef toscaPonClearDef =
    { "toscaPonClear", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaPonClearFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaPonClear(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
    else printf("0x%08x\n", val);
}

static void toscaPonDevConfigureFunc(const iocshArgBuf *args)
{
    toscaPonDevConfigure(args[0].sval);
}

static void toscaPonRegistrar(void)
{
    iocshRegister(&toscaPonDevConfigureDef, toscaPonDevConfigureFunc);
    iocshRegister(&toscaPonReadDef, toscaPonReadFunc);
    iocshRegister(&toscaPonWriteDef, toscaPonWriteFunc);
    iocshRegister(&toscaPonWriteMaskedDef, toscaPonWriteMaskedFunc);
    iocshRegister(&toscaPonSetDef, toscaPonSetFunc);
    iocshRegister(&toscaPonClearDef, toscaPonClearFunc);
}

epicsExportRegistrar(toscaPonRegistrar);
