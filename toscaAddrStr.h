#ifndef toscaAddrStr_h
#define toscaAddrStr_h

#include "toscaMap.h"

vmeaddr_t toscaStrToOffset(const char* str);
size_t toscaStrToSize(const char* str);
toscaMapAddr_t toscaStrToAddr(const char* str);

#endif
