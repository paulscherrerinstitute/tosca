#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <epicsTypes.h>
#include <epicsStdio.h>
#include <iocsh.h>

#include "memDisplay.h"
#include "i2cDev.h"
#include "symbolname.h"
#include "keypress.h"

#include "toscaMap.h"
#include "toscaReg.h"
#include "toscaIntr.h"
#include "toscaDma.h"
#include "toscaDevLib.h"

#include <epicsExport.h>

#define TOSCA_DEBUG_NAME toscaIocsh
#include "toscaDebug.h"

static const iocshFuncDef toscaNumDevicesDef =
    { "toscaNumDevices", 0, (const iocshArg *[]) {
}};

static void toscaNumDevicesFunc(const iocshArgBuf *args __attribute__((unused)))
{
    printf("%u\n", toscaNumDevices());
}

static const iocshFuncDef toscaListDevicesDef =
    { "toscaListDevices", 0, (const iocshArg *[]) {
}};

static void toscaListDevicesFunc(const iocshArgBuf *args __attribute__((unused)))
{
    toscaListDevices();
}

static const iocshFuncDef toscaDeviceTypeDef =
    { "toscaDeviceType", 1, (const iocshArg *[]) {
    &(iocshArg) { "device", iocshArgInt },
}};

static void toscaDeviceTypeFunc(const iocshArgBuf *args __attribute__((unused)))
{
    unsigned int type = toscaDeviceType(args[0].ival);
    if (type)
        printf("%04x\n", type);
    else
        perror(NULL);
}

static const iocshFuncDef toscaMapDef =
    { "toscaMap", 4, (const iocshArg *[]) {
    &(iocshArg) { "[device:](A16|A24|A32|CRCSR|USER[1|2]|SMEM[1|2]|TCSR|TIO|SRAM|SLAVE)[:address]", iocshArgString },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "[(USER[1|2]|SMEM[1|2]):address", iocshArgString },
    &(iocshArg) { "[SWAP]]", iocshArgString },
}};

static void toscaMapFunc(const iocshArgBuf *args)
{
    toscaMapAddr_t addr, res_addr = {0,0};
    size_t size;
    volatile void* ptr;

    if (!args[0].sval)
    {
        iocshCmd("help toscaMap");
        return;
    }
    addr = toscaStrToAddr(args[0].sval, NULL);
    if (!addr.addrspace)
    {
        fprintf(stderr, "invalid Tosca address %s\n",
            args[0].sval);
        return;
    }
    size = toscaStrToSize(args[1].sval);
    if (size == (size_t)-1)
    {
        fprintf(stderr, "invalid size %s\n",
            args[1].sval);
        return;
    }
    if (args[2].sval)
    {
        if (!(addr.addrspace & VME_SLAVE))
        {
            fprintf(stderr, "invalid argument %s: no SLAVE address space\n",
                args[2].sval);
            return;
        }
        else
        {
            if (strcasecmp(args[2].sval, "SWAP") == 0)
                addr.addrspace |= VME_SWAP;
            else
            {
                res_addr = toscaStrToAddr(args[2].sval, NULL);
                if (!res_addr.addrspace)
                {
                    fprintf(stderr, "invalid Tosca address %s\n",
                        args[2].sval);
                    return;
                }
                if (res_addr.addrspace >> 16)
                {
                    fprintf(stderr, "invalid use of device number in %s\n",
                        args[2].sval);
                    return;
                }
                addr.addrspace |= res_addr.addrspace;
                if (args[3].sval)
                {
                    if (strcasecmp(args[3].sval, "SWAP") == 0)
                        addr.addrspace |= VME_SWAP;
                    else
                    {
                        fprintf(stderr, "invalid argument %s: SWAP expected\n",
                            args[3].sval);
                        return;
                    }
                }
            }
        }
    }
    errno = 0;
    ptr = toscaMap(addr.addrspace, addr.address, size, res_addr.address);
    if (!ptr)
    {
        perror(NULL);
        return;
    }
    printf("%p\n", ptr);
}

static const iocshFuncDef toscaMapLookupAddrDef =
    { "toscaMapLookupAddr", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgString },
}};

static void toscaMapLookupAddrFunc(const iocshArgBuf *args)
{
    size_t addr = toscaStrToSize(args[0].sval);
    toscaMapAddr_t vme_addr = toscaMapLookupAddr((void*)addr);
    if (!vme_addr.addrspace)
        printf("%p is not a TOSCA address\n", (void*)addr);
    else
        printf("%s:0x%"PRIx64"\n",
            toscaAddrSpaceToStr(vme_addr.addrspace),
            vme_addr.address);
}

