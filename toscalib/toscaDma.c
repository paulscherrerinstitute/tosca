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

int toscaDmaDebug;
FILE* toscaDmaDebugFile = NULL;

#define debug_internal(m, fmt, ...) if(m##Debug) fprintf(m##DebugFile?m##DebugFile:stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#define debugErrno(fmt, ...) debug(fmt " failed: %s", ##__VA_ARGS__, strerror(errno))
#define debug(fmt, ...) debug_internal(toscaDma, fmt, ##__VA_ARGS__)

const char* toscaDmaRouteToStr(int route)
{
    switch (route)
    {
        case VME_DMA_VME_TO_MEM:      /* works with swap 0x400 0x800 0xc00, but 0x100 0x200 0x300 are like 0x400 0x400 0x000 */
            return "VME->MEM";
        case VME_DMA_MEM_TO_VME:      /* works with swap 0x400 0x800 0xc00, but 0x100 0x200 0x300 are like 0x400 0x400 0x000 */
            return "MEM->VME";
        case VME_DMA_VME_TO_VME:      /* copies, but ignores swap 0x400 0x800 0xc00 */
            return "VME->VME";
        case VME_DMA_MEM_TO_MEM:      /* not implemented (EINVALID) */
            return "MEM->MEM";
        case VME_DMA_PATTERN_TO_VME:  /* not implemented (EINVALID) */
            return "PAT->VME";
        case VME_DMA_PATTERN_TO_MEM:  /* not implemented (EINVALID) */
            return "PAT->MEM";
        case VME_DMA_MEM_TO_USER:     /* works */
            return "MEM->USER";
        case VME_DMA_USER_TO_MEM:     /* works */
            return "USER->MEM";
        case VME_DMA_VME_TO_USER:     /* copies, but ignores swap 0x400 0x800 0xc00 */
            return "VME->USER";
        case VME_DMA_USER_TO_VME:     /* copies, but ignores swap 0x400 0x800 0xc00 */
            return "USER->VME";
        case VME_DMA_VME_TO_SHM:      /* works */
            return "VME->SHM";
        case VME_DMA_SHM_TO_VME:      /* works */
            return "SHM->VME";
        case VME_DMA_MEM_TO_SHM:      /* copies, but ignores swap 0x400 0x800 0xc00 */
            return "MEM->SHM";
        case VME_DMA_SHM_TO_MEM:      /* works */
            return "SHM->MEM";
        case VME_DMA_USER_TO_SHM:     /* works */
            return "USER->SHM";
        case VME_DMA_SHM_TO_USER:     /* works */
            return "SHM->USER";
        default:
            return "unknown";
    }
}

const char* toscaDmaTypeToStr(int type)
{
    switch (type)
    {
        case 0:
            return "MEM";
        case TOSCA_USER:
            return "USER";
        case TOSCA_SHM:
            return "SHM";
        case VME_SCT:
            return "VME_SCT";
        case VME_BLT:
            return "VME_BLT";
        case VME_MBLT:
            return "VME_MBLT";
        case VME_2eVME:
            return "VME_2eVME";
        case VME_2eVMEFast:
            return "VME_2eVMEFast";
        case VME_2eSST160:
            return "VME_2eSST160";
        case VME_2eSST267:
            return "VME_2eSST267";
        case VME_2eSST320:
            return "VME_2eSST320";
        default:
            return "????";
    }
}

int toscaDmaStrToType(const char* str)
{
    if (!str || !*str) return 0;
    if (strcmp(str, "0") == 0)
        return 0;
    if (strcmp(str, "MEM") == 0)
        return 0;
    if (strcmp(str, "USER") == 0)
        return TOSCA_USER;
    if (strcmp(str, "SHM") == 0)
        return TOSCA_SHM;
    if (strcmp(str, "VME") == 0)
        return VME_SCT;
    if (strncmp(str, "VME_", 4) == 0) str += 4;
    if (strcmp(str, "A32") == 0)
        return VME_SCT;
    if (strcmp(str, "SCT") == 0)
        return VME_SCT;
    if (strcmp(str, "BLT") == 0)
        return VME_BLT;
    if (strcmp(str, "MBLT") == 0)
        return VME_MBLT;
    if (strcmp(str, "2eVME") == 0)
        return VME_2eVME;
    if (strcmp(str, "2eVMEFast") == 0)
        return VME_2eVMEFast;
    if (strcmp(str, "2eSST160") == 0)
        return VME_2eSST160;
    if (strcmp(str, "2eSST267") == 0)
        return VME_2eSST267;
    if (strcmp(str, "2eSST320") == 0)
        return VME_2eSST320;
    return -1;
}

struct dmaRequest
{
    struct dma_request req;
    toscaDmaCallback callback;
    void *usr;
    struct dmaRequest* next;
    struct dmaRequest* prev;
} *pendingRequests;


