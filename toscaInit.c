#include <epicsThread.h>
#include <initHooks.h>
#include <epicsExit.h>

#include "toscaIntr.h"
#include "toscaDma.h"
#include <epicsExport.h>
#include "toscaInit.h"

#define TOSCA_DEBUG_NAME toscaInit
#include "toscaDebug.h"
epicsExportAddress(int, toscaInitDebug);

int toscaIntrPrio = 80;
epicsExportAddress(int, toscaIntrPrio);

int toscaDmaPrio = 80;
epicsExportAddress(int, toscaDmaPrio);

int toscaIntrLoopStart(void)
{
    epicsThreadId tid;

    debug("starting interrupt handler thread");
    tid = epicsThreadCreate("irq-TOSCA", toscaIntrPrio,
        epicsThreadGetStackSize(epicsThreadStackMedium),
        (EPICSTHREADFUNC)toscaIntrLoop, NULL);
    if (!tid) {
        debugErrno("starting irq-TOSCA thread"); 
        return -1;
    }
    debug("irq-TOSCA tid = %p", tid);
    return 0;
}

int toscaDmaLoopsStart(unsigned int n)
{
    epicsThreadId tid;
    unsigned int i;
    int status = 0;

    debug("starting dma handler threads");
    for (i = 1; i <= n; i++)
    {
        char name[32];
        sprintf(name, "dma%d-TOSCA", i);
        tid = epicsThreadCreate(name, toscaDmaPrio,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC)toscaDmaLoop, NULL);
        if (!tid) {
            debugErrno("starting %s thread", name);
            status = -1;
        }
        else debug("%s tid = %p", name, tid);
    }
    return status;
}

void toscaInitHook(initHookState state)
{
    unsigned int n; 

    if (state != initHookAfterInitDrvSup) return;
    
    n = toscaNumDevices();
    if (n == 0)
    {
        fprintf(stderr, "No Tosca device found. Kernel driver not loaded?\n");
        return;
    }
    if (toscaInitDebug > 0)
    {
        unsigned int device, type;
        for (device = 0; device < n; device++)
        {
            type = toscaDeviceType(device);
            printf("Tosca device %u: %04x\n", device, type);
        }
    }
    toscaIntrLoopStart();
    epicsAtExit(toscaIntrLoopStop,NULL);

    toscaDmaLoopsStart(toscaDeviceType(0) == 0x1210 ? 2 : 4);
    epicsAtExit(toscaDmaLoopsStop,NULL);
}

static void toscaInitRegistrar ()
{
    initHookRegister(toscaInitHook);
}

epicsExportRegistrar(toscaInitRegistrar);
