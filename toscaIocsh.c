#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <epicsTypes.h>
#include <iocsh.h>
#include <epicsExport.h>

#include "memDisplay.h"
#include "symbolname.h"

#include "toscaMap.h"
#include "toscaIntr.h"
#include "toscaDma.h"

static const iocshFuncDef toscaMapDef =
    { "toscaMap", 3, (const iocshArg *[]) {
    &(iocshArg) { "A16|A24|A32|CRCSR|USER|SHM|TCSR", iocshArgString },
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "size", iocshArgInt },
}};

static void toscaMapFunc(const iocshArgBuf *args)
{
    int aspace;
    const char* addrstr = args[0].sval;
    int address = args[1].ival;
    int size = args[2].ival;

    if (!addrstr) 
    {
        iocshCmd("help toscaMap");
        return;
    }
    if (strncmp(addrstr,"VME_", 4) == 0) { addrstr+=4; }
    if (strcmp(addrstr,"A16") == 0) aspace = VME_A16;
    else
    if (strcmp(addrstr,"A24") == 0) aspace = VME_A24;
    else
    if (strcmp(addrstr,"A32") == 0) aspace = VME_A32;
    else
    if (strcmp(addrstr,"CRCSR") == 0) aspace = VME_CRCSR;
    else
    if (strcmp(addrstr,"USER") == 0) aspace = TOSCA_USER;
    else
    if (strcmp(addrstr,"USER1") == 0) aspace = VME_USER1;
    else
    if (strcmp(addrstr,"USER2") == 0) aspace = VME_USER2;
    else
    if (strcmp(addrstr,"USER3") == 0) aspace = VME_USER3;
    else
    if (strcmp(addrstr,"USER4") == 0) aspace = VME_USER4;
    else
    if (strcmp(addrstr,"SHM") == 0) aspace = TOSCA_SHM;
    else
    if (strcmp(addrstr,"TCSR") == 0) aspace = TOSCA_CSR;
    else
    aspace = strtoul(addrstr, NULL, 0);
    printf("%p\n", toscaMap(aspace, address, size));
}

static const iocshFuncDef toscaMapLookupAddrDef =
    { "toscaMapLookupAddr", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgString },
}};

static void toscaMapLookupAddrFunc(const iocshArgBuf *args)
{
    size_t addr = strtoul(args[0].sval, NULL, 0);
    toscaMapAddr_t vme_addr = toscaMapLookupAddr((void*)addr);
    if (!vme_addr.aspace)
        printf("%p is not a TOSCA address\n", (void*)addr);
    else
        printf("%s:%#0llx\n", toscaAddrSpaceToStr(vme_addr.aspace), vme_addr.address);
}

static const iocshFuncDef toscaMapShowDef =
    { "toscaMapShow", 0, (const iocshArg *[]) {
}};

int toscaMapPrintInfo(toscaMapInfo_t info)
{
    printf("%-9s 0x%08llx (0x%08zx = %3d %ciB) @ %p\n",
        toscaAddrSpaceToStr(info.aspace), info.address,
        info.size, info.size >= 0x00100000 ? (info.size >> 20) : (info.size >> 10), info.size >= 0x00100000 ? 'M' : 'K',
        info.ptr);
    return 0;
}

static void toscaMapShowFunc(const iocshArgBuf *args)
{
    toscaMapForeach(toscaMapPrintInfo);
}

static const iocshFuncDef toscaMapFindDef =
    { "toscaMapFind", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgString },
}};

static void toscaMapFindFunc(const iocshArgBuf *args)
{
    size_t addr = strtoul(args[0].sval, NULL, 0);
    toscaMapInfo_t info = toscaMapFind((void*)addr);
    if (!info.aspace)
        printf("%p is not a TOSCA address\n", (void*)addr);
    else
        toscaMapPrintInfo(info);
}

static const iocshFuncDef toscaGetVmeErrDef =
    { "toscaGetVmeErr", 0, (const iocshArg *[]) {
}};

