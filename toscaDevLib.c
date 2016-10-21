#define _GNU_SOURCE /* for vasprintf */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include <devLibVME.h>
#include <epicsMutex.h>
#include <epicsTypes.h>
#include <epicsExit.h>
#include "toscaDevLib.h"
#include "toscaDma.h"
#include <epicsExport.h>

#ifndef S_dev_badCRCSR
#define S_dev_badCRCSR S_dev_badA24
#endif

#include "symbolname.h"

/* EPICS has no way to request VME supervisory or user mode. Use Supervisory for all maps. */
#define VME_DEFAULT_MODE VME_SUPER

#define TOSCA_DEBUG_NAME toscaDevLib
#include "toscaDebug.h"
epicsExportAddress(int, toscaDevLibDebug);

/** VME mapping *****************/
const char* addrTypeName[] = {"atVMEA16","atVMEA24","atVMEA32","atISA","atVMECSR"};

long toscaDevLibMapAddr(
    epicsAddressType addrType,
    unsigned int options,
    size_t vmeAddress,
    size_t size,
    volatile void **ppPhysicalAddress)
{
    volatile void *mapAddress;

    /* toscaMap() keeps track of and shares already existing maps.
       No need to track already existing maps here.
    */
    if (addrType >= atLast)
    {
        debug("illegal addrType %d", addrType);
        return S_dev_badArgument;
    }
    debug("addrType=%s, options=%#x, vmeAddress=%#zx, size=%#zx, ppPhysicalAddress=%p",
            addrTypeName[addrType], options, vmeAddress, size, ppPhysicalAddress);
    if (vmeAddress + size < vmeAddress)
    {
        debug("address size overflow");
        return S_dev_badArgument;
    }
    switch (addrType)
    {
        case atVMEA16:
        {
            if (vmeAddress + size > 0x10000ULL)
            {
                debug("A16 address %#zx out of range", vmeAddress + size);
                return S_dev_badA16;
            }
            /* Map full A16 (64KiB). */
            mapAddress = toscaMap(VME_A16 | VME_DEFAULT_MODE, 0, 0x10000);
            if (mapAddress) mapAddress += vmeAddress;
            break;
        }
        case atVMEA24:
        {
            if (vmeAddress + size > 0x1000000ULL)
            {
                debug("A24 address %#zx out of range", vmeAddress + size);
                return S_dev_badA24;
            }
            /* Map A24 (16MiB) in 4 MiB chunks as long as the request does not cross a 4 MiB boundary. */
            if (((vmeAddress ^ (vmeAddress + size)) & 0xc00000) == 0) /* All is in one 4 MiB block. */
            {
                mapAddress = toscaMap(VME_A24 | VME_DEFAULT_MODE, vmeAddress & 0xc00000, 0x400000);
                if (mapAddress) mapAddress += (vmeAddress & 0x3fffff);
            }
            else
                mapAddress = toscaMap(VME_A24 | VME_DEFAULT_MODE, vmeAddress, size);
            break;
        }
        case atVMEA32:
#if __WORDSIZE > 32
            if (vmeAddress + size > 0x100000000ULL)
            {
                debug("A32 address %#zx out of range", vmeAddress + size);
                return S_dev_badA32;
            }
#endif
            mapAddress = toscaMap(VME_A32 | VME_DEFAULT_MODE, vmeAddress, size);
            break;
        case atVMECSR:
            if (vmeAddress + size > 0x1000000ULL)
            {
                debug("CRCSR address %#zx out of range", vmeAddress + size);
                return S_dev_badCRCSR;
            }
            mapAddress = toscaMap(VME_CRCSR, vmeAddress, size);
            break;
        default:
            return S_dev_uknAddrType;
    }
    if (!mapAddress)
    {
        debug("toscaMap failed");
        return S_dev_addrMapFail;
    }
    *ppPhysicalAddress = mapAddress;
    debug("%s:%#zx[%#zx] mapped to %p",
            addrTypeName[addrType], vmeAddress, size, mapAddress);
    return S_dev_success;
}


/** VME probing *****************/

epicsMutexId probeMutex;

