#ifndef toscaReg_h
#define toscaReg_h

#ifdef __cplusplus
extern "C" {
#endif

/* TOSCA CSR ACCESS */

/* Access to the configuration space registers (CSR) of the local TOSCA.
   Values are automatically converted to and from host byte order.
   Address should be a multiple of 4.
   On error these functions set errno and return -1 or 0xffffffff, respectively.
   An invalid address sets errno to EINVAL. Other errors may come from open() and mmap() on first use.
   Be aware that 0xffffffff can be a valid result of toscaCsrRead. First clear and then check errno.
   If using more than one Tosca, use address|(tosca<<16).
*/
unsigned int toscaCsrRead(unsigned int address);
unsigned int toscaCsrWrite(unsigned int address, unsigned int value);  /* Write new value. */
unsigned int toscaCsrSet(unsigned int address, unsigned int value);    /* Set bits in value, leave others unchanged. */
unsigned int toscaCsrClear(unsigned int address, unsigned int value);  /* Clear bits in value, leave others unchanged. */

/* The same for TOSCA IO Registers */
unsigned int toscaIoRead(unsigned int address);
unsigned int toscaIoWrite(unsigned int address, unsigned int value);   /* Write new value. */
unsigned int toscaIoSet(unsigned int address, unsigned int value);     /* Set bits in value, leave others unchanged. */
unsigned int toscaIoClear(unsigned int address, unsigned int value);   /* Clear bits in value, leave others unchanged. */

/* Access to Virtex-6 System Monitor via toscaCsr */
unsigned int toscaSmonRead(unsigned int address);
unsigned int toscaSmonWrite(unsigned int address, unsigned int value);
unsigned int toscaSmonStatus();

/* If you prefer to access Tosca CSR or IO directly using
   toscaMap instead of using functions above,
   be aware that all registers are little endian.
   Use htole32() for writing and le32toh() for reading.
*/

/* Access to PON registers via ELB */
const char* toscaPonAddrToRegname(unsigned int address);
unsigned int toscaPonRead(unsigned int address);
unsigned int toscaPonWrite(unsigned int address, unsigned int value);

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

#ifdef __cplusplus
}
#endif

#endif
