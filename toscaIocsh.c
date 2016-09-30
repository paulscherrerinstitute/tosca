#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <epicsTypes.h>
#include <epicsTime.h>
#include <iocsh.h>
#include <epicsExport.h>

#include "memDisplay.h"

#include "toscaMap.h"
#include "toscaIntr.h"
#include "toscaDma.h"

static const iocshFuncDef toscaMapDef =
    { "toscaMap", 2, (const iocshArg *[]) {
    &(iocshArg) { "(A16|A24|A32|CRCSR|USER|SHM|TCSR|VME_SLAVE):address", iocshArgString },
    &(iocshArg) { "size", iocshArgString },
}};

static void toscaMapFunc(const iocshArgBuf *args)
{
    unsigned int aspace;
    size_t size;
    size_t address;
    volatile void* addr;
    char* p;

    if (!args[0].sval) 
    {
        iocshCmd("help toscaMap");
        return;
    }
    p = strchr(args[0].sval, ':');
    if (!p)
    {
        fprintf(stderr, "missing address space\n");
        return;
    }
    *p++ = 0;
    aspace = toscaStrToAddrSpace(args[0].sval);
    if (!aspace)
    {
        fprintf(stderr, "invalid address space %s\n", args[0].sval);
        return;
    }
    address = toscaStrToSize(p);
    size = toscaStrToSize(args[1].sval);
    addr = toscaMap(aspace, address, size);
    if (!addr)
    {
        fprintf(stderr, "mapping failed: %m\n");
        return;
    }
    printf("%p\n", addr);
}

static const iocshFuncDef toscaMapVMESlaveDef =
    { "toscaMapVMESlave", 4, (const iocshArg *[]) {
    &(iocshArg) { "(A32|USER|SHM):address", iocshArgString },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "VME_address", iocshArgString },
    &(iocshArg) { "swap", iocshArgInt },
}};

static void toscaMapVMESlaveFunc(const iocshArgBuf *args)
{
    unsigned int aspace;
    size_t size;
    size_t address;
    size_t vme_address;
    char* p;

    if (!args[0].sval) 
    {
        iocshCmd("help toscaMapVMESlave");
        return;
    }
    p = strchr(args[0].sval, ':');
    if (!p)
    {
        fprintf(stderr, "missing address space\n");
        return;
    }
    *p++ = 0;
    aspace = toscaStrToAddrSpace(args[0].sval);
    if (!(aspace & (VME_A32|TOSCA_USER1|TOSCA_SHM)))
    {
        fprintf(stderr, "invalid address space %s\n", args[0].sval);
        return;
    }
    address = toscaStrToSize(p);
    size = toscaStrToSize(args[1].sval);
    vme_address = toscaStrToSize(args[2].sval);
    if (toscaMapVMESlave(aspace, address, size, vme_address, args[3].ival) != 0)
    {
        fprintf(stderr, "mapping failed: %m\n");
        return;
    }
}

static const iocshFuncDef toscaMapLookupAddrDef =
    { "toscaMapLookupAddr", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgString },
}};

static void toscaMapLookupAddrFunc(const iocshArgBuf *args)
{
    size_t addr = toscaStrToSize(args[0].sval);
    toscaMapAddr_t vme_addr = toscaMapLookupAddr((void*)addr);
    if (!vme_addr.aspace)
        printf("%p is not a TOSCA address\n", (void*)addr);
    else
        printf("%s:%#0llx\n",
            toscaAddrSpaceToStr(vme_addr.aspace),
            (unsigned long long)vme_addr.address);
}

static const iocshFuncDef toscaMapShowDef =
    { "toscaMapShow", 0, (const iocshArg *[]) {
}};

int toscaMapPrintInfo(toscaMapInfo_t info, void* unused)
{
    char buf[SIZE_STRING_BUFFER_SIZE];
    printf("%s:0x%llx [%s] MEM:%p\n",
        toscaAddrSpaceToStr(info.aspace),
        (unsigned long long)info.address,
        toscaSizeToStr(info.size, buf),info.ptr);
    return 0;
}

static void toscaMapShowFunc(const iocshArgBuf *args)
{
    toscaMapForeach(toscaMapPrintInfo, NULL);
    toscaCheckSlaveMaps(0,0);
}

static const iocshFuncDef toscaMapFindDef =
    { "toscaMapFind", 1, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgString },
}};

