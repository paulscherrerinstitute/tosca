#include <stdlib.h>
#include <errno.h>

#include <epicsTypes.h>
#include <iocsh.h>
#include <regDev.h>
#include "toscaReg.h"
#include <epicsExport.h>

#define TOSCA_EXTERN_DEBUG
#define TOSCA_DEBUG_NAME toscaReg
#include "toscaDebug.h"

static const char* smonAddrToStr(unsigned int addr)
{
    switch (addr)
    {
        case 0x00: return "Temp"; 
        case 0x01: return "Vccint";
        case 0x02: return "Vccaux";
        case 0x03: return "Vadj";
        case 0x04: return "VrefP";
        case 0x05: return "VrefN";
        case 0x08: return "Supply offs";
        case 0x09: return "ADC offs";
        case 0x10: return "Vaux[0]";
        case 0x11: return "Vaux[1]";
        case 0x12: return "Vaux[2]";
        case 0x13: return "Vaux[3]";
        case 0x14: return "Vaux[4]";
        case 0x15: return "Vaux[5]";
        case 0x16: return "Vaux[6]";
        case 0x17: return "Vaux[7]";
        case 0x18: return "Vaux[8]";
        case 0x19: return "Vaux[9]";
        case 0x1a: return "Vaux[A]";
        case 0x1b: return "Vaux[B]";
        case 0x1c: return "Vaux[C]";
        case 0x1d: return "Vaux[D]";
        case 0x1e: return "Vaux[E]";
        case 0x1f: return "Vaux[F]";
        case 0x20: return "Temp Max";
        case 0x21: return "Vccint Max";
        case 0x22: return "Vccaux Max";
        case 0x24: return "Temp Min";
        case 0x25: return "Vccint Min";
        case 0x26: return "Vccaux Min";
        case 0x3f: return "Flag";
        case 0x40: return "Config #0";
        case 0x41: return "Config #1";
        case 0x42: return "Config #2";
        case 0x43: return "Test #0";
        case 0x44: return "Test #1";
        case 0x45: return "Test #2";
        case 0x46: return "Test #3";
        case 0x47: return "Test #4";
        case 0x48: return "Seq #0";
        case 0x49: return "Seq #1";
        case 0x4a: return "Seq #2";
        case 0x4b: return "Seq #3";
        case 0x4c: return "Seq #4";
        case 0x4d: return "Seq #5";
        case 0x4e: return "Seq #6";
        case 0x4f: return "Seq #7";
        case 0x50: return "Alarm #0";
        case 0x51: return "Alarm #1";
        case 0x52: return "Alarm #2";
        case 0x53: return "Alarm #3";
        case 0x54: return "Alarm #4";
        case 0x55: return "Alarm #5";
        case 0x56: return "Alarm #6";
        case 0x57: return "Alarm #7";
        default: return "Undefined";
    }
}

static void smonShow(unsigned int addr, unsigned int val)
{
    if (val == 0xffffffff && errno != 0)
    {
        perror(NULL);
        return;
    }
    printf("%-11s 0x%04x", smonAddrToStr(addr), val);
    switch (addr)
    {
        case 0x00: /* Temp */
        case 0x20: /* Temp Max */
        case 0x24: /* Temp Min */
            printf(" = %.2f C\n", (val>>6) * 503.975 / 1024 - 273.15);
            return;
        case 0x01: /* Vccint */
        case 0x21: /* Vccint Max */
        case 0x25: /* Vccint Min */
        case 0x02: /* Vccaux */
        case 0x22: /* Vccaux Max */
        case 0x26: /* Vccaux Min */
        case 0x04: /* VrepP */
        case 0x05: /* VrepN */
        
        case 0x03: /*VP/VN */
        case 0x08: /* supply Offset */
        case 0x09: /* ADC Offset */
        case 0x10 /*VauxP[x]/VauxN[x] */:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x1b:
        case 0x1c:
        case 0x1d:
        case 0x1e:
        case 0x1f:
            printf(" = %.3f V\n", (val>>6) * 3.0 / 1024);
            return;
        case 0x3f:
        case 0x40:
        case 0x41:
        case 0x42:
            printf(" = %u%u%u%u:%u%u%u%u:%u%u%u%u:%u%u%u%u\n",
                (val>>15)&1, (val>>14)&1, (val>>13)&1, (val>>12)&1,
                (val>>11)&1, (val>>10)&1, (val>>9)&1, (val>>8)&1,
                (val>>7)&1, (val>>6)&1, (val>>5)&1, (val>>4)&1,
                (val>>3)&1, (val>>2)&1, (val>>1)&1, val&1);
            return;
    }
    printf("\n");
}

static const iocshFuncDef toscaSmonReadDef =
    { "toscaSmonRead", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgString },
}};

