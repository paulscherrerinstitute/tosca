#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <epicsTypes.h>
#include <epicsStdio.h>
#include <iocsh.h>

#include "memDisplay.h"
#include "symbolname.h"
#include "keypress.h"

#include "toscaMap.h"
#include "toscaReg.h"
#include "toscaIntr.h"
#include "toscaDma.h"
#include "toscaInit.h"

#include <epicsStdioRedirect.h>
#include <epicsTime.h>
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
        fprintf(stderr, "%m\n");
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
    ssize_t size;
    volatile void* ptr;

    if (!args[0].sval)
    {
        iocshCmd("help toscaMap");
        return;
    }
    addr = toscaStrToAddr(args[0].sval, NULL);
    if (!addr.addrspace)
    {
        fprintf(stderr, "Invalid Tosca address \"%s\"\n",
            args[0].sval);
        return;
    }
    size = toscaStrToSize(args[1].sval);
    if (size == -1)
    {
        fprintf(stderr, "Invalid size \"%s\"\n",
            args[1].sval);
        return;
    }
    if (args[2].sval)
    {
        if (!(addr.addrspace & VME_SLAVE))
        {
            fprintf(stderr, "Invalid argument \"%s\": no SLAVE address space\n",
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
                    fprintf(stderr, "Invalid Tosca address %s\n",
                        args[2].sval);
                    return;
                }
                if (res_addr.addrspace >> 16)
                {
                    fprintf(stderr, "Invalid use of device number in %s\n",
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
                        fprintf(stderr, "Invalid argument \"%s\": SWAP expected\n",
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
        fprintf(stderr, "%m\n");
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
    ssize_t addr = toscaStrToSize(args[0].sval);
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
    toscaMapForEach(toscaMapPrintInfo, NULL);
}

static const iocshFuncDef toscaMapFindDef =
    { "toscaMapFind", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgString },
}};

static void toscaMapFindFunc(const iocshArgBuf *args)
{
    ssize_t addr = toscaStrToSize(args[0].sval);
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
        fprintf(stderr, "%m\n");
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

static const iocshFuncDef toscaReadDef =
    { "toscaRead", 1, (const iocshArg *[]) {
    &(iocshArg) { "[device:]addrspace:address", iocshArgString },
}};

static void toscaReadFunc(const iocshArgBuf *args)
{
    toscaMapAddr_t addr = toscaStrToAddr(args[0].sval, NULL);
    if (!addr.addrspace)
    {
        fprintf(stderr, "Invalid Tosca address \"%s\"\n",
            args[0].sval);
        return;
    }
    epicsUInt32 val;
    val = toscaRead(addr.addrspace, addr.address);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaWriteDef =
    { "toscaWrite", 2, (const iocshArg *[]) {
    &(iocshArg) { "[device:]addrspace:address", iocshArgString },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaWriteFunc(const iocshArgBuf *args)
{
    toscaMapAddr_t addr = toscaStrToAddr(args[0].sval, NULL);
    if (!addr.addrspace)
    {
        fprintf(stderr, "Invalid Tosca address \"%s\"\n",
            args[0].sval);
        return;
    }
    epicsUInt32 val = args[1].ival;
    val = toscaWrite(addr.addrspace, addr.address, val);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaSetDef =
    { "toscaSet", 2, (const iocshArg *[]) {
    &(iocshArg) { "[device:]addrspace:address", iocshArgString },
    &(iocshArg) { "setbits", iocshArgInt },
}};

static void toscaSetFunc(const iocshArgBuf *args)
{
    toscaMapAddr_t addr = toscaStrToAddr(args[0].sval, NULL);
    if (!addr.addrspace)
    {
        fprintf(stderr, "Invalid Tosca address \"%s\"\n",
            args[0].sval);
        return;
    }
    epicsUInt32 val = args[1].ival;
    val = toscaSet(addr.addrspace, addr.address, val);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaClearDef =
    { "toscaClear", 2, (const iocshArg *[]) {
    &(iocshArg) { "[device:]addrspace:address", iocshArgString },
    &(iocshArg) { "clearbits", iocshArgInt },
}};

static void toscaClearFunc(const iocshArgBuf *args)
{
    toscaMapAddr_t addr = toscaStrToAddr(args[0].sval, NULL);
    if (!addr.addrspace)
    {
        fprintf(stderr, "Invalid Tosca address \"%s\"\n",
            args[0].sval);
        return;
    }
    epicsUInt32 val = args[1].ival;
    val = toscaClear(addr.addrspace, addr.address, val);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaCsrReadDef =
    { "toscaCsrRead", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
}};

static void toscaCsrReadFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    val = toscaCsrRead(args[0].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
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
    val = toscaCsrWrite(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
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
    val = toscaCsrSet(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
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
    val = toscaCsrClear(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
    else printf("0x%08x\n", val);
}

static const iocshFuncDef toscaIoReadDef =
    { "toscaIoRead", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
}};

static void toscaIoReadFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    val = toscaIoRead(args[0].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
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
    val = toscaIoWrite(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
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
    val = toscaIoSet(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
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
    val = toscaIoClear(args[0].ival, args[1].ival);
    if (val == 0xffffffff && errno != 0) fprintf(stderr, "%m\n");
    else printf("0x%08x\n", val);
}

size_t toscaIntrPrintInfo(const toscaIntrHandlerInfo_t* info, void* user)
{
    static unsigned long long prevIntrCount[TOSCA_NUM_INTR];
    unsigned long long count, delta;
    char* fname, *pname;
    int level = *(int*) user;
    int n=12;

    debug("index=%d", info->index);
    count = info->count;
    delta = count - prevIntrCount[info->index];
    if (delta == 0 && level < 0) return 0;
    prevIntrCount[info->index] = count;
    if (info->device != 0)
        n -= printf(" %d:%s", info->device, toscaIntrBitToStr(info->intrmaskbit));
    else
        n -= printf(" %s", toscaIntrBitToStr(info->intrmaskbit));
    if (info->intrmaskbit & TOSCA_VME_INTR_ANY) n -= printf(".%-3d", info->vec);
    printf("%*ccount=%llu (+%llu)", n, ' ', count, delta);
    if (level > 0)
    {
        printf(" %s(%s)",
            fname=symbolName(info->function, (level - 1)| F_SYMBOL_NAME_DEMANGE_FULL),
            pname=symbolName(info->parameter, (level - 1)| F_SYMBOL_NAME_DEMANGE_FULL)),
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
    epicsTimeStamp sched, now;
    int wait;

    if (level < 0)
    {
        printf("\e[7mPress any key to stop periodic output \e[0m\n");
    }
    epicsTimeGetCurrent(&sched);
    do
    {
        if (rep) printf("\n");
        count = toscaIntrCount();
        delta = count - prevIntrTotalCount;
        prevIntrTotalCount = count;
        printf("total number of interrupts: %llu (+%llu)\n", count, delta);
        toscaIntrForEachHandler(toscaIntrPrintInfo, &level);
        rep = 1;
        epicsTimeAddSeconds(&sched, -level);
        epicsTimeGetCurrent(&now);
        wait=1000*(epicsTimeDiffInSeconds(&sched, &now));
        if (wait<0) wait=0;
    } while (level < 0 && !waitForKeypress(wait));
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

static const char maskhelp[] = "mask: USER[1|2|*][-(0-15)]|VME[-(1-7)](.0-255)|VME-(SYSFAIL|ACFAIL|ERROR|FAIL)\n";

static const iocshFuncDef toscaStrToIntrMaskDef =
    { "toscaStrToIntrMask", 1, (const iocshArg *[]) {
    &(iocshArg) { "intstr", iocshArgArgv },
}};

static void toscaStrToIntrMaskFunc(const iocshArgBuf *args __attribute__((unused)))
{
    intrmask_t mask;
    if (!args[0].aval.av[1])
    {
        iocshCmd("help toscaStrToIntrMask");
        printf(maskhelp);
        return;
    }
    if (args[0].aval.av[2])
    {
        fprintf(stderr, "Too many args \"%s\": Did you use unquoted comma?\n", args[0].aval.av[2]);
        return;
    }
    mask = toscaStrToIntrMask(args[0].aval.av[1]);
    if (!mask)
    {
        fprintf(stderr, "Invalid mask \"%s\"\n" , args[0].aval.av[1]);
        return;
    }
    printf("0x%016"PRIx64"\n", mask);
}


static const iocshFuncDef toscaIntrConnectHandlerDef =
    { "toscaIntrConnectHandler", 3, (const iocshArg *[]) {
    &(iocshArg) { "intmask", iocshArgString },
    &(iocshArg) { "function", iocshArgString },
    &(iocshArg) { "[argument]", iocshArgString },
}};

static void toscaIntrConnectHandlerFunc(const iocshArgBuf *args)
{
    intrmask_t mask = toscaStrToIntrMask(args[0].sval);
    void (*function)() = symbolAddr(args[1].sval);
    void* arg = args[2].sval ? strdup(args[2].sval) : strdup(args[0].sval);
    
    if (!args[0].sval)
    {
        iocshCmd("help toscaIntrConnectHandler");
        printf(maskhelp);
        return;
    }
    if (!mask)
    {
        fprintf(stderr, "Invalid mask \"%s\"\n" , args[0].sval);
        return;
    }
    if (!function)
    {
        fprintf(stderr, "Invalid function \"%s\"\n", args[1].sval);
        return;
    }
    if (toscaIntrConnectHandler(mask, function, arg) != 0) fprintf(stderr, "%m\n");
}

static const iocshFuncDef toscaIntrDisconnectHandlerDef =
    { "toscaIntrDisconnectHandler", 2, (const iocshArg *[]) {
    &(iocshArg) { "intmask", iocshArgString },
    &(iocshArg) { "function", iocshArgString },
}};

static void toscaIntrDisconnectHandlerFunc(const iocshArgBuf *args)
{
    intrmask_t mask = toscaStrToIntrMask(args[0].sval);
    void (*function)() = symbolAddr(args[1].sval);
    int n;
    
    if (!args[0].sval)
    {
        iocshCmd("help toscaIntrDisconnectHandler");
        printf(maskhelp);
        return;
    }
    if (!mask)
    {
        fprintf(stderr, "Invalid mask \"%s\"\n" , args[0].sval);
        return;
    }
    if (!function)
    {
        fprintf(stderr, "Invalid function \"%s\"\n", args[1].sval);
        return;
    }
    n = toscaIntrDisconnectHandler(mask, function, NULL);
    printf("%d handlers disconnected\n", n);
}

void toscaDebugIntrHandler(void* param, unsigned int inum, unsigned int ivec)
{
    printf("interrupt: param %s level %u vector %u\n", (char*)param, inum, ivec);
}

void toscaDummyIntrHandler(void* param, unsigned int inum, unsigned int ivec)
{
}

static const iocshFuncDef toscaIntrEnableDef =
    { "toscaIntrEnable", 1, (const iocshArg *[]) {
    &(iocshArg) { "intmask", iocshArgString },
}};

static void toscaIntrEnableFunc(const iocshArgBuf *args)
{
    intrmask_t mask = toscaStrToIntrMask(args[0].sval);
    if (!args[0].sval) 
    {
        iocshCmd("help toscaIntrDisable");
        printf(maskhelp);
        return;
    }
    if (!mask)
    {
        fprintf(stderr, "Invalid mask \"%s\"\n" , args[0].sval);
        return;
    }
    if (toscaIntrEnable(mask) != 0) fprintf(stderr, "%m\n");
}

static const iocshFuncDef toscaIntrDisableDef =
    { "toscaIntrDisable", 1, (const iocshArg *[]) {
    &(iocshArg) { "intmask", iocshArgString },
}};

static void toscaIntrDisableFunc(const iocshArgBuf *args)
{
    intrmask_t mask = toscaStrToIntrMask(args[0].sval);
    if (!args[0].sval) 
    {
        iocshCmd("help toscaIntrDisable");
        printf(maskhelp);
        return;
    }
    if (!mask)
    {
        fprintf(stderr, "Invalid mask \"%s\"\n" , args[0].sval);
        return;
    }
    if (toscaIntrDisable(mask) != 0) fprintf(stderr, "%m\n");
}

static const iocshFuncDef toscaSendVMEIntrDef =
    { "toscaSendVMEIntr", 2, (const iocshArg *[]) {
    &(iocshArg) { "level(1-7)", iocshArgInt },
    &(iocshArg) { "vector(0-255)", iocshArgInt },
}};

static void toscaSendVMEIntrFunc(const iocshArgBuf *args)
{
    if (toscaSendVMEIntr(args[0].ival, args[1].ival) == -1) fprintf(stderr, "%m\n");
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
    ssize_t source_addr, dest_addr, size;
    const char *s;
    
    if (!args[0].sval || !args[1].sval) 
    {
        iocshCmd("help toscaDmaTransfer");
        printf("addrspaces: USER[1|2], SMEM[1|2], A32, BLT, MBLT, 2eVME, 2eVMEFast, 2eSST(160|267|320)\n");
        return;
    }

    source = toscaStrToDmaSpace(args[0].sval, &s);
    source_addr = toscaStrToSize(s);
    if (source == -1 && source_addr == -1)
    {
        fprintf(stderr, "Invalid DMA source \"%s\"\n", args[0].sval);
        return;
    }
    if (source == -1) source = 0;

    dest = toscaStrToDmaSpace(args[1].sval, &s);
    dest_addr = toscaStrToSize(s);
    if (dest == -1 && dest_addr == -1)
    {
        fprintf(stderr, "Invalid DMA dest \"%s\"\n", s);
        return;
    }
    if (dest == -1) dest = 0;

    size = toscaStrToSize(args[2].sval);
    if (size == -1)
    {
        fprintf(stderr, "Invalid size \"%s\"\n", args[2].sval);
        return;
    }

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
            fprintf(stderr, "Invalid swap \"%s\", must be WS, DS, or QS\n",
                args[3].sval);
            return;
        }
    }
    
    errno = 0;
    toscaDmaTransfer(source, source_addr, dest, dest_addr, size, swap, args[4].ival, NULL, NULL);
    printf("%m\n");
}

static const iocshFuncDef toscaStrToDmaSpaceDef =
    { "toscaStrToDmaSpace", 1, (const iocshArg *[]) {
    &(iocshArg) { "addrspace[:address]", iocshArgString },
}};

static void toscaStrToDmaSpaceFunc(const iocshArgBuf *args)
{
    errno = 0;
    int dmaspace = toscaStrToDmaSpace(args[0].sval, NULL);
    if (errno) fprintf(stderr, "%m\n");
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
    if (errno) fprintf(stderr, "%m\n");
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

volatile void* toscaAddrTranslator(const char* addrstr, size_t offset, size_t size)
{
    toscaMapAddr_t addr = toscaStrToAddr(addrstr, NULL);
    return toscaMap(addr.addrspace, addr.address+offset, size, 0);
}

static void toscaIocshRegistrar(void)
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
    iocshRegister(&toscaReadDef, toscaReadFunc);
    iocshRegister(&toscaWriteDef, toscaWriteFunc);
    iocshRegister(&toscaSetDef, toscaSetFunc);
    iocshRegister(&toscaClearDef, toscaClearFunc);
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
    iocshRegister(&toscaStrToIntrMaskDef, toscaStrToIntrMaskFunc);
    iocshRegister(&toscaIntrConnectHandlerDef, toscaIntrConnectHandlerFunc);
    iocshRegister(&toscaIntrDisconnectHandlerDef, toscaIntrDisconnectHandlerFunc);
    iocshRegister(&toscaIntrEnableDef, toscaIntrEnableFunc);
    iocshRegister(&toscaIntrDisableDef, toscaIntrDisableFunc);
    iocshRegister(&toscaSendVMEIntrDef, toscaSendVMEIntrFunc);
    iocshRegister(&toscaInstallSpuriousVMEInterruptHandlerDef, toscaInstallSpuriousVMEInterruptHandlerFunc);
    iocshRegister(&toscaDmaTransferDef, toscaDmaTransferFunc);
    iocshRegister(&toscaStrToDmaSpaceDef, toscaStrToDmaSpaceFunc);
    iocshRegister(&toscaDmaSpaceToStrDef, toscaDmaSpaceToStrFunc);
    iocshRegister(&toscaStrToAddrDef, toscaStrToAddrFunc);
    iocshRegister(&toscaAddrSpaceToStrDef, toscaAddrSpaceToStrFunc);
}

epicsExportRegistrar(toscaIocshRegistrar);

epicsExportAddress(int, toscaMapDebug);
epicsExportAddress(int, toscaIntrDebug);
epicsExportAddress(int, toscaDmaDebug);

