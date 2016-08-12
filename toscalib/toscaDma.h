#ifndef toscaDma_h
#define toscaDma_h

#include "toscaMap.h"

/* VME block transfer access modes from vme.h */
#define VME_SCT		0x1
#define VME_BLT		0x2
#define VME_MBLT	0x4
#define VME_2eVME	0x8
#define VME_2eVMEFast	0x40
#define VME_2eSST160	0x100
#define VME_2eSST267	0x200
#define VME_2eSST320	0x400

extern int toscaDmaDebug;
extern FILE* toscaDmaDebugFile;

const char* toscaDmaTypeToStr(int type);
int toscaDmaStrToType(const char* str);

typedef void (*toscaDmaCallback)(void* usr, int status);

int toscaDmaTransfer(int source, size_t source_addr, int dest, size_t dest_addr, size_t size, int swap);
static inline int toscaDmaWrite(void* source_addr, int dest, size_t dest_addr, size_t size, int swap)
{
    return toscaDmaTransfer(0, (size_t)source_addr, dest, dest_addr, size, swap);
}
static inline int toscaDmaRead(int source, size_t source_addr, void* dest_addr, size_t size, int swap)
{
    return toscaDmaTransfer(source, source_addr, 0, (size_t)dest_addr, size, swap);
}

#endif

