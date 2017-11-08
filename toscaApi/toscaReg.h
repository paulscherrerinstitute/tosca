#ifndef toscaReg_h
#define toscaReg_h

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* set to 1 to see debug output */
extern int toscaRegDebug;

/* set to redirect debug output  */
extern FILE* toscaRegDebugFile;

/* TOSCA CSR ACCESS */

/* Access to the configuration space registers (CSR) of TOSCA.
   All values use 32 bit access and are automaticaly converted
   from or to little endian if host byte order differs.
   Address should be a multiple of 4.
   On success these functions return a read back value, which may be different
   from the value set if some or all bits of the register are read-only.
   On error they set errno and return (unsigned int)-1.
   An invalid address sets errno to EINVAL.
   Other errors may come from open() and mmap() on first use.
   Be aware that (unsigned int)-1 can be a valid result.
   The functions set errno to 0 on success.
   Caution: Setting inappropriate registers or using invalid addresses may crash the system!
   If using more than one Tosca device, use (first argument)|(device<<16).
*/
unsigned int toscaCsrRead(unsigned int address);
unsigned int toscaCsrWrite(unsigned int address, unsigned int value);
unsigned int toscaCsrSet(unsigned int address, unsigned int value);
unsigned int toscaCsrClear(unsigned int address, unsigned int value);

/* The same for TOSCA IO Registers */
unsigned int toscaIoRead(unsigned int address);
unsigned int toscaIoWrite(unsigned int address, unsigned int value);
unsigned int toscaIoSet(unsigned int address, unsigned int value);
unsigned int toscaIoClear(unsigned int address, unsigned int value);

/* And the same generic for any mapable address space. */
unsigned int toscaRead(unsigned int addrspace, unsigned int address);
unsigned int toscaWrite(unsigned int addrspace, unsigned int address, unsigned int value);
unsigned int toscaSet(unsigned int addrspace, unsigned int address, unsigned int value);
unsigned int toscaClear(unsigned int addrspace, unsigned int address, unsigned int value);

/* Access to Virtex-6 System Monitor via toscaCsr */
/* Address range is 0x00 to 0x7c but only addresses from 0x40 on are writable. */
unsigned int toscaSmonRead(unsigned int address);
unsigned int toscaSmonWrite(unsigned int address, unsigned int value);
unsigned int toscaSmonWriteMasked(unsigned int address, unsigned int mask, unsigned int value);
unsigned int toscaSmonSet(unsigned int address, unsigned int value);
unsigned int toscaSmonClear(unsigned int address, unsigned int value);

/* If you prefer to access Tosca CSR or IO directly using
   toscaMap instead of using functions above,
   be aware that all registers are little endian.
   Use htole32() for writing and le32toh() for reading.
*/

/* Access to PON registers via ELB */
const char* toscaPonAddrToRegname(unsigned int address);
unsigned int toscaPonRead(unsigned int address);
unsigned int toscaPonWrite(unsigned int address, unsigned int value);
unsigned int toscaPonWriteMasked(unsigned int address, unsigned int mask, unsigned int value);
unsigned int toscaPonSet(unsigned int address, unsigned int value);
unsigned int toscaPonClear(unsigned int address, unsigned int value);

/* Read (and clear) VME error status. Error is latched and not overwritten until read. */
typedef struct {
    uint64_t address;         /* Lowest two bits are always 0. */
    union {
     unsigned int status:32;
     struct {
      unsigned int err:1;     /* Error has happened since last readout. */
      unsigned int over:1;    /* [overflow, not implemented] */
      unsigned int write:1;   /* Error was on write access. */
      unsigned int timeout:1; /* Error was a bus timeout */
      unsigned int source:2;  /* 0=PCIe 2=IDMA 3=USER */
      unsigned int id:17;     /* [What is this?] */
      unsigned int length:5;  /* [In words, for block transfer modes ?] */
      unsigned int mode:4;    /* 0=CRCSR 1=A16 2=A24 3=A32 4=BLT 5=MBLT 6=2eVME 8=2eSST160 9=2eSST267 10=2eSST320 15=IACK */
     };
   };
} toscaMapVmeErr_t;
toscaMapVmeErr_t toscaGetVmeErr(unsigned int device);

#ifdef __cplusplus
}
#endif

#endif
