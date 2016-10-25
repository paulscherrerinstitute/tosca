#ifndef TOSCA_DEBUG_NAME
#error Please define TOSCA_DEBUG_NAME before including toscaDebug.h
#endif

#include <stdio.h>

#ifdef TOSCA_EXTERN_DEBUG
#define __EX extern
#else
#define __EX
#endif
#define CAT(a, b) a ## b
#define TOSCA_DEBUG_VARS(m) \
__EX int CAT(m,Debug); \
__EX FILE* CAT(m,DebugFile);
TOSCA_DEBUG_VARS(TOSCA_DEBUG_NAME)

#undef __EX
#undef __TOSCA_DEBUG_VARS

#define debug_internal(m,l,fmt,...) if(CAT(m,Debug)>=l) fprintf(CAT(m,DebugFile)?CAT(m,DebugFile):stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#define debugErrno(fmt,...) debugLvl(-1, fmt" failed: %m",##__VA_ARGS__)
#define debugLvl(l,fmt,...) debug_internal(TOSCA_DEBUG_NAME,l,fmt,##__VA_ARGS__)
#define debug(fmt,...) debugLvl(1,fmt,##__VA_ARGS__)
#define error(fmt,...) fprintf(stderr,"%s: "fmt"\n",__FUNCTION__,##__VA_ARGS__)

#include <time.h>

#ifdef CLOCK_MONOTONIC_RAW
#define CLOCK CLOCK_MONOTONIC_RAW
#else
#define CLOCK CLOCK_MONOTONIC
#endif