int toscaDmaHandleTransfer(struct dma_request *req)
{
    const char *filename = "/dev/dmaproxy0";
    int fd = 0;

    struct dma_execute ex = {0};
    struct timespec start, finished;
 
    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        debugErrno("open %s", filename);
        return errno;
    }
    debug("ioctl VME_DMA_SET");
    if (ioctl(fd, VME_DMA_SET, req) != 0)
    {
        debugErrno("ioctl VME_DMA_SET %s 0x%llx->0x%llx [0x%zx]",
            toscaDmaRouteToStr(req->route), req->src_addr, req->dst_addr,
            req->size);
        close(fd);
        return errno;
    }
    debug("ioctl VME_DMA_EXECUTE");
    if (toscaDmaDebug)
        clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    if (ioctl(fd, VME_DMA_EXECUTE, &ex) != 0)
    {
        debugErrno("ioctl VME_DMA_EXECUTE %s 0x%llx->0x%llx [0x%zx]",
            toscaDmaRouteToStr(req->route), req->src_addr, req->dst_addr,
            req->size);
        close(fd);
        return errno;
    }
    close(fd);
    if (toscaDmaDebug)
    {
        double sec;
        clock_gettime(CLOCK_MONOTONIC_RAW, &finished);
        finished.tv_sec  -= start.tv_sec;
        if ((finished.tv_nsec -= start.tv_nsec) < 0)
        {
            finished.tv_nsec += 1000000000;
            finished.tv_sec--;
        }
        sec = finished.tv_sec + finished.tv_nsec * 1e-9;
        debug("%d %sB / %.3f msec (%.1f MiB/s = %.1f MB/s)",
            req->size >= 0x00100000 ? (req->size >> 20) : req->size >= 0x00000400 ? (req->size >> 10) : req->size,
            req->size >= 0x00100000 ? "Mi" : req->size >= 0x00000400 ? "Ki" : "",
            sec * 1000, req->size/sec/0x00100000, req->size/sec/1000000);
    }
    return 0;
}

int toscaDmaTransfer(int source, size_t source_addr, int dest, size_t dest_addr, size_t size, int swap)
{
    struct dma_request req = {0};
    
    
    req.src_addr = source_addr;
    req.dst_addr = dest_addr;
    req.size = size;
    
    switch (swap)
    {
        case 2:
            req.dwidth = 0x400;
            break;
        case 4:
            req.dwidth = 0x800;
            break;
        case 8:
            req.dwidth = 0xc00;
            break;
    }
        
    switch (source)
    {
        case 0:
            req.src_type = VME_DMA_PCI;
            break;
        case TOSCA_USER:
            req.src_type = VME_DMA_USER;
            break;
        case TOSCA_SHM:
            req.src_type = VME_DMA_SHM;
            break;
        case VME_SCT:
        case VME_BLT:
        case VME_MBLT:
        case VME_2eVME:
        case VME_2eVMEFast:
        case VME_2eSST160:
        case VME_2eSST267:
        case VME_2eSST320:
            req.src_type = VME_DMA_VME;
            req.cycle = source;
            req.aspace = VME_A32;
            break;
    }
    
    switch (dest)
    {
        case 0:
            req.dst_type = VME_DMA_PCI;
            switch (req.src_type)
            {
                case VME_DMA_USER:
                    req.route = VME_DMA_USER_TO_MEM;
                    break;
                case VME_DMA_SHM:
                    req.route = VME_DMA_SHM_TO_MEM;
                    break;
                case VME_DMA_VME:
                    req.route = VME_DMA_VME_TO_MEM;
                    break;
            }
            break;
        case TOSCA_USER:
            req.dst_type = VME_DMA_USER;
            switch (req.src_type)
            {
                case VME_DMA_PCI:
                    req.route = VME_DMA_MEM_TO_USER;
                    break;
                case VME_DMA_SHM:
                    req.route = VME_DMA_SHM_TO_USER;
                    break;
                case VME_DMA_VME:
                    req.route = VME_DMA_VME_TO_USER;
                    break;
            }
            break;
        case TOSCA_SHM:
            req.dst_type = VME_DMA_SHM;
            switch (req.src_type)
            {
                case VME_DMA_PCI:
                    req.route = VME_DMA_MEM_TO_SHM;
                    break;
                case VME_DMA_USER:
                    req.route = VME_DMA_USER_TO_SHM;
                    break;
                case VME_DMA_VME:
                    req.route = VME_DMA_VME_TO_SHM;
                    break;
            }
            break;
        case VME_SCT:
        case VME_BLT:
        case VME_MBLT:
        case VME_2eVME:
        case VME_2eVMEFast:
        case VME_2eSST160:
        case VME_2eSST267:
        case VME_2eSST320:
            req.dst_type = VME_DMA_VME;
            req.cycle = dest;
            req.aspace = VME_A32;
            switch (req.src_type)
            {
                case VME_DMA_PCI:
                    req.route = VME_DMA_MEM_TO_VME;
                    break;
                case VME_DMA_USER:
                    req.route = VME_DMA_USER_TO_VME;
                    break;
                case VME_DMA_SHM:
                    req.route = VME_DMA_SHM_TO_VME;
                    break;
                case VME_DMA_VME:
                    if (source == dest) req.route = VME_DMA_VME_TO_VME;
                    break;
            }
            break;
    }
    if (!req.route)
    {
        debug("invalid DMA route %s -> %s", toscaDmaTypeToStr(source), toscaDmaTypeToStr(dest));
        return errno = EINVAL;
    }
    debug("%s 0x%llx->0x%llx [0x%zx] dw=0x%x %s cy=0x%x %s",
        toscaDmaRouteToStr(req.route), req.src_addr, req.dst_addr,
        req.size, req.dwidth, (const char*[]){"","WS","DS","QS"}[req.dwidth>>10],
        req.cycle, req.cycle ? toscaDmaTypeToStr(req.cycle) : "");
    return toscaDmaHandleTransfer(&req);
}
