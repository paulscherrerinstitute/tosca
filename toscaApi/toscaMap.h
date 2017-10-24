#ifndef toscaMap_h
#define toscaMap_h

#include <stdio.h>
#include <inttypes.h>

#include "memDisplay.h"

/* VME access modes */
#define VME_A16	          0x1
#define VME_A24	          0x2
#define	VME_A32	          0x4
#define VME_A64	          0x8
#define VME_CRCSR        0x10
#define TOSCA_USER       0x20
#define TOSCA_USER1      0x20
#define TOSCA_USER2      0x40
#define TOSCA_SMEM       0x80
#define TOSCA_SMEM1      0x80
#define TOSCA_SMEM2    0x2100 /* had to squeeze this in for ifc1211+ while staying backward compatible */
#define TOSCA_CSR       0x100
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

/* Report found Tosca devices */
unsigned int toscaNumDevices();
unsigned int toscaListDevices();
unsigned int toscaDeviceType(unsigned int device);

volatile void* toscaMap(unsigned int addrspace, uint64_t address, size_t size, uint64_t res_address);
/* Maps a Tosca resource to user space (or to VME SLAVE). */
/* Re-uses existing maps if possible. */

/* For addrspace use
   * for VME address spaces: VME_CRCSR, VME_A16, VME_A24, VME_A32, VME_A64 ( | VME_SUPER, VME_PROG)
   * for Tosca FPGA user blocks: TOSCA_USER1 (or TOSCA_USER), TOSCA_USER2
   * for Tosca shared memory: TOSCA_SMEM1 (or TOSCA_SMEM), TOSCA_SMEM2
   * for Tosca configuration space registers: TOSCA_CSR
   * for Tosca IO space registers: TOSCA_IO
   * for Tosca PON SRAM on ELB: TOSCA_SRAM
   * for VME A32 slave windows: VME_SLAVE|{TOSCA_USER1, TOSCA_USER2, TOSCA_SMEM} ( | VME_SWAP) and pass res_address
   If using more than one Tosca device, use addrspace|(device<<16).
   At the moment, Tosca does not support A64 but maybe one day?
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

toscaMapInfo_t toscaMapForeach(int(*func)(toscaMapInfo_t info, void *usr), void *usr);
/* Iterates over all maps (while func returns 0). */
/* Returns info of map for which func returned not 0. */

toscaMapInfo_t toscaMapFind(const volatile void* ptr);
/* Gets map info from a user space pointer. */

toscaMapAddr_t toscaMapLookupAddr(const volatile void* ptr);
/* Finds a VME address from a user space pointer. */

const char* toscaAddrSpaceToStr(unsigned int addrspace);
/* Converts addrspace code string and back. */

unsigned int toscaStrToAddrSpace(const char* str, char** end);
/* Convert string to addrspace code. */
/* Returns 0 and sets errno on error */
/* If end != NULL, passes first mismatch char, else mismatch is an error */

size_t toscaStrToSize(const char* str);
/* Converts (hex, dec, or "1M2k"-like) string to size. */
/* returns (size_t)-1 and sets errno on error */

toscaMapAddr_t toscaStrToAddr(const char* str, char** end);
/* Converts addrspace:address string to address structure. */
/* returns 0 addrspace and sets errno on error */
/* If end != NULL, passes first mismatch char, else mismatch is an error */

#ifdef __cplusplus
}
#endif

#endif