static void toscaSmonReadFunc(const iocshArgBuf *args)
{
    unsigned int addr;
    if (!args[0].sval)
    {
        for (addr = 0; addr < 0x43; addr++)
        {
            if (addr == 0x06) addr = 0x08;
            if (addr == 0x0a) addr = 0x10;
            if (addr == 0x23) addr = 0x24;
            if (addr == 0x27) addr = 0x3f;
            printf("0x%02x ", addr);
            errno = 0;
            smonShow(addr, toscaSmonRead(addr));
        }
        return;
    }
    addr = strtol(args[0].sval, NULL, 0);
    errno = 0;
    smonShow(addr, toscaSmonRead(addr));
}

static const iocshFuncDef toscaSmonWriteDef =
    { "toscaSmonWrite", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaSmonWriteFunc(const iocshArgBuf *args)
{
    unsigned int addr = args[0].ival;
    unsigned int val  = args[1].ival;
    errno = 0;
    smonShow(addr, toscaSmonWrite(addr, val));
}

static const iocshFuncDef toscaSmonWriteMaskedDef =
    { "toscaSmonWriteMasked", 3, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "mask", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaSmonWriteMaskedFunc(const iocshArgBuf *args)
{
    unsigned int addr = args[0].ival;
    unsigned int mask = args[1].ival;
    unsigned int val  = args[2].ival;
    errno = 0;
    smonShow(addr, toscaSmonWriteMasked(addr, mask, val));
}

static const iocshFuncDef toscaSmonSetDef =
    { "toscaSmonSet", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaSmonSetFunc(const iocshArgBuf *args)
{
    unsigned int addr = args[0].ival;
    unsigned int val = args[1].ival;
    errno = 0;
    smonShow(addr, toscaSmonSet(addr, val));
}

static const iocshFuncDef toscaSmonClearDef =
    { "toscaSmonClear", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaSmonClearFunc(const iocshArgBuf *args)
{
    unsigned int addr = args[0].ival;
    unsigned int val = args[1].ival;
    errno = 0;
    smonShow(addr, toscaSmonClear(addr, val));
}

/* RegDev Interface */

struct regDevice
{
    void* nope;
};

void smonDevReport(regDevice *device __attribute__((unused)), int level __attribute__((unused)))
{
    printf("Tosca Virtex FPGA System Monitor\n");
}

int smonDevRead(
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

    if (dlen != 2)
    {
        error("%s %s: dlen must be 4 bytes", regDevName(device), user);
        return -1;
    }
    for (i = 0; i < nelem; i++)
        ((epicsUInt16*) pdata)[i] = toscaSmonRead(offset+i);
    return 0;
}

int smonDevWrite(
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

    if (dlen != 2)
    {
        error("%s %s: dlen must be 2 bytes", regDevName(device), user);
        return -1;
    }
    if (pmask && *(epicsUInt16*)pmask != 0xffff)
    {
        epicsUInt16 mask = *(epicsUInt16*)pmask;
        for (i = 0; i < nelem; i++)
        {
            toscaSmonWriteMasked(offset+i, mask, ((epicsUInt16*) pdata)[i]);
        }
    }
    else
        for (i = 0; i < nelem; i++)
            toscaSmonWrite(offset+i, ((epicsUInt16*) pdata)[i]);
    return 0;
}

struct regDevSupport smonDev = {
    .report = smonDevReport,
    .read = smonDevRead,
    .write = smonDevWrite,
};

int toscaSmonDevConfigure(const char* name)
{
    regDevice *device = NULL;
    
    if (!name || !name[0])
    {
        printf("usage: toscaSmonDevConfigure name\n");
        return -1;
    }
    device = malloc(sizeof(regDevice));
    if (!device)
    {
        perror("malloc regDevice");
        goto fail;
    }
    if (regDevRegisterDevice(name, &smonDev, device, 0x100) != SUCCESS)
    {
        perror("regDevRegisterDevice() failed");
        goto fail;
    }
    if (regDevInstallWorkQueue(device, 100) != SUCCESS)
    {
        perror("regDevInstallWorkQueue() failed");
        return -1;
    }
    return 0;

fail:
    free(device);
    return -1;
}

static const iocshFuncDef toscaSmonDevConfigureDef =
    { "toscaSmonDevConfigure", 1, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
}};

static void toscaSmonDevConfigureFunc(const iocshArgBuf *args)
{
    toscaSmonDevConfigure(args[0].sval);
}

static void toscaSmonRegistrar(void)
{
    iocshRegister(&toscaSmonDevConfigureDef, toscaSmonDevConfigureFunc);
    iocshRegister(&toscaSmonReadDef, toscaSmonReadFunc);
    iocshRegister(&toscaSmonWriteDef, toscaSmonWriteFunc);
    iocshRegister(&toscaSmonWriteMaskedDef, toscaSmonWriteMaskedFunc);
    iocshRegister(&toscaSmonSetDef, toscaSmonSetFunc);
    iocshRegister(&toscaSmonClearDef, toscaSmonClearFunc);
}

epicsExportRegistrar(toscaSmonRegistrar);
