#ifndef TOSCA_DEBUG_NAME
#error Please define TOSCA_DEBUG_NAME before including toscaDebug.h
#endif

#include <errno.h>
#include <stdio.h>

#define ___CAT(a, b) a ## b
#ifdef TOSCA_EXTERN_DEBUG
#define ___TOSCA_DEBUG_VARS(m) \
extern int ___CAT(m,Debug); \
extern FILE* ___CAT(m,DebugFile);
#else
#define ___TOSCA_DEBUG_VARS(m) \
int ___CAT(m,Debug) = -1; \
FILE* ___CAT(m,DebugFile);
#endif
___TOSCA_DEBUG_VARS(TOSCA_DEBUG_NAME)
#undef ___TOSCA_DEBUG_VARS

#define debug_internal(m,l,fmt,...) if(___CAT(m,Debug)>=l) ({int e=errno;fprintf(___CAT(m,DebugFile)?___CAT(m,DebugFile):stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__);errno=e;})
#define debugErrno(fmt,...) debugLvl(0, fmt" failed: %m",##__VA_ARGS__)
#define debugLvl(l,fmt,...) debug_internal(TOSCA_DEBUG_NAME,l,fmt,##__VA_ARGS__)
#define debug(fmt,...) debugLvl(1,fmt,##__VA_ARGS__)
#define error(fmt,...) debugLvl(0,fmt,##__VA_ARGS__)
