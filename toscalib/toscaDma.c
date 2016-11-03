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
#include <symbolname.h>

typedef uint64_t __u64;
typedef uint32_t __u32;
typedef uint8_t __u8;
#include "vme.h"
#include "vme_user.h"

#include "toscaDma.h"

#define TOSCA_DEBUG_NAME toscaDma
#include "toscaDebug.h"

pthread_mutex_t dma_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK pthread_mutex_lock(&dma_mutex)
#define UNLOCK pthread_mutex_unlock(&dma_mutex)

pthread_cond_t dma_wakeup = PTHREAD_COND_INITIALIZER;
#define UNLOCK_AND_SLEEP pthread_cond_wait(&dma_wakeup, &dma_mutex);
#define WAKEUP pthread_cond_signal(&dma_wakeup);

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
        case TOSCA_SMEM:
            return "SMEM";
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

const char* toscaDmaWidthToSwapStr(int width)
{
    return (const char*[]){"","WS","DS","QS"}[width>>10];
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
    if (strcmp(str, "SMEM") == 0)
        return TOSCA_SMEM;
    if (strcmp(str, "SHM") == 0)
        return TOSCA_SMEM;
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
    int fd;
    int timeout;
    int oneShot;
    toscaDmaCallback callback;
    void *user;
    struct dmaRequest* next;
} *pending, *freelist, **insert=&pending;

int toscaDmaDoTransfer(struct dmaRequest* r)
{
    struct dma_execute ex = {0};
    struct timespec start, finished;
 
#ifdef VME_DMA_TIMEOUT
    debugLvl(2, "ioctl(%d, VME_DMA_TIMEOUT, %d ms)", r->fd, r->timeout);
    ioctl(r->fd, VME_DMA_TIMEOUT, r->timeout);
#endif
    if (toscaDmaDebug)
        clock_gettime(CLOCK, &start);
    debugLvl(2, "ioctl(%d, VME_DMA_EXECUTE, {%s 0x%llx->0x%llx [0x%x] dw=0x%x %s cy=0x%x=%s})",
        r->fd,
        toscaDmaRouteToStr(r->req.route),
        (unsigned long long) r->req.src_addr,
        (unsigned long long) r->req.dst_addr,
        r->req.size,
        r->req.dwidth,
        toscaDmaWidthToSwapStr(r->req.dwidth),
        r->req.cycle,
        toscaDmaTypeToStr(r->req.cycle));
    if (ioctl(r->fd, VME_DMA_EXECUTE, &ex) != 0)
    {
        debugErrno("ioctl(%d, VME_DMA_EXECUTE, {%s 0x%llx->0x%llx [0x%x] dw=0x%x %s cy=0x%x=%s})",
            r->fd,
            toscaDmaRouteToStr(r->req.route),
            (unsigned long long) r->req.src_addr,
            (unsigned long long) r->req.dst_addr,
            r->req.size,
            r->req.dwidth,
            toscaDmaWidthToSwapStr(r->req.dwidth),
            r->req.cycle,
            toscaDmaTypeToStr(r->req.cycle));
        if (r->oneShot) toscaDmaRelease(r);
        return errno;
    }
    if (toscaDmaDebug)
    {
        double sec;
        clock_gettime(CLOCK, &finished);
        finished.tv_sec  -= start.tv_sec;
        if ((finished.tv_nsec -= start.tv_nsec) < 0)
        {
            finished.tv_nsec += 1000000000;
            finished.tv_sec--;
        }
        sec = finished.tv_sec + finished.tv_nsec * 1e-9;
        debug("%s %d %sB / %.3f msec (%.1f MiB/s = %.1f MB/s)",
            toscaDmaRouteToStr(r->req.route), 
            r->req.size >= 0x00100000 ? (r->req.size >> 20) : r->req.size >= 0x00000400 ? (r->req.size >> 10) : r->req.size,
            r->req.size >= 0x00100000 ? "Mi" : r->req.size >= 0x00000400 ? "Ki" : "",
            sec * 1000, r->req.size/sec/0x00100000, r->req.size/sec/1000000);
    }
    if (r->oneShot) toscaDmaRelease(r);
    return 0;
}

static int loopRunning = 0;