int toscaMapPrintInfo(toscaMapInfo_t info, void* unused __attribute__((unused)))
{
    unsigned int device = info.addrspace >> 16;
    char buf[60];
    int n = 7;
    if (device) n-=printf("%u:", device);
    if ((info.addrspace & (TOSCA_USER1|TOSCA_USER2|TOSCA_SMEM|VME_SLAVE)) > VME_SLAVE)
    printf("%*s:0x%-8"PRIx64" %16s %7s:0x%-8zx%s\n", n,
        toscaAddrSpaceToStr(info.addrspace),
        info.baseaddress,
        sizeToStr(info.size, buf),
        toscaAddrSpaceToStr(info.addrspace & (TOSCA_USER1|TOSCA_USER2|TOSCA_SMEM)),
        (size_t) info.baseptr,
        info.addrspace & VME_SWAP ? " SWAP" : "");
    else
    printf("%*s:0x%-8"PRIx64" %16s   %-16p%s\n", n,
        toscaAddrSpaceToStr(info.addrspace),
        info.baseaddress,
        sizeToStr(info.size, buf),
        info.baseptr,
        info.addrspace & VME_SWAP ? "SWAP" : "");
    return 0;
}

static const iocshFuncDef toscaMapShowDef =
    { "toscaMapShow", 0, (const iocshArg *[]) {
}};

static void toscaMapShowFunc(const iocshArgBuf *args __attribute__((unused)))
{
    printf("\e[4maddrspace:baseaddr         size         pointer%*c\e[0m\n", (int)sizeof(void*)-3, ' ');
    toscaMapForeach(toscaMapPrintInfo, NULL);
}

static const iocshFuncDef toscaMapFindDef =
    { "toscaMapFind", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgString },
}};

static void toscaMapFindFunc(const iocshArgBuf *args)
{
    size_t addr = toscaStrToSize(args[0].sval);
    toscaMapInfo_t info = toscaMapFind((void*)addr);
    if (!info.addrspace)
        printf("%p is not a TOSCA address\n", (void*)addr);
    else
        toscaMapPrintInfo(info, NULL);
}

static const iocshFuncDef toscaGetVmeErrDef =
    { "toscaGetVmeErr", 1, (const iocshArg *[]) {
    &(iocshArg) { "device", iocshArgInt },
}};

static void toscaGetVmeErrFunc(const iocshArgBuf *args)
{
    errno = 0;
    toscaMapVmeErr_t err = toscaGetVmeErr(args[0].ival);
    if (errno)
    {
        perror(NULL);
        return;
    }
    printf("0x%08"PRIx64",0x%"PRIx32" (%s %s%c%s %s id=%d len=%d %s:0x%"PRIx64")\n",
        err.address,
        err.status,
        err.err ? "ERR" : "OK",
        err.over ? "OVER " : "",
        err.write ? 'W' : 'R',
        err.timeout ? " TOUT" : "",
        (const char*[]){"PCIe","???","DMA","USER"}[err.source],
        err.id,
        err.length,
        (const char*[]){"CRCSR","A16","A24","A32","BLT","MBLT","2eVME","?7?","2eSST160","2eSST267","2eSST320","?B?","?C?","?D?","?E?","IACK"}[err.mode],
        err.address & (err.mode == 1 ? 0xfffc : err.mode == 2 || err.mode == 0 ? 0xfffffc : 0xfffffffc)
        );
}

static const iocshFuncDef toscaCsrReadDef =
    { "toscaCsrRead", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
}};

static void toscaCsrReadFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaCsrRead(args[0].ival);
    if (val == 0xffffffff && errno != 0) perror(NULL);
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaCsrWriteDef =
    { "toscaCsrWrite", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaCsrWriteFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaCsrWrite(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) perror(NULL);
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaCsrSetDef =
    { "toscaCsrSet", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "setbits", iocshArgInt },
}};

static void toscaCsrSetFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaCsrSet(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) perror(NULL);
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaCsrClearDef =
    { "toscaCsrClear", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "clearbits", iocshArgInt },
}};

static void toscaCsrClearFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaCsrClear(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) perror(NULL);
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaIoReadDef =
    { "toscaIoRead", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
}};

static void toscaIoReadFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaIoRead(args[0].ival);
    if (val == 0xffffffff && errno != 0) perror(NULL);
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaIoWriteDef =
    { "toscaIoWrite", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaIoWriteFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaIoWrite(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) perror(NULL);
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaIoSetDef =
    { "toscaIoSet", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "setbits", iocshArgInt },
}};