static void toscaGetVmeErrFunc(const iocshArgBuf *args)
{
    toscaMapVmeErr_t err = toscaGetVmeErr();
    printf("0x%08x,0x%08x (%s %s%c%s %s id=%d len=%d %s:0x%x)\n",
        err.address,
        err.status,
        err.err ? "ERR" : "OK",
        err.over ? "OVER " : "",
        err.write ? 'W' : 'R',
        err.timeout ? " TOUT" : "",
        (const char*[]){"PCIe","???","IDMA","USER"}[err.source],
        err.id,
        err.length,
        (const char*[]){"CRCSR","A16","A24","A32","BLT","MBLT","2eVME","2eSST","?8?","?9?","?A?","?B?","?C?","?D?","?E?","IACK"}[err.mode],
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
    else printf("%#x\n", val);
}

static const iocshFuncDef toscaCsrWriteDef =
    { "toscaCsrWrite", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaCsrWriteFunc(const iocshArgBuf *args)
{
    if (toscaCsrWrite(args[0].ival, args[1].ival) == -1) perror(NULL);
}

static const iocshFuncDef toscaCsrSetDef =
    { "toscaCsrSet", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "setbits", iocshArgInt },
}};

static void toscaCsrSetFunc(const iocshArgBuf *args)
{
    if (toscaCsrSet(args[0].ival, args[1].ival) == -1) perror(NULL);
}

static const iocshFuncDef toscaCsrClearDef =
    { "toscaCsrClear", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "clearbits", iocshArgInt },
}};

static void toscaCsrClearFunc(const iocshArgBuf *args)
{
    if (toscaCsrClear(args[0].ival, args[1].ival) == -1) perror(NULL);
}

static const iocshFuncDef toscaIntrShowDef =
    { "toscaIntrShow", 0, (const iocshArg *[]) {
}};

int toscaIntrHandlerPrintInfo(toscaIntrHandlerInfo_t handlerInfo)
{
    char* fname;
    
    if (handlerInfo.intrmaskbit & INTR_VME_LVL_ANY)
    {
        printf("%s %3u %s(%p) %llu\n",
            toscaIntrBitStr(handlerInfo.intrmaskbit), handlerInfo.vec,
            fname=symbolName(handlerInfo.function,0), handlerInfo.parameter, handlerInfo.count);
        free(fname);
    }
    else
    {
        printf("%s %s(%p) %llu\n",
            toscaIntrBitStr(handlerInfo.intrmaskbit),
            fname=symbolName(handlerInfo.function,0), handlerInfo.parameter, handlerInfo.count);
        free(fname);
    }
    
    return 0;
}

static void toscaIntrShowFunc(const iocshArgBuf *args)
{
    unsigned int vectorNumber;
    
    for (vectorNumber = 0; vectorNumber < 256; vectorNumber++)
        toscaIntrForeachHandler(INTR_VME_LVL_ANY, vectorNumber, toscaIntrHandlerPrintInfo);
    toscaIntrForeachHandler(-1ULL-INTR_VME_LVL_ANY, 0, toscaIntrHandlerPrintInfo);
}


static const iocshFuncDef toscaDmaTransferDef =
    { "toscaDmaTransfer", 6, (const iocshArg *[]) {
    &(iocshArg) { "route", iocshArgInt },
    &(iocshArg) { "source_addr", iocshArgInt },
    &(iocshArg) { "dest_addr", iocshArgInt },
    &(iocshArg) { "size", iocshArgInt },
    &(iocshArg) { "dwidth", iocshArgInt },
    &(iocshArg) { "cycle", iocshArgInt },
}};

static void toscaDmaTransferFunc(const iocshArgBuf *args)
{
    if (!args[0].ival) 
    {
        iocshCmd("help toscaDmaTransfer");
        return;
    }

    toscaDmaTransfer(args[0].ival, args[1].ival, args[2].ival, args[3].ival, args[4].ival, args[5].ival);
}

static const iocshFuncDef mallocDef =
    { "malloc", 2, (const iocshArg *[]) {
    &(iocshArg) { "size", iocshArgInt },
    &(iocshArg) { "alignment", iocshArgInt },
}};

