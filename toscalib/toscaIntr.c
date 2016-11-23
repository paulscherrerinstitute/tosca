#include <sys/select.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
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

static int newIntrFd[2];
static fd_set intrFdSet;
static int intrFdMax = -1;
static int intrFd[TOSCA_NUM_INTR];
static unsigned long long totalIntrCount, intrCount[TOSCA_NUM_INTR];
static struct intr_handler* handlers[TOSCA_NUM_INTR];

#define TOSCA_INTR_INDX_USER(i)        (i)                            
#define TOSCA_INTR_INDX_USER1(i)       TOSCA_INTR_INDX_USER(i)
#define TOSCA_INTR_INDX_USER2(i)       TOSCA_INTR_INDX_USER((i)+16)
#define TOSCA_INTR_INDX_VME(level,vec) (32+(((level)-1)*256)+(vec))
#define TOSCA_INTR_INDX_ERR(i)         (32+7*256+(i))
#define TOSCA_INTR_INDX_SYSFAIL()      TOSCA_INTR_INDX_ERR(0)
#define TOSCA_INTR_INDX_ACFAIL()       TOSCA_INTR_INDX_ERR(1)
#define TOSCA_INTR_INDX_ERROR()        TOSCA_INTR_INDX_ERR(2)

#define INTR_INDEX_TO_BIT(i)  ((i)<32?TOSCA_USER1_INTR(i):(i)>=TOSCA_INTR_INDX_ERR(0)?TOSCA_VME_FAIL((i)-TOSCA_INTR_INDX_ERR(0)):TOSCA_VME_INTR((((i)-32)>>8)+1))
#define INTR_INDEX_TO_INUM(i) ((i)<32?(i)&31:(i)>=TOSCA_INTR_INDX_ERR(0)?(i)-TOSCA_INTR_INDX_ERR(0):(((i)-32)>>8)+1)
#define INTR_INDEX_TO_IVEC(i) ((i)<32||(i)>=TOSCA_INTR_INDX_ERR(0)?0:((i)-32)&255)
#define toscaIntrIndexToStr(i) toscaIntrBitToStr(INTR_INDEX_TO_BIT(i))

#define IX(src,...) TOSCA_INTR_INDX_##src(__VA_ARGS__)
#define FOREACH_HANDLER(h, index) for(h = handlers[index]; h; h = h->next)

#define FOR_BITS_IN_MASK(first, last, index, maskbit, mask, action) \
    { int i; for (i=first; i <= last; i++) if (mask & maskbit) action(index, maskbit) }

