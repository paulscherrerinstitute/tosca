#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L /* needed for pselect */
#include <sys/select.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include "symbolname.h"
#include "toscaIntr.h"

pthread_mutex_t handlerlist_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK pthread_mutex_lock(&handlerlist_mutex)
#define UNLOCK pthread_mutex_unlock(&handlerlist_mutex)

#define TOSCA_DEBUG_NAME toscaIntr
#include "toscaDebug.h"

struct intr_handler {
    void (*function)();
    void *parameter;
    struct intr_handler* next;
};

#define EPOLL_EVENTS 32
static int epollfd;
static int intr_fd[TOSCA_NUM_INTR];
static unsigned long long intrCount[TOSCA_NUM_INTR];
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

int toscaIntrConnectHandler(intrmask_t intrmask, unsigned int vec, void (*function)(), void* parameter)
{
    struct epoll_event ev = {.events = EPOLLIN|EPOLLET};
    char* fname;
    int i;
    char filename[30];
    
    LOCK; /* only need to lock installation, not calling of handlers */
    debug("intrmask=0x%016llx vec=0x%x function=%s, parameter=%p epollfd=%d",
        intrmask, vec, fname=symbolName(function,0), parameter, epollfd), free(fname);

    #define ADD_FD(index, name, ...)                                      \
    {                                                                     \
        if (intr_fd[index] == 0) {                                        \
            sprintf(filename, name , ## __VA_ARGS__ );                    \
            intr_fd[index] = open(filename, O_RDWR);                      \
            debug ("open %s fd= %d", filename, intr_fd[index]);           \
            if (intr_fd[index]< 0) debug("open %s failed: %m", filename); \
            ev.data.u32 = index;                                          \
            if (epoll_ctl(epollfd,EPOLL_CTL_ADD,intr_fd[index],&ev))      \
                debugErrno("epoll");                                      \
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
            UNLOCK;
            return -1;
        }

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
        if (!(handler = calloc(1,sizeof(struct intr_handler)))) { UNLOCK; return -1;}    \
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
    return 0;
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

void toscaIntrLoop()
{
    int i;
    int nevents;
    struct epoll_event events[EPOLL_EVENTS];
    int index;
    int inum, vec;

    debug("starting interrupt handling");
    while (1)
    {
        debugLvl(2,"waiting for interrupts");
        nevents = epoll_wait(epollfd, events, EPOLL_EVENTS, -1);
        debugLvl(2,"epoll_wait returned %d", nevents);
        if (nevents < 1)
            debugErrno("epoll_wait");
        for(i = 0; i < nevents; i++)
        {
            struct intr_handler* handler;
            index=events[i].data.u32;
            inum=index<32?index&31:index>=TOSCA_INTR_INDX_ERR(0)?index-TOSCA_INTR_INDX_ERR(0):((index-32)>>8)+1;
            vec=index<32||index>=TOSCA_INTR_INDX_ERR(0)?0:(index-32)&255;
            intrCount[index]++;
            FOREACH_HANDLER(handler, index) {
                char* fname;
                debugLvl(2, "index 0x%x %s, #%llu %s(%p, %d, %u)",
                    index, toscaIntrBitToStr(INTR_INDEX_TO_BIT(index)), intrCount[index],
                    fname=symbolName(handler->function,0),
                    handler->parameter, inum, vec),
                    free(fname);
                handler->function(handler->parameter, inum, vec);
            }
            write(intr_fd[index], NULL, 0);  /* re-enable interrupt */
        }
    }
}

void toscaIntrInit () __attribute__((constructor));
void toscaIntrInit ()
{
    epollfd = epoll_create(EPOLL_EVENTS);
    debug("epoll handle = %d", epollfd);
}
