#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <glob.h>

#include "symbolname.h"

#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#define open(path,flags) ({int _fd=open(path,(flags)&~O_CLOEXEC); if ((flags)&O_CLOEXEC) fcntl(_fd, F_SETFD, fcntl(_fd, F_GETFD)|FD_CLOEXEC); _fd; })
#define pipe2(fds,flags) ({int _st=pipe(fds); fcntl(fds[0], F_SETFD, fcntl(fds[0], F_GETFD) | FD_CLOEXEC); fcntl(fds[1], F_SETFD, fcntl(fds[1], F_GETFD)|FD_CLOEXEC); _st; })
#endif

#ifndef EPOLL_CLOEXEC
#define epoll_create1(F) ({int _fd=epoll_create(20); fcntl(_fd, F_SETFD, fcntl(_fd, F_GETFD) | FD_CLOEXEC); _fd; })
#endif

typedef uint64_t __u64;
typedef uint32_t __u32;
typedef uint8_t __u8;
#include "vme_user.h"
#include "toscaIntr.h"
#include "toscaMap.h"

pthread_mutex_t handlerlist_mutex = PTHREAD_MUTEX_INITIALIZER;
#define sLOCK pthread_mutex_lock(&handlerlist_mutex)
#define sUNLOCK pthread_mutex_unlock(&handlerlist_mutex)

#define LOCK
#define UNLOCK

#define TOSCA_DEBUG_NAME toscaIntr
#include "toscaDebug.h"

struct intr_handler {
    void (*function)();
    void *parameter;
    struct intr_handler* next;
};

static int epollfd = -1;
static int intrFd[TOSCA_NUM_INTR];
static unsigned long long totalIntrCount, intrCount[TOSCA_NUM_INTR];
static struct intr_handler* handlers[TOSCA_NUM_INTR];

#define TOSCA_USER_INTR(n)        TOSCA_USER1_INTR(n)
#define TOSCA_INTR_DEVICE(m)      (m>>24&&0xff)

#define TOSCA_INTR_INDX_USER(i)   (i)
#define TOSCA_INTR_INDX_VME(l,v)  (32+(((l)-1)*256)+(v))
#define TOSCA_INTR_INDX_ERR(i)    (32+7*256+(i))
#define TOSCA_INTR_INDX_SYSFAIL() TOSCA_INTR_INDX_ERR(0)
#define TOSCA_INTR_INDX_ACFAIL()  TOSCA_INTR_INDX_ERR(1)
#define TOSCA_INTR_INDX_ERROR()   TOSCA_INTR_INDX_ERR(2)

#define INTR_INDEX_TO_BIT(i)  ((i)<32?TOSCA_USER1_INTR(i):(i)>=TOSCA_INTR_INDX_ERR(0)?TOSCA_VME_FAIL((i)-TOSCA_INTR_INDX_ERR(0)):TOSCA_VME_INTR((((i)-32)>>8)+1))
#define INTR_INDEX_TO_INUM(i) ((i)<32?(i)&31:(i)>=TOSCA_INTR_INDX_ERR(0)?(i)-TOSCA_INTR_INDX_ERR(0):(((i)-32)>>8)+1)
#define INTR_INDEX_TO_IVEC(i) ((i)<32||(i)>=TOSCA_INTR_INDX_ERR(0)?0:((i)-32)&255)
#define toscaIntrIndexToStr(i) toscaIntrBitToStr(INTR_INDEX_TO_BIT(i))

#define IX(src,...) TOSCA_INTR_INDX_##src(__VA_ARGS__)
#define FOREACH_HANDLER(h, index) for(h = handlers[index]; h; h = h->next)

#define FOR_BITS_IN_MASK(first, last, index, maskbit, mask, action) \
    { unsigned int i; for (i=first; i <= last; i++) if (mask & maskbit) action(index, maskbit) }

#define FOREACH_MASKBIT(mask, action) \
{                                                                                       \
    /* handle VME_SYSFAIL, VME_ACFAIL, VME_ERROR */                                     \
    if ((mask) & TOSCA_VME_FAIL_ANY)                                                    \
        FOR_BITS_IN_MASK(0, 2, IX(ERR, i), TOSCA_VME_FAIL(i), (mask), action)           \
    /* handle VME_LVL */                                                                \
    if ((mask) & TOSCA_VME_INTR_ANY) {                                                  \
        unsigned int ivec = (mask)>>16&0xff, ivec2 = (mask)>>24&0xff;                   \
        do                                                                              \
            FOR_BITS_IN_MASK(1, 7, IX(VME, i, ivec), TOSCA_VME_INTR(i), (mask), action) \
        while (++ivec <= ivec2);                                                        \
    }                                                                                   \
    /* handle USER */                                                                   \
    if ((mask) & TOSCA_USER_INTR_ANY)                                                   \
        FOR_BITS_IN_MASK(0, 31, IX(USER, i), TOSCA_USER_INTR(i), (mask), action)        \
}