static void toscaIoSetFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaIoSet(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) perror(NULL);
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaIoClearDef =
    { "toscaIoClear", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "clearbits", iocshArgInt },
}};

static void toscaIoClearFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    errno = 0;
    val = toscaIoClear(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) perror(NULL);
    else printf("0x%08x\n", val);
}

size_t toscaIntrPrintInfo(toscaIntrHandlerInfo_t info, void* user)
{
    static unsigned long long prevIntrCount[TOSCA_NUM_INTR];
    unsigned long long count, delta;
    char* fname, *pname;
    int level = *(int*) user;

    debug("index=%d", info.index);
    count = info.count;
    delta = count - prevIntrCount[info.index];
    if (delta == 0 && level < 0) return 0;
    prevIntrCount[info.index] = count;
    printf(" %s", toscaIntrBitToStr(info.intrmaskbit));
    if (info.intrmaskbit & TOSCA_VME_INTR_ANY) printf(".%-3d ", info.vec);
    printf(" count=%llu (+%llu)", count, delta);
    if (level > 0)
    {
        printf(" %s (%s)",
            fname=symbolName(info.function, (level - 1)| F_SYMBOL_NAME_DEMANGE_FULL),
            pname=symbolName(info.parameter, (level - 1)| F_SYMBOL_NAME_DEMANGE_FULL)),
            free(fname),
            free(pname);
    }
    printf("\n");
    return 0;
}

void toscaIntrShow(int level)
{
    static unsigned long long prevIntrTotalCount;
    unsigned long long count, delta;
    int rep = 0;

    if (level < 0)
    {
        printf("\e[7mPress any key to stop periodic output \e[0m\n");
    }
    do
    {
        if (rep) printf("\n");
        count = toscaIntrCount();
        delta = count - prevIntrTotalCount;
        prevIntrTotalCount = count;
        printf("total number of interrupts: %llu (+%llu)\n", count, delta);
        toscaIntrForeachHandler(toscaIntrPrintInfo, &level);
        rep = 1;
    } while (level < 0 && !waitForKeypress(-1000*level));
}

static const iocshFuncDef toscaIntrShowDef =
    { "toscaIntrShow", 1, (const iocshArg *[]) {
    &(iocshArg) { "level(<0:periodic)", iocshArgInt },
}};

static void toscaIntrShowFunc(const iocshArgBuf *args)
{
    toscaIntrShow(args[0].ival);
}

static const iocshFuncDef toscaIntrLoopStartDef =
    { "toscaIntrLoopStart", 0, (const iocshArg *[]) {
}};

static void toscaIntrLoopStartFunc(const iocshArgBuf *args __attribute__((unused)))
{
    toscaIntrLoopStart();
}

static const iocshFuncDef toscaIntrLoopIsRunningDef =
    { "toscaIntrLoopIsRunning", 0, (const iocshArg *[]) {
}};

static void toscaIntrLoopIsRunningFunc(const iocshArgBuf *args __attribute__((unused)))
{
    printf("%s\n", toscaIntrLoopIsRunning() ? "yes" : "no");
}

static const iocshFuncDef toscaIntrLoopStopDef =
    { "toscaIntrLoopStop", 0, (const iocshArg *[]) {
}};

static void toscaIntrLoopStopFunc(const iocshArgBuf *args __attribute__((unused)))
{
    toscaIntrLoopStop();
}

static const iocshFuncDef toscaIntrConnectHandlerDef =
    { "toscaIntrConnectHandler", 3, (const iocshArg *[]) {
    &(iocshArg) { "intmask", iocshArgString },
    &(iocshArg) { "function", iocshArgString },
    &(iocshArg) { "[argument]", iocshArgString },
}};

static void toscaIntrConnectHandlerFunc(const iocshArgBuf *args)
{
    intrmask_t mask = toscaIntrStrToBit(args[0].sval);
    void (*function)() = symbolAddr(args[1].sval);
    void* arg = args[2].sval;
    
    if (!args[0].sval)
    {
        iocshCmd("help toscaIntrConnectHandler");
        return;
    }
    if (!mask)
    {
        printf("invalid mask");
        return;
    }
    if (!function)
    {
        printf("invalid function");
        return;
    }
    toscaIntrConnectHandler(mask, function, arg);
}

