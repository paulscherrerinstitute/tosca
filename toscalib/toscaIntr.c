#include <sys/select.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

#include <symbolname.h>
#include <keypress.h>

typedef uint64_t __u64;
typedef uint32_t __u32;
typedef uint8_t __u8;
#include "vme_user.h"
#include "toscaIntr.h"

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

static int newIntrFd[2];
static fd_set intrFdSet;
static int intrFdMax = -1;
static int intrFd[TOSCA_NUM_INTR];
static unsigned long long totalIntrCount, intrCount[TOSCA_NUM_INTR], prevTotalIntrCount, prevIntrCount[TOSCA_NUM_INTR];
static struct intr_handler* handlers[TOSCA_NUM_INTR];

#define IX(src,...) TOSCA_INTR_INDX_##src(__VA_ARGS__)
#define FOREACH_HANDLER(h, index) for(h = handlers[index]; h; h = h->next)

#define FOR_BITS_IN_MASK(first, last, index, maskbit, mask, action) \
    { int i; for (i=first; i <= last; i++) if (mask & maskbit) action(index, maskbit) }

#define FOREACH_MASKBIT(mask, vec, action) \
{                                                                                \
    /* handle VME_SYSFAIL, VME_ACFAIL, VME_ERROR */                              \
    if ((mask) & INTR_VME_FAIL_ANY)                                              \
        FOR_BITS_IN_MASK(0, 2, IX(ERR, i), INTR_VME_FAIL(i), (mask), action)     \
    /* handle VME_LVL */                                                         \
    if ((mask) & INTR_VME_LVL_ANY)                                               \
        FOR_BITS_IN_MASK(1, 7, IX(VME, i, vec), INTR_VME_LVL(i), (mask), action) \
    /* handle USER */                                                            \
    if ((mask) & (INTR_USER1_ANY | INTR_USER2_ANY))                              \
        FOR_BITS_IN_MASK(0, 31, IX(USER, i), INTR_USER1_INTR(i), (mask), action) \
}

const char* toscaIntrBitToStr(intrmask_t intrmaskbit)
{
    switch(intrmaskbit)
    {
        case INTR_VME_LVL_1:     return "VME-1";
        case INTR_VME_LVL_2:     return "VME-2";
        case INTR_VME_LVL_3:     return "VME-3";
        case INTR_VME_LVL_4:     return "VME-4";
        case INTR_VME_LVL_5:     return "VME-5";
        case INTR_VME_LVL_6:     return "VME-6";
        case INTR_VME_LVL_7:     return "VME-7";
        case INTR_VME_SYSFAIL:   return "VME-SYSFAIL";
        case INTR_VME_ACFAIL:    return "VME-ACFAIL";
        case INTR_VME_ERROR:     return "VME-ERROR";
        case INTR_USER1_INTR0:   return "USER1-0";
        case INTR_USER1_INTR1:   return "USER1-1";
        case INTR_USER1_INTR2:   return "USER1-2";
        case INTR_USER1_INTR3:   return "USER1-3";
        case INTR_USER1_INTR4:   return "USER1-4";
        case INTR_USER1_INTR5:   return "USER1-5";
        case INTR_USER1_INTR6:   return "USER1-6";
        case INTR_USER1_INTR7:   return "USER1-7";
        case INTR_USER1_INTR8:   return "USER1-8";
        case INTR_USER1_INTR9:   return "USER1-9";
        case INTR_USER1_INTR10:  return "USER1-10";
        case INTR_USER1_INTR11:  return "USER1-11";
        case INTR_USER1_INTR12:  return "USER1-12";
        case INTR_USER1_INTR13:  return "USER1-13";
        case INTR_USER1_INTR14:  return "USER1-14";
        case INTR_USER1_INTR15:  return "USER1-15";
        case INTR_USER2_INTR0:   return "USER2-0";
        case INTR_USER2_INTR1:   return "USER2-1";
        case INTR_USER2_INTR2:   return "USER2-2";
        case INTR_USER2_INTR3:   return "USER2-3";
        case INTR_USER2_INTR4:   return "USER2-4";
        case INTR_USER2_INTR5:   return "USER2-5";
        case INTR_USER2_INTR6:   return "USER2-6";
        case INTR_USER2_INTR7:   return "USER2-7";
        case INTR_USER2_INTR8:   return "USER2-8";
        case INTR_USER2_INTR9:   return "USER2-9";
        case INTR_USER2_INTR10:  return "USER2-10";
        case INTR_USER2_INTR11:  return "USER2-11";
        case INTR_USER2_INTR12:  return "USER2-12";
        case INTR_USER2_INTR13:  return "USER2-13";
        case INTR_USER2_INTR14:  return "USER2-14";
        case INTR_USER2_INTR15:  return "USER2-15";
        default:                 return "unknown";
    }  
}

