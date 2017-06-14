#define _GNU_SOURCE
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>

#include <symbolname.h>

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
static int intrLoopStopEvent = -1;

#define TOSCA_INTR_INDX_USER(i)   (i)                            
#define TOSCA_INTR_INDX_USER1(i)  TOSCA_INTR_INDX_USER(i)
#define TOSCA_INTR_INDX_USER2(i)  TOSCA_INTR_INDX_USER((i)+16)
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
    if ((mask) & (TOSCA_USER1_INTR_ANY | TOSCA_USER2_INTR_ANY))                         \
        FOR_BITS_IN_MASK(0, 31, IX(USER, i), TOSCA_USER1_INTR(i), (mask), action)       \
}

const char* toscaIntrBitToStr(intrmask_t intrmaskbit)
{
    switch(intrmaskbit & 0xffffffff0000ffff)
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

intrmask_t toscaIntrStrToBit(const char* str)
{
    char *s = (char*) str;
    intrmask_t mask;
    long n = 0, v;
    if (!s || !s[0]) return 0;
    
    if ((strncasecmp(s, "USR", 3) == 0 && (s+=3)) ||
        (strncasecmp(s, "USER", 4) == 0 && (s+=4)) ||
        (strncasecmp(s, "TOSCA_USER", 10) == 0 && (s+=10)))
    {
        if (*s == '2')
        {
            s++;
            if (*s == 0) return TOSCA_USER2_INTR_ANY;
            if (*s == '-')
            {
                n += strtoul(s, &s, 0);
                if (n < 16) return TOSCA_USER2_INTR(n);
            }
        }
        else
        {
            if (*s == '1') s++;
            if (*s == 0) return TOSCA_USER1_INTR_ANY;
            if (*s == '-')
            {
                n += strtoul(s, &s, 0);
                if (n < 16) return TOSCA_USER1_INTR(n);
            }
        }
    }
    else
    if ((strncasecmp(s, "VME", 3) == 0 && (s+=3)) ||
        (strncasecmp(s, "TOSCA_VME", 9) == 0 && (s+=9)))
    {
        if (*s == '-')
        {
            s++;
            if (strcasecmp(s, "SYSFAIL") == 0)
                return TOSCA_VME_SYSFAIL;
            if (strcasecmp(s, "ACFAIL") == 0)
                return TOSCA_VME_ACFAIL;
            if (strcasecmp(s, "ERROR") == 0)
                return TOSCA_VME_ERROR;
            if (strcasecmp(s, "FAIL") == 0)
                return TOSCA_VME_FAIL_ANY;
            
            n = strtoul(s, &s, 0);
            if (n >= 1 && n <= 7 && *s++ == '.')
            {
                v = strtoul(s, &s, 0);
                return TOSCA_VME_INTR_VEC(n, v);
            }
        }
        if (*s++ == '.')
        {
            v = strtoul(s, &s, 0);
            return TOSCA_VME_INTR_ANY_VEC(v);
        }
    }
    else
    {
        mask = strtoull(s, &s, 0);
        if (*s == 0) return mask;
    }
    debug("invalid interrupt string");
    errno = EINVAL;
    return 0;
}

int toscaIntrMonitorFile(int index, const char* filename)
{
    struct epoll_event ev;

    intrFd[index] = open(filename, O_RDWR|O_CLOEXEC);
    debug ("%s open %s intrFd[%d]=%d", toscaIntrIndexToStr(index), filename, index, intrFd[index]);
    if (intrFd[index] < 0)
    {
        debugErrno("%s open %s", toscaIntrIndexToStr(index), filename);
        return -1;
    } 
    ev.events = EPOLLIN;
    ev.data.u32 = index;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, intrFd[index], &ev) < 0)
        debugErrno("epoll_ctl ADD %d", intrFd[index]);
    return 0;
}

int toscaIntrConnectHandler(intrmask_t intrmask, void (*function)(), void* parameter)
{
    char* fname;
    unsigned int i;
    char filename[30];
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

    #define ADD_FD(i, name, ...)                         \
    {                                                    \
        if (intrFd[i] == 0) {                            \
            sprintf(filename, name , ## __VA_ARGS__ );   \
            status |= toscaIntrMonitorFile(i, filename); \
        }                                                \
    }

    if (intrmask & TOSCA_USER_INTR_ANY)
    {
        for (i = 0; i < 32; i++)
        {
            if (intrmask & TOSCA_USER1_INTR(i))
                ADD_FD(IX(USER, i), "/dev/toscauserevent%u.%u", i > 15 ? 2 : 1, i & 15);
        }
    }
    if (intrmask & TOSCA_VME_INTR_ANY)
    {
        for (i = 1; i <= 7; i++) if (intrmask & TOSCA_VME_INTR(i))
        {
            unsigned int ivec = (intrmask >> 16) & 0xff, ivec2 = (intrmask >> 24) & 0xff;
            do {
                ADD_FD(IX(VME, i, ivec), "/dev/toscavmeevent%u.%u", i, ivec);
            } while (++ivec <= ivec2);
        }
    }
    if (intrmask & TOSCA_VME_SYSFAIL)
        ADD_FD(IX(SYSFAIL), "/dev/toscavmesysfail");
    if (intrmask & TOSCA_VME_ACFAIL)
        ADD_FD(IX(ACFAIL), "/dev/toscavmeacfail");
    if (intrmask & TOSCA_VME_ERROR)
        ADD_FD(IX(ERROR), "/dev/toscavmeerror");

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

int toscaIntrForeachHandler(int (*callback)(toscaIntrHandlerInfo_t, void* user), void* user)
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

void toscaIntrLoop(void* dummy __attribute__((unused)))
{
    unsigned int i, n, index, inum, ivec;
    
    /* handle up to 64 simultanious interrupts in one system call */
    #define MAX_EVENTS 64
    struct epoll_event events[MAX_EVENTS];
    
    if (intrLoopStopEvent >= 0)
    {
        debug("interrupt loop already running");
        return;
    }
    debug("starting interrupt handling");

    intrLoopStopEvent = eventfd(0, EFD_CLOEXEC);
    events[0].events = EPOLLIN;
    events[0].data.u32 = -1;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, intrLoopStopEvent, &events[0]) < 0)
        debugErrno("epoll_ctl ADD %d", intrLoopStopEvent);
    
    int running = 1;
    while (running)
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
                running = 0;
                continue;
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
    uint64_t e;
    read(intrLoopStopEvent,&e,sizeof(e));
}

int toscaIntrLoopIsRunning(void)
{
    return (intrLoopStopEvent >= 0);
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
    uint64_t e = 0xfffffffffffffffe;
    if (intrLoopStopEvent < 0) return;
    write(intrLoopStopEvent, &e, sizeof(e));
    debug("waiting for intrLoop to terminate");
    write(intrLoopStopEvent, &e, sizeof(e));
    close(intrLoopStopEvent);
    intrLoopStopEvent = -1;
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
    const char* filename = "/dev/bus/vme/ctl";
    static int fd = -1;
    struct vme_irq_id {
        __u8 level;
        __u8 ivec;
    } irq;
    
    if (fd < 0)
    {
        fd = open(filename, O_RDWR|O_CLOEXEC);
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
    debug("level %u vector %u", inum, ivec);
}

void toscaInstallSpuriousVMEInterruptHandler(void)
{
    static int first = 1;
    if (!first) return;
    first = 0;
    toscaIntrConnectHandler(TOSCA_VME_INTR_ANY_VEC(255), toscaSpuriousVMEInterruptHandler, NULL);
}
