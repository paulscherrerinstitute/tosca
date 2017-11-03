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

intrmask_t toscaStrToIntrMask(const char* s);

int toscaIntrConnectHandler(intrmask_t intrmask, void (*function)(), void* parameter);
/* Returns 0 on success. */
/* The user function will be called with arguments (void* parameter, int inum, int ivec) */

int toscaIntrDisconnectHandler(intrmask_t intrmask, void (*function)(), void* parameter);
/* Remark: Checks parameter only if it is not NULL. */
/* Returns number of disconnected handlers. Thus 0 means: fail, there is not such handler. */

int toscaIntrDisable(intrmask_t intrmask);
int toscaIntrEnable(intrmask_t intrmask);
/* Temporarily suspends interrupt handling but keeps interrupts in queue. */

void toscaIntrLoop(void*);
/* Handles incoming interrupt and calls installed handlers. */
/* To be started in a worker thread. */
/* The ignored void* argument is for compatibility with pthread_create. */
/* Cannot run twice at the same time. (Second try will terminate immediately.) */

int toscaIntrLoopIsRunning(void);
/* Returns 1 if the toscaIntrLoop is already running, else 0. */

void toscaIntrLoopStop();
/* Terminate the interrupt loop. */
/* Returns after loop has stopped and no handler is active any more. */

typedef struct {
    intrmask_t intrmaskbit;    /* one of the mask bits */
    unsigned int index;        /* 0...TOSCA_NUM_INTR-1, unique for each intr bit (and VME vector) */
    unsigned int vec;          /* 0...255 for intr bits in TOSCA_VME_INTR_ANY, else 0 */
    void (*function)();
    void *parameter;
    unsigned long long count;  /* number of times the interrupt has been received */
} toscaIntrHandlerInfo_t;

size_t toscaIntrForeachHandler(size_t (*callback)(toscaIntrHandlerInfo_t, void* user), void* user);
/* Calls callback for each installed handler until a callback returns something else than 0. */
/* Returns what the last callback had returned. */
/* (The return type is large enough to hold a pointer if necessary.) */

unsigned long long toscaIntrCount();
/* Returns total number of interrupts received by toscaIntrLoop since start of this API. */

int toscaSendVMEIntr(unsigned int level, unsigned int vec);
/* Generates an interrupt on the VME bus. */

void toscaInstallSpuriousVMEInterruptHandler(void);
/* Installs dummy interrupt handlers on levels 1...7 vector 255. */
/* Interrupt vector 255 is a hint that something went wrong with a pervious interrupt */

#ifdef __cplusplus
}
#endif

#endif
