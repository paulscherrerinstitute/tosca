#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

typedef uint64_t __u64;
typedef uint32_t __u32;
typedef uint8_t __u8;
#include "vme.h"
#include "vme_user.h"

#include "toscaDma.h"

pthread_mutex_t dma_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK pthread_mutex_lock(&dma_mutex)
#define UNLOCK pthread_mutex_unlock(&dma_mutex)

int toscaDmaDebug;
FILE* toscaDmaDebugFile = NULL;

#define debug_internal(m, fmt, ...) if(m##Debug) fprintf(m##DebugFile?m##DebugFile:stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#define debugErrno(fmt, ...) debug(fmt " failed: %s", ##__VA_ARGS__, strerror(errno))
#define debug(fmt, ...) debug_internal(toscaDma, fmt, ##__VA_ARGS__)

const char* toscaDmaRouteToStr(int route)
{
    switch (route)
    {
        case VME_DMA_VME_TO_MEM:
            return "VME->MEM";
        case VME_DMA_MEM_TO_VME:
            return "MEM->VME";
        case VME_DMA_VME_TO_VME:
            return "VME->VME";
        case VME_DMA_MEM_TO_MEM:
            return "MEM->MEM";
        case VME_DMA_PATTERN_TO_VME:
            return "PAT->VME";
        case VME_DMA_PATTERN_TO_MEM:
            return "PAT->MEM";
        case VME_DMA_MEM_TO_USER:
            return "MEM->USER";
        case VME_DMA_USER_TO_MEM:
            return "USER->MEM";
        case VME_DMA_VME_TO_USER:
            return "VME->USER";
        case VME_DMA_USER_TO_VME:
            return "USER->VME";
        case VME_DMA_VME_TO_SHM:
            return "VME->SHM";
        case VME_DMA_SHM_TO_VME:
            return "SHM->VME";
        case VME_DMA_MEM_TO_SHM:
            return "MEM->SHM";
        case VME_DMA_SHM_TO_MEM:
            return "SHM->MEM";
        case VME_DMA_USER_TO_SHM:
            return "USER->SHM";
        case VME_DMA_SHM_TO_USER:
            return "SHM->USER";
        default:
            return "unknown";
    }
}

const char* toscaDmaTypeToStr(int type, int cycle)
{
    switch (type)
    {
        case VME_DMA_VME:
            switch (cycle & 0xfff)
            {
                case VME_SCT:
                    return "VME_SCT";
                case VME_BLT:
                    return "VME_BLT";
                case VME_MBLT:
                    return "VME_MBLT";
                case VME_2eVME:
                    return "VME_2eVME";
                case VME_2eSST:
                    return "VME_2eSST";
                case VME_2eSSTB:
                    return "VME_2eSSTB";
                case VME_2eVMEFast:
                    return "VME_2eVMEFast";
                case VME_2eSST160:
                    return "VME_2eSST160";
                case VME_2eSST267:
                    return "VME_2eSST267";
                case VME_2eSST320:
                    return "VME_2eSST320";
                default:
                    return "VME_????";
            }
        case VME_DMA_PCI:
            return "MEM";
        case VME_DMA_PATTERN:
            return "PAT";
        case VME_DMA_USER:
            return "USER";
        case VME_DMA_SHM:
            return "SHM";
        default:
            return "???";
    }
}

struct dmaRequest
{
    struct dma_request req;
    toscaDmaCallback callback;
    void *usr;
};

static int toscaDmaFd = 0;

int toscaDmaHandleTransfer(struct dma_request *req)
{
    int status = 0;
    if (ioctl(toscaDmaFd, VME_DMA_SET, req) != 0)
    {
        debugErrno("dma request %s %s:0x%zx->%s:0x%zx [0x%zx]",
            toscaDmaRouteToStr(req->route), 
            toscaDmaTypeToStr(req->src_type, req->cycle), req->src_addr,
            toscaDmaTypeToStr(req->dst_type, req->cycle), req->dst_addr,
            req->size);
        status = errno;
    }
    return status;
}

int toscaDmaTransfer(int route, size_t source_addr, size_t dst_addr, size_t size, unsigned int dwidth, unsigned int cycle, toscaDmaCallback callback, void *usr)
{
    static int fd = 0;
    const char *filename = "/dev/dma_proxy0";
    struct dmaRequest request;
    
    LOCK;
    if (!toscaDmaDd)
    {
        fd = open(filename, O_RDWR);
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            UNLOCK;
            return -1;
        }
    }
    UNLOCK;

    request.req.src_addr = source_addr;
    request.req.dst_addr = dst_addr;
    request.req.size = size;
    request.req.route = route;
    
    request.req.src_type = 
        (route & (VME_DMA_VME_TO_MEM|VME_DMA_VME_TO_VME|VME_DMA_VME_TO_USER|VME_DMA_VME_TO_SHM)) ? VME_DMA_VME :    
        (route & (VME_DMA_MEM_TO_VME|VME_DMA_MEM_TO_MEM|VME_DMA_MEM_TO_USER|VME_DMA_MEM_TO_SHM)) ? VME_DMA_PCI :
        (route & (VME_DMA_PATTERN_TO_VME|VME_DMA_PATTERN_TO_MEM))                  ? VME_DMA_PATTERN :              
        (route & (VME_DMA_USER_TO_MEM|VME_DMA_USER_TO_VME|VME_DMA_USER_TO_SHM))    ? VME_DMA_USER :                 
        (route & (VME_DMA_SHM_TO_VME|VME_DMA_SHM_TO_MEM|VME_DMA_SHM_TO_USER))      ? VME_DMA_SHM : 0;               
    
    request.req.dst_type =
        (route & (VME_DMA_MEM_TO_VME|VME_DMA_VME_TO_VME|VME_DMA_PATTERN_TO_VME|VME_DMA_USER_TO_VME|VME_DMA_SHM_TO_VME)) ? VME_DMA_VME :
        (route & (VME_DMA_VME_TO_MEM|VME_DMA_MEM_TO_MEM|VME_DMA_PATTERN_TO_MEM|VME_DMA_USER_TO_MEM|VME_DMA_SHM_TO_MEM)) ? VME_DMA_PCI :
        (route & (VME_DMA_MEM_TO_USER|VME_DMA_VME_TO_USER|VME_DMA_SHM_TO_USER))    ? VME_DMA_USER :                               
        (route & (VME_DMA_VME_TO_SHM|VME_DMA_MEM_TO_SHM|VME_DMA_USER_TO_SHM))      ? VME_DMA_SHM : 0;                              
    
    request.req.aspace = VME_A32;
    request.req.cycle = cycle;
    request.req.dwidth = dwidth;

    debug("%s %s:0x%zx->%s:0x%zx [0x%zx]", toscaDmaRouteToStr(route), 
        toscaDmaTypeToStr(request.req.src_type, cycle), source_addr, toscaDmaTypeToStr(request.req.dst_type, cycle), dst_addr, size);

    if (!callback) return toscaDmaHandleTransfer(&request);
    request.callback = callback;
    request.usr = usr; 
    return epicsMessageQueueTrySend(toscaDmaMsgQ, &request, sizeof(struct dmaRequest));
}