const char* toscaIntrBitToStr(intrmask_t intrmaskbit)
{
    switch(intrmaskbit & 0xffffffff0000ffffLL)
    {
        case TOSCA_VME_INTR(1):    return "VME-1";       
        case TOSCA_VME_INTR(2):    return "VME-2";       
        case TOSCA_VME_INTR(3):    return "VME-3";       
        case TOSCA_VME_INTR(4):    return "VME-4";       
        case TOSCA_VME_INTR(5):    return "VME-5";       
        case TOSCA_VME_INTR(6):    return "VME-6";       
        case TOSCA_VME_INTR(7):    return "VME-7";       
        case TOSCA_VME_SYSFAIL:    return "VME-SYSFAIL"; 
        case TOSCA_VME_ACFAIL:     return "VME-ACFAIL";  
        case TOSCA_VME_ERROR:      return "VME-ERROR";   
        case TOSCA_USER1_INTR(0):  return "USER1-0";     
        case TOSCA_USER1_INTR(1):  return "USER1-1";     
        case TOSCA_USER1_INTR(2):  return "USER1-2";     
        case TOSCA_USER1_INTR(3):  return "USER1-3";     
        case TOSCA_USER1_INTR(4):  return "USER1-4";     
        case TOSCA_USER1_INTR(5):  return "USER1-5";     
        case TOSCA_USER1_INTR(6):  return "USER1-6";     
        case TOSCA_USER1_INTR(7):  return "USER1-7";     
        case TOSCA_USER1_INTR(8):  return "USER1-8";     
        case TOSCA_USER1_INTR(9):  return "USER1-9";     
        case TOSCA_USER1_INTR(10): return "USER1-10";    
        case TOSCA_USER1_INTR(11): return "USER1-11";    
        case TOSCA_USER1_INTR(12): return "USER1-12";    
        case TOSCA_USER1_INTR(13): return "USER1-13";    
        case TOSCA_USER1_INTR(14): return "USER1-14";    
        case TOSCA_USER1_INTR(15): return "USER1-15";    
        case TOSCA_USER2_INTR(0):  return "USER2-0";     
        case TOSCA_USER2_INTR(1):  return "USER2-1";     
        case TOSCA_USER2_INTR(2):  return "USER2-2";     
        case TOSCA_USER2_INTR(3):  return "USER2-3";     
        case TOSCA_USER2_INTR(4):  return "USER2-4";     
        case TOSCA_USER2_INTR(5):  return "USER2-5";     
        case TOSCA_USER2_INTR(6):  return "USER2-6";     
        case TOSCA_USER2_INTR(7):  return "USER2-7";     
        case TOSCA_USER2_INTR(8):  return "USER2-8";     
        case TOSCA_USER2_INTR(9):  return "USER2-9";     
        case TOSCA_USER2_INTR(10): return "USER2-10";    
        case TOSCA_USER2_INTR(11): return "USER2-11";    
        case TOSCA_USER2_INTR(12): return "USER2-12";    
        case TOSCA_USER2_INTR(13): return "USER2-13";    
        case TOSCA_USER2_INTR(14): return "USER2-14";    
        case TOSCA_USER2_INTR(15): return "USER2-15";    
        default:                   return "unknown";     
    }  
}

static unsigned int toscaStrToRangeMask(unsigned int min, unsigned int max, const char* s, const char** end)
{
    unsigned long n;
    unsigned int mask = 0;
    const char* p;
    int range = -1;

    if (!s || !*s) return 0;
    while(*s) {
        n = strtoul(s, (char**)&p, 0);
        if (p == s) break;
        s = p;
        if (n < (unsigned long)min || n > (unsigned long)max)
        {
            error("%ld out of range %d-%d", n, min, max);
            mask = 0;
            break;
        }
        if (range != -1)
        {
            if (range > (int)n)
            {
                error("range %d-%ld backwards", range, n);
                mask = 0;
                break;
            }
            while (range < (int)n)
                mask |= 1 << range++;
        }
        mask |= 1 << n;
        if (*s == '-')
        {
            s++;
            range = n;
            continue;
        }
        if (*s != ',' && *s != ';') break;
        s++;
        range = -1;
    }
    if (end) *end = s;
    return mask;
}

