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

/* callbacks (i.e. non-blocking DMA) not yet implemented */
typedef void (*toscaDmaCallback)(void* usr, int status);


/* You can set up a DMA once and use the handle for multiple transfers.
   Release the handle after it is no longer in use (after callback returned).
*/
   
struct dmaRequest* toscaDmaSetup(
    int source, size_t source_addr, int dest, size_t dest_addr,
    size_t size, int swap, int timeout, toscaDmaCallback callback, void* user);

/* source and/or dest are one of:
   0, TOSCA_USER, TOSCA_SHM, VME_SCT, VME_BLT, VME_MBLT, VME_2eVME, VME_2eVMEFast, VME_2eSST160, VME_2eSST267, VME_2eSST320
   0 means local buffer (RAM)
   source and dest cannot be the same space (RAM, USER, SHM, VME)
   Returns NULL on error and sets errno (EINVAL: invalid parameter, e.g. invalid DMA route).
*/

int toscaDmaExecute(struct dmaRequest*);
/* Requests without callback will block */
/* Requests with callback will not block and either return errno or call callback later */
/* The callback function will be called with 0 or errno status */
/* Returns 0 on success or errno */

void toscaDmaRelease(struct dmaRequest*);


/* toscaDmaTransfer works like toscaDmaSetup, toscaDmaExecute, toscaDmaRelease */

int toscaDmaTransfer(
    int source, size_t source_addr, int dest, size_t dest_addr,
    size_t size, int swap, int timeout, toscaDmaCallback callback, void* user);

static inline int toscaDmaWrite(void* source_addr, int dest, size_t dest_addr,
    size_t size, int swap, int timeout, toscaDmaCallback callback, void* user)
{
    return toscaDmaTransfer(0, (size_t)source_addr, dest, dest_addr, size, swap, timeout, callback, user);
}

static inline int toscaDmaRead(int source, size_t source_addr, void* dest_addr,
    size_t size, int swap, int timeout, toscaDmaCallback callback, void* user)
{
    return toscaDmaTransfer(source, source_addr, 0, (size_t)dest_addr, size, swap, timeout, callback, user);
}

/* start this in a separate thread to handle requests with callback */
void toscaDmaLoop();

int toscaDmaLoopIsRunning(void);
/* Is 1 if the toscaDmaLoop is already running. */
/* Further attemts to start the tread terminate silently. */

void toscaDmaLoopStop();
/* Terminate the DMA loop. */
/* Only returns after loop has stopped. */

#endif