void toscaDmaLoop()
{
    struct dmaRequest* r;
    int status;
    toscaDmaCallback callback;
    void* user;
    
    LOCK;
    if (loopRunning)
    {
        UNLOCK;
        return;
    }
    loopRunning = 1;

    while (1)
    {
        while ((r = pending) != NULL)
        {
            if (insert == &r->next) insert = &pending;
            pending = r->next;
            r->next = NULL;
            UNLOCK;
            if (r->fd <= 0) /* may have been canceled */
            {
                if (r->oneShot) toscaDmaRelease(r);
            }
            else
            {
                callback = r->callback;
                user = r->user;
                status = toscaDmaDoTransfer(r); /* blocks */
                callback(user, status);
            }
            LOCK;
        }
        UNLOCK_AND_SLEEP;
        if (loopRunning < 0) break;
    }
    loopRunning = 0;
    UNLOCK;
}

int toscaDmaLoopIsRunning(void)
{
    return loopRunning;
}

void toscaDmaLoopStop()
{
    struct timespec wait = { 0, 10000000 };
    loopRunning = -1;
    WAKEUP;
    while (loopRunning) nanosleep(&wait, NULL);
}

int toscaDmaExecute(struct dmaRequest* r)
{
    char* fname;
    if (!r || r->fd <= 0) return errno = EINVAL;
    if (r->callback)
    {
        debugLvl(2, "queuing: callback=%s(%p)", fname=symbolName(r->callback,0), r->user), free(fname);
        LOCK;
        if (!pending) WAKEUP;
        r->next = NULL;
        *insert = r;
        insert = &r->next;
        UNLOCK;
        return 0;
    }
    else return toscaDmaDoTransfer(r);
}

struct dmaRequest* toscaDmaRequestCreate(void)
{
    struct dmaRequest* r;

    LOCK;
    if ((r = freelist) != NULL)
    {
        freelist = r->next;
        debugLvl(3, "got request %p from freelist, freelist=%p", r, freelist);
    }
    UNLOCK;
    if (!r)
    {
        r = malloc(sizeof(struct dmaRequest));
        if (!r) 
        {
            debugErrno("malloc struct dmaRequest");
            return NULL;
        }
        debugLvl(3, "got request %p from malloc", r);
    }
    r->fd = -1;
    r->next = NULL;
    return r;
}

void toscaDmaRelease(struct dmaRequest* r)
{
    if (!r) return;
    LOCK;
    if (r->fd > 0) close(r->fd);
    r->fd = -1;
    if (!r->next)
    {
        debugLvl(3, "put back request %p to freelist, freelist = %p", r, freelist);
        r->next = freelist;
        freelist = r;
    }
    UNLOCK;
}

