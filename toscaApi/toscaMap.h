#ifndef toscaMap_h
#define toscaMap_h

#include <stdio.h>
#include <inttypes.h>

#include "memDisplay.h"

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

/* Set to 1 to see debug output */
extern int toscaMapDebug;

/* Set to redirect debug output  */
extern FILE* toscaMapDebugFile;

/* Report number of found Tosca devices */
unsigned int toscaNumDevices();

/* Map a Tosca resource to user space. Re-use maps if possible. */
volatile void* toscaMap(unsigned int addrspace, uint64_t address, size_t size, uint64_t res_address);

/* For addrspace use
   * for VME address spaces A16, A24, A32, A64: VME_A16, VME_A24, VME_A32, VME_A64 ( | VME_SUPER, VME_PROG)
   * for VME CR/CSR addres space: VME_CRCSR
   * for Tosca FPGA USER1, USER2: TOSCA_USER1 (or TOSCA_USER), TOSCA_USER2
   * for Tosca shared memory: TOSCA_SMEM
   * for Tosca configuration space registers: TOSCA_CSR
   * for Tosca IO space registers: TOSCA_IO
   * for Tosca PON SRAM on ELB: TOSCA_SRAM
   * for VME A32 slave windows: VME_SLAVE|{TOSCA_USER1, TOSCA_USER2, TOSCA_SMEM} ( | VME_SWAP) and pass res_address
   * if using more than one Tosca device, use addrspace|(device<<16).
   At the moment Tosca does not support A64 but one day?
*/

/* Several map lookup functions. addrspace will be 0 if map is not found. */
typedef struct {
    uint64_t baseaddress;
    volatile void* baseptr;
    size_t size;
    unsigned int addrspace;
} toscaMapInfo_t;

typedef struct {
    uint64_t address;
    unsigned int addrspace;
} toscaMapAddr_t;

/* Iterate over all maps (while func returns 0). */
/* Returns info of map for which func returned not 0 */
toscaMapInfo_t toscaMapForeach(int(*func)(toscaMapInfo_t info, void *usr), void *usr);

/* Get map info from a user space pointer. */
toscaMapInfo_t toscaMapFind(const volatile void* ptr);

/* Find a VME address from a user space pointer. */
toscaMapAddr_t toscaMapLookupAddr(const volatile void* ptr);

/* Convert addrspace code string and back. */
const char* toscaAddrSpaceToStr(unsigned int addrspace);

/* Convert string to addrspace code. */
/* Returns 0 and sets errno on error */
/* If end != NULL, passes first mismatch char, else mismatch is an error */
unsigned int toscaStrToAddrSpace(const char* str, char** end);

/* Convert (hex, dec, or 1M2k like) string to size. */
/* returns (size_t)-1 and sets errno on error */
size_t toscaStrToSize(const char* str);

/* Convert addrspace:address string to address structure. */
/* returns 0 addrspace and sets errno on error */
/* If end != NULL, passes first mismatch char, else mismatch is an error */
toscaMapAddr_t toscaStrToAddr(const char* str, char** end);

#ifdef __cplusplus
}
#endif

#endif