intrmask_t toscaStrToIntrMask(const char* str)
{
    const char *s = str;
    intrmask_t mask = 0;
    unsigned int device = 0;

    if (!s || !s[0]) return 0;
    
    if (strchr(s, ':')) {
        const char* e;
        device = strtoul(s, (char**)&e, 0);
        if (e == s)
        {
            error("device number expected");
        }
        else if (device >= toscaNumDevices())
        {
            error("device=%u but only %u tosca devices found", device, toscaNumDevices());
        }
        else s = e+1;
    }
    
    if (strncasecmp(s, "TOSCA_", 6) == 0) s+=6;
    if ((strncasecmp(s, "USR", 3) == 0 && (s+=3)) ||
        (strncasecmp(s, "USER", 4) == 0 && (s+=4)))
    {
        if (*s == '*')
        {
            s++;
            if (*s == 0) return TOSCA_DEV_USER_INTR_ANY(device);
        }
        else if (*s == '2')
        {
            s++;
            if (*s == 0) return TOSCA_DEV_USER2_INTR_ANY(device);
            if (*s == '-')
            {
                mask = toscaStrToRangeMask(0, 15, s+1, &s);
                if (mask && *s == 0) return TOSCA_DEV_USER2_INTR_MASK(device, mask);
            }
        }
        else
        {
            if (*s == '1') s++;
            if (*s == 0) return TOSCA_DEV_USER1_INTR_ANY(device);
            if (*s == '-')
            {
                mask = toscaStrToRangeMask(0, 15, s+1, &s);
                if (mask && *s == 0) return TOSCA_DEV_USER1_INTR_MASK(device, mask);
            }
        }
    }
    else
    if ((strncasecmp(s, "VME", 3) == 0 && (s+=3)))
    {
        if (device != 0)
        {
            error("VME interrupts only possible on device 0");
        }
        else
        {
            if (*s == '-')
            {
                if (strcasecmp(s+1, "SYSFAIL") == 0)
                    return TOSCA_VME_SYSFAIL;
                if (strcasecmp(s+1, "ACFAIL") == 0)
                    return TOSCA_VME_ACFAIL;
                if (strcasecmp(s+1, "ERROR") == 0)
                    return TOSCA_VME_ERROR;
                if (strcasecmp(s+1, "FAIL") == 0)
                    return TOSCA_VME_FAIL_ANY;

                mask = toscaStrToRangeMask(1, 7, s+1, &s);
            }
            else
            {
                mask = TOSCA_VME_INTR_ANY;
            }
            if (*s == '.')
            {
                const char* e;
                unsigned long vec = strtoul(s+1, (char**)&e, 0);
                if (e == s+1)
                {
                    error("vector number expected");
                }
                else if (vec > 255)
                {
                    error("vector %lu out of range 0-255", vec);
                }
                else
                {
                    s = e;
                    if (*s == '-')
                    {
                        unsigned long vec2 = strtoul(s+1, (char**)&e, 0);
                        if (e == s+1)
                        {
                            error("vector number expected");
                        }
                        else if (vec2 > 255)
                        {
                            error("vector %lu out of range 0-255", vec2);
                        }
                        else
                        {
                            if (vec < (uint8_t)(mask >> 16))
                            {
                                error("range %ld-%ld backwards", vec, vec2);
                            }
                            else
                            {
                                s = e;
                                mask = TOSCA_VME_INTR_MASK_VECS(mask, vec, vec2);
                            }
                        }
                    }
                    else
                    {
                        mask = TOSCA_VME_INTR_MASK_VEC(mask,vec);
                    }
                }
            }
            else
            {
                /* all vectors */
                mask |= 255UL << 24;
            }
        }
    }
    else
    {
        mask = strtoull(s, (char**)&s, 0);
    }
    if (*s == 0) return mask;
    error("invalid mask string: \"%s\"", s);
    errno = EINVAL;
    return 0;
}

