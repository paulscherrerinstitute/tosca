#ifndef toscaMap_h
#define toscaMap_h

#include <stdint.h>
#include <stdio.h>

/* VME access modes from vme.h */
#define VME_A16	          0x1
#define VME_A24	          0x2
#define	VME_A32	          0x4
#define VME_A64	          0x8
#define VME_CRCSR        0x10
#define VME_USER1        0x20
#define VME_USER2        0x40
#define VME_USER3        0x80
#define VME_USER4       0x100
#define TOSCA_USER  VME_USER1
#define TOSCA_USER1 VME_USER1
#define TOSCA_USER2 VME_USER2
#define TOSCA_SMEM  VME_USER3
#define TOSCA_CSR   VME_USER4
#define TOSCA_IO        0x200
#define TOSCA_SRAM      0x400
#define VME_SLAVE       0x800
#define	VME_SUPER      0x1000
#define	VME_PROG       0x4000
#define	VME_SWAP       0x8000

#ifdef __cplusplus
extern "C" {
#endif

/* set to 1 to see debug output */
extern int toscaMapDebug;

/* set to redirect debug output  */
extern FILE* toscaMapDebugFile;

/* One day we may have A64 in Tosca */
typedef uint64_t vmeaddr_t;

/* Map a Tosca resource to user space. Re-use maps if possible. */
volatile void* toscaMap(unsigned int aspace, vmeaddr_t address, size_t size, vmeaddr_t res_address);

/* For aspace use
   * for VME address spaces A16, A24, A32, A64: VME_A16, VME_A24, VME_A32, VME_A64 ( | VME_SUPER, VME_PROG)
   * for VME CR/CSR addres space: VME_CRCSR
   * for Tosca FPGA USER1, USER2: TOSCA_USER1 (or TOSCA_USER), TOSCA_USER2
   * for Tosca shared memory: TOSCA_SMEM
   * for Tosca configuration space registers: TOSCA_CSR
   * for Tosca IO space registers: TOSCA_IO
   * for Tosca PON SRAM on ELB: TOSCA_SRAM
   * for VME A32 slave windows: VME_SLAVE|{TOSCA_USER1, TOSCA_USER2, TOSCA_SMEM} ( | VME_SWAP) and pass res_address
   * if using more than one Tosca, use aspace|(tosca<<16).
   At the moment Tosca does not support A64.
*/

/* Convert aspace code to string. */
const char* toscaAddrSpaceToStr(unsigned int aspace);

/* Several map lookup functions. aspace will be 0 if map is not found. */
typedef struct {
    unsigned int aspace;
    vmeaddr_t baseaddress;
    size_t size;
    volatile void* baseptr;
} toscaMapInfo_t;

typedef struct {
    unsigned int aspace;
    vmeaddr_t address;
} toscaMapAddr_t;

/* Iterate over all maps (while func returns 0). */
/* Returns info of map for which func returned not 0 */
toscaMapInfo_t toscaMapForeach(int(*func)(toscaMapInfo_t info, void *usr), void *usr);

/* Get map info from a user space pointer. */
toscaMapInfo_t toscaMapFind(const volatile void* ptr);

/* Find a VME address from a user space pointer. */
toscaMapAddr_t toscaMapLookupAddr(const volatile void* ptr);

/* Show all maps to out or stdout */
int toscaMapPrintInfo(toscaMapInfo_t info, FILE* file);
void toscaMapShow(FILE* out);

/* Read (and clear) VME error status. Error is latched and not overwritten until read. */
typedef struct {
    unsigned int address:32;  /* Lower 2 bits are swap control (whatever that is). */
    union {
     unsigned int status:32;
     struct {
      unsigned int err:1;     /* Error has happened since last readout. */
      unsigned int over:1;    /* [not implemented] */
      unsigned int write:1;   /* Error was on write access. */
      unsigned int timeout:1; /* Error was a bus timeout */
      unsigned int source:2;  /* 0=PCIe 2=IDMA 3=USER */
      unsigned int id:17;     /* [What is this?] */
      unsigned int length:5;  /* [In words? For block transfer modes?] */
      unsigned int mode:4;    /* 0=CRCSR 1=A16 2=A24 3=A32 4=BLT 5=MBLT 6=2eVME 7=2eSST 15=IACK */
     };
   };
} toscaMapVmeErr_t;
toscaMapVmeErr_t toscaGetVmeErr(unsigned int tosca);

/* TOSCA CSR ACCESS */

/* Access to the configuration space registers (CSR) of the local TOSCA.
   Values are automatically converted to and from host byte order.
   Address should be a multiple of 4.
   On error these functions set errno and return -1 or 0xffffffff, respectively.
   An invalid address sets errno to EINVAL. Other errors may come from open() and mmap() on first use.
   Be aware that 0xffffffff can be a valid result of toscaCsrRead. First clear and then check errno.
   If using more than one Tosca, use address|(tosca<<16).
*/
uint32_t toscaCsrRead(unsigned int address);
int toscaCsrWrite(unsigned int address, uint32_t value);  /* Write new value. */
int toscaCsrSet(unsigned int address, uint32_t value);    /* Set bits in value, leave others unchanged. */
int toscaCsrClear(unsigned int address, uint32_t value);  /* Clear bits in value, leave others unchanged. */

/* The same for TOSCA IO Registers */
uint32_t toscaIoRead(unsigned int address);
int toscaIoWrite(unsigned int address, uint32_t value);   /* Write new value. */
int toscaIoSet(unsigned int address, uint32_t value);     /* Set bits in value, leave others unchanged. */
int toscaIoClear(unsigned int address, uint32_t value);   /* Clear bits in value, leave others unchanged. */

/* Access to Virtex-6 System Monitor via toscaCsr */
uint16_t toscaSmonRead(unsigned int address);
int toscaSmonWrite(unsigned int address, uint16_t value);
uint32_t toscaSmonStatus();

/* If you prefer to access Tosca CSR or IO directly using
   toscaMap instead of using functions above,
   be aware that all registers are little endian.
   Use htole32() for writing and le32toh() for reading.
*/

/* Some utilities */
char* toscaSizeToStr(vmeaddr_t size, char* str);
#define SIZE_STRING_BUFFER_SIZE 60

#ifdef __cplusplus
}
#endif

#endif
