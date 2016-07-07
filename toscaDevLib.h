#ifndef toscaDev_h
#include <epicsThread.h>
#include "toscaMap.h"
#include "toscaIntr.h"
epicsThreadId toscaStartIntrThread(const char* threadname, intrmask_t intrmask, unsigned int vec);
#endif
