#include <stdlib.h>
#include <errno.h>

#include <epicsTypes.h>

#include <regDev.h>

#include <iocsh.h>
#include <epicsExport.h>

#include "toscaReg.h"

#define TOSCA_EXTERN_DEBUG
#define TOSCA_DEBUG_NAME toscaPon
#include "toscaDebug.h"
epicsExportAddress(int, toscaPonDebug);

struct regDevice
{
    int dummy;
};

void toscaPonDevReport(regDevice *device, int level)
{
    printf("Tosca PON\n");
}

int toscaPonDevRead(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    int priority,
    regDevTransferComplete callback,
    const char* user)
{
    int i;
    
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
    int priority,
    regDevTransferComplete callback,
    const char* user)
{
    int i;
    
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
        perror("malloc regDevice failed");
        return -1;
    }
    errno = 0;
    if (regDevRegisterDevice(name, &toscaPonDevRegDev, device, 0x44) != SUCCESS)
    {
        if (errno) perror("regDevRegisterDevice failed");
        free(device);
        return -1;
    }
    if (regDevInstallWorkQueue(device, 100) != SUCCESS)
    {
        perror("regDevInstallWorkQueue() failed");
        return -1;
    }
    return 0;
}

static const iocshFuncDef toscaPonReadDef =
    { "toscaPonRead", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
}};

static void toscaPonReadFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaPonRead(args[0].ival);
    if (val == 0xffffffff && errno != 0) perror(NULL);
    else printf("0x%08x\n", val);
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
    if (val == 0xffffffff && errno != 0) perror(NULL);
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaPonDevConfigureDef =
    { "toscaPonDevConfigure", 1, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
}};

static void toscaPonDevConfigureFunc(const iocshArgBuf *args)
{
    toscaPonDevConfigure(args[0].sval);
}

static void toscaPonRegistrar(void)
{
    iocshRegister(&toscaPonDevConfigureDef, toscaPonDevConfigureFunc);
    iocshRegister(&toscaPonReadDef, toscaPonReadFunc);
    iocshRegister(&toscaPonWriteDef, toscaPonWriteFunc);
}

epicsExportRegistrar(toscaPonRegistrar);