int toscaIntrMonitorFile(int index, const char* filename)
{
    char dummy = 0;
    intrFd[index] = open(filename, O_RDWR);
    debug ("open %s fd=%d", filename, intrFd[index]);
    if (intrFd[index] < 0)
    {
        debugErrno("open %s", filename);
        return -1;
    } 
    if (intrFd[index] >= FD_SETSIZE)
    {
        debug("%s fd %d loo large for select", filename, intrFd[index]);
        return -1;
    }
    FD_SET(intrFd[index], &intrFdSet);
    if (intrFd[index] > intrFdMax) intrFdMax = intrFd[index];
    if (write(newIntrFd[1], &dummy, 1) != 1)
        debugErrno("write newIntrFd[1]=%d", newIntrFd[1]);
    return 0;
}

int toscaIntrConnectHandler(intrmask_t intrmask, unsigned int vec, void (*function)(), void* parameter)
{
    char* fname;
    int i;
    char filename[30];
    int status = 0;
    static int count = 0;

    debug("intrmask=0x%016llx vec=0x%x function=%s, parameter=%p intrFdMax=%d count=%d",
        (unsigned long long)intrmask, vec, fname=symbolName(function,0), parameter, intrFdMax, count++), free(fname);
    LOCK; /* only need to lock installation, not calling of handlers */

    #define ADD_FD(index, name, ...)                                      \
    {                                                                     \
        if (intrFd[index] == 0) {                                         \
            sprintf(filename, name , ## __VA_ARGS__ );                    \
            status |= toscaIntrMonitorFile(index, filename);              \
        }                                                                 \
    }

    if (intrmask & (INTR_USER1_ANY | INTR_USER2_ANY))
        for (i = 0; i < 32; i++)
        {
            if (intrmask & INTR_USER1_INTR(i))
                ADD_FD(IX(USER, i), "/dev/toscauserevent%u.%u", i & INTR_USER2_ANY ? 2 : 1, i & 15);
        }
    if (intrmask & INTR_VME_LVL_ANY)
    {
        if (vec > 255)
        {
            debug("vec %u out of range", vec);
            errno = EINVAL;
            status = -1;
        }
        else
        for (i = 1; i <= 7; i++)
        {
            if (intrmask & INTR_VME_LVL(i))
                ADD_FD(IX(VME, i, vec), "/dev/toscavmeevent%u.%u", i, vec);
        }
    }
    if (intrmask & INTR_VME_SYSFAIL)
        ADD_FD(IX(SYSFAIL), "/dev/toscavmesysfail");
    if (intrmask & INTR_VME_ACFAIL)
        ADD_FD(IX(ACFAIL), "/dev/toscavmeacfail");
    if (intrmask & INTR_VME_ERROR)
        ADD_FD(IX(ERROR), "/dev/toscavmeerror");

    #define INSTALL_HANDLER(index, bit)                                                  \
    {                                                                                    \
        struct intr_handler** phandler, *handler;                                        \
        if (!(handler = calloc(1,sizeof(struct intr_handler)))) {                        \
            debugErrno("calloc"); status = -1; break; }                                  \
        handler->function = function;                                                    \
        handler->parameter = parameter;                                                  \
        handler->next = NULL;                                                            \
        for (phandler = &handlers[index]; *phandler; phandler = &(*phandler)->next);     \
        *phandler = handler;                                                             \
            debug("%s vec=0x%x: %s(%p)",                                                 \
                toscaIntrBitToStr(bit), vec,                                             \
                fname=symbolName(handler->function,0), handler->parameter), free(fname); \
    }
    
    FOREACH_MASKBIT(intrmask, vec, INSTALL_HANDLER);
    UNLOCK;
    return status;
}

int toscaIntrDisconnectHandler(intrmask_t intrmask, unsigned int vec, void (*function)(), void* parameter)
{
    int n = 0;
    #define REMOVE_HANDLER(index, bit)                             \
    {                                                              \
        struct intr_handler** phandler, *handler;                  \
        phandler = &handlers[index];                               \
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
    FOREACH_MASKBIT(intrmask, vec, REMOVE_HANDLER);
    UNLOCK;
    return n;
}

int toscaIntrForeachHandler(intrmask_t intrmask, unsigned int vec, int (*callback)(toscaIntrHandlerInfo_t))
{
    #define REPORT_HANDLER(index, bit)                     \
    {                                                      \
        struct intr_handler* handler;                      \
        FOREACH_HANDLER(handler, index) {                  \
            int status = callback((toscaIntrHandlerInfo_t) \
                {bit, index, vec, handler->function,       \
                 handler->parameter, intrCount[index]});   \
            if (status != 0) return status;                \
        }                                                  \
    }
    LOCK;
    FOREACH_MASKBIT(intrmask, vec, REPORT_HANDLER);
    UNLOCK;
    return 0;
}

void toscaIntrShow(int level)
{
    intrmask_t intrmask;
    unsigned long long count, delta;
    struct intr_handler* handler;
    char* fname;
    unsigned int index;
    unsigned int period = 0;
    unsigned int symbolDetail = 0;
    int rep = 0;

    if (level > 1) symbolDetail = (level-1) | F_SYMBOL_NAME_DEMANGE_FULL;
    if (level < 0) { period = -1000*level; level = 0; }
    
    do
    {
        LOCK;
        count = totalIntrCount;
        delta = count - prevTotalIntrCount;
        prevTotalIntrCount = count;
        if (rep) printf("\n");
        printf("total interrupt count=%llu (+%llu)\n", count, delta);
        for (index = 0; index < TOSCA_NUM_INTR; index++)
        {
            count = intrCount[index];
            delta = count - prevIntrCount[index];
            prevIntrCount[index] = count;
            if (count == 0 && handlers[index] == NULL) continue;
            intrmask = INTR_INDEX_TO_BIT(index);
            printf("  %s", toscaIntrBitToStr(intrmask));
            if (intrmask & INTR_VME_LVL_ANY)
                printf("-%-3d ", INTR_INDEX_TO_IVEC(index));
            printf(" count=%llu (+%llu)\n", count, delta);
            prevIntrCount[index] = count;
            if (level) for (handler = handlers[index]; handler; handler = handler->next)
            {
                printf("    %s (%p)\n", fname=symbolName(handler->function, symbolDetail), handler->parameter), free(fname);
            }
        }
        UNLOCK;
        rep = 1;
    } while (period != 0 && !waitForKeypress(period));
}

static int loopRunning = 0;

void toscaIntrLoop()
{
    int n, index, inum, vec;
    fd_set read_fs;
    
    if (loopRunning) return;
    loopRunning = 1;
    
    debug("starting interrupt handling");
    while (1)
    {
        read_fs = intrFdSet;
        debugLvl(2,"waiting for interrupts intrFdMax=%d", intrFdMax);
        n = select(intrFdMax + 1, &read_fs, NULL, NULL, NULL);
        debugLvl(2,"select returned %d", n);
        if (n < 1)
            debugErrno("select");
        if (FD_ISSET(newIntrFd[0], &read_fs))
        {
            long long dummy = 0;
            debugLvl(1, "new fd notification on newIntrFd[0]=%d", newIntrFd[0]);
            /* we have a new fd in the intrFdSet and need to restart select */
            if (read(newIntrFd[0], &dummy, sizeof(dummy)) < 0)
                debugErrno("read newIntrFd[0]=%d: %llx", newIntrFd[0], dummy);
            if (dummy != 0) break; /* exit the loop (see toscaIntrLoopStop) */
            n--;
        }
        for (index=0; index < TOSCA_NUM_INTR && n > 0; index++)
        {
            debugLvl(2, "checking index %d, n=%d", index, n);
            if (intrFd[index] > 0 && FD_ISSET(intrFd[index], &read_fs))
            {
                struct intr_handler* handler;
                inum = INTR_INDEX_TO_INUM(index);
                vec = INTR_INDEX_TO_IVEC(index);
                totalIntrCount++;
                intrCount[index]++;
                FOREACH_HANDLER(handler, index) {
                    char* fname;
                    debugLvl(2, "index 0x%x fd=%d %s, #%llu %s(%p, %d, %u)",
                        index,
                        intrFd[index],
                        toscaIntrBitToStr(INTR_INDEX_TO_BIT(index)),
                        intrCount[index],
                        fname=symbolName(handler->function,0),
                        handler->parameter, inum, vec),
                        free(fname);
                    handler->function(handler->parameter, inum, vec);
                }
                write(intrFd[index], NULL, 0);  /* re-enable interrupt */
                n--;
            }
        }
    }
    loopRunning = 0;
}

int toscaIntrLoopIsRunning(void)
{
    return loopRunning;
}

void toscaIntrLoopStop()
{
    char x = 0xff;
    struct timespec wait = { 0, 10000000 };
    if (!loopRunning) return;
    write(newIntrFd[1], &x, 1);
    while (loopRunning) nanosleep(&wait, NULL);
}

void toscaIntrInit () __attribute__((constructor));
void toscaIntrInit ()
{
    pipe(newIntrFd);
    intrFdMax = newIntrFd[0];
    FD_SET(newIntrFd[0], &intrFdSet);
}

int toscaSendVMEIntr(unsigned int level, unsigned int vec)
{
    const char* filename = "/dev/bus/vme/ctl";
    static int fd = 0;
    struct vme_irq_id {
        __u8 level;
        __u8 vec;
    } irq;
    
    if (level < 1 || level > 7)
    {
        errno = EINVAL;
        debug("invalid VME interrupt level %d", level);
        return -1;
    }
    if (vec > 255)
    {
        errno = EINVAL;
        debug("invalid VME interrupt vector %d", vec);
        return -1;
    }
    if (fd <= 0)
    {
        fd = open(filename, O_RDWR);
        if (fd < 0)
        {
            debugErrno("open %s", filename);
            return -1;
        }
    }
    irq.level = level;
    irq.vec = vec;
    if (ioctl(fd, VME_IRQ_GEN, &irq) != 0)
    {
        debugErrno("ioctl(%d, VME_IRQ_GEN, {level=%d, vec=%d})", fd, irq.level, irq.vec);
        return -1;
    }
    return 0;
}
