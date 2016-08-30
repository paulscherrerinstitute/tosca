#ifndef toscaIntr_h
#define toscaIntr_h

#include <stdint.h>
#include <signal.h>

extern int toscaIntrDebug;

typedef uint64_t intrmask_t;

#define INTR_VME_LVL_1       0x01ULL
#define INTR_VME_LVL_2       0x02ULL
#define INTR_VME_LVL_3       0x04ULL
#define INTR_VME_LVL_4       0x08ULL
#define INTR_VME_LVL_5       0x10ULL
#define INTR_VME_LVL_6       0x20ULL
#define INTR_VME_LVL_7       0x40ULL
#define INTR_VME_LVL(n)    (INTR_VME_LVL_1<<((n)-1))
#define INTR_VME_LVL_ANY     0x7fULL
#define INTR_VME_SYSFAIL    0x100ULL
#define INTR_VME_ACFAIL     0x200ULL
#define INTR_VME_ERROR      0x400ULL
#define INTR_VME_FAIL(n)   (INTR_VME_SYSFAIL<<(n))
#define INTR_VME_FAIL_ANY   0x700ULL

#define INTR_USER1_INTR0    0x000100000000ULL
#define INTR_USER1_INTR1    0x000200000000ULL
#define INTR_USER1_INTR2    0x000400000000ULL
#define INTR_USER1_INTR3    0x000800000000ULL
#define INTR_USER1_INTR4    0x001000000000ULL
#define INTR_USER1_INTR5    0x002000000000ULL
#define INTR_USER1_INTR6    0x004000000000ULL
#define INTR_USER1_INTR7    0x008000000000ULL
#define INTR_USER1_INTR8    0x010000000000ULL
#define INTR_USER1_INTR9    0x020000000000ULL
#define INTR_USER1_INTR10   0x040000000000ULL
#define INTR_USER1_INTR11   0x080000000000ULL
#define INTR_USER1_INTR12   0x100000000000ULL
#define INTR_USER1_INTR13   0x200000000000ULL
#define INTR_USER1_INTR14   0x400000000000ULL
#define INTR_USER1_INTR15   0x800000000000ULL
#define INTR_USER1_INTR(n) (INTR_USER1_INTR0<<(n))
#define INTR_USER1_ANY      0xffff00000000ULL

#define INTR_USER2_INTR0    0x0001000000000000ULL
#define INTR_USER2_INTR1    0x0002000000000000ULL
#define INTR_USER2_INTR2    0x0004000000000000ULL
#define INTR_USER2_INTR3    0x0008000000000000ULL
#define INTR_USER2_INTR4    0x0010000000000000ULL
#define INTR_USER2_INTR5    0x0020000000000000ULL
#define INTR_USER2_INTR6    0x0040000000000000ULL
#define INTR_USER2_INTR7    0x0080000000000000ULL
#define INTR_USER2_INTR8    0x0100000000000000ULL
#define INTR_USER2_INTR9    0x0200000000000000ULL
#define INTR_USER2_INTR10   0x0400000000000000ULL
#define INTR_USER2_INTR11   0x0800000000000000ULL
#define INTR_USER2_INTR12   0x1000000000000000ULL
#define INTR_USER2_INTR13   0x2000000000000000ULL
#define INTR_USER2_INTR14   0x4000000000000000ULL
#define INTR_USER2_INTR15   0x8000000000000000ULL
#define INTR_USER2_INTR(n) (INTR_USER2_INTR0<<(n))
#define INTR_USER2_ANY      0xffff000000000000ULL

const char* toscaIntrBitStr(intrmask_t intrmaskbit);

intrmask_t toscaIntrWait(intrmask_t intrmask, unsigned int vec, const struct timespec *timeout, const sigset_t *sigmask);
#define toscaIntrWaitVME(vec) toscaIntrWait(INTR_VME_LVL_ANY, vec, NULL, NULL)
#define toscaIntrWaitUSR1() toscaIntrWait(INTR_USER1_ANY, 0, NULL, NULL)
#define toscaIntrWaitUSR2() toscaIntrWait(INTR_USER2_ANY, 0, NULL, NULL)

/* intrmask is any combination of the INTR_* bits */
/* vec is for VME_LVL_* only and is ignored for other INTR_* bits */
/* timeout may be NULL to wait forever */
/* sigmask may be NULL not to wait for signals */
/* returns a handle to be used in other functions or 0 on timeout, signal or error */

int toscaIntrConnectHandler(intrmask_t intrmask, unsigned int vec, void (*function)(), void* parameter);
#define toscaIntrConnectHandlerVME(vec, function, parameter) toscaIntrConnectHandler(INTR_VME_LVL_ANY, vec, function, parameter)
/* returns 0 on success */

int toscaIntrDisconnectHandler(intrmask_t intrmask, unsigned int vec, void (*function)(), void* parameter);
/* check parameter only if it is not NULL */
/* returns number of disconnected handlers */

typedef struct {
    intrmask_t intrmaskbit; /* one of the INTR_* bits above */
    unsigned int index;        /* 0..1826 (16+16+7*256+3), unique for each intr bit (and VME vector) */
    unsigned int vec;          /* 0...255 for intr  bits in INTR_VME_LVL_ANY, else 0 */
    void (*function)();
    void *parameter;
    unsigned long long count;  /* number of times the interrupt has been received */
} toscaIntrHandlerInfo_t;

int toscaIntrForeachHandler(intrmask_t intrmask, unsigned int vec, int (*callback)(toscaIntrHandlerInfo_t));

/* toscaIntrLoop calls toscaIntrWait in a loop */
/* Start it in a separate worker thread (one for each VME vec). */

typedef struct {
    intrmask_t intrmask;
    unsigned int vec;
    struct timespec *timeout;
    sigset_t* sigmask;
} toscaIntrLoopArg_t;

void toscaIntrLoop(void* arg);
/* The arg must be a pointer to a persistent toscaIntrLoopArg_t. */


#endif
