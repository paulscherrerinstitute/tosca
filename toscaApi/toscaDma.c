#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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
#include <time.h>

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#include "symbolname.h"
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
        case VME_DMA_MEM_TO_USER1:    /* works */
            return "MEM->USER";
        case VME_DMA_USER1_TO_MEM:    /* works */
            return "USER->MEM";
        case VME_DMA_VME_TO_USER1:    /* copies, but ignores swap 0x400 0x800 0xc00 */
            return "VME->USER";
        case VME_DMA_USER1_TO_VME:    /* copies, but ignores swap 0x400 0x800 0xc00 */
            return "USER->VME";
        case VME_DMA_VME_TO_SHM1:     /* works */
            return "VME->SHM";
        case VME_DMA_SHM1_TO_VME:     /* works */
            return "SHM->VME";
        case VME_DMA_MEM_TO_SHM1:     /* copies, but ignores swap 0x400 0x800 0xc00 */
            return "MEM->SHM";
        case VME_DMA_SHM1_TO_MEM:     /* works */
            return "SHM->MEM";
        case VME_DMA_USER1_TO_SHM1:   /* works */
            return "USER->SHM";
        case VME_DMA_SHM1_TO_USER1:   /* works */
            return "SHM->USER";
        case 0:
            return "none";
        default:
            return "unknown";
    }
}

const char* toscaDmaSpaceToStr(unsigned int dmaspace)
{
    switch (dmaspace & 0xffff)
    {
        case 0:
            return "MEM";
        case TOSCA_USER1:
            return "USER1";
        case TOSCA_USER2:
            return "USER2";
        case TOSCA_SMEM1:
            return "SMEM1";
        case TOSCA_SMEM2:
            return "SMEM2";
        case VME_SCT:
            return "VME_SCT";
        case VME_BLT:
            return "BLT";
        case VME_MBLT:
            return "MBLT";
        case VME_2eVME:
            return "2eVME";
        case VME_2eSST160:
            return "2eSST160";
        case VME_2eSST267:
            return "2eSST267";
        case VME_2eSST320:
            return "2eSST320";
        default:
            return "????";
    }
}

static const char* toscaDmaTypeToStr(unsigned int type)
{
    switch (type)
    {
        case VME_DMA_PATTERN:
            return "VME_DMA_PATTERN";
        case VME_DMA_PCI:
            return "VME_DMA_PCI";
        case VME_DMA_VME:
            return "VME_DMA_VME";
        case VME_DMA_SHM1:
            return "VME_DMA_SHM1";
        case VME_DMA_SHM2:
            return "VME_DMA_SHM2";
        case VME_DMA_USER1:
            return "VME_DMA_USER1";
        case VME_DMA_USER2:
            return "VME_DMA_USER2";
        default:
            return "????";
    }
}

const char* toscaDmaWidthToSwapStr(int width)
{
    return (const char*[]){"NS","WS","DS","QS"}[(width>>10)&3];
}

