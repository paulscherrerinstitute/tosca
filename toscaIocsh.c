#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <epicsTypes.h>
#include <epicsStdio.h>
#include <iocsh.h>

#include "memDisplay.h"
#include "i2cDev.h"

#include "toscaAddrStr.h"
#include "toscaMap.h"
#include "toscaReg.h"
#include "toscaIntr.h"
#include "toscaDma.h"

#include <epicsExport.h>

#define TOSCA_DEBUG_NAME toscaIocsh
#include "toscaDebug.h"

static const iocshFuncDef toscaMapDef =
    { "toscaMap", 4, (const iocshArg *[]) {
    &(iocshArg) { "(A16|A24|A32|CRCSR|USER|SMEM|TCSR|TIO|SRAM|SLAVE):address", iocshArgString },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "[(USER|SMEM):address", iocshArgString },
    &(iocshArg) { "[SWAP]]", iocshArgString },
}};

static void toscaMapFunc(const iocshArgBuf *args)
{
    toscaMapAddr_t addr, res_addr = {0};
    size_t size;
    volatile void* ptr;

    if (!args[0].sval)
    {
        iocshCmd("help toscaMap");
        return;
    }
    addr = toscaStrToAddr(args[0].sval);
    if (!addr.aspace)
    {
        fprintf(stderr, "invalid Tosca address %s\n",
            args[0].sval);
        return;
    }
    size = toscaStrToSize(args[1].sval);
    if (size == -1)
    {
        perror("toscaStrToSize failed");
        return;
    }
    if (args[2].sval)
    {
        if (!(addr.aspace & VME_SLAVE))
        {
            fprintf(stderr, "invalid argument %s: no SLAVE address space\n",
                args[2].sval);
            return;
        }
        else
        {
            if (strcasecmp(args[2].sval, "SWAP") == 0)
                addr.aspace |= VME_SWAP;
            else
            {
                res_addr = toscaStrToAddr(args[2].sval);
                if (!res_addr.aspace)
                {
                    fprintf(stderr, "invalid Tosca address %s\n",
                        args[2].sval);
                    return;
                }
                if (res_addr.aspace >> 16)
                {
                    fprintf(stderr, "invalid use of card number in %s\n",
                        args[2].sval);
                    return;
                }
                addr.aspace |= res_addr.aspace;
                if (args[3].sval)
                {
                    if (strcasecmp(args[3].sval, "SWAP") == 0)
                        addr.aspace |= VME_SWAP;
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
    ptr = toscaMap(addr.aspace, addr.address, size, res_addr.address);
    if (!ptr && errno)
    {
        perror("toscaMap failed");
        return;
    }
    printf("0x%zx\n", (size_t) ptr);
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
        printf("%s:0x%"PRIx64"\n",
            toscaAddrSpaceToStr(vme_addr.aspace),
            vme_addr.address);
}

int toscaMapPrintInfo(toscaMapInfo_t info, void* unused)
{
    unsigned int card = info.aspace >> 16;
    char buf[60];
    if (card) printf("%u:", card);
    if ((info.aspace & ~(VME_A16|VME_A24|VME_A32|VME_A64|VME_SWAP)) > VME_SLAVE)
    printf("%7s:0x%-8"PRIx64" %16s %7s:0x%-8zx%s\n",
        toscaAddrSpaceToStr(info.aspace),
        info.baseaddress,
        sizeToStr(info.size, buf),
        toscaAddrSpaceToStr(info.aspace & ~(VME_SLAVE|VME_A16|VME_A24|VME_A32|VME_A64|VME_SWAP)),
        (size_t) info.baseptr,
        info.aspace & VME_SWAP ? "SWAP" : "");
    else
    printf("%7s:0x%-8"PRIx64" %16s   %-16p%s\n",
        toscaAddrSpaceToStr(info.aspace),
        info.baseaddress,
        sizeToStr(info.size, buf),
        info.baseptr,
        info.aspace & VME_SWAP ? "SWAP" : "");
    return 0;
}

static const iocshFuncDef toscaMapShowDef =
    { "toscaMapShow", 0, (const iocshArg *[]) {
}};

static void toscaMapShowFunc(const iocshArgBuf *args)
{
    printf("#aspace:baseaddr            size      pointer\n");
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
    if (!info.aspace)
        printf("%p is not a TOSCA address\n", (void*)addr);
    else
        toscaMapPrintInfo(info, NULL);
}

static const iocshFuncDef toscaGetVmeErrDef =
    { "toscaGetVmeErr", 1, (const iocshArg *[]) {
    &(iocshArg) { "card", iocshArgInt },
}};

static void toscaGetVmeErrFunc(const iocshArgBuf *args)
{
    errno = 0;
    toscaMapVmeErr_t err = toscaGetVmeErr(args[0].ival);
    if (errno)
    {
        perror("toscaGetVmeErr failed");
        return;
    }
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

static const iocshFuncDef toscaInstallSpuriousVMEInterruptHandlerDef =
    { "toscaInstallSpuriousVMEInterruptHandler", 0, (const iocshArg *[]) {
}};

static void toscaInstallSpuriousVMEInterruptHandlerFunc(const iocshArgBuf *args)
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
    int card;
    char *s, *p;
    
    if (!args[0].sval || !args[1].sval) 
    {
        iocshCmd("help toscaDmaTransfer");
        printf("addrspaces: USER, SMEM, A32, BLT, MBLT, 2eVME, 2eVMEFast, 2eSST(160|267|320)\n");
        return;
    }

    card = strtoul(args[0].sval, &s, 0);
    if (*s == ':') s++;
    p = strchr(s, ':');
    if (p)
    {
        *p++ = 0;
        source = toscaDmaStrToType(s);
        if (source == -1)
        {
            fprintf(stderr, "invalid DMA source %s\n",
                args[0].sval);
            return;
        }
        source |= card << 16;
    }
    else
        p = args[0].sval;
    source_addr = toscaStrToSize(p);

    card = strtoul(args[1].sval, &s, 0);
    if (*s == ':') s++;
    p = strchr(s, ':');
    if (p)
    {
        *p++ = 0;
        dest = toscaDmaStrToType(s);
        if (dest == -1)
        {
            fprintf(stderr, "invalid DMA dest %s\n",
                args[1].sval);
            return;
        }
        dest |= card << 16;
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
            fprintf(stderr, "invalid swap %s, must be WS, DS, or QS\n",
                args[3].sval);
            return;
        }
    }
    
    int status = toscaDmaTransfer(source, source_addr, dest, dest_addr, size, swap, args[4].ival, NULL, NULL);
    if (status)
    {
        perror("toscaDmaTransfer failed");
    }
}

static const iocshFuncDef toscaStrToAddrDef =
    { "toscaStrToAddr", 1, (const iocshArg *[]) {
    &(iocshArg) { "addrspace[:address]", iocshArgString },
}};

static void toscaStrToAddrFunc(const iocshArgBuf *args)
{
    toscaMapAddr_t addr = toscaStrToAddr(args[0].sval);
    printf("0x%x:0x%"PRIx64"\n", addr.aspace, addr.address);
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
    return toscaMap(aspace, address, size, 0);
}

static void toscaRegistrar(void)
{
    /* register with 'md' command */

    memDisplayInstallAddrHandler("A16",  toscaAddrHandler, VME_A16);
    memDisplayInstallAddrHandler("A24",  toscaAddrHandler, VME_A24);
    memDisplayInstallAddrHandler("A32",  toscaAddrHandler, VME_A32);
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

    memDisplayInstallAddrHandler("SRAM",  toscaAddrHandler, TOSCA_SRAM);

    /* best access these with wordsize = -4 */
    memDisplayInstallAddrHandler("USER",  toscaAddrHandler, TOSCA_USER1);
    memDisplayInstallAddrHandler("USER1", toscaAddrHandler, TOSCA_USER1);
    memDisplayInstallAddrHandler("USER2", toscaAddrHandler, TOSCA_USER2);
    memDisplayInstallAddrHandler("SMEM",  toscaAddrHandler, TOSCA_SMEM);
    memDisplayInstallAddrHandler("SHM",   toscaAddrHandler, TOSCA_SMEM);
    memDisplayInstallAddrHandler("TCSR",  toscaAddrHandler, TOSCA_CSR);
    memDisplayInstallAddrHandler("TIO",   toscaAddrHandler, TOSCA_IO);

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
    iocshRegister(&toscaSendVMEIntrDef, toscaSendVMEIntrFunc);
    iocshRegister(&toscaInstallSpuriousVMEInterruptHandlerDef, toscaInstallSpuriousVMEInterruptHandlerFunc);
    iocshRegister(&toscaDmaTransferDef, toscaDmaTransferFunc);
    iocshRegister(&toscaStrToAddrDef, toscaStrToAddrFunc);
    iocshRegister(&toscaAddrSpaceToStrDef, toscaAddrSpaceToStrFunc);
}

epicsExportRegistrar(toscaRegistrar);

epicsExportAddress(int, toscaMapDebug);
epicsExportAddress(int, toscaIntrDebug);
epicsExportAddress(int, toscaDmaDebug);

