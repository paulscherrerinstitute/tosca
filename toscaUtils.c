#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <byteswap.h>
#include "toscaMap.h"
#include "toscaDma.h"

#include <iocsh.h>
#include <epicsExport.h>

#define debug_internal(m, fmt, ...) if(m##Debug) fprintf(m##DebugFile?m##DebugFile:stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#define debugErrno(fmt, ...) debug(fmt " failed: %s", ##__VA_ARGS__, strerror(errno))
#define debug(fmt, ...) debug_internal(toscaDma, fmt, ##__VA_ARGS__)

extern size_t strToSize(const char* str);

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

static const iocshFuncDef memfillDef =
    { "memfill", 5, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgString },
    &(iocshArg) { "pattern", iocshArgInt },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "width", iocshArgInt },
    &(iocshArg) { "increment", iocshArgInt },
}};

static void memfillFunc(const iocshArgBuf *args)
{
    jmp_buf memfillFail;
    void memfillSigAction(int sig, siginfo_t *info, void *ctx)
    {
        fprintf(stdout, "\nInvalid address %p.\n", info->si_addr);
        longjmp(memfillFail, 1);
    }
    struct sigaction sa = {
        .sa_sigaction = memfillSigAction,
        .sa_flags = SA_SIGINFO | SA_NODEFER, /* Do not block signal */
    }, oldsa;
    sigaction(SIGSEGV, &sa, &oldsa);
    if (setjmp(memfillFail) != 0)
    {
        sigaction(SIGSEGV, &oldsa, NULL);
        return;
    }

    if (!args[0].sval)
    {
        iocshCmd("help memfill");
        return;
    }
    size_t address = strtoul(args[0].sval, NULL, 0);
    uint32_t pattern = args[1].ival;
    int size = strToSize(args[2].sval);
    int width = args[3].ival;
    int increment = args[4].ival;
    int i;

    switch (width)
    {
        case 0:
        case 1:
            for (i = 0; i < size; i++)
            {
                ((uint8_t*)address)[i] = pattern;
                pattern += increment;
            }
            break;
        case 2:
            size >>= 1;
            for (i = 0; i < size; i++)
            {
                ((uint16_t*)address)[i] = pattern;
                pattern += increment;
            }
            break;
        case 4:
            size >>= 2;
            for (i = 0; i < size; i++)
            {
                ((uint32_t*)address)[i] = pattern;
                pattern += increment;
            }
            break;
        default:
            fprintf(stderr, "Illegal width %d: must be 1, 2, or 4\n", width);
    }

    sigaction(SIGSEGV, &oldsa, NULL);
}

static const iocshFuncDef toscaCopyDef =
    { "toscaCopy", 4, (const iocshArg *[]) {
    &(iocshArg) { "[aspace:]source", iocshArgString },
    &(iocshArg) { "[aspace:]dest", iocshArgString },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "width", iocshArgInt },
}};

void toscaCopyFunc(const iocshArgBuf *args)
{
    struct timespec start, finished;
    volatile void* sourceptr;
    volatile void* destptr;
    int aspace;
    size_t size;
    char* p;
    int i;
    
    if (!args[0].sval || !args[1].sval || !args[2].sval)
    {
        iocshCmd("help toscaCopy");
        return;
    }
    size = strToSize(args[2].sval);

    p = strchr(args[0].sval, ':');
    if (p)
    {
        *p++ = 0;
        aspace = toscaStrToAddrSpace(args[0].sval);
        if (!aspace)
        {
            fprintf(stderr, "invalid source address space %s\n", args[0].sval);
            return;
        }
        sourceptr = toscaMap(aspace, strtoul(p, NULL, 0), size);
    }
    else
        sourceptr = (volatile void*)strtoul(args[0].sval, NULL, 0);

    p = strchr(args[1].sval, ':');
    if (p)
    {
        *p++ = 0;
        aspace = toscaStrToAddrSpace(args[1].sval);
        if (!aspace)
        {
            fprintf(stderr, "invalid dest address space %s\n", args[1].sval);
            return;
        }
        destptr = toscaMap(aspace, strtoul(p, NULL, 0), size);
    }
    else
        destptr = (volatile void*)strtoul(args[1].sval, NULL, 0);

    int width = args[3].ival;

    if (toscaDmaDebug)
        clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    switch (width)
    {
        case 0:
            memcpy((void*)destptr, (void*)sourceptr, size);
            break;
        case 1:
        case -1:
            for (i = 0; i < size; i++)
            {
                ((uint8_t*)destptr)[i] = ((uint8_t*)sourceptr)[i];
            }
            break;
        case 2:
            for (i = 0; i < size/2; i++)
            {
                ((uint16_t*)destptr)[i] = ((uint16_t*)sourceptr)[i];
            }
            break;
        case 4:
            for (i = 0; i < size/4; i++)
            {
                ((uint32_t*)destptr)[i] = ((uint32_t*)sourceptr)[i];
            }
            break;
        case 8:
            for (i = 0; i < size/8; i++)
            {
                ((uint64_t*)destptr)[i] = ((uint64_t*)sourceptr)[i];
            }
            break;
        case -2:
            for (i = 0; i < size/2; i++)
            {
                ((uint16_t*)destptr)[i] = bswap_16(((uint16_t*)sourceptr)[i]);
            }
            break;
        case -4:
            for (i = 0; i < size/4; i++)
            {
                ((uint32_t*)destptr)[i] = bswap_32(((uint32_t*)sourceptr)[i]);
            }
            break;
        case -8:
            for (i = 0; i < size/8; i++)
            {
                ((uint64_t*)destptr)[i] = bswap_64(((uint64_t*)sourceptr)[i]);
            }
            break;
        default:
            fprintf(stderr, "Illegal width %d: must be 1, 2, 4, 8, -2, -4, -8\n", width);
            return;
    }
    if (toscaDmaDebug)
    {
        double sec;
        clock_gettime(CLOCK_MONOTONIC_RAW, &finished);
        finished.tv_sec  -= start.tv_sec;
        if ((finished.tv_nsec -= start.tv_nsec) < 0)
        {
            finished.tv_nsec += 1000000000;
            finished.tv_sec--;
        }
        sec = finished.tv_sec + finished.tv_nsec * 1e-9;
        debug("%d %sB / %.3f msec (%.1f MiB/s = %.1f MB/s)",
            size >= 0x00100000 ? (size >> 20) : size >= 0x00000400 ? (size >> 10) : size,
            size >= 0x00100000 ? "Mi" : size >= 0x00000400 ? "Ki" : "",
            sec * 1000, size/sec/0x00100000, size/sec/1000000);
    }
    
}

static void toscaUtilsRegistrar(void)
{
    iocshRegister(&mallocDef, mallocFunc);
    iocshRegister(&memfillDef, memfillFunc);
    iocshRegister(&toscaCopyDef, toscaCopyFunc);
}

epicsExportRegistrar(toscaUtilsRegistrar);