int toscaIntrMonitorFile(int index, const char* filepattern, ...)
{
    char* filename = NULL;
    struct epoll_event ev;
    va_list ap;
    glob_t globresults;

    if (intrFd[index] > 0) return 0;
    va_start(ap, filepattern);
    vasprintf(&filename, filepattern, ap);
    va_end(ap);
    if (!filename)
    {
        debugErrno("%s vasprintf %s", toscaIntrIndexToStr(index), filepattern);
        return -1;
    }
    debug("%s glob(%s)", toscaIntrIndexToStr(index), filename);
    if (glob(filename, 0, NULL, &globresults) != 0)
    {
        error("cannot find %s", filename);
        free(filename);
        errno = ENOENT;
        return -1;
    }
    free(filename);
    intrFd[index] = open(globresults.gl_pathv[0], O_RDWR|O_CLOEXEC);
    debug ("%s open %s intrFd[%d]=%d", toscaIntrIndexToStr(index), globresults.gl_pathv[0], index, intrFd[index]);
    if (intrFd[index] < 0)
    {
        debugErrno("%s open %s", toscaIntrIndexToStr(index), globresults.gl_pathv[0]);
        globfree(&globresults);
        return -1;
    } 
    ev.events = EPOLLIN;
    ev.data.u32 = index;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, intrFd[index], &ev) < 0)
    {
        debugErrno("epoll_ctl ADD %d %s", intrFd[index], globresults.gl_pathv[0]);
    }
    globfree(&globresults);
    return 0;
}

int toscaIntrConnectHandler(intrmask_t intrmask, void (*function)(), void* parameter)
{
    char* fname;
    unsigned int i;
    int status = 0;

    debug("intrmask=0x%016"PRIx64" function=%s, parameter=%p",
        intrmask, fname=symbolName(function,0), parameter), free(fname);
        
    if (!function)
    {
        debug("handler function is NULL");
        errno = EINVAL;
        return -1;
    }

    LOCK; /* only need to lock installation, not calling of handlers */

    /* We should handle device numbers. Skipped for now because there is always only 1 VME and 1 USER device */
    /* Older tosca driver for IFC1210 has only 1 device, newer driver has device number in file names. */
    /* Using * in the fine name patterns makes it easier to handle both cases assuming only 1 device of each type. */
    if (intrmask & TOSCA_USER_INTR_ANY)
    {
        for (i = 0; i < 32; i++)
        {
            if (intrmask & TOSCA_USER_INTR(i))
            {
                if (toscaIntrMonitorFile(IX(USER, i), "/dev/toscauserevent*%u.%u", i > 15 ? 2 : 1, i & 15) != 0)
                {
                    intrmask &= ~TOSCA_USER_INTR(i);
                    status = -1;
                }
            }
        }
    }
    if (intrmask & TOSCA_VME_INTR_ANY)
    {
        for (i = 1; i <= 7; i++) if (intrmask & TOSCA_VME_INTR(i))
        {
            unsigned int ivec = (intrmask >> 16) & 0xff, ivec2 = (intrmask >> 24) & 0xff;
            unsigned int connected = 0;
            do {
                if (toscaIntrMonitorFile(IX(VME, i, ivec), "/dev/toscavmeevent*%u.%u", i, ivec) == 0)
                    connected = 1;
            } while (++ivec <= ivec2);
            if (!connected)
            {
                intrmask &= ~TOSCA_VME_INTR(i);
                status = -1;
            }
        }
    }
    if (intrmask & TOSCA_VME_SYSFAIL)
        if (toscaIntrMonitorFile(IX(SYSFAIL), "/dev/toscavmesysfail*") != 0)
        {
            intrmask &= ~TOSCA_VME_SYSFAIL;
            status = -1;
        }
    if (intrmask & TOSCA_VME_ACFAIL)
        if (toscaIntrMonitorFile(IX(ACFAIL), "/dev/toscavmeacfail*") != 0)
        {
            intrmask &= ~TOSCA_VME_ACFAIL;
            status = -1;
        }
    if (intrmask & TOSCA_VME_ERROR)
        if (toscaIntrMonitorFile(IX(ERROR), "/dev/toscavmeerror*") != 0)
        {
            intrmask &= ~TOSCA_VME_ERROR;
            status = -1;
        }

    #define INSTALL_HANDLER(i, bit)                                                  \
    {                                                                                \
        struct intr_handler** phandler, *handler;                                    \
        if (!(handler = calloc(1,sizeof(struct intr_handler)))) {                    \
            debugErrno("calloc"); status = -1; break; }                              \
        handler->function = function;                                                \
        handler->parameter = parameter;                                              \
        handler->next = NULL;                                                        \
        for (phandler = &handlers[i]; *phandler; phandler = &(*phandler)->next);     \
        *phandler = handler;                                                         \
        debug("%s ivec=%d: %s(%p)",                                                  \
            toscaIntrBitToStr(bit), INTR_INDEX_TO_IVEC(i),                           \
            fname=symbolName(handler->function,0), handler->parameter), free(fname); \
    }
    
    FOREACH_MASKBIT(intrmask, INSTALL_HANDLER);
    UNLOCK;
    return status;
}