static void mallocFunc(const iocshArgBuf *args)
{
    if (args[1].ival)
        printf ("%p\n", memalign(args[0].ival, args[1].ival));
    else
        printf ("%p\n", valloc(args[0].ival));
}

/* register with 'md' command */
static volatile void* toscaAddrHandler(size_t address, size_t size, size_t aspace)
{
    return toscaMap(aspace, address, size);
}

static void toscaRegistrar(void)
{
    memDisplayInstallAddrHandler("A16-",  toscaAddrHandler, VME_A16);
    memDisplayInstallAddrHandler("A24-",  toscaAddrHandler, VME_A24);
    memDisplayInstallAddrHandler("A32-",  toscaAddrHandler, VME_A32);
    memDisplayInstallAddrHandler("A16*",  toscaAddrHandler, VME_A16 | VME_SUPER);
    memDisplayInstallAddrHandler("A24*",  toscaAddrHandler, VME_A24 | VME_SUPER);
    memDisplayInstallAddrHandler("A32*",  toscaAddrHandler, VME_A32 | VME_SUPER);
    memDisplayInstallAddrHandler("A16#",  toscaAddrHandler, VME_A16 | VME_PROG);
    memDisplayInstallAddrHandler("A24#",  toscaAddrHandler, VME_A24 | VME_PROG);
    memDisplayInstallAddrHandler("A32#",  toscaAddrHandler, VME_A32 | VME_PROG);
    memDisplayInstallAddrHandler("A16*#", toscaAddrHandler, VME_A16 | VME_PROG | VME_SUPER);
    memDisplayInstallAddrHandler("A24*#", toscaAddrHandler, VME_A24 | VME_PROG | VME_SUPER);
    memDisplayInstallAddrHandler("A32*#", toscaAddrHandler, VME_A32 | VME_PROG | VME_SUPER);
    memDisplayInstallAddrHandler("A16#*", toscaAddrHandler, VME_A16 | VME_PROG | VME_SUPER);
    memDisplayInstallAddrHandler("A24#*", toscaAddrHandler, VME_A24 | VME_PROG | VME_SUPER);
    memDisplayInstallAddrHandler("A32#*", toscaAddrHandler, VME_A32 | VME_PROG | VME_SUPER);

    /* best access these with wordsize = -4 */
    memDisplayInstallAddrHandler("USER",  toscaAddrHandler, TOSCA_USER1);
    memDisplayInstallAddrHandler("USER1", toscaAddrHandler, TOSCA_USER1);
    memDisplayInstallAddrHandler("USER2", toscaAddrHandler, TOSCA_USER2);
    memDisplayInstallAddrHandler("SHM",   toscaAddrHandler, TOSCA_SHM);
    memDisplayInstallAddrHandler("TCSR",  toscaAddrHandler, TOSCA_CSR);

    iocshRegister(&toscaMapDef, toscaMapFunc);
    iocshRegister(&toscaMapLookupAddrDef, toscaMapLookupAddrFunc);
    iocshRegister(&toscaMapShowDef, toscaMapShowFunc);
    iocshRegister(&toscaMapFindDef, toscaMapFindFunc);
    iocshRegister(&toscaGetVmeErrDef, toscaGetVmeErrFunc);
    iocshRegister(&toscaCsrReadDef, toscaCsrReadFunc);
    iocshRegister(&toscaCsrWriteDef, toscaCsrWriteFunc);
    iocshRegister(&toscaCsrSetDef, toscaCsrSetFunc);
    iocshRegister(&toscaCsrClearDef, toscaCsrClearFunc);
    iocshRegister(&toscaIntrShowDef, toscaIntrShowFunc);
    iocshRegister(&toscaDmaTransferDef, toscaDmaTransferFunc);
    iocshRegister(&mallocDef, mallocFunc);
}

epicsExportRegistrar(toscaRegistrar);

epicsExportAddress(int, toscaMapDebug);
epicsExportAddress(int, toscaIntrDebug);
epicsExportAddress(int, toscaDmaDebug);

