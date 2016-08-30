#ifndef TOSCA_DEBUG_NAME
#error Please define TOSCA_DEBUG_NAME before including toscaDebug.h
#endif

#define CAT(a, b) a ## b
#define TOSCA_DEBUG_VARS(m) \
int CAT(m,Debug); \
FILE* CAT(m,DebugFile) = NULL;
TOSCA_DEBUG_VARS(TOSCA_DEBUG_NAME)

#define debug_internal(m, fmt, ...) if(CAT(m,Debug)) fprintf(CAT(m,DebugFile)?CAT(m,DebugFile):stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#define debugErrno(fmt, ...) debug(fmt " failed: %m", ##__VA_ARGS__)
#define debug(fmt, ...) debug_internal(TOSCA_DEBUG_NAME, fmt, ##__VA_ARGS__)
#define error(fmt, ...) fprintf(stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
