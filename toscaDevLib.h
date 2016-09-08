#ifndef toscaDevLib_h
#define toscaDevLib_h

#ifdef __cplusplus
extern "C" {
#endif

#include <epicsThread.h>
#include <compilerDependencies.h>
#include "toscaMap.h"
#include "toscaIntr.h"
epicsThreadId toscaStartIntrThread(intrmask_t intrmask, unsigned int vec, const char* threadname, ...) EPICS_PRINTF_STYLE(3,4);

/* Add 0x100 to vec for USER1 0-15 and 0x110 for USER2 0-15 interrupt lines
 * else vec is VME interrupt vector 0-255 on any interrupt level. */
long toscaDevLibConnectInterrupt(unsigned int vec, void (*function)(), void *parameter);

#ifdef __cplusplus
}
#endif
#endif