#define FOREACH_MASKBIT(mask, action) \
{                                                                                      \
    /* handle VME_SYSFAIL, VME_ACFAIL, VME_ERROR */                                    \
    if ((mask) & TOSCA_VME_FAIL_ANY)                                                   \
        FOR_BITS_IN_MASK(0, 2, IX(ERR, i), TOSCA_VME_FAIL(i), (mask), action)          \
    /* handle VME_LVL */                                                               \
    if ((mask) & TOSCA_VME_INTR_ANY) {                                                 \
        unsigned int vec = (mask)>>16&0xff, vec2 = (mask)>>24&0xff;                    \
        do                                                                             \
            FOR_BITS_IN_MASK(1, 7, IX(VME, i, vec), TOSCA_VME_INTR(i), (mask), action) \
        while (++vec <= vec2);                                                         \
    }                                                                                  \
    /* handle USER */                                                                  \
    if ((mask) & (TOSCA_USER1_INTR_ANY | TOSCA_USER2_INTR_ANY))                        \
        FOR_BITS_IN_MASK(0, 31, IX(USER, i), TOSCA_USER1_INTR(i), (mask), action)      \
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

int toscaIntrMonitorFile(int index, const char* filename)
{
    intrFd[index] = open(filename, O_RDWR);
    debug ("%s open %s intrFd[%d]=%d", toscaIntrIndexToStr(index), filename, index, intrFd[index]);
    if (intrFd[index] < 0)
    {
        debugErrno("%s open %s", toscaIntrIndexToStr(index), filename);
        return -1;
    } 
    if (intrFd[index] >= FD_SETSIZE)
    {
        debug("%s fd %d too large for select", filename, intrFd[index]);
        return -1;
    }
    FD_SET(intrFd[index], &intrFdSet);
    if (intrFd[index] > intrFdMax) intrFdMax = intrFd[index];
    return 0;
}

int toscaIntrConnectHandler(intrmask_t intrmask, void (*function)(), void* parameter)
{
    char* fname;
    int i;
    char filename[30];
    int status = 0;
    static int count = 0;
    char dummy = 0;

    debug("intrmask=0x%016"PRIx64" function=%s, parameter=%p intrFdMax=%d count=%d",
        intrmask, fname=symbolName(function,0), parameter, intrFdMax, count++), free(fname);
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
                ADD_FD(IX(USER, i), "/dev/toscauserevent%u.%u", i & TOSCA_USER2_INTR_ANY ? 2 : 1, i & 15);
        }
    }
    if (intrmask & TOSCA_VME_INTR_ANY)
    {
        for (i = 1; i <= 7; i++) if (intrmask & TOSCA_VME_INTR(i))
        {
            unsigned int vec = (intrmask >> 16) & 0xff, vec2 = (intrmask >> 24) & 0xff;
            do {
                ADD_FD(IX(VME, i, vec), "/dev/toscavmeevent%u.%u", i, vec);
            } while (++vec <= vec2);
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
        debug("%s vec=%d: %s(%p)",                                                   \
            toscaIntrBitToStr(bit), INTR_INDEX_TO_IVEC(i),                           \
            fname=symbolName(handler->function,0), handler->parameter), free(fname); \
    }
    
    FOREACH_MASKBIT(intrmask, INSTALL_HANDLER);
    UNLOCK;
    if (write(newIntrFd[1], &dummy, 1) != 1)
        debugErrno("write newIntrFd[1]=%d", newIntrFd[1]);
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
    char dummy = 0;
    #define DISABLE_INTR(i, bit)                     \
    {                                                \
        if (intrFd[i] > 0) {                         \
            debug("disable %s vec=%d intrFd[%i]=%d", \
                toscaIntrIndexToStr(i),              \
                INTR_INDEX_TO_IVEC(i),               \
                i, intrFd[i]);                       \
            FD_CLR(intrFd[i], &intrFdSet);           \
        }                                            \
    }
    FOREACH_MASKBIT(intrmask, DISABLE_INTR);
    if (write(newIntrFd[1], &dummy, 1) != 1)
        debugErrno("write newIntrFd[1]=%d", newIntrFd[1]);
    return 0;
}

int toscaIntrEnable(intrmask_t intrmask)
{
    char dummy = 0;
    #define ENABLE_INTR(i, bit)                      \
    {                                                \
        if (intrFd[i] > 0) {                         \
            debug("enable %s vec=%d intrFd[%i]=%d",  \
                toscaIntrIndexToStr(i),              \
                INTR_INDEX_TO_IVEC(i),               \
                i, intrFd[i]);                       \
            FD_SET(intrFd[i], &intrFdSet);           \
        }                                            \
    }
    FOREACH_MASKBIT(intrmask, ENABLE_INTR);
    if (write(newIntrFd[1], &dummy, 1) != 1)
        debugErrno("write newIntrFd[1]=%d", newIntrFd[1]);
    return 0;
}

int toscaIntrForeachHandler(int (*callback)(toscaIntrHandlerInfo_t, void* user), void* user)
{
    #define REPORT_HANDLER(i, bit)                     \
    {                                                  \
        struct intr_handler* handler;                  \
        int status;                                    \
        debug("%s %d index=%d handlers=%p",            \
            toscaIntrBitToStr(bit),                    \
            INTR_INDEX_TO_IVEC(i), i, handlers[i]);    \
        FOREACH_HANDLER(handler, i) {                  \
            debug("%p", handler->function);            \
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
            /* we have at lease one new fd in the intrFdSet and need to restart select */
            if (read(newIntrFd[0], &dummy, sizeof(dummy)) < 0)
                debugErrno("read newIntrFd[0]=%d", newIntrFd[0]);
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
/*
    return toscaCsrWrite(0x40c, 0x1000+(level<<8)+vec);

*/
    const char* filename = "/dev/bus/vme/ctl";
    static int fd = 0;
    struct vme_irq_id {
        __u8 level;
        __u8 vec;
    } irq;
    
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

void toscaSpuriousVMEInterruptHandler(void* param, int inum, int vec)
{
    debug("level %d vector %d", inum, vec);
}

void toscaInstallSpuriousVMEInterruptHandler(void)
{
    static int first = 1;
    if (!first) return;
    first = 0;
    debug("");
    toscaIntrConnectHandler(TOSCA_VME_INTR_ANY_VEC(255), toscaSpuriousVMEInterruptHandler, NULL);
}