void toscaDebugIntrHandler(void* param, unsigned int inum, unsigned int ivec)
{
    printf("param %s level %u vector %u\n", (char*)param, inum, ivec);
}

static const iocshFuncDef toscaSendVMEIntrDef =
    { "toscaSendVMEIntr", 2, (const iocshArg *[]) {
    &(iocshArg) { "level(1-7)", iocshArgInt },
    &(iocshArg) { "vector(0-255)", iocshArgInt },
}};

static void toscaSendVMEIntrFunc(const iocshArgBuf *args)
{
    if (toscaSendVMEIntr(args[0].ival, args[1].ival) == -1) perror(NULL);
}

static const iocshFuncDef toscaInstallSpuriousVMEInterruptHandlerDef =
    { "toscaInstallSpuriousVMEInterruptHandler", 0, (const iocshArg *[]) {
}};

static void toscaInstallSpuriousVMEInterruptHandlerFunc(const iocshArgBuf *args __attribute__((unused)))
{
    toscaInstallSpuriousVMEInterruptHandler();
}

static const iocshFuncDef toscaDmaTransferDef =
    { "toscaDmaTransfer", 5, (const iocshArg *[]) {
    &(iocshArg) { "[addrspace:]sourceaddr", iocshArgString },
    &(iocshArg) { "[addrspace:]destaddr", iocshArgString },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "swap(WS|DS|QS)", iocshArgString },
    &(iocshArg) { "timeout(0:block|-1:nowait|ms)", iocshArgInt },
}};

static void toscaDmaTransferFunc(const iocshArgBuf *args)
{
    int source = 0, dest = 0, swap = 0;
    size_t source_addr, dest_addr, size;
    unsigned long device;
    char *s, *p;
    
    if (!args[0].sval || !args[1].sval) 
    {
        iocshCmd("help toscaDmaTransfer");
        printf("addrspaces: USER[1|2], SMEM[1|2], A32, BLT, MBLT, 2eVME, 2eVMEFast, 2eSST(160|267|320)\n");
        return;
    }

    device = strtoul(args[0].sval, &s, 0);
    if (*s == ':') s++;
    p = strchr(s, ':');
    if (p) *p++ = 0;
    else p = args[0].sval;
    source = toscaStrToDmaSpace(s);
    if (source == -1)
    {
        fprintf(stderr, "invalid DMA source %s\n",
            args[0].sval);
        return;
    }
    source_addr = toscaStrToSize(p);
    if (source != 0)
    {
        if (source_addr == (size_t)-1) source_addr = 0;
        source |= device << 16;
    }

    device = strtoul(args[1].sval, &s, 0);
    if (*s == ':') s++;
    p = strchr(s, ':');
    if (p) *p++ = 0;
    else p = args[1].sval;
    dest = toscaStrToDmaSpace(s);
    if (dest == -1)
    {
        fprintf(stderr, "invalid DMA dest %s\n",
            args[1].sval);
        return;
    }
    dest_addr = toscaStrToSize(p);
    if (dest != 0)
    {
        if (dest_addr == (size_t)-1) dest_addr = 0;
        dest |= device << 16;
    }
    
    size = toscaStrToSize(args[2].sval);

    if (args[3].sval)
    {
        if (strcasecmp(args[3].sval, "NS") == 0)
            swap = 0;
        else
        if (strcasecmp(args[3].sval, "WS") == 0)
            swap = 2;
        else
        if (strcasecmp(args[3].sval, "DS") == 0)
            swap = 4;
        else
        if (strcasecmp(args[3].sval, "QS") == 0)
            swap = 8;
        else
        {
            fprintf(stderr, "invalid swap %s, must be WS, DS, or QS\n",
                args[3].sval);
            return;
        }
    }
    
    errno = 0;
    toscaDmaTransfer(source, source_addr, dest, dest_addr, size, swap, args[4].ival, NULL, NULL);
    perror(NULL);
}

static const iocshFuncDef toscaStrToDmaSpaceDef =
    { "toscaStrToDmaSpace", 1, (const iocshArg *[]) {
    &(iocshArg) { "addrspace[:address]", iocshArgString },
}};

static void toscaStrToDmaSpaceFunc(const iocshArgBuf *args)
{
    errno = 0;
    int dmaspace = toscaStrToDmaSpace(args[0].sval);
    if (errno) perror(NULL);
    else printf("0x%x\n", dmaspace);
}

static const iocshFuncDef toscaDmaSpaceToStrDef =
    { "toscaDmaSpaceToStr", 1, (const iocshArg *[]) {
    &(iocshArg) { "dmaspace", iocshArgInt },
}};