int toscaIntrDisconnectHandler(intrmask_t intrmask, void (*function)(), void* parameter)
{
    int n = 0;
    #define REMOVE_HANDLER(i, bit)                                 \
    {                                                              \
        struct intr_handler** phandler, *handler;                  \
        phandler = &handlers[i];                                   \
        while (*phandler) {                                        \
            handler = *phandler;                                   \
            if (handler->function == function &&                   \
                (!parameter || parameter == handler->parameter)) { \
                *phandler = handler->next;                         \
                n++;                                               \
                continue;                                          \
            }                                                      \
            phandler = &handler->next;                             \
        }                                                          \
    }
    LOCK; /* do not free handler or we need to lock calling of handlers too */
    FOREACH_MASKBIT(intrmask, REMOVE_HANDLER);
    UNLOCK;
    return n;
}

int toscaIntrDisable(intrmask_t intrmask)
{
    struct epoll_event ev;
    
    debug("intrmask=0x%016"PRIx64"", intrmask);
    ev.events = 0;
    ev.data.u32 = 0;
    #define DISABLE_INTR(i, bit)                                       \
    {                                                                  \
        if (intrFd[i] > 0) {                                           \
            debug("disable %s ivec=%u intrFd[%u]=%d",                  \
                toscaIntrIndexToStr(i),                                \
                INTR_INDEX_TO_IVEC(i),                                 \
                i, intrFd[i]);                                         \
            if (epoll_ctl(epollfd, EPOLL_CTL_MOD, intrFd[i], &ev) < 0) \
                debugErrno("epoll_ctl MOD %d", intrFd[i]);             \
        }                                                              \
    }
    FOREACH_MASKBIT(intrmask, DISABLE_INTR);
    return 0;
}

int toscaIntrEnable(intrmask_t intrmask)
{
    struct epoll_event ev;

    debug("intrmask=0x%016"PRIx64"", intrmask);
    ev.events = EPOLLIN;
    #define ENABLE_INTR(i, bit)                                        \
    {                                                                  \
        if (intrFd[i] > 0) {                                           \
            debug("enable %s ivec=%u intrFd[%u]=%d",                   \
                toscaIntrIndexToStr(i),                                \
                INTR_INDEX_TO_IVEC(i),                                 \
                i, intrFd[i]);                                         \
            ev.data.u32 = i;                                           \
            if (epoll_ctl(epollfd, EPOLL_CTL_MOD, intrFd[i], &ev) < 0) \
                debugErrno("epoll_ctl MOD %d", intrFd[i]);             \
        }                                                              \
    }
    FOREACH_MASKBIT(intrmask, ENABLE_INTR);
    return 0;
}

size_t toscaIntrForeachHandler(size_t (*callback)(toscaIntrHandlerInfo_t, void* user), void* user)
{
    #define REPORT_HANDLER(i, bit)                     \
    {                                                  \
        struct intr_handler* handler;                  \
        int status;                                    \
        FOREACH_HANDLER(handler, i) {                  \
            status = callback((toscaIntrHandlerInfo_t) \
                { .intrmaskbit = bit,                  \
                  .index = i,                          \
                  .vec = INTR_INDEX_TO_IVEC(i),        \
                  .function = handler->function,       \
                  .parameter = handler->parameter,     \
                  .count = intrCount[i] }, user);      \
            if (status != 0) return status;            \
        }                                              \
    }
    FOREACH_MASKBIT(TOSCA_INTR_ANY, REPORT_HANDLER);
    return 0;
}

unsigned long long toscaIntrCount()
{
    return totalIntrCount;
}

static int toscaIntrLoopRunning = 0;
static int intrLoopStopEvent[2];

