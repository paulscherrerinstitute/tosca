#include <iocsh.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <epicsTypes.h>
#include <epicsExport.h>

#include "memDisplay.h"
#include "toscaMap.h"
#include "devLibVME.h"

static const iocshFuncDef toscaMapDef =
    { "toscaMap", 3, (const iocshArg *[]) {
    &(iocshArg) { "busnumber", iocshArgInt },
    &(iocshArg) { "A16|A24|A32|CRCSR", iocshArgString },
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
        printf("usage: toscaMap A16|A24|A32|CRCSR|USER|SHM, address, size\n");
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
    if (strcmp(addrstr,"SHMEM") == 0) aspace = TOSCA_SHMEM;
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
    if (vme_addr.bus == -1)
        printf("%p is not a TOSCA address\n", (void*)addr);
    else
        printf("bus %d: %s:%#0llx\n", vme_addr.bus, toscaAddrSpaceStr(vme_addr.aspace), vme_addr.address);
}

static const iocshFuncDef toscaMapShowDef =
    { "toscaMapShow", 0, (const iocshArg *[]) {
}};

int toscaMapPrintInfo(toscaMapInfo_t info)
{
    printf("bus %d: %-9s 0x%08llx (0x%08zx = %3d %ciB) @ %p\n",
        info.bus, toscaAddrSpaceStr(info.aspace), info.address,
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
    if (info.bus == -1)
        printf("%p is not a TOSCA address\n", (void*)addr);
    else
        toscaMapPrintInfo(info);
}

static const iocshFuncDef toscaMapGetVmeErrDef =
    { "toscaMapGetVmeErr", 0, (const iocshArg *[]) {
}};

static void toscaMapGetVmeErrFunc(const iocshArgBuf *args)
{
    toscaMapVmeErr_t err = toscaMapGetVmeErr();
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
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaCsrSetFunc(const iocshArgBuf *args)
{
    if (toscaCsrSet(args[0].ival, args[1].ival) == -1) perror(NULL);
}

static const iocshFuncDef toscaCsrClearDef =
    { "toscaCsrClear", 2, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "value", iocshArgInt },
}};

static void toscaCsrClearFunc(const iocshArgBuf *args)
{
    if (toscaCsrClear(args[0].ival, args[1].ival) == -1) perror(NULL);
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
    memDisplayInstallAddrHandler("SHMEM", toscaAddrHandler, TOSCA_SHMEM);
    memDisplayInstallAddrHandler("TCSR",  toscaAddrHandler, TOSCA_CSR);

    iocshRegister(&toscaMapDef, toscaMapFunc);
    iocshRegister(&toscaMapLookupAddrDef, toscaMapLookupAddrFunc);
    iocshRegister(&toscaMapShowDef, toscaMapShowFunc);
    iocshRegister(&toscaMapFindDef, toscaMapFindFunc);
    iocshRegister(&toscaMapGetVmeErrDef, toscaMapGetVmeErrFunc);
    iocshRegister(&toscaCsrReadDef, toscaCsrReadFunc);
    iocshRegister(&toscaCsrWriteDef, toscaCsrWriteFunc);
    iocshRegister(&toscaCsrSetDef, toscaCsrSetFunc);
    iocshRegister(&toscaCsrClearDef, toscaCsrClearFunc);
}

epicsExportRegistrar(toscaRegistrar);

epicsExportAddress(int, toscaMapDebug);