int toscaStrToDmaSpace(const char* str, const char** end)
{
    unsigned int dmaspace = -1;
    unsigned long device = 0;
    const char *s;
    
    if (!str || !*str)
    {
fault:
        errno = EINVAL;
        if (end) *end = str;
        return -1;
    }
    
    device = strtoul(str, (char**)&s, 0);
    if (*s == ':')
        s++;
    else
    {
        device = 0;
        s = str;
    }
    if ((strncasecmp(s, "MEM", 3) == 0 && (s+=3)))
        dmaspace = 0;
    else
    {
        if (strncasecmp(s, "TOSCA_", 6) == 0) s+=6;
        if ((strncasecmp(s, "USR", 3) == 0 && (s+=3)) ||
            (strncasecmp(s, "USER", 4) == 0 && (s+=4)))
        {
            if (*s == '2')
            {
                dmaspace = TOSCA_USER2;
                s++;
            }
            else
            {
                dmaspace = TOSCA_USER1;
                if (*s == '1') s++;
            }
        }
        else
        if ((strncasecmp(s, "SH_MEM", 6) == 0 && (s+=6)) ||
            (strncasecmp(s, "SHMEM", 5) == 0 && (s+=5)) ||
            (strncasecmp(s, "SMEM", 4) == 0 && (s+=4)) ||
            (strncasecmp(s, "SHM", 3) == 0 && (s+=3)))
        {
            if (*s == '2')
            {
                dmaspace = TOSCA_SMEM2;
                s++;
            }
            else
            {
                dmaspace = TOSCA_SMEM1;
                if (*s == '1') s++;
            }
        }
        else
        if (((strncasecmp(s, "VME_A32", 7) == 0) && (s+=7)) ||
            ((strncasecmp(s, "A32", 3) == 0) && (s+=3)) ||
            ((strncasecmp(s, "VME", 3) == 0) && (s+=3)) ||
            ((strncasecmp(s, "SCT", 3) == 0) && (s+=3)))
             dmaspace = VME_SCT;
        else
        if ((strncasecmp(s, "BLT", 3) == 0 && (s+=3)))
            dmaspace = VME_BLT;
        else
        if ((strncasecmp(s, "MBLT", 4) == 0 && (s+=4)))
            dmaspace = VME_MBLT;
        else
        if ((strncasecmp(s, "2eVME", 5) == 0 && (s+=5)))
            dmaspace = VME_2eVME;
        else
        if ((strncasecmp(s, "2eSST160", 8) == 0 && (s+=8)))
            dmaspace = VME_2eSST160;
        else
        if ((strncasecmp(s, "2eSST267", 8) == 0 && (s+=8)))
            dmaspace = VME_2eSST267;
        else
        if ((strncasecmp(s, "2eSST320", 8) == 0 && (s+=8)) ||
            (strncasecmp(s, "2eSST", 5) == 0 && (s+=5)))
            dmaspace = VME_2eSST320;
        else goto fault;
    }
    if (*s != 0 && *s !=':')
        goto fault;
    if (*s == ':') s++;
    if (end) *end = s;
    return device << 16 | dmaspace;
}

/* backward compatibility only */
int toscaDmaStrToSpace(const char* str)
{
    return toscaStrToDmaSpace(str, NULL);
}

struct dmaRequest
{
    struct dma_request req;
    int fd;
    int source;
    int dest;
    long timeout;
    int oneShot;
    toscaDmaCallback callback;
    void *user;
    struct dmaRequest* next;
} *pending, *freelist, **insert=&pending;

int toscaDmaDoTransfer(struct dmaRequest* r)
{
    struct dma_execute ex = {0,0};
    struct timespec start, finished;
 
#ifdef VME_DMA_TIMEOUT
    r->timeout = 1000;
    debugLvl(2, "ioctl(%d, VME_DMA_TIMEOUT, %ld ms)", r->fd, r->timeout);
    if (ioctl(r->fd, VME_DMA_TIMEOUT, &r->timeout) != 0)
    {
        debugErrno("ioctl(%d, VME_DMA_TIMEOUT, %ld ms)", r->fd, r->timeout);
        /* ignore and do dma anyway */
        errno = 0;
    }
#endif
    if (toscaDmaDebug)
        clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    debugLvl(2, "ioctl(%d, VME_DMA_EXECUTE)",
        r->fd);
    if (ioctl(r->fd, VME_DMA_EXECUTE, &ex) != 0)
    {
        debugErrno("ioctl (%d, VME_DMA_EXECUTE, {%s 0x%"PRIx64"->0x%"PRIx64" [0x%x] dw=0x%x %s cy=0x%x=%s})",
            r->fd,
            toscaDmaRouteToStr(r->req.route),
            r->req.src_addr,
            r->req.dst_addr,
            r->req.size,
            r->req.dwidth,
            toscaDmaWidthToSwapStr(r->req.dwidth),
            r->req.cycle,
            toscaDmaSpaceToStr(r->req.cycle));
        if (r->oneShot) toscaDmaRelease(r);
        return errno;
    }
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
        debug("%s:0x%"PRIx64"->%s:0x%"PRIx64" %d %sB / %.3f msec (%.1f MiB/s = %.1f MB/s)",
            toscaDmaSpaceToStr(r->source), r->req.src_addr,
            toscaDmaSpaceToStr(r->dest), r->req.dst_addr,
            r->req.size >= 0x00100000 ? (r->req.size >> 20) : r->req.size >= 0x00000400 ? (r->req.size >> 10) : r->req.size,
            r->req.size >= 0x00100000 ? "Mi" : r->req.size >= 0x00000400 ? "Ki" : "",
            sec * 1000, r->req.size/sec/0x00100000, r->req.size/sec/1000000);
    }
    if (r->oneShot) toscaDmaRelease(r);
    return 0;
}