void toscaIntrLoop(void* dummy __attribute__((unused)))
{
    unsigned int i, n, index, inum, ivec;
    
    /* handle up to 64 simultanious interrupts in one system call */
    #define MAX_EVENTS 64
    struct epoll_event events[MAX_EVENTS];
    
    if (toscaIntrLoopRunning)
    {
        debug("interrupt loop already running");
        return;
    }
    toscaIntrLoopRunning = 1;

    debug("starting interrupt handling");
    pipe2(intrLoopStopEvent, O_NONBLOCK|O_CLOEXEC);
    
    events[0].events = EPOLLIN;
    events[0].data.u32 = (uint32_t)-1;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, intrLoopStopEvent[0], &events[0]) < 0)
        debugErrno("epoll_ctl ADD %d", intrLoopStopEvent[0]);
    
    while (toscaIntrLoopRunning)
    {
        debugLvl(2,"waiting for interrupts");
        n = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (n < 1)
        {
            if (errno == EINTR) continue;
            error("epoll_wait");
            break;
        }
        for (i = 0; i < n; i++)
        {
            struct intr_handler* handler;

            index = events[i].data.u32;
            if (index == (uint32_t)-1)
            {
                /* got intrLoopStopEvent */
                close(intrLoopStopEvent[0]);
                close(intrLoopStopEvent[1]);
                toscaIntrLoopRunning = 0;
                break;
            }
            inum = INTR_INDEX_TO_INUM(index);
            ivec = INTR_INDEX_TO_IVEC(index);
            totalIntrCount++;
            intrCount[index]++;
            debugLvl(2, "interrupt %llu index=%u inum=%u ivec=%u", totalIntrCount, index, inum, ivec);
            FOREACH_HANDLER(handler, index) {
                char* fname;
                debugLvl(2, "index=%u fd=%d %s, #%llu %s(%p, %u, %u)",
                    index,
                    intrFd[index],
                    toscaIntrBitToStr(INTR_INDEX_TO_BIT(index)),
                    intrCount[index],
                    fname=symbolName(handler->function,0),
                    handler->parameter, inum, ivec),
                    free(fname);
                handler->function(handler->parameter, inum, ivec);
            }
            if (index >= TOSCA_INTR_INDX_VME(1,0) && index < TOSCA_INTR_INDX_ERR(0))
                write(intrFd[index], NULL, 0);  /* re-enable VME interrupt */
        }
    }
    debug("interrupt handling ended");
}

int toscaIntrLoopIsRunning(void)
{
    return (toscaIntrLoopRunning);
}

void toscaIntrInit () __attribute__((constructor));
void toscaIntrInit ()
{
    epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd < 0)
        debugErrno("epoll_create");
}

void toscaIntrLoopStop()
{
    char e = 1;
    if (!toscaIntrLoopRunning) return;
    write(intrLoopStopEvent[1], &e, 1);
    while (toscaIntrLoopRunning) usleep(10);
}

int toscaSendVMEIntr(unsigned int level, unsigned int ivec)
{
    if (level < 1 || level > 7)
    {
        errno = EINVAL;
        debug("invalid VME interrupt level %u", level);
        return -1;
    }
    if (ivec > 255)
    {
        errno = EINVAL;
        debug("invalid VME interrupt vector %u", ivec);
        return -1;
    }
/*
    return toscaCsrWrite(0x40c, 0x1000+(level<<8)+ivec);

*/
    const char* filename;
    static int fd = -1;
    struct vme_irq_id {
        __u8 level;
        __u8 ivec;
    } irq;
    
    
    if (fd < 0)
    {
        filename = "/dev/bus/vme/ctl0";
        
        fd = open(filename, O_RDWR|O_CLOEXEC);
        if (fd < 0)
        {
            filename = "/dev/bus/vme/ctl";
            fd = open(filename, O_RDWR|O_CLOEXEC);       
        }
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            return -1;
        }
    }
    irq.level = level;
    irq.ivec = ivec;
    if (ioctl(fd, VME_IRQ_GEN, &irq) != 0)
    {
        debugErrno("ioctl(%d, VME_IRQ_GEN, {level=%u, vec=%u})", fd, irq.level, irq.ivec);
        return -1;
    }
    return 0;
}

void toscaSpuriousVMEInterruptHandler(void* param __attribute__((unused)), unsigned int inum, unsigned int ivec)
{
    fprintf(stderr, "Spurious VME interrupt level %u vector %u\n", inum, ivec);
}

void toscaInstallSpuriousVMEInterruptHandler(void)
{
    static int first = 1;
    if (!first) return;
    first = 0;
    if (toscaIntrConnectHandler(TOSCA_VME_INTR_ANY_VEC(255), toscaSpuriousVMEInterruptHandler, NULL) != 0)
        error("%m");
}