long toscaDevLibProbe(
    int isWrite,
    unsigned int wordSize,
    volatile const void *ptr,
    void *pValue)
{
    toscaMapAddr_t vme_addr;
    toscaMapVmeErr_t vme_err;
    int i;
    int readval;
    void* readptr;

    if (isWrite)
        readptr = &readval;
    else
        readptr = pValue;

    vme_addr = toscaMapLookupAddr(ptr);

    if (!vme_addr.aspace) return S_dev_addressNotFound;

    /* I would really like to pause all other threads and processes here. */
    /* At least make sure that we are alone here. */
    epicsMutexMustLock(probeMutex);

    /* Read once to clear BERR bit. */
    toscaGetVmeErr(vme_addr.aspace>>16);

    for (i = 1; i < 1000; i++)  /* We don't want to loop forever. */
    {
        switch (wordSize)
        {
            case 1:
                if (isWrite)
                    *(epicsUInt8 *)(ptr) = *(epicsUInt8 *)pValue;
                else
                *(epicsUInt8 *)readptr = *(epicsUInt8 *)(ptr);
                break;
            case 2:
                if (isWrite)
                    *(epicsUInt16 *)(ptr) = *(epicsUInt16 *)pValue;
                else
                *(epicsUInt16 *)readptr = *(epicsUInt16 *)(ptr);
                break;
            case 4:
                if (isWrite)
                    *(epicsUInt32 *)(ptr) = *(epicsUInt32 *)pValue;
                else
                *(epicsUInt32 *)readptr = *(epicsUInt32 *)(ptr);
                break;
            default:
                epicsMutexUnlock(probeMutex);
                return S_dev_badArgument;
        }
        vme_err = toscaGetVmeErr(vme_addr.aspace>>16);

        if (!vme_err.err)
            return S_dev_success;

        /* Now check if the error came from our access. */
        debug("Our access was %s %#llx",
            toscaAddrSpaceToStr(vme_addr.aspace),
            (unsigned long long) vme_addr.address);
        if (vme_err.source == 0 && /* Error from PCIe, maybe our access. */
            isWrite == vme_err.write) /* Read/write access matches. */
            switch (vme_err.mode) /* Check address space of error. */
            {
                case 0: /* CRCSR */
                    debug("VME bus error at CRCSR %#x", vme_err.address & 0xfffffc);
                    if ((vme_addr.aspace & 0xfff) == VME_CRCSR &&
                        ((vme_err.address ^ vme_addr.address) & 0xfffffc) == 0)
                    {
                        epicsMutexUnlock(probeMutex);
                        return S_dev_noDevice;
                    }
                    break;
                case 1: /* A16 */
                    debug("VME bus error at A16 %#x", vme_err.address & 0xfffc);
                    if ((vme_addr.aspace & 0xfff) == VME_A16 &&
                        ((vme_err.address ^ vme_addr.address) & 0xfffc) == 0)
                    {
                        epicsMutexUnlock(probeMutex);
                        return S_dev_noDevice;
                    }
                    break;
                case 2: /* A24 */
                    debug("VME bus error at A24 %#x", vme_err.address & 0xfffffc);
                    if ((vme_addr.aspace & 0xfff) == VME_A24 &&
                        ((vme_err.address ^ vme_addr.address) & 0xfffffc) == 0)
                    {
                        epicsMutexUnlock(probeMutex);
                        return S_dev_noDevice;
                    }
                    break;
                case 3: /* A32 */
                    debug("VME bus error at A32 %#x", vme_err.address & 0xfffffffc);
                    if ((vme_addr.aspace & 0xfff) == VME_A32 &&
                        ((vme_err.address ^ vme_addr.address) & 0xfffffffc) == 0)
                    {
                        epicsMutexUnlock(probeMutex);
                        return S_dev_noDevice;
                    }
                    break;
            }
#if 0
        /* Does not work. TOSCA never sets bit 30. */
        /* Error was not from us, do we have more than one error? */
        if (!(vme_err.status & (1<<30) /* VME_Error_Over */))
        {
            /* No second error thus our access was OK. */
            epicsMutexUnlock(probeMutex);
            return S_dev_success;
        }
#endif
        debug("try again i=%d", i);
    } /* Repeat until success or error matches our address */
    /* ...or give up. Errors have always been other addresses so far. */
    epicsMutexUnlock(probeMutex);
    return S_dev_success;
}