static void toscaDmaSpaceToStrFunc(const iocshArgBuf *args)
{
    printf("%s\n", toscaDmaSpaceToStr(args[0].ival));
}

static const iocshFuncDef toscaStrToAddrDef =
    { "toscaStrToAddr", 1, (const iocshArg *[]) {
    &(iocshArg) { "addrspace[:address]", iocshArgString },
}};

static void toscaStrToAddrFunc(const iocshArgBuf *args)
{
    errno = 0;
    toscaMapAddr_t addr = toscaStrToAddr(args[0].sval, NULL);
    if (errno) perror(NULL);
    else printf("0x%x:0x%"PRIx64"\n", addr.addrspace, addr.address);
}

static const iocshFuncDef toscaAddrSpaceToStrDef =
    { "toscaAddrSpaceToStr", 1, (const iocshArg *[]) {
    &(iocshArg) { "addrspace", iocshArgInt },
}};

static void toscaAddrSpaceToStrFunc(const iocshArgBuf *args)
{
    printf("%s\n", toscaAddrSpaceToStr(args[0].ival));
}

volatile void* toscaAddrHandler(size_t address, size_t size, size_t addrspace)
{
    return toscaMap(addrspace, address, size, 0);
}

volatile void* toscaAddrTranslator(const char* addrstr, size_t offset, size_t size)
{
    toscaMapAddr_t addr = toscaStrToAddr(addrstr, NULL);
    return toscaMap(addr.addrspace, addr.address, size, 0);
}

static void toscaRegistrar(void)
{
    /* register with 'md' command */
    memDisplayInstallAddrTranslator(toscaAddrTranslator);

    iocshRegister(&toscaNumDevicesDef, toscaNumDevicesFunc);
    iocshRegister(&toscaListDevicesDef, toscaListDevicesFunc);
    iocshRegister(&toscaDeviceTypeDef, toscaDeviceTypeFunc);
    iocshRegister(&toscaMapDef, toscaMapFunc);
    iocshRegister(&toscaMapLookupAddrDef, toscaMapLookupAddrFunc);
    iocshRegister(&toscaMapShowDef, toscaMapShowFunc);
    iocshRegister(&toscaMapFindDef, toscaMapFindFunc);
    iocshRegister(&toscaGetVmeErrDef, toscaGetVmeErrFunc);
    iocshRegister(&toscaCsrReadDef, toscaCsrReadFunc);
    iocshRegister(&toscaCsrWriteDef, toscaCsrWriteFunc);
    iocshRegister(&toscaCsrSetDef, toscaCsrSetFunc);
    iocshRegister(&toscaCsrClearDef, toscaCsrClearFunc);
    iocshRegister(&toscaIoReadDef, toscaIoReadFunc);
    iocshRegister(&toscaIoWriteDef, toscaIoWriteFunc);
    iocshRegister(&toscaIoSetDef, toscaIoSetFunc);
    iocshRegister(&toscaIoClearDef, toscaIoClearFunc);
    iocshRegister(&toscaIntrShowDef, toscaIntrShowFunc);
    iocshRegister(&toscaIntrLoopStartDef, toscaIntrLoopStartFunc);
    iocshRegister(&toscaIntrLoopIsRunningDef, toscaIntrLoopIsRunningFunc);
    iocshRegister(&toscaIntrLoopStopDef, toscaIntrLoopStopFunc);
    iocshRegister(&toscaIntrConnectHandlerDef, toscaIntrConnectHandlerFunc);
    iocshRegister(&toscaSendVMEIntrDef, toscaSendVMEIntrFunc);
    iocshRegister(&toscaInstallSpuriousVMEInterruptHandlerDef, toscaInstallSpuriousVMEInterruptHandlerFunc);
    iocshRegister(&toscaDmaTransferDef, toscaDmaTransferFunc);
    iocshRegister(&toscaStrToDmaSpaceDef, toscaStrToDmaSpaceFunc);
    iocshRegister(&toscaDmaSpaceToStrDef, toscaDmaSpaceToStrFunc);
    iocshRegister(&toscaStrToAddrDef, toscaStrToAddrFunc);
    iocshRegister(&toscaAddrSpaceToStrDef, toscaAddrSpaceToStrFunc);
}

epicsExportRegistrar(toscaRegistrar);

epicsExportAddress(int, toscaMapDebug);
epicsExportAddress(int, toscaIntrDebug);
epicsExportAddress(int, toscaDmaDebug);