static int loopsRunning = 0;
static int stopLoops = 0;

void toscaDmaLoop(void* dummy __attribute__((unused)))
{
    struct dmaRequest* r;
    int status;
    toscaDmaCallback callback;
    void* user;
    int loopnumber;
    
    LOCK;
    loopnumber = loopsRunning++;
    debug("DMA loop %d started", loopnumber);

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
        if (stopLoops) break;
    }
    debug("DMA loop %d stopped", loopnumber);
    loopsRunning--;
    UNLOCK;
}

int toscaDmaLoopsRunning(void)
{
    return loopsRunning;
}

void toscaDmaLoopsStop()
{
    stopLoops = 1;
    debug("stopping DMA loops");
    while (loopsRunning)
    {
        WAKEUP;
        usleep(10);
    }
    debug("DMA loops stopped");
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
    memset(r, 0, sizeof(struct dmaRequest));
    return r;
}

void toscaDmaRelease(struct dmaRequest* r)
{
    if (!r) return;
    LOCK;
    if (r->fd > 0) close(r->fd);
    if (!r->next)
    {
        debugLvl(3, "put back request %p to freelist, freelist = %p", r, freelist);
        r->next = freelist;
        freelist = r;
    }
    UNLOCK;
}

struct dmaRequest* toscaDmaSetup(unsigned int source, uint64_t source_addr, unsigned int dest, uint64_t dest_addr,
    size_t size, unsigned int swap, int timeout,
    toscaDmaCallback callback, void* user)
{
    struct dmaRequest* r;
    char* fname;
    char filename[20];
    unsigned int sdev = source >> 16;
    unsigned int ddev = dest >> 16;
    
    debugLvl(2, "%d:%s(0x%x):0x%"PRIx64"->%d:%s(0x%x):0x%"PRIx64"[0x%zx] swap=%d tout=%d cb=%s(%p)",
        sdev, toscaDmaSpaceToStr(source), source, source_addr,
        ddev, toscaDmaSpaceToStr(dest), dest, dest_addr,
        size, swap, timeout, fname=symbolName(callback,0), user), free(fname);
    r = toscaDmaRequestCreate();
    if (!r) return NULL;
    r->timeout = timeout;
    r->source = source,
    r->dest = dest;
    r->callback = callback;
    r->user = user;
    r->req.src_addr = source_addr;
    r->req.dst_addr = dest_addr;
#if __WORDSIZE > 32
    if (size >= 0x100000000ULL)
    {
        errno = EINVAL;
        error("size too long (max 32 bit)");
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
        case 0:
            r->req.dwidth = 0;
            break;
        default:
            error("invalid swap = %u, using 0", swap);
            r->req.dwidth = 0;
    }
        
    switch (source & 0xffff)
    {
        case 0:
            r->req.src_type = VME_DMA_PCI;
            break;
        case TOSCA_USER:
            r->req.src_type = VME_DMA_USER1;
            break;
        case TOSCA_USER2:
            r->req.src_type = VME_DMA_USER2;
            break;
        case TOSCA_SMEM1:
            r->req.src_type = VME_DMA_SHM1;
            break;
        case TOSCA_SMEM2:
            r->req.src_type = VME_DMA_SHM2;
            break;
        case VME_SCT:
        case VME_BLT:
        case VME_MBLT:
        case VME_2eVME:
        case VME_2eSST160:
        case VME_2eSST267:
        case VME_2eSST320:
            r->req.src_type = VME_DMA_VME;
            r->req.cycle = source & 0xffff;
            r->req.aspace = VME_A32;
            break;
    }
    
/* Old driver for ifc1210 needs route, newer driver does not need it */
/* SHM2 and USER2 are only available in the new driver */
    switch (dest & 0xffff)
    {
        case 0:
            r->req.dst_type = VME_DMA_PCI;
            switch (r->req.src_type)
            {
                case VME_DMA_USER1:
                    r->req.route = VME_DMA_USER1_TO_MEM;
                    break;
                case VME_DMA_SHM1:
                    r->req.route = VME_DMA_SHM1_TO_MEM;
                    break;
                case VME_DMA_VME:
                    r->req.route = VME_DMA_VME_TO_MEM;
                    break;
            }
            break;
        case TOSCA_USER1:
            r->req.dst_type = VME_DMA_USER1;
            switch (r->req.src_type)
            {
                case VME_DMA_PCI:
                    r->req.route = VME_DMA_MEM_TO_USER1;
                    break;
                case VME_DMA_SHM1:
                    r->req.route = VME_DMA_SHM1_TO_USER1;
                    break;
                case VME_DMA_VME:
                    r->req.route = VME_DMA_VME_TO_USER1;
                    break;
            }
            break;
        case TOSCA_USER2:
            r->req.dst_type = VME_DMA_USER2;
            break;
        case TOSCA_SMEM1:
            r->req.dst_type = VME_DMA_SHM1;
            switch (r->req.src_type)
            {
                case VME_DMA_PCI:
                    r->req.route = VME_DMA_MEM_TO_SHM1;
                    break;
                case VME_DMA_USER1:
                    r->req.route = VME_DMA_USER1_TO_SHM1;
                    break;
                case VME_DMA_VME:
                    r->req.route = VME_DMA_VME_TO_SHM1;
                    break;
            }
            break;
        case TOSCA_SMEM2:
            r->req.dst_type = VME_DMA_SHM2;
            break;
        case VME_SCT:
        case VME_BLT:
        case VME_MBLT:
        case VME_2eVME:
        case VME_2eSST160:
        case VME_2eSST267:
        case VME_2eSST320:
            r->req.dst_type = VME_DMA_VME;
            r->req.cycle = dest & 0xffff;
            r->req.aspace = VME_A32;
            switch (r->req.src_type)
            {
                case VME_DMA_PCI:
                    r->req.route = VME_DMA_MEM_TO_VME;
                    break;
                case VME_DMA_USER1:
                    r->req.route = VME_DMA_USER1_TO_VME;
                    break;
                case VME_DMA_SHM1:
                    r->req.route = VME_DMA_SHM1_TO_VME;
                    break;
                case VME_DMA_VME:
                    if (source == dest) r->req.route = VME_DMA_VME_TO_VME;
                    break;
            }
            break;
    }
    if (!r->req.src_type || !r->req.dst_type)
    {
        errno = EINVAL;
        debugErrno("DMA route %s -> %s", toscaDmaSpaceToStr(source), toscaDmaSpaceToStr(dest));
        toscaDmaRelease(r);
        return NULL;
    }
    sprintf(filename, "/dev/dmaproxy%u", ddev > sdev ? ddev : sdev);
    r->fd = open(filename, O_RDWR|O_CLOEXEC);
    if (r->fd < 0)
    {
        debugErrno("open %s", filename);
        toscaDmaRelease(r);
        return NULL;
    }
    debugLvl(2, "ioctl(%d (%s), VME_DMA_SET, {route=%s(0x%x) src_type=%s(0x%02x) src_addr=0x%"PRIx64" dst_type=%s(0x%02x) dst_addr=0x%"PRIx64" size=0x%x dwidth=0x%x(%s) cycle=0x%x=%s})",
        r->fd, filename,
        toscaDmaRouteToStr(r->req.route), r->req.route,
        toscaDmaTypeToStr(r->req.src_type), r->req.src_type, 
        r->req.src_addr,
        toscaDmaTypeToStr(r->req.dst_type), r->req.dst_type,
        r->req.dst_addr,
        r->req.size,
        r->req.dwidth,
        toscaDmaWidthToSwapStr(r->req.dwidth),
        r->req.cycle,
        toscaDmaSpaceToStr(r->req.cycle));
    if (ioctl(r->fd, VME_DMA_SET, &r->req) != 0)
    {
        toscaDmaRelease(r);
        return NULL;
    }
    return r;
}

int toscaDmaTransfer(
    unsigned int source, uint64_t source_addr,
    unsigned int dest, uint64_t dest_addr,
    size_t size, unsigned int swap, int timeout,
    toscaDmaCallback callback, void* user)
{
    struct dmaRequest* r = toscaDmaSetup(source, source_addr, dest, dest_addr, size, swap, timeout, callback, user);
    if (!r) return errno;
    r->oneShot = 1;
    return toscaDmaExecute(r);
}
