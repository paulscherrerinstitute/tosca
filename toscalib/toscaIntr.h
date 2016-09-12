#ifndef toscaIntr_h
#define toscaIntr_h

#include <stdint.h>
#include <signal.h>

extern int toscaIntrDebug;

/* index for arrays of interrupts */
#define TOSCA_NUM_INTR (16+16+7*256+3)
#define TOSCA_INTR_INDX_USER(i)        (i)                            
#define TOSCA_INTR_INDX_USER1(i)       TOSCA_INTR_INDX_USER(i)
#define TOSCA_INTR_INDX_USER2(i)       TOSCA_INTR_INDX_USER((i)+16)
#define TOSCA_INTR_INDX_VME(level,vec) (32+(((level)-1)*256)+(vec))
#define TOSCA_INTR_INDX_ERR(i)         (32+7*256+(i))
#define TOSCA_INTR_INDX_SYSFAIL()      TOSCA_INTR_INDX_ERR(0)
#define TOSCA_INTR_INDX_ACFAIL()       TOSCA_INTR_INDX_ERR(1)
#define TOSCA_INTR_INDX_ERROR()        TOSCA_INTR_INDX_ERR(2)


/* interrupt masks */
typedef uint64_t intrmask_t;

#define INTR_VME_LVL_1       0x01ULL
#define INTR_VME_LVL_2       0x02ULL
#define INTR_VME_LVL_3       0x04ULL
#define INTR_VME_LVL_4       0x08ULL
#define INTR_VME_LVL_5       0x10ULL
#define INTR_VME_LVL_6       0x20ULL
#define INTR_VME_LVL_7       0x40ULL
#define INTR_VME_LVL(n)    (INTR_VME_LVL_1<<((n)-1)) /* n = 1...7 */
#define INTR_VME_LVL_ANY     0x7fULL
#define INTR_VME_SYSFAIL    0x100ULL
#define INTR_VME_ACFAIL     0x200ULL
#define INTR_VME_ERROR      0x400ULL
#define INTR_VME_FAIL(n)   (INTR_VME_SYSFAIL<<(n)) /* n = 0...2 */
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
#define INTR_USER1_INTR(n) (INTR_USER1_INTR0<<(n)) /* n = 0...31 */
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
#define INTR_USER2_INTR(n) (INTR_USER2_INTR0<<(n)) /* n = 0...15 */
#define INTR_USER2_ANY      0xffff000000000000ULL

#define INTR_INDEX_TO_BIT(i) ((i)<32?INTR_USER1_INTR(i):(i)>=TOSCA_INTR_INDX_ERR(0)?INTR_VME_FAIL((i)-TOSCA_INTR_INDX_ERR(0)):INTR_VME_LVL(((i)-32)>>8)+1)
#define INTR_INDEX_TO_INUM(i) (i)<32?(i)&31:(i)>=TOSCA_INTR_INDX_ERR(0)?(i)-TOSCA_INTR_INDX_ERR(0):(((i)-32)>>8)+1;
#define INTR_INDEX_TO_IVEC(i) (i)<32||(i)>=TOSCA_INTR_INDX_ERR(0)?0:((i)-32)&255;

const char* toscaIntrBitToStr(intrmask_t intrmaskbit);
#define toscaIntrIndexToStr(i) toscaIntrBitToStr(INTR_INDEX_TO_BIT(i))

int toscaIntrConnectHandler(intrmask_t intrmask, unsigned int vec, void (*function)(), void* parameter);
#define toscaIntrConnectHandlerVME(vec, function, parameter) toscaIntrConnectHandler(INTR_VME_LVL_ANY, vec, function, parameter)
/* returns 0 on success */

int toscaIntrDisconnectHandler(intrmask_t intrmask, unsigned int vec, void (*function)(), void* parameter);
/* check parameter only if it is not NULL */
/* returns number of disconnected handlers */

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
    intrmask_t intrmaskbit;    /* one of the INTR_* bits above */
    unsigned int index;        /* 0...TOSCA_NUM_INTR-1, unique for each intr bit (and VME vector) */
    unsigned int vec;          /* 0...255 for intr bits in INTR_VME_LVL_ANY, else 0 */
    void (*function)();
    void *parameter;
    unsigned long long count;  /* number of times the interrupt has been received */
} toscaIntrHandlerInfo_t;

int toscaIntrForeachHandler(intrmask_t intrmask, unsigned int vec, int (*callback)(toscaIntrHandlerInfo_t));
/* calls callback for each installed handler that matches intrmask (and vec for VME) until a callback returns not 0 */
/* returns what the last callback had returned */

#endif