struct dmaRequest* toscaDmaSetup(int source, size_t source_addr, int dest, size_t dest_addr,
    size_t size, int swap, int timeout,
    toscaDmaCallback callback, void* user)
{
    struct dmaRequest* r;
    char* fname;
    unsigned int card = (source | dest) >> 16;
    char filename[20];
    
    debugLvl(2, "0x%x=%s:0x%zx -> 0x%x=%s:0x%zx [0x%zx] swap=%d tout=%d cb=%s(%p)",
        source, toscaDmaTypeToStr(source), source_addr, dest, toscaDmaTypeToStr(dest), dest_addr,
        size, swap, timeout, fname=symbolName(callback,0), user), free(fname);
    r = toscaDmaRequestCreate();
    if (!r) return NULL;
    r->timeout = timeout;
    r->callback = callback;
    r->user = user;
    r->req.src_addr = source_addr;
    r->req.dst_addr = dest_addr;
#if __WORDSIZE > 32
    if (size >= 0x100000000ULL)
    {
        errno = EINVAL;
        debug("size too long (man 32 bit)");
        return  NULL;
    }
#endif
    r->req.size = size;
    r->req.cycle = 0;
   
    switch (swap)
    {
        case 2:
            r->req.dwidth = 0x400;
            break;
        case 4:
            r->req.dwidth = 0x800;
            break;
        case 8:
            r->req.dwidth = 0xc00;
            break;
    }
        
    switch (source)
    {
        case 0:
            r->req.src_type = VME_DMA_PCI;
            break;
        case TOSCA_USER:
            r->req.src_type = VME_DMA_USER;
            break;
        case TOSCA_SMEM:
            r->req.src_type = VME_DMA_SHM;
            break;
        case VME_SCT:
        case VME_BLT:
        case VME_MBLT:
        case VME_2eVME:
        case VME_2eVMEFast:
        case VME_2eSST160:
        case VME_2eSST267:
        case VME_2eSST320:
            r->req.src_type = VME_DMA_VME;
            r->req.cycle = source;
            r->req.aspace = VME_A32;
            break;
    }
    
    switch (dest)
    {
        case 0:
            r->req.dst_type = VME_DMA_PCI;
            switch (r->req.src_type)
            {
                case VME_DMA_USER:
                    r->req.route = VME_DMA_USER_TO_MEM;
                    break;
                case VME_DMA_SHM:
                    r->req.route = VME_DMA_SHM_TO_MEM;
                    break;
                case VME_DMA_VME:
                    r->req.route = VME_DMA_VME_TO_MEM;
                    break;
            }
            break;
        case TOSCA_USER:
            r->req.dst_type = VME_DMA_USER;
            switch (r->req.src_type)
            {
                case VME_DMA_PCI:
                    r->req.route = VME_DMA_MEM_TO_USER;
                    break;
                case VME_DMA_SHM:
                    r->req.route = VME_DMA_SHM_TO_USER;
                    break;
                case VME_DMA_VME:
                    r->req.route = VME_DMA_VME_TO_USER;
                    break;
            }
            break;
        case TOSCA_SMEM:
            r->req.dst_type = VME_DMA_SHM;
            switch (r->req.src_type)
            {
                case VME_DMA_PCI:
                    r->req.route = VME_DMA_MEM_TO_SHM;
                    break;
                case VME_DMA_USER:
                    r->req.route = VME_DMA_USER_TO_SHM;
                    break;
                case VME_DMA_VME:
                    r->req.route = VME_DMA_VME_TO_SHM;
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
            r->req.dst_type = VME_DMA_VME;
            r->req.cycle = dest;
            r->req.aspace = VME_A32;
            switch (r->req.src_type)
            {
                case VME_DMA_PCI:
                    r->req.route = VME_DMA_MEM_TO_VME;
                    break;
                case VME_DMA_USER:
                    r->req.route = VME_DMA_USER_TO_VME;
                    break;
                case VME_DMA_SHM:
                    r->req.route = VME_DMA_SHM_TO_VME;
                    break;
                case VME_DMA_VME:
                    if (source == dest) r->req.route = VME_DMA_VME_TO_VME;
                    break;
            }
            break;
    }
    if (!r->req.route)
    {
        errno = EINVAL;
        debugErrno("DMA route %s -> %s", toscaDmaTypeToStr(source), toscaDmaTypeToStr(dest));
        toscaDmaRelease(r);
        return NULL;
    }
    sprintf(filename, "/dev/dmaproxy%u", card);
    r->fd = open(filename, O_RDWR);
    if (r->fd < 0)
    {
        debugErrno("open %s", filename);
        toscaDmaRelease(r);
        return NULL;
    }
    debugLvl(2, "ioctl(%d, VME_DMA_SET, {%s 0x%llx->0x%llx [0x%x] dw=0x%x %s cy=0x%x=%s})",
        r->fd,
        toscaDmaRouteToStr(r->req.route),
        (unsigned long long) r->req.src_addr,
        (unsigned long long) r->req.dst_addr,
        r->req.size,
        r->req.dwidth,
        toscaDmaWidthToSwapStr(r->req.dwidth),
        r->req.cycle,
        toscaDmaTypeToStr(r->req.cycle));
    if (ioctl(r->fd, VME_DMA_SET, &r->req) != 0)
    {
        debugErrno("ioctl(%d, VME_DMA_SET, {%s 0x%llx->0x%llx [0x%x] dw=0x%x %s cy=0x%x=%s})",
            r->fd,
            toscaDmaRouteToStr(r->req.route),
            (unsigned long long) r->req.src_addr,
            (unsigned long long) r->req.dst_addr,
            r->req.size,
            r->req.dwidth,
            toscaDmaWidthToSwapStr(r->req.dwidth),
            r->req.cycle,
            toscaDmaTypeToStr(r->req.cycle));
        toscaDmaRelease(r);
        return NULL;
    }
    return r;
}

int toscaDmaTransfer(
    int source, size_t source_addr,
    int dest, size_t dest_addr,
    size_t size, int swap, int timeout,
    toscaDmaCallback callback, void* user)
{
    struct dmaRequest* r = toscaDmaSetup(source, source_addr, dest, dest_addr, size, swap, timeout, callback, user);
    if (!r) return errno;
    r->oneShot = 1;
    return toscaDmaExecute(r);
}