static void toscaMapFindFunc(const iocshArgBuf *args)
{
    size_t addr = toscaStrToSize(args[0].sval);
    toscaMapInfo_t info = toscaMapFind((void*)addr);
    if (!info.aspace)
        printf("%p is not a TOSCA address\n", (void*)addr);
    else
        toscaMapPrintInfo(info, NULL);
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
    { "toscaIntrShow", 1, (const iocshArg *[]) {
    &(iocshArg) { "level(<0:periodic)", iocshArgInt },
}};

static void toscaIntrShowFunc(const iocshArgBuf *args)
{
    toscaIntrShow(args[0].ival);
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
    char* p;
    
    if (!args[0].sval || !args[1].sval) 
    {
        iocshCmd("help toscaDmaTransfer");
        printf("addrspaces: USER, SHM, A32, BLT, MBLT, 2eVME, 2eVMEFast, 2eSST(160|267|320)\n");
        return;
    }

    p = strchr(args[0].sval, ':');
    if (p)
    {
        *p++ = 0;
        source = toscaDmaStrToType(args[0].sval);
        if (source == -1)
        {
            fprintf(stderr, "invalid DMA source %s\n", args[0].sval);
            return;
        }
    }
    else
        p = args[0].sval;
    source_addr = toscaStrToSize(p);

    p = strchr(args[1].sval, ':');
    if (p)
    {
        *p++ = 0;
        dest = toscaDmaStrToType(args[1].sval);
        if (dest == -1)
        {
            fprintf(stderr, "invalid DMA dest %s\n", args[1].sval);
            return;
        }
    }
    else
        p = args[1].sval;
    dest_addr = toscaStrToSize(p);
    
    size = toscaStrToSize(args[2].sval);

    if (args[3].sval)
    {
        if (strcmp(args[3].sval, "WS") == 0)
            swap = 2;
        else
        if (strcmp(args[3].sval, "DS") == 0)
            swap = 4;
        else
        if (strcmp(args[3].sval, "QS") == 0)
            swap = 8;
        else
        {
            fprintf(stderr, "invalid swap %s, must be WS, DS, or QS\n", args[3].sval);
            return;
        }
    }
    
    int status = toscaDmaTransfer(source, source_addr, dest, dest_addr, size, swap, args[4].ival, NULL, NULL);
    if (status)
    {
        fprintf(stderr, "toscaDmaTransfer failed: %m\n");
    }
}

static const iocshFuncDef toscaStrToAddrSpaceDef =
    { "toscaStrToAddrSpace", 1, (const iocshArg *[]) {
    &(iocshArg) { "addrspace", iocshArgString },
}};

static void toscaStrToAddrSpaceFunc(const iocshArgBuf *args)
{
    printf("0x%x\n", toscaStrToAddrSpace(args[0].sval));
}

static const iocshFuncDef toscaAddrSpaceToStrDef =
    { "toscaAddrSpaceToStr", 1, (const iocshArg *[]) {
    &(iocshArg) { "addrspace", iocshArgInt },
}};

static void toscaAddrSpaceToStrFunc(const iocshArgBuf *args)
{
    printf("%s\n", toscaAddrSpaceToStr(args[0].ival));
}

volatile void* toscaAddrHandler(size_t address, size_t size, size_t aspace)
{
    return toscaMap(aspace, address, size);
}

static void toscaRegistrar(void)
{
    /* register with 'md' command */

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
    iocshRegister(&toscaMapVMESlaveDef, toscaMapVMESlaveFunc);
    iocshRegister(&toscaMapLookupAddrDef, toscaMapLookupAddrFunc);
    iocshRegister(&toscaMapShowDef, toscaMapShowFunc);
    iocshRegister(&toscaMapFindDef, toscaMapFindFunc);
    iocshRegister(&toscaGetVmeErrDef, toscaGetVmeErrFunc);
    iocshRegister(&toscaCsrReadDef, toscaCsrReadFunc);
    iocshRegister(&toscaCsrWriteDef, toscaCsrWriteFunc);
    iocshRegister(&toscaCsrSetDef, toscaCsrSetFunc);
    iocshRegister(&toscaCsrClearDef, toscaCsrClearFunc);
    iocshRegister(&toscaIntrShowDef, toscaIntrShowFunc);
    iocshRegister(&toscaSendVMEIntrDef, toscaSendVMEIntrFunc);
    iocshRegister(&toscaDmaTransferDef, toscaDmaTransferFunc);
    iocshRegister(&toscaStrToAddrSpaceDef, toscaStrToAddrSpaceFunc);
    iocshRegister(&toscaAddrSpaceToStrDef, toscaAddrSpaceToStrFunc);
}

epicsExportRegistrar(toscaRegistrar);

epicsExportAddress(int, toscaMapDebug);
epicsExportAddress(int, toscaIntrDebug);
epicsExportAddress(int, toscaDmaDebug);