long toscaDevLibReadProbe(
    unsigned int wordSize,
    volatile const void *ptr,
    void *pValue)
{
    debug("wordSize=%d ptr=%p", wordSize, ptr);
    return toscaDevLibProbe(0, wordSize, ptr, pValue);
}

long toscaDevLibWriteProbe(
    unsigned int wordSize,
    volatile void *ptr,
    const void *pValue)
{
    debug("wordSize=%d ptr=%p", wordSize, ptr);
    return toscaDevLibProbe(1, wordSize, ptr, (void *)pValue);
}


/** VME interrupts *****************/

long toscaDevLibDisableInterruptLevelVME(unsigned int level)
{
    /* We can't disable the interrupts. */
    return S_dev_intDissFail;
}

long toscaDevLibEnableInterruptLevelVME(unsigned int level)
{
    /* Interrupts are always enabled. */
    return S_dev_success;
}

int toscaIntrPrio = 80;
epicsExportAddress(int, toscaIntrPrio);

long toscaDevLibConnectInterrupt(
    unsigned int vec,
    void (*function)(),
    void *parameter)
{
    return toscaIntrConnectHandler(
        vec < 256 ? INTR_VME_LVL_ANY : INTR_USER1_INTR(vec&31),
        vec, function, parameter);
}

long toscaDevLibDisconnectInterrupt(
    unsigned int vec,
    void (*function)())
{
    return toscaIntrDisconnectHandler(
        vec < 256 ? INTR_VME_LVL_ANY : INTR_USER1_INTR(vec&31),
        vec, function, NULL) ? S_dev_success : S_dev_vectorNotInUse;
}

int toscaDevLibInterruptInUseVME(unsigned int vec)
{
    /* Actually this asks if a new handler cannot be connected to vec.
       Since we keep a linked list, a new handler can always be connected.
    */
    return FALSE;
}

/** VME A24 DMA memory *****************/

void *toscaDevLibA24Malloc(size_t size)
{
    /* This function should allocate some DMA capable memory
     * and map it into a A24 slave window.
     * But TOSCA supports only A32 slave windows.
     */
    return NULL;
}

void toscaDevLibA24Free(void *pBlock) {};


/** Initialization *****************/
long toscaDevLibInit(void)
{
    return S_dev_success;
}


/* compatibility with older versions of EPICS */
#if defined(pdevLibVirtualOS) && !defined(devLibVirtualOS)
#define devLibVirtualOS devLibVME
#endif

devLibVirtualOS toscaVirtualOS = {
    toscaDevLibMapAddr,
    toscaDevLibReadProbe,
    toscaDevLibWriteProbe,
    toscaDevLibConnectInterrupt,
    toscaDevLibDisconnectInterrupt,
    toscaDevLibEnableInterruptLevelVME,
    toscaDevLibDisableInterruptLevelVME,
    toscaDevLibA24Malloc,
    toscaDevLibA24Free,
    toscaDevLibInit,
#ifdef pdevLibVirtualOS
    toscaDevLibInterruptInUseVME
#endif
};

static void toscaDevLibRegistrar ()
{
    probeMutex = epicsMutexMustCreate();
    pdevLibVirtualOS = &toscaVirtualOS;
    epicsThreadId tid;
    
    toscaInstallSpuriousVMEInterruptHandler();
    debug("starting interrupt handler thread");
    tid = epicsThreadCreate("irq-TOSCA", toscaIntrPrio,
        epicsThreadGetStackSize(epicsThreadStackMedium),
        toscaIntrLoop, NULL);
    if (!tid)
    {
        debugErrno("starting interrupt handler thread");
    }
    debug("tid = %p", tid);
    epicsAtExit(toscaIntrLoopStop,NULL);

    debug("starting dma handler thread");
    tid = epicsThreadCreate("dma-TOSCA", toscaIntrPrio,
        epicsThreadGetStackSize(epicsThreadStackMedium),
        toscaDmaLoop, NULL);
    if (!tid)
    {
        debugErrno("starting dma handler thread");
    }
    debug("tid = %p", tid);
    epicsAtExit(toscaDmaLoopStop,NULL);
}

epicsExportRegistrar(toscaDevLibRegistrar);
