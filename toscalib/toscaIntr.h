#ifndef toscaIntr_h
#define toscaIntr_h

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* set to 1 to see debug output */
extern int toscaIntrDebug;

/* set to redirect debug output  */
extern FILE* toscaIntrDebugFile;

/* index for arrays of interrupts */
#define TOSCA_NUM_INTR (16+16+7*256+3)

/* interrupt masks */
typedef uint64_t intrmask_t;

#define TOSCA_VME_INTR(l)              (0x1ULL<<((l)-1))
#define TOSCA_VME_INTR_VECS(l,v1,v2)   (TOSCA_VME_INTR(l) | (uint32_t)(v1)<<16 | (uint32_t)(v2)<<24) /* level = 1...7, v1,v2 = 0...255 */
#define TOSCA_VME_INTR_VEC(l,v)         TOSCA_VME_INTR_VECS(l,(v),0)
#define TOSCA_VME_INTR_ANY              0x7fULL 
#define TOSCA_VME_INTR_ANY_VECS(v1,v2) (TOSCA_VME_INTR_ANY | (uint32_t)(v1)<<16 | (uint32_t)(v2)<<24)
#define TOSCA_VME_INTR_ANY_VEC(v)       TOSCA_VME_INTR_ANY_VECS((v),0)
#define TOSCA_VME_SYSFAIL               0x100ULL
#define TOSCA_VME_ACFAIL                0x200ULL
#define TOSCA_VME_ERROR                 0x400ULL
#define TOSCA_VME_FAIL(n)              (TOSCA_VME_SYSFAIL<<(n)) /* n = 0...2 */
#define TOSCA_VME_FAIL_ANY              0x700ULL

#define TOSCA_USER1_INTR(n)            (0x100000000ULL<<(n)) /* n = 0...31 */
#define TOSCA_USER1_INTR_ANY            0xffff00000000ULL

#define TOSCA_USER2_INTR(n)            (0x1000000000000ULL<<(n)) /* n = 0...15 */
#define TOSCA_USER2_INTR_ANY            0xffff000000000000ULL
#define TOSCA_USER_INTR_ANY            (TOSCA_USER1_INTR_ANY|TOSCA_USER2_INTR_ANY)
#define TOSCA_INTR_ANY                 (TOSCA_USER1_INTR_ANY|TOSCA_USER2_INTR_ANY|TOSCA_VME_INTR_ANY_VECS(0,255))

const char* toscaIntrBitToStr(intrmask_t intrmaskbit);

int toscaIntrConnectHandler(intrmask_t intrmask, void (*function)(), void* parameter);
/* returns 0 on success */

int toscaIntrDisconnectHandler(intrmask_t intrmask, void (*function)(), void* parameter);
/* check parameter only if it is not NULL */
/* returns number of disconnected handlers */

int toscaIntrDisable(intrmask_t intrmask);
int toscaIntrEnable(intrmask_t intrmask);
/* Temporarily suspends interrupt handling but keeps interrupts in queue */

void toscaIntrLoop();
/* handles the interrupts and calls installed handlers */
/* Start it in a worker thread. */

int toscaIntrLoopIsRunning(void);
/* Is 1 if the toscaIntrLoop is already running. */
/* Further attemts to start the tread terminate silently. */

void toscaIntrLoopStop();
/* Terminate the interrupt loop. */
/* Only returns after loop has stopped. */

typedef struct {
    intrmask_t intrmaskbit;    /* one of the mask bits */
    unsigned int index;        /* 0...TOSCA_NUM_INTR-1, unique for each intr bit (and VME vector) */
    unsigned int vec;          /* 0...255 for intr bits in TOSCA_VME_INTR_ANY, else 0 */
    void (*function)();
    void *parameter;
    unsigned long long count;  /* number of times the interrupt has been received */
} toscaIntrHandlerInfo_t;

int toscaIntrForeachHandler(intrmask_t intrmask, int (*callback)(toscaIntrHandlerInfo_t, void* user), void* user);
/* calls callback for each installed handler that matches intrmask (and vec range for VME) until a callback returns not 0 */
/* returns what the last callback had returned */

unsigned long long toscaIntrCount();
/* returns total number of received interrupts */

int toscaSendVMEIntr(unsigned int level, unsigned int vec);

void toscaInstallSpuriousVMEInterruptHandler(void);

#ifdef __cplusplus
}
#endif

#endif
