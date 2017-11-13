# Tosca Driver

## Purpose of the driver

This EPICS driver gives access to the resources of the "Tosca" FPGA
framework on IOxOS boards like the IFC1210, IFC1211 or IFC1410.
It interfaces the "tosca" Linux kernel driver written by Denx.
It is incompatible with the alternative kernel drivers "pev" and "althea"
by IOxOS for the same boards!

The software is divided into parts: A non-EPICS library provides a
[C API](#c-api) to the Tosca resources.
Any program can use this library without the need for EPICS.

But several EPICS interfaces also use this API:
[IOC shell functions](#ioc-shell-functions) mainly for debugging,
a [devLibVME interface](#devlibvme-interface) for EPICS drivers using the
standard VME interface,
and a [regDev interface](#regdev-interface) to access all Tosca resources
(not only VME) in a generic way and to use DMA where possible.

It is assumed the PSI build and load system for EPICS modules is used.
If a different system is used, a different Makefile than the provided
GNUmakefile must be written and loading the resulting library may be done
differently than described here.

## Supported hardware

The API and EPICS driver have been developed for and tested with the IOxOS
IFC1210, IFC1211 and IFC1410 boards but may also work with other IOxOS
products using the Tosca framework and kernel driver.
It is required to use Tosca Central and Pon FPGA firmware that is
compatible with the "tosca" kernel driver.
In particular, the kernel driver must show the Tosca device in
`/sys/bus/pci/drivers/tosca/`.

It is possible to handle multiple Tosca devices in the same system.
One example is the IFC1211 where the VME interface is on device 0 while
the USER FPGA interface is on device 1.
Also connecting multiple IFC boards with PCIe and running the second as
a slave of the first should be possible but this has never been tested.

## External software dependencies

This software requires the following external software modules:

* Tosca kernel driver
* [symbolname](https://github.com/paulscherrerinstitute/symbolname)
* [memDisplay](https://github.com/paulscherrerinstitute/memDisplay)
* [sysfs](https://github.com/paulscherrerinstitute/sysfs)
* [EPICS base](http://www.aps.anl.gov/epics/base) release 3.14.12 or higher, when built with EPICS support.
* [keypress](https://github.com/paulscherrerinstitute/keypress), when built with EPICS iocsh support.
* [regDev](https://github.com/paulscherrerinstitute/regDev), when built with EPICS register device support.
* [i2cDev](https://github.com/paulscherrerinstitute/i2cDev), when build with pev compatibility.
* pev header files, when build with pev compatibility.

## License

This software is published under the terms of the
GNU General Public License version 3 or any later version.
See [https://www.gnu.org/licenses/].

## C API

The C API provides access to the following Tosca resources:

* [Memory maps](#memory-maps) of several Tosca resources.
  * Tosca **USER1** and **USER2** blocks in the Central FPGA
  * Tosca **SMEM1** and **SMEM2** shared memory
  * Tosca **TCSR** and **TIO** configuration space registers
  * Tosca **SRAM** through PON FPGA and ELB
  * VME master in **A16**, **A24**, **A32**, **A64** and **CRCSR** address space (Tosca currently does not support **A64**.)
  * VME **SLAVE** in with mapping to memory, USER, SMEM (Tosca currently supports slave windows only on **A32** address space.)
* [Register access functions](#register-access-functions) which read or
  write registers on tosca resources as 32 bit words and take care of byte
  swapping and atomic access where necessary.
  * TCSR and TIO Tosca configuration registers
  * PON FPGA registers through ELB
  * Generic for TCSR, TIO, USER, SMEM ...
  * SMON Virtex FPGA system monitor registers through TCSR
* [DMA transfers](#dma-transfers).
  * VME A32 <-> RAM, SMEM, USER, VME A32
    * Single 32 bit transfers
    * BLT
    * MBLT
    * 2eVME
    * 2eSST (160, 267, 320)
  * USER, SMEM <-> RAM
  * USER <-> SMEM
* [Interrupt handler](#interrupt-handling) connection for several interrupt sources.
  * USER1 and USER2 (16 interrupt lines each)
  * VME (7 levels with 256 vectors)
  * VME errors (sys fail, ac fail, bus error)
* [Interrupt generation](#interrupt-generation) on VME.

Currently implemented Tosca resources depending on the board type:

* IFC1210: SMEM1, USER1, VME, TCSR, TIO, SRAM
* IFC1211 VME, SMEM1, TCSR, TIO, SRAM, 1:USER1, 1:USER2, 1:SMEM1, 1:SMEM2, 1:TCSR, 1:TIO
* IFC1410: TCSR, TIO, USER1, USER2, SMEM1, SMEM2, SRAM

I²C devices on the board can be handled by the standard Linux I²C support
but see also the [simple I²C API](#ic-bus-access) described later.

For debugging purposes, the API functions are also available from the
EPICS [IOC shell](#ioc-shell-functions).

### Using the Tosca API without EPICS

To build your own non-EPICS C/C++ programs with the Tosca API, some
compiler and linker flags are required in order to find the API library
and header files.
A program can use either the static or the shared API library.
For convenience a single header file `toscaApi.h` can be included which
in turn includes all other required header files.

Preprocessor flags:

```C
-I $PATH_TO_IFC_ROOT_DIRECTORY/usr/local/include
```

Shared library linker flags:

```C
-L $PATH_TO_IFC_ROOT_DIRECTORY/usr/local/lib/ -l toscaApi -Wl,-rpath,/usr/local/lib
```

Static library linker flags:

```C
$PATH_TO_IFC_ROOT_DIRECTORY/usr/local/lib/libtoscaAPI.a -lpthread -lrt -ldl
```

Header file:

```C
#include "toscaApi.h"
```

Hint for printing Tosca addresses: The Tosca API uses 64 bit values for
addresses. The underlying C data type depends on the CPU architecture
(32 or 64 bit). This makes it a bit inconvenient to find the correct
_printf()_ converter (`"%x"` or `"%lx"` or `"%llx"`).
Best use the converter macros from _inttypes.h_, e.g `"%"PRIx64`.

### Discover available Tosca devices

```C
unsigned int toscaNumDevices(void);
```

Returns the number of available Tosca devices.
The devices are numbered 0 ... _toscaNumDevices()_ - 1 in the order they
appear in a directory listing of `/sys/bus/pci/drivers/tosca/`.

```C
unsigned int toscaDeviceType(unsigned int device);
```

Returns the PCI device type of the given Tosca device.
One finds the the following:

* IFC1210: toscaDeviceType(0) = 0x1210
* IFC1211: toscaDeviceType(0) = 0x1211, toscaDeviceType(1) = 0x1001
* IFC1410: toscaDeviceType(0) = 0x1001

```C
unsigned int toscaListDevices();
```

Prints information about the found Tosca devices to stdout.

### Memory maps

```C
volatile void* toscaMap(unsigned int addrspace, unit64_t address, size_t size, uint64_t res_address);
volatile void* toscaMapMaster(unsigned int addrspace, unit64_t address, size_t size);
```

This function creates a new or re-uses an existing map of a master or
slave window to a Tosca resource.
It will try to map at least `size` bytes starting at `address` in the
selected address space `addrspace`.
If a matching map already exists, it will be re-used. Thus the function
can safely be called multiple times without wasting resources.
But to keep this simple there is no unmap.
The maps and associated kernel and Tosca resources are released
automatically when the program terminates.

For `addrspace` use one of `VME_A16`, `VME_A24`, `VME_A32`,
`VME_A64`, `VME_CRCSR`,  `TOSCA_USER1`, `TOSCA_USER2`,
`TOSCA_SMEM1`, `TOSCA_SMEM2`, `TOSCA_CSR`, `TOSCA_IO`, or
`TOSCA_SRAM`.
For VME, `addrspace` can be combined (bitwise _or_) with
`|VME_SUPER` for "supervisory" access and/or
`|VME_PROG` for "program" access.

In case more than one Tosca device is available, use
`(addrspace)|(device<<16)`.

If `addrspace` is invalid, the behavior is undefined
(may fail or giveyou a map you did not expect).

Not all address spaces are supported by all Tosca implementations.
At the moment none implements `VME_A64`.
The IFC1210 does not implement `TOSCA_USER2` or `TOSCA_SMEM2`.
The IFC1211 implements VME only on device 0 and USER only on device 1.
Also `TOSCA_SMEM2` on device 0 is not accessible.
The IFC1410 does not implement VME.
The function will fail and return `NULL` if you try an unsupported map.

#### Master maps

Master maps give access to Tosca resources like VME, USER or SMEM to the
program. They are mapped into user space and are accessible through the
returned pointer much like local memory.
Due to the granularity of Tosca mapping windows, the map actually created
may be larger than requested in both directions.
It may start at a lower address than `address` and may cover higher
addresses than `address+size`.
It is not necessary to call the function with aligned `address` or
`size` to the Tosca window granularity.

In any case the returned pointer corresponds to the requested `address`,
even if the actual mapping window starts at an earlier address.
Do not access any location before the returned pointer or after and
including the returned pointer + `size`.

If the request cannot be fulfilled, the function returns `NULL` and
sets `errno`.

Master maps no not use the `res_address` argument.
Pass `NULL` or use the macro _toscaMapMaster()_ which does exactly this.

Be aware that Tosca resources may use byte orders different from the CPU
and data may only be meaningful with correct data width and alignment.
In particular TCSR and TIO registers are 4 byte aligned little endian
while VME uses big endian and allows 1, 2, 4, or 8 byte access.
Use _htole32()_ or _le32toh()_ (from endian.h) where necessary.
The [register access functions](#register-access-functions) described below
take care of this.

#### VME SLAVE maps

Tosca supports SLAVE maps that expose program memory or another Tosca
resource like USER or SMEM on the VME bus.
This makes local resources accessible by other VME cards.
Of course VME SLAVE maps are only possible on Tosca devices that
support VME.

To use SLAVE maps set `addrspace` to a combination (bitwise _or_) of
`VME_SLAVE`, one of the VME address spaces `VME_A*` and, if not
mapping to memory, another Tosca resource like `TOSCA_USER1`.
If combined with `VME_SWAP` the data will be swapped when accessed from
VME according to the access data width.
This is probably useful for mapping USER to VME because the registers on
USER are likely to be in little endian byte order while VME uses big
endian byte order according to the VME standard.
The byte order of SMEM data depends on the application.

The current Tosca implementation supports VME SLAVE maps only on
`VME_A32` address space and can only map memory or the USER and SMEM
resources. 
For convenience, `VME_A32` is implicit if no VME address space is given
with `VME_SLAVE`.

Even though Tosca supports multiple VME SLAVE maps, all must lie in the
same 512 MB of the VME address range.
The kernel driver currently only supports the first 512 MB.
All VME SLAVE maps have a granularity of 1 MB and different SLAVE maps
cannot overlap.
Thus it is not possible to allocate less than one MB in the A32 address
space or to map two different resources to the same MB.

If mapping a VME SLAVE to USER or SMEM, the address on that resource is
needed in addition to the VME address.
Set `res_address` to the USER or SMEM address which should be mapped
to `address` on VME.
Both must have the same offset from 1 MB alignment, which means that the
lower 20 bits must be identical in `address` and `res_address`
(for example 0).

In this case the returned value is  NULL even on success, because nothing
is mapped to memory and thus there is no pointer to return.
Use `errno` to test for errors, that means set `errno` to 0 before
calling _toscaMap()_ and check for a non-zero value immediately afterwards.

To map program memory to VME set `addrspace` to `VME_SLAVE|VME_A32`
and pass `NULL` for `res_address`.
The current IFC boards use big endian CPUs, thus one should not set
`VME_SWAP`.
This would be different if a Tosca device is accessed by a little endian
CPU.

The function returns a pointer to memory visible on the VME bus.
A maximum of 4 MB (after alignment to full MBs) can be mapped this way
because the Linux kernel does not allow to allocate more linear address
space.

#### Map error codes

If mapping fails, _toscaMap()_ returns `NULL` and sets `errno` to one of
the following values:

* `ENODEV` Tosca device not found
* `EFAULT` address or size out of range
* `EINVAL` invalid address space
* `EADDRINUSE` overlap with existing VME SLAVE window
* `EACCES` no permission to read and write Tosca device
* `ENOMEM`, `EMFILE`, `ENFILE`, `EAGAIN` insufficient system resources

#### Map lookup functions

```C
toscaMapInfo_t toscaMapForEach(int(*func)(toscaMapInfo_t info, void *usr), void *usr);
toscaMapInfo_t toscaMapFind(const volatile void* ptr);
toscaMapAddr_t toscaMapLookupAddr(const volatile void* ptr);
```

The _toscaMapForEach()_ function calls a user specified callback function
for each installed map until the callback returns a value other than 0.
The result describes the map for which the callback returned the non 0
value.

The `toscaMapInfo_t` type is a structure with the following fields
(in unspecified order):

```C
unsigned int addrspace;
uint64_t baseaddress;
size_t size;
volatile void* baseptr;
```

The _toscaMapFind()_ function returns the description of the map in which the
passed `ptr` lies. (It uses _toscaMapForEach()_.)
The _toscaMapLookupAddr()_ function translates the passed `ptr` to the
following structure describing to which Tosca resource and address the
pointer refers. (It uses _toscaMapFind()_.)

The `toscaMapAddr_t` type is a structure with the following fields
(in unspecified order):

```C
unsigned int addrspace;
uint64_t address;
```

All three functions set `addrspace` to 0 if no match is found.
These functions are not necessarily quick because they have to check each
installed map.

**Debugging:** The global variable `toscaMapDebug` can be set to enable
debug output, either to stderr or to `toscaMapDebugFile` if that global
`FILE*` variable is set.

### Register access functions

```C
unsigned int toscaRead(unsigned int addrspace, unsigned int address);
unsigned int toscaWrite(unsigned int addrspace, unsigned int address, unsigned int value);
unsigned int toscaSet(unsigned int addrspace, unsigned int address, unsigned int bitsToSet);
unsigned int toscaClear(unsigned int addrspace, unsigned int address, unsigned int bitsToClear);

unsigned int toscaCsrRead(unsigned int address);
unsigned int toscaCsrWrite(unsigned int address, unsigned int value);
unsigned int toscaCsrSet(unsigned int address, unsigned int bitsToSet);
unsigned int toscaCsrClear(unsigned int address, unsigned int bitsToClear);

unsigned int toscaIoRead(unsigned int address);
unsigned int toscaIoWrite(unsigned int address, unsigned int value);
unsigned int toscaIoSet(unsigned int address, unsigned int bitsToSet);
unsigned int toscaIoClear(unsigned int address, unsigned int bitsToClear);

unsigned int toscaSmonRead(unsigned int address);
unsigned int toscaSmonWrite(unsigned int address, unsigned int value);
unsigned int toscaSmonSet(unsigned int address, unsigned int bitsToSet);
unsigned int toscaSmonClear(unsigned int address, unsigned int bitsToClear);

unsigned int toscaPonRead(unsigned int address);
unsigned int toscaPonWrite(unsigned int address, unsigned int value);
unsigned int toscaPonSet(unsigned int address, unsigned int bitsToSet);
unsigned int toscaPonClear(unsigned int address, unsigned int bitsToClear);

toscaMapVmeErr_t toscaGetVmeErr(unsigned int device);
```

This is a set of convenience functions to access registers on Tosca
resources.
Internally they use the [memory maps](#memory-maps) described above but
simplify the usage.
They assume 32 bit registers and little endian byte order on the Tosca
resources.
The passed `address` should be a multiple of 4.
They take care of swapping from and to host byte order if necessary.

The specific _toscaCsr*()_ and _toscaIo*()_ functions are simply shortcuts
for using `TOSCA_CSR` or `TOSCA_IO` as `addrspace` in the generic
functions.
The generic functions can be used as well with USER or SMEM address spaces.

The _tosca_Smon*()_ and _toscaPon*()_ functions are more specific and the
generic functions cannot be used instead.
The _toscaSmon*()_ functions access the Virtex (Central) FPGA system monitor
registers through TCSR and the _toscaPon*_ functions access PON FPGA
registers over the processor local bus.
The `address` range of the Smon registers is limited to `0x00` to
`0x7c` and only registers above `0x40` are writable.
The `address` range of the PON registers is limited to `0x00` to
`0x24` plus `0x40`.

In case more than one Tosca device is available, combine (bitwise _or_)
the first argument with `device<<16`. Not all resources are available on
all devices.

The _*Set()_ and _*Clear()_ functions atomically set the given bits to 1
or 0 respectively and leave the other bits untouched.
The _*Write()_, _*Set()_, and _*Clear()_ functions return the new register
value, which may be different from the value written due to read-only bits
in the register.

In case of errors these functions return `(unsigned int)-1` and set
`errno`. On success they set `errno` to 0.

The function _toscaGetVmeErr()_ returns and re-arms the VME Error registers
in the TCSR address space.
These registers store address and status of the first VME error after it
had been re-armed.

The function returns the following structure:

```C
typedef struct {
    uint64_t address;         /* Lowest two bits are always 0. */
    union {
     unsigned int status:32;
     struct {
      unsigned int err:1;     /* Error has happened since last readout. */
      unsigned int over:1;    /* [multiple errors, not implemented] */
      unsigned int write:1;   /* Error was on write access. */
      unsigned int timeout:1; /* Error was a bus timeout */
      unsigned int source:2;  /* 0=PCIe 2=IDMA 3=USER */
      unsigned int id:17;     /* [What is this?] */
      unsigned int length:5;  /* [In words, for block transfer modes ?] */
      unsigned int mode:4;    /* 0=CRCSR 1=A16 2=A24 3=A32 4=BLT 5=MBLT
                      6=2eVME 8=2eSST160 9=2eSST267 10=2eSST320 15=IACK */
     };
   };
} toscaMapVmeErr_t;
```

**Debugging:** The global variable `toscaRegDebug` can be set to enable
debug output, either to stderr or to `toscaRegDebugFile` if that global
`FILE*` variable is set.

**Pev compatibility note:** When converting from pev functions
_pev_csr_rd()_ and _pev_csr_wr()_ to Tosca functions, be aware that the pev
functions could access both, the TIO and TCSR address space and used the
highest address bit (`0x80000000`) to select TCSR address space while in
Tosca different maps are used.
Thus if you had addresses `x > 0x8000000` change to _toscaCsr*()_
functions with address `x - 0x80000000` while with addresses
`x < 0x8000000` change to _toscaIo*()_ functions using address `x`.
For example, for reading user blocks TCSR registers, instead of using
`pev_csr_rd(0x80001000+offset)` one should use
`toscaCsrRead(0x1000+offset)`.

### DMA transfers

```C
typedef void (*toscaDmaCallback)(void* usr, int status);
int toscaDmaTransfer(unsigned int source, uint64_t source_addr,
         unsigned int dest, uint64_t dest_addr,
         size_t size, unsigned int swap, int timeout,
         toscaDmaCallback callback, void* user);
int toscaDmaWrite(void* source_addr,
         unsigned int dest, uint64_t dest_addr,
         size_t size, unsigned int swap, int timeout,
         toscaDmaCallback callback, void* user);
int toscaDmaRead(unsigned int source, uint64_t source_addr,
         void* dest_addr,
         size_t size, unsigned int swap, int timeout,
         toscaDmaCallback callback, void* user);
```

These functions perform a DMA transfer of `size` bytes from address
space `source` address `source_addr` to address space `dest` address
`dest_addr`.
All addresses and `size` must be multiples of 4.
The choices for `source` and `dest` are `0` (memory),
`TOSCA_USER1`, `TOSCA_USER2`, `TOSCA_SMEM2`, `TOSCA_SMEM2`, or one
of the VME block transfer modes `VME_SCT` (A32 single 32 bit transfers),
`VME_BLT`, `VME_MBLT`, `VME_2eVME`, `VME_2eSST160`,
`VME_2eSST267`, or `VME_2eSST320`.
Not every combination of `source` and `dest` is valid.
At least one TOSCA resource must be involved, thus memory to memory is not
supported.
But Tosca resource to Tosca resource is possible, e.g USER to SMEM.
When transferring from VME to VME both must use the same block transfer
mode.

For convenience the two functions _toscaDmaWrite()_ and _toscaDmaRead()_
are provided to transfer from and to memory.
These functions call _toscaDmaTransfer()_.
No special DMA enabled memory is required but page aligned memory
(e.g. allocated with _valloc()_) increases efficiency a bit.
The Tosca Linux kernel driver takes care of physically fragmented virtual
memory and of page locking.

If the `swap` parameter is 2, 4, or 8, the data is `swap` byte wise
swapped during transfer, thus allowing to convert between big and little
endian resources.

A positive `timeout` in milliseconds limits the time the system waits
until a DMA channel becomes available and may result in a timeout error.
Once a DMA transfer has actually started it will not be interrupted by
timeout.
If `timeout` is negative, the function can block indefinitely if all
DMA channels are reserved and unavailable. On IFC1210, timeouts are not
supported at the moment.

If `callback` is `NULL`, the function blocks until the DMA transfer
has completed or times out and returns 0 on success or an error status.
Otherwise the function immediately returns 0 on success or an error code
and (if 0 was returned) starts the DMA in a
[worker thread](#dma-worker-thread).
It calls the `callback` function with parameter `user` and the error
status when the DMA has completed (or timed out) in the context of the
DMA worker thread with the usual multi threading implications:
The `callback` function is not allowed to block for arbitrary long time.
Keep mutual exclusion in mind becasue the callback function may interrupt
other threads at any time.

**Debugging:** The global variable `toscaDmaDebug` can be set to enable
debug output, either to stderr or to `toscaDmaDebugFile` if that global
`FILE*` variable is set.


#### DMA error codes

* `EINVAL` Invalid combination of `source` and `dest`
* `ETIMEDOUT` Transfer timed out
* `ENOMEM` Out of memory
* `EACCES` No permission to use DMA device `/dev/dmaproxy*`
* `ENOENT` DMA device not found

#### DMA worker thread

```C
void* toscaDmaLoop();
int toscaDmaLoopsRunning(void);
void toscaDmaLoopsStop();
```

Asynchronous DMA transfers are executed in the context of a DMA worker
thread executing the _toscaDmaLoop()_ function.
This function blocks until a DMA transfers become pending (i.e. the user
called [_toscaDmaTransfer()_](#dma-transfers) with a callback function).
It starts the DMA and waits for completion and then calls the callback
function (which should not block indefinitely).
Then it starts over waiting for the next asynchronous transfer.
The `void*` result is only provided for compatibility with
_pthread_create()_. The function only terminates when _toscaDmaLoopsStop()_
is called and then always returns `NULL`.

**The user is responsible for starting one or more  DMA worker threads
which execute _toscaDmaLoop()_.**
This allows application specific choices for thread parameters like
priority and stack size.
The stack must be sufficient for any DMA transfer callback function.
Starting multiple DMA worker threads may improve throughput because the
IFC1210 has and IFC1211 and IFC1410 have four DMA channels which can work
in parallel.
The EPICS interface starts two four DMA worker threads on an IFC1210 and
four otherwise.

The _toscaDmaLoopsRunning()_ function can be used to test how many worker
threads are running and _toscaDmaLoopsStop()_
can be used to send the worker threads a signal to terminate. It does not
return until all worker threads have stopped.

### Interrupt handling

```C
int toscaIntrConnectHandler(intrmask_t intrmask, void (*function)(), void* parameter);
int toscaIntrDisconnectHandler(intrmask_t intrmask, void (*function)(), void* parameter);
int toscaIntrDisable(intrmask_t intrmask);
int toscaIntrEnable(intrmask_t intrmask);
void toscaInstallSpuriousVMEInterruptHandler(void);
```

These functions allow to connect and disconnect user defined interrupt
handler functions to a given USER1, USER2 or VME
interrupt and to disable and enable certain interrupt sources.
All interrupts start enabled as soon as a handler is connected.

The user `function` will be called in the context of an
[interrupt handler thread](#interrupt-handler-thread) with 3 arguments:
The provided `parameter`, the interrupt number and the interrupt vector
(both `int`).
The function is not required to use or even accept all three arguments.

The `intrmask` is a combination of bits that stand for interrupt sources.
The mask structure allows to access multiple interrupt sources at once.
Possible values are bitwise combinations of:

* `TOSCA_USER1_INTR(n)`
    an interrupt line `n` in the range from 0 to 15
* `TOSCA_USER1_INTR_MASK(m)`
    a set of interrupts lines according to bits 0 to 15 of mask m
* `TOSCA_USER1_INTR_ANY`
    all 16 USER1 interrupt lines
* The same for `USER2`
* `TOSCA_VME_INTR_VEC(n,vec)`
    an interrupt level `n` from 1 to 7 with vector `vec`
* `TOSCA_VME_INTR_MASK_VEC(m,vec)`
    a set of interrupt levels according to bits 0 to 6 of mask m
    with vector `vec`
* `TOSCA_VME_INTR_ANY_VEC(vec)`
    all 7 VME interrupt levels with vector `vec`
* `TOSCA_VME_SYSFAIL`
    VME system failure
* `TOSCA_VME_ACFAIL`
    VME power failure
* `TOSCA_VME_ERROR`
    VME bus error
* `TOSCA_VME_FAIL(n)`
    with `n` in the range 0 to 2 meaning one of the above failures
* `TOSCA_VME_FAIL_ANY`
    all 3 above failures
* `TOSCA_DEV_*(d,...)`
    same as above with additional device number

It is the number `n` that will be passed to the interrupt handler
`function`.
This allows to install the same handler function for multiple
interrupt sources ans still distinguish them.

Using `TOSCA_USER1_INTR(n)` with `n` in the range from 16 to 31 is
equivalent to using `TOSCA_USER2_INTR(n-16)`.

Use the VME vector range 1 to 254 only, because vector 255 may be
generated in case of errors in the interrupt acknowledge VME bus cycle.
Vector 0 means "all vectors" in the functions
_toscaIntrDisconnectHandler()_,
_toscaIntrDisable()_ and _toscaIntrEnable()_.

The _toscaInstallSpuriousVMEInterruptHandler()_ function installs a handler
for `TOSCA_VME_INTR_ANY_VEC(255)` which prints an error message.
The EPICS [DevLibVME interface](#devlibvme-interface) calls this function
automatically the first time VME is used.

**Debugging:** The global variable `toscaIntrDebug` can be set to enable
debug output, either to stderr or to `toscaIntrDebugFile` if that global
`FILE*` variable is set.

#### Infos on interrupt handling

```C
unsigned long long toscaIntrCount();
size_t toscaIntrForEachHandler(size_t (*callback)(const toscaIntrHandlerInfo_t*, void*), void* user);
```

The _toscaIntrCount()_ function returns the total number of interrupts
that have been processed since program start.

The _toscaIntrForEachHandler()_ function can be used to iterate over all
installed interrupt handlers.
It calls the `callback` function for each installed handler until a callback
returns a non zero value.
It returns the result of the last called callback.
It passes two arguments to the callback function:
A pointer to a structure with the fields below (in unspecified order) and
the `user` argument.

```C
unsigned int device;       /* device numner */
intrmask_t intrmaskbit;    /* one of the mask bits */
unsigned int index;        /* 0...TOSCA_NUM_INTR-1, unique for each intr bit and VME vector */
unsigned int vec;          /* 1...255 for VME interrupts, else 0 */
void (*function)();        /* installed handler function */
void *parameter;           /* parameter of installed handler function */
unsigned long long count;  /* number of times this interrupt has been received */
```

#### Interrupt handler thread

```C
void* toscaIntrLoop();
int toscaIntrLoopIsRunning(void);
void toscaIntrLoopStop();
```

Interrupts are handled in the context of a single thread executing the
_toscaIntrLoop()_ function.
This function blocks until one of the connected interrupts has been
received and
then calls the installed interrupt handlers for this interrupt (which
should not block indefinitely).
Then it starts over waiting for the next interrupt.
No two interrupt handler functions will ever execute at the same time.

**The user is responsible for starting one interrupt worker thread which
executes the _toscaIntrLoop()_ function.**
This allows application specific choices for thread parameters like
priority and stack size.
The stack must be sufficient for any installed interrupt handler function.
It is not possible to start more than one interrupt handler thread.
The EPICS interface starts an interrupt handler thread.

The _toscaIntrLoopIsRunning()_ function returns 1 if the interrupt handler
thread is running, else 0.
The _toscaIntrLoopStop()_ function sends the interrupt handler thread a
signal to terminate.
It does not return until the interrupt handler thread has stopped.

### Interrupt generation

```C
int toscaSendVMEIntr(unsigned int level, unsigned int vec);
```

The Tosca VME interface can also generate interrupts on the VME bus to
notify other VME cards.
This is often used together with a [VME SLAVE map](#vme-slave-maps).
Pass `level` in the range of 1 to 7 and `vec` in the range of 0 to 255.

### I²C bus access

Strictly speaking this is not part of the Tosca API but it is documented
here for completeness and helping programmers to convert from pev to Tosca.

```C
int i2cOpen(const char* path, unsigned int address);
int i2cRead(int fd, unsigned int command, unsigned int dlen, void* value);
int i2cWrite(int fd, unsigned int command, unsigned int dlen, int value);
```

The _i2cOpen()_ function takes a path to an I²C bus and the I²C device
address on this bus.
The I²C bus can be given as a device file like `"/dev/i2c-2"` or simply
as a number "2",
but it is sometimes hard to say which number is assigned by the kernel to
a given I²C bus.
Therefore it is possible to pass a sysfs pattern instead.

The _i2cOpen()_ function uses this _glob()_ compatible pattern to find an
i2c bus. The first match is used and must end in `i2c-<number>`.

The function returns a file descriptor to be used in _i2cRead()_ and
_i2cWrite()_ functions.
On failure it returns -1 and sets `errno`. 
When done with the device the file descriptor can be closed with _close()_.

**Example:** The pattern `/sys/devices/{,*/}*localbus/*c0.pon-i2c/i2c*`
resolves to
`/sys/devices/platform/ffe05000.localbus/ffb000c0.pon-i2c/i2c-5` on an
IFC1210 with kernel 4.9 or to
`/sys/devices/ffe05000.localbus/ffb000c0.pon-i2c/i2c-5` with older
kernels.
It ends in `i2c-5`, thus `/dev/i2c-5` will be used.

The IFC1210 I²C devices can be found from the Linux shell with:

```
> ls -d /sys/devices/{,*/}*localbus/*pon-i2c/i2c*
```

Using a 3.* kernel it returns

```
/sys/devices/ffe05000.localbus/ffb00080.pon-i2c/i2c-2/
/sys/devices/ffe05000.localbus/ffb000a0.pon-i2c/i2c-3/
/sys/devices/ffe05000.localbus/ffb000b0.pon-i2c/i2c-4/
/sys/devices/ffe05000.localbus/ffb000c0.pon-i2c/i2c-5/
/sys/devices/ffe05000.localbus/ffb000d0.pon-i2c/i2c-6/
/sys/devices/ffe05000.localbus/ffb000e0.pon-i2c/i2c-7/
/sys/devices/ffe05000.localbus/ffb000f0.pon-i2c/i2c-8/
```

and using a 4.* kernel it returns

```
/sys/devices/platform/ffe05000.localbus/ffb00080.pon-i2c/i2c-2/
/sys/devices/platform/ffe05000.localbus/ffb000a0.pon-i2c/i2c-3/
/sys/devices/platform/ffe05000.localbus/ffb000b0.pon-i2c/i2c-4/
/sys/devices/platform/ffe05000.localbus/ffb000c0.pon-i2c/i2c-5/
/sys/devices/platform/ffe05000.localbus/ffb000d0.pon-i2c/i2c-6/
/sys/devices/platform/ffe05000.localbus/ffb000e0.pon-i2c/i2c-7/
/sys/devices/platform/ffe05000.localbus/ffb000f0.pon-i2c/i2c-8/
```

This shows the logical I²C bus numbers (here 2...8) in relation to the
hardware address on the processor localbus. 
To do the opposite, find the hardware addresses of all logical I²C buses,
use:

`ls -l /sys/bus/i2c/devices/i2c-*`


### Backward compatibility

For compatibility with software written for the older _pev_ driver and API,
several _pev_ API functions are implemented but actually use the Tosca
(or I²C) API.

The global variable `pevDebug` can be set to enable debug output, either
to stderr or to `pevDebugFile` if that global `FILE*` variable is set.

Supported in their _pev*_ and (where existed) in their _pevx*_ form:

* *pev(x)_init()*  
* *pev(x)_csr_rd()*, *pev(x)_csr_wr()*, *pev(x)_csr_set()*
   (for [TCSR and TIO registers](#register-access-functions))
* *pev(x)_elb_rd()*, *pev(x)_elb_wr()*
   (for [PON registers](#register-access-functions) and [SRAM](#memory-maps))
* *pev_smon_rd()*, *pev_smon_wr()*
   (for [Virtex system monitor registers](#register-access-functions))
* *pev_bmr_read()*, *pev_bmr_write()*,
  *pev_bmr_conv_11bit_u()*, *pev_bmr_conv_11bit_s()*, *pev_bmr_conv_16bit_u()*
   (for BMR&nbsp;463 DC/DC regulators, uses [I²C API](#ic-bus-access))
* *pev(x)_map_alloc()*, *pev(x)_map_free()*,
  *pev(x)_mmap()*, *pev(x)_munmap()*, *pev(x)_map_modify()*
   (for [memory maps](#memory-maps))
* *pev(x)_evt_queue_alloc()*, *pev(x)_evt_queue_free()*,
  *pev(x)_evt_register()*, *pev(x)_evt_read()*, *pev(x)_evt_queue_enable()*,
  *pev(x)_evt_queue_disable()*, *pev(x)_evt_mask()*, *pev(x)_evt_unmask()*
   (for [interrupts](#interrupt-handling))
* *pev(x)_buf_alloc()*, *pev(x)_buf_free()*,
  *pev(x)_dma_move()*,  *pev(x)_dma_status()*
   (for [DMA transfers](#dma-transfers))

Also the wrappers functions we had in the old EPICS pev library are
available and use the new Tosca interface:

* _pevMap()_, _pevMapToAddr()_, _pevMapExt()_, _pevUnmap()_
* _pevIntrConnect()_, _pevIntrDisconnect()_, _pevIntrEnable()_, _pevIntrDisable()_
* _pevDmaAlloc()_, _pevDmaFree()_, _pevDmaRealloc()_
* _pevDmaTransfer()_, _pevDmaTransferWait()_, _pevDmaFromBuffer()_,
  _pevDmaToBuffer()_, _pevDmaFromBufferWait()_, _pevDmaToBufferWait()_


#### Limitations

Due to fundamental differences between _pev_ and _Tosca_ driver, some
functions behave differently.

* *pev(x)_map_alloc()* cannot be used to map VME SLAVE windows (but *pevMap* can).
* *pev(x)_munmap()*, *pev(x)_map_free()* and *pevUnmap()* do nothing.
   The resources are released when the program exits.
* *pev(x)_map_modify()* always fails.
* *pev(x)_evt_register()* allocates 256 file descriptors for each VME interrupt
   level, which may bring the program to the file descriptor limit.
* *pev(x)_evt_queue_disable()* and *pevIntrDisable()* simply make the API
   ignore the interrupts.
* *pev(x)\_evt\_\*()* and *pevIntr\*()* functions work with Tosca device 0 only.
* *pev(x)_buf_alloc()* and *pevDmaAlloc()* simply allocate (page aligned)
   heap memory, which is sufficient for Tosca DMA.
* *pev(x)_dma_move()* cannot move from or to PCI other than user space memory.
* *pev(x)_dma_status()* returns an empty structure because the requested
   information is not accessible.

## Linux command line utility

The command line utility `tosca` provides easy access to Tosca
[memory maps](#memory-maps).

```
tosca addrspace:offset [wordsize] [bytes]

command | tosca addrspace:offset [wordsize] [bytes]
tosca addrspace:offset [wordsize] [bytes] < file

tosca addrspace:offset [wordsize] [bytes] | command
tosca addrspace:offset [wordsize] [bytes] > file
command $(tosca addrspace:offset [wordsize] [bytes])
command `tosca addrspace:offset [wordsize] [bytes]`
```

The Tosca resource or address space `addrspace` is any of `TCSR`,
`TIO`, `SMEM` or `SMEM`,  `USER1` or `USER`, `USER2`, `SRAM`,
`A16`, `A24`, `A32`, `CRCSR`.
The A* address spaces normally access VME in "User Data" mode.
To select "Supervisory" mode add a `*`, to select "Program" mode add a
`#` (`A16*`, `A16#`, `A16*#` or `A16#*`, ...).

The start address `offset` in bytes as well as the size `bytes` can be
passed as decimal or hex numbers or in more human readable form with
suffixes `k`, `M`, `G` (for powers of 1024, not case sensitive),
e.g. `1M` meaning `0x100000` or `1M3k-80` meaning `0x100bb0`.
An offset of `:0` may be skipped.
Possible values for `wordsize` are 1, 2, 4, 8, -2, -4, -8.
Negative `wordsize` makes the program swap bytes.

In the first form (without redirection of stdin or stdout) `tosca`
displays the contents of the given address space formatted according to
`wordsize`. Default `wordsize` is 2 and default `bytes` is 0x100.

In the second form (with stdin redirected) `tosca` copies received data
to the given address space.
If `wordsize` is negative, the received data is swapped.
If `bytes` is given and the input size does not match, input is either
truncated or filled with 0 bytes.

In the third form (with stdout redirected) `tosca` copies data from the
given address space to the output.
If `wordsize` is negative, the sent data is swapped.
If `bytes` is given it limits the number of bytes written, otherwise the
output is limited by the address space size or the next MB boundary,
whatever comes first.

### Examples

Display USER address space with dword swap.

```
# tosca USER -4 64
0000: 00000003 20160523 5374616e 64617264 .... ..#Standard
0010: 20494f43 20617070 6c696361 74696f6e  IOC application
0020: 00000000 00000000 4e6f2046 4d433120 ........No FMC1 
0030: 63617264 00000000 4e6f2046 4d433220 card....No FMC2 
```

Read data from USER address space.

```
# echo $(tosca USER:8 -4 24)
Standard IOC application
```

Copy range from USER to SMEM offset 1 MB with dword swap.

```
# tosca USER -4 0x100 | tosca SMEM:1M
```

Overwrite first 16 bytes of the copy with 0 filled data.

```
# echo "blabla" | tosca SMEM:1M 1 16
```

Overwrite more bytes of the copy with data.

```
# echo "blabla" | tosca SMEM:1M16
```

Overwrite more bytes of the copy with truncated data.

```
# echo "blabla" | tosca SMEM:1M32 1 3
```

Display the copy.

```
# tosca SMEM:1M 4 64
00100000: 626c6162 6c610a00 00000000 00000000 blabla..........
00100010: 626c6162 6c610a70 6c696361 74696f6e blabla.plication
00100020: 626c6100 00000000 4e6f2046 4d433120 bla.....No FMC1 
00100030: 63617264 00000000 4e6f2046 4d433220 card....No FMC2 
```

## IOC shell functions

These functions exist mainly for debug purposes from inside the EPICS IOC
shell.
The [API](#c-api) is usually accessed either by
[DevLibVME](#devlibvme-interface) when using EPICS VME device drivers or by
the [regDev interface](#regdev-interface).
But sometimes it may be helpful to have a more direct access to the API.

See `help tosca*` on the IOC shell for a list of available functions and
their arguments.
Also many functions print a help text when called without arguments.

To ease access, address space arguments are available by string names.
Possible values are `TCSR`, `TIO`, `USER1` (or `USER`), `USER2`,
`SMEM1` (or `SMEM`), `SMEM2`, `SRAM`, `A16`, `A24`, `A32`,
`CRCSR`.
The `A*` address spaces normally access VME in "User Data" mode.
To select "Supervisory" mode add a `*`, to select "Program" mode add a
`#` (`A16*`, `A16#`, `A16*#` or `A16#*`, ...).
For VME slave maps use `SLAVE`.

Any address or size can be written in decimal, hexadecimal with leading
`0x` or in more human readable form with suffixes `k`, `M`, `G`
(for powers of 1024, not case sensitive).
Also sums and differences are supported.
E.g. `1M` meaning `0x100000` or `1M3k-80` meaning `0x100bb0`.

Resource addresses are passed as one string in the general format
`device:addrspace:address`, but `0:` for device 0 (the first
and often only Tosca device) can be skipped, as well as `:0` to access
address 0 of a given Tosca resource.
Thus `0:USER:0` can be written as `USER`.
The following examples all use device 0 only and thus skip `0:`.

To test [mapping](#memory-maps) of a toscaResource, use:

```
toscaMap addrspace:address size
```

It prints the returned pointer (or an error message).

This command can also be used to [map a VME SLAVE](#vme-slave-maps) to
USER, SMEM or program memory.
For automatic swapping add `swap`.

```
toscaMap SLAVE:address size USER:address swap
toscaMap SLAVE:address size SMEM:address
toscaMap SLAVE:address size
```

To display data in memory or on a Tosca resource use the `md`
([memory display](https://github.com/paulscherrerinstitute/memDisplay))
function:

```
md [address](addrspace:) [wordsize] [bytes]
```

The `md` command automatically maps Tosca resources with
[_toscaMap()_](#memory-maps) as necessary.
If `addrspace:` is not given, the `address` is in memory, e.g. a
pointer returned by `toscaMap`.
Default `wordsize` is 2 with the options 1, 2, 4, 8, -2, -4, -8.
Negative values swap the displayed values.
Default number of `bytes` is 128.
With no arguments, the function displays the next block of memory using
the previous settings.

All memory maps installed by the running IOC can be listed with
`toscaMapShow`.
It uses _[toscaMapForEach()](#map-lookup-functions)_ with a function that
prints the map description.

```
toscaMapShow
addrspace:baseaddr         size         pointer
   TCSR:0x0               0x2000=8K   0xb7b5f000  
   A32*:0x100000        0x100000=1M   0xb60ae000      
SLAVE32:0x100000        0x100000=1M   SMEM1:0x0       
```

To test [DMA transfers](#dma-transfers) use:

```
toscaDmaTransfer [addrspace:]sourceaddr [addrspace:]destaddr size [swap]
```

The possible `addrspace` values are `USER1` (or `USER`), `USER2`, `SMEM1`
(or `SMEM`), `SMEM2` or one of the VME transfer modes `A32` (for single
32 bit word transfers), `BLT`, `MBLT`, `2eVME`, `2eSST160`, `2eSST267`,
or `2eSST320`.
Without `addrspace:` the address is a memory pointer.
The optional `swap` parameter is either `NS` for no swap, `WS` for
word (2 byte) swap, `DS` for double word (4 byte) swap, or `QS` for
quad word (8 byte) swap. The default is no swap.

The function `malloc` can be used to allocate (page aligned) memory
which can be used here.
It sets the environment variable `BUFFER` to the start of the allocated
memory, so that commands can use `$(BUFFER)` to refer to the memory.

```
malloc 1k
BUFFER = 0xb5fad000
toscaDmaTransfer USER1 $(BUFFER) 1k DS
```

To get information on interrupt usage, call:

```
toscaIntrShow [level]
```

Depending on `level`, different amount of information is shown.
Level 0, the default, only shows the number of received interrupts since
IOC start and since last _toscaIntrShow_  call for each interrupt source.
Level 1 also lists installed interrupt handler functions and arguments for
each interrupt source.
Level 2 adds the name of the library in which the function was found and
finally level 3 shows the full path of the library.

Negative `level` numbers have a different meaning. The output is repeated
every `-level` seconds until a key is pressed.
For example `toscaIntrShow -1` repeats every second.
This allows to see interrupt rates.
Only interrupts which have been received since the last output are shown.

The global debug control variables
`toscaMapDebug`, `toscaRegDebug`, `toscaIntrDebug`, and
`toscaDmaDebug` can be set in the IOC shell with the _var_ command.

### Examples

Check how many and which Tosca device we have:

```
> toscaNumDevices
1
> toscaDeviceType 0
1210
> toscaListDevices
0 0000:03:00.0 1210 bridgenum`-1 driververs`0
```

[Map](#memory-maps) range of 1024 bytes from VME A24 starting at VME
address 0x10000 to memory and return pointer:

```
> toscaMap A24:0x10000 1024
0xb6411000
```

Display (and automatically map) some VME A16 memory:

```
> md A16:0x3400
3400: 0000 0000 0000 0000 f800 f800 f800 f800 ................
3410: 0000 0000 0000 0000 0000 0000 0000 0000 ................
3420: 0000 0000 0000 0000 0000 0000 0000 0000 ................
3430: 0000 0000 0000 0000 0000 0000 0000 0000 ................
3440: 0000 0000 0000 0000 0000 0000 0000 0000 ................
3450: 0000 0000 0000 0000 0000 0000 0000 0000 ................
3460: 0000 0000 0000 0000 0000 0000 0000 0000 ................
3470: 0000 0000 0000 0000 0000 0000 0000 0000 ................
```

Display (and map) USER, 4 byte wise swapped as needed by the
application in USER:

```
> md USER -4
0000: 00000003 20160523 5374616e 64617264 .... ..#Standard
0010: 20494f43 20617070 6c696361 74696f6e  IOC application
0020: 00000000 00000000 4e6f2046 4d433120 ........No FMC1 
0030: 63617264 00000000 4e6f2046 4d433220 card....No FMC2 
0040: 63617264 00000000 00000000 00000000 card............
0050: 00000000 00000000 00000000 00000000 ................
0060: 00000000 00000000 00000000 00000000 ................
0070: 00000000 00000000 00000000 00000000 ................
```

Map VME A32 [slave window](#vme-slave-maps) at 3MB to first 256kB USER:

```
> toscaMap SLAVE:3M 256k USER swap
Success
```

Display above VME slave window to USER with wrong 2 byte access:

```
> md A32:3M
00300000: 0003 0000 0523 2016 616e 5374 7264 6461 .....# .anStrdda
00300010: 4f43 2049 7070 2061 6361 6c69 6f6e 7469 OC Ipp acalionti
00300020: 0000 0000 0000 0000 2046 4e6f 3120 4d43 ........ FNo1 MC
00300030: 7264 6361 0000 0000 2046 4e6f 3220 4d43 rdca.... FNo2 MC
00300040: 7264 6361 0000 0000 0000 0000 0000 0000 rdca............
00300050: 0000 0000 0000 0000 0000 0000 0000 0000 ................
00300060: 0000 0000 0000 0000 0000 0000 0000 0000 ................
00300070: 0000 0000 0000 0000 0000 0000 0000 0000 ................
```

Display above VME slave window to USER with correct 4 byte access:
(Note that swapping is handled by the SLAVE window.)

```
> md A32:3M 4
00300000: 00000003 20160523 5374616e 64617264 .... ..#Standard
00300010: 20494f43 20617070 6c696361 74696f6e  IOC application
00300020: 00000000 00000000 4e6f2046 4d433120 ........No FMC1 
00300030: 63617264 00000000 4e6f2046 4d433220 card....No FMC2 
00300040: 63617264 00000000 00000000 00000000 card............
00300050: 00000000 00000000 00000000 00000000 ................
00300060: 00000000 00000000 00000000 00000000 ................
00300070: 00000000 00000000 00000000 00000000 ................
```

[Lookup existing maps](#map-lookup-functions) (of this IOC):

```
> toscaMapShow
addrspace:baseaddr            size      pointer 
   TCSR:0x0               0x2000=8K   0xb7eb2000      
    A24:0x0             0x100000=1M   0xb6401000      
    A16:0x0             0x10000=64K   0xb63f1000      
  USER1:0x0             0x100000=1M   0xb62f1000      
SLAVE32:0x300000        0x100000=1M   USER1:0x0       SWAP
    A32:0x300000        0x100000=1M   0xb61f1000      
```

Do 128 kB  [DMA transfer](#dma-transfers) from USER to memory with double
word swap and display the transferred data:

```
> malloc 128k
BUFFER = 0xb5eea000
> toscaDmaTransfer USER:0x100 $(BUFFER) 128k DS
Success
> md $(BUFFER) 4
b5eea000: 00000003 20160523 5374616e 64617264 .... ..#Standard
b5eea010: 20494f43 20617070 6c696361 74696f6e  IOC application
b5eea020: 00000000 00000000 4e6f2046 4d433120 ........No FMC1 
b5eea030: 63617264 00000000 4e6f2046 4d433220 card....No FMC2 
b5eea040: 63617264 00000000 00000000 00000000 card............
b5eea050: 00000000 00000000 00000000 00000000 ................
b5eea060: 00000000 00000000 00000000 00000000 ................
b5eea070: 00000000 00000000 00000000 00000000 ................
> md
b5eea080: 00000000 00000000 00000000 00000000 ................
b5eea090: 00000000 00000000 00000000 00080000 ................
b5eea0a0: 00080000 00080000 00080000 00080000 ................
b5eea0b0: 00080000 00080000 00080000 00080000 ................
b5eea0c0: 00080000 00080000 00080000 00080000 ................
b5eea0d0: 00080000 00080000 00080000 00000000 ................
b5eea0e0: 00000000 00000000 00000000 00000000 ................
b5eea0f0: 00000000 00000000 00000000 00000000 ................
```

Try 1 MB [DMA transfer](#dma-transfers) from A32 to SMEM using MBLT mode
from an address where no MBLT capable VME card is available:

```
> toscaDmaTransfer MBLT:2M SMEM:0 1M
Input/output error
```

[Check for VME errors](#register-access-functions) and re-arm error
catching:

```
> toscaGetVmeErr
0x00200000,0x98000805 (ERR R TOUT DMA id=4 len=0 MBLT:0x200000)
```

Connect some interrupts:

```
> toscaIntrConnectHandler USER1-1-4;8 toscaDebugIntrHandler
> toscaIntrConnectHandler VME-4.18 toscaDebugIntrHandler
> toscaIntrConnectHandler VME.20 toscaDebugIntrHandler
```

Check interrupts:

```
> toscaIntrShow 1
total number of interrupts: 0 (+0)
 VME-4.18   count=0 (+0) toscaDebugIntrHandler(0x100d9e50)
 VME-1.20   count=0 (+0) toscaDebugIntrHandler(0x100d9ab0)
 VME-2.20   count=0 (+0) toscaDebugIntrHandler(0x100d9ab0)
 VME-3.20   count=0 (+0) toscaDebugIntrHandler(0x100d9ab0)
 VME-4.20   count=0 (+0) toscaDebugIntrHandler(0x100d9ab0)
 VME-5.20   count=0 (+0) toscaDebugIntrHandler(0x100d9ab0)
 VME-6.20   count=0 (+0) toscaDebugIntrHandler(0x100d9ab0)
 VME-7.20   count=0 (+0) toscaDebugIntrHandler(0x100d9ab0)
 USER1-1 count=0 (+0) toscaDebugIntrHandler(0x100d9c10)
 USER1-2 count=0 (+0) toscaDebugIntrHandler(0x100d9c10)
 USER1-3 count=0 (+0) toscaDebugIntrHandler(0x100d9c10)
 USER1-4 count=0 (+0) toscaDebugIntrHandler(0x100d9c10)
 USER1-8 count=0 (+0) toscaDebugIntrHandler(0x100d9c10)
```

[Read some registers](#register-access-functions):

```
> toscaIoRead 0
0x805d0910

> toscaCsrRead 0
0x805d0910

> md TCSR -4 16
0000: 805d0910 000000ba 00000000 00030000 .]..............

>toscaSmonRead 0x08
Supply offs 0xfe67 = 2.979 V

>toscaSmonRead
0x00 Temp        0xb341 = 79.73 C
0x01 Vccint      0x55b0 = 1.002 V
0x02 Vccaux      0xddc3 = 2.599 V
0x03 Vadj        0x0000 = 0.000 V
0x04 VrefP       0x0000 = 0.000 V
0x05 VrefN       0x0000 = 0.000 V
0x08 Supply offs 0xfe64 = 2.979 V
0x09 ADC offs    0xfeae = 2.982 V
0x10 Vaux[0]     0x0000 = 0.000 V
0x11 Vaux[1]     0x0000 = 0.000 V
0x12 Vaux[2]     0x0000 = 0.000 V
0x13 Vaux[3]     0x0000 = 0.000 V
0x14 Vaux[4]     0x0000 = 0.000 V
0x15 Vaux[5]     0x0000 = 0.000 V
0x16 Vaux[6]     0x0000 = 0.000 V
0x17 Vaux[7]     0x0000 = 0.000 V
0x18 Vaux[8]     0x0000 = 0.000 V
0x19 Vaux[9]     0x0000 = 0.000 V
0x1a Vaux[A]     0x0000 = 0.000 V
0x1b Vaux[B]     0x0000 = 0.000 V
0x1c Vaux[C]     0x0000 = 0.000 V
0x1d Vaux[D]     0x0000 = 0.000 V
0x1e Vaux[E]     0x0000 = 0.000 V
0x1f Vaux[F]     0x0000 = 0.000 V
0x20 Temp Max    0xb395 = 80.22 C
0x21 Vccint Max  0x56fc = 1.017 V
0x22 Vccaux Max  0xde22 = 2.602 V
0x24 Temp Min    0xaf69 = 71.86 C
0x25 Vccint Min  0x5544 = 0.999 V
0x26 Vccaux Min  0xdd57 = 2.593 V
0x3f Flag        0x0000 = 0000:0000:0000:0000
0x40 Config #0   0x0000 = 0000:0000:0000:0000
0x41 Config #1   0x0000 = 0000:0000:0000:0000
0x42 Config #2   0x0800 = 0000:1000:0000:0000

> toscaPonRead 0x1c
signature      0x04042016

> toscaPonRead
0x00 vendor         0x73571210
0x04 static_options 0x00000910
0x08 vmectl         0x3000ff7e
0x0c mezzanine      0xc077f703
0x10 general        0xffffff98
0x14 pciectl        0x00010201
0x18 user           0x00000000
0x1c signature      0x04042016
0x20 cfgctl         0x80010707
0x24 cfgdata        0x00000000
0x40 bmrctl         0x00000000
```

## DevLibVME interface
The _devLibVME_ interface is automatically initialized when this EPICS
driver is loaded. 
It implements the standard EPICS VME bus access functions for Tosca device
0 (the first and often only Tosca device).
Thus any existing EPICS driver using functions from devLibVME.h should be
able to access the VME bus using the Tosca VME interface.
(However older drivers may need a conversion from VxWorks to the EPICS osi
(operating system independent) support.)

The EPICS _devLibVME_ interface has no means to select "User" or
"Supervisory" mode, nor "Data" or "Program" mode for
[VME memory maps](#memory-maps).
To be compatible with what we had in vxWorks, the used mode is fixed to
"Supervisory Data" on A16, A24 and A32.
This was done because some VME cards allow access only in "Supervisory"
mode and most others ignore this flag.

[Interrupt handlers](#interrupt-handling) can be installed for any VME
interrupt vector 1 to 255.
However 255 may be the result of a problem and should not be used in
applications.
This driver automatically installs handlers for spurious interrupt
detection for interrupt vectors 255.

Tosca handles VME interrupts per combination of interrupt level (1-7) and
vector.
But the EPICS _devLibVME_ interface allows to install handlers only per
vector.
Thus this driver always installs a handler for a given interrupt vector
for all 7 interrupt levels.
The interrupt handlers are called in the context of the
[thread](#interrupt-handler-thread) "irq-TOSCA".
The EPICS osi priority of this thread is 80 by default but can be set with
the IOC shell variable `toscaIntrPrio` (before _iocInit_).

The _devLibVME_ functions _devLibA24Malloc()_ and _devLibA24Free()_ are
unsupported (_devLibA24Malloc()_ always returns NULL) because Tosca does not
support VME A24 slave windows.

The function _devInterruptInUseVME()_ always returns FALSE, because the
driver can handle a list of interrupt handlers for each interrupt vector.
The function is supposed to return TRUE if the vector cannot be used any
more (I think).

Two or four (depending on the Tosca device type)
[DMA worker threads](#dma-worker-thread) "dma*-TOSCA" are
started automatically (even though _devLibVME_ has no DMA support).
The EPICS osi priority of these threads is 80 by default but can be set
with the IOC shell variable `toscaDmaPrio` (before _iocInit_).

**Debugging:** Debug messages can be enabled by setting the IOC shell
variable `toscaDevLibDebug` to 1.
Also all other tosca debug variables like `toscaMapDebug` are available
from the IOC shell.

## RegDev interface

The generic [regDev](https://github.com/paulscherrerinstitute/regDev)
device support is meant to allow EPICS records access to "registers" on
almost any type of hardware. It has support for all hardware accessing
record types from EPICS base, including the array records _aai_ and _aao_.
For details see the regDev manual.

This driver makes all the [mappable Tosca resources](#memory-maps)
(USER1, USER2, SMEM1, SMEM2, TCSR, TIO, SRAM, VME A16, A24, A32, CRCSR and
VME SLAVE) as well as the PON and SMON [registers](#register-access-functions)
available to regDev.

The I²C devices on the IFC1210 are available through regDev as well.
However the I²C devices are not implemented by the Tosca driver but by the
[i2cDev](https://github.com/paulscherrerinstitute/i2cDev) driver using
the [API described above](#ic-bus-access).
This allows to access all resources on IFC boards in a unified way.

In addition, [DMA](#dma-transfers) is supported for transfers from and to
USER1, USER2, SMEM1, SMEM2 and VME A32, which will be used for large arrays
or in regDev's [block mode](#block-mode).

DMA transfers from and to VME can be implemented with different VME
transfer modes, but the availability depends on the accessed VME card.
Not all cards support all modes.
The default DMA mode is SCT (single cycle transfer).
It is most portable because it does not use any VME block transfer mode but
only 32 bit accesses to the A32 address space. It is also the slowest mode.

Measured VME transfer speeds in MB/sec using VME SLAVE map on the same
IFC1210.

| VME DMA mode      | read | write |
|:------------------|-----:|------:|
| memory map 32 bit |    2 |    14 |
| SCT               |    7 |    19 |
| BLT               |   25 |    36 |
| MBLT              |   48 |    61 |
| 2eVME             |  100 |    93 |
| 2eSST160          |  110 |    95 |
| 2eSST267          |  149 |   119 |
| 2eSST320          |  169 |   137 |

### Startup script

```
require "tosca"
toscaRegDevConfigure name addrspace:address size flags
toscaSmonDevConfigure name
toscaPonDevConfigure name
```

The _toscaRegDevConfigure_ function creates a new logical regDev device
with the given `name` which [maps](#memory-maps) to an address range
starting at `addrspace:address` with the given `size` in bytes.
It is possible to create multiple regDev devices on the same Tosca
resource, even with overlapping ranges, which can make sense if the
`flags` differ.
The offsets used in the record links are bytes relative to the start
address of the address range.

For `addrspace` use one of `USER1` (or `USER`), `USER2`, `SMEM1` (or
`SMEM`), `SMEM2`, `TCSR`, `TIO`, `SRAM`, `A16`, `A24`, `A32`, `CRCSR`.
Add `*`  for "Supervisory" and `#` for "Program" access to the `A`
modes.

Possible `flags` are:

* swapping
  * `NS` no swap
  * `WS`, `WL`, `WB` word (2 byte) swap, little endian, big endian
  * `DS`, `DL`, `DB` double word (4 byte) swap, little endian, big endian
  * `QS`, `QL`, `QB` quad word (8 byte) swap, little endian, big endian
* VME DMA modes (only for DMA on `A32` address space)
  * `SCT` "single cycle transfer", the default
  * `BLT` VME 32 bit block transfer
  * `MBLT` VME 64 bit block transfer
  * `2eVME` double edge VME 64 bit block transfer
  * `2eSST160` double edge source synchronous transfer max 160 MB/sec
  * `2eSST267` double edge source synchronous transfer max 267 MB/sec
  * `2eSST320` (or short `2eSST`) double edge source synchronous transfer max 320 MB/sec
* block mode (transfer full address range with DMA)
  * `block` use [block mode](#block-mode) for reading and writing
  * `blockread` use block mode for reading
  * `blockwrite` use block mode for writing
* DMA limits for arrays (minimum number of elements)
  * `dmaReadLimit`= default 100
  * `dmaWriteLimit`= default 2k
  * `dmaonly` sets both limits to 1
  * `nodma` sets both limits to 0
* default interrupt vector (if not [set in the record](#record-configuration))
  * `intr`= 1...254 for VME, 0-15 for USER1, USER2

The `*L` and `*B` swap modes define the endianess of the accessed
resource independently of the endianess of the CPU. Thus `*L` modes
(for little endian resources) only swap on big endian CPUs and likewise
the `*B` modes only on little endian CPUs.
For USER1, USER2, TSCR and TIO address space the default is `DL`,
for all others (including SMEM1 and SMEM2) it is `NS`.

The driver transfers data either by reading and writing through a
[memory map](#memory-maps) or using [DMA tranfers](#dma-transfers).
DMA is used for reading or writing arrays if the number of elements is at
least `dmaReadLimit` or `dmaWriteLimit` respectively.
The default values are chosen at the break even point, where DMA becomes
faster than memory mapped access.
This point depends on the time needed to set up a DMA transfer compared to
the memory mapped read and write speed.
Setting the limit to 1 uses DMA for any transfer.
If both limits are 1 (e.g. using `dmaonly`) no memory map is created.
If both limits are 0 (e.g. using `nodma`) DMA is never used.

SMON and PON [register access](#register-access-functions) need no
configuration parameters and have fixed size,
thus the only parameter to pass to _toscaSmonDevConfigure_ and
_toscaPonDevConfigure_ is a `name` to be used in the record links.

### Block mode

The block mode treats the whole device as one large array which is
transferred to or from memory when a record with PRIO=HIGH reads from or
writes to the device.
This allows all records, not only arrays, to benefit from the speed of a
single DMA transfer which gives a performance boost when reading many
values at the same time.
Any other records accessing the same device which do not have PRIO=HIGH
only read from or write to the buffer. Make sure that the record which
triggers the DMA read is processed first and the record which triggers the
DMA write is processed last.

DMA block read may be combined with interrupt triggered processing.
The device (FPGA USER or a VME device) may generate an interrupt when new
data is available.
The record triggering the DMA read can use the "V=vector" flag in its INP
link and SCAN="I/O Intr" to enable interrupt processing. For VME vector is
1-254, for USER or SMEM vector is 0-15. (SMEM devices use USER interrupts).

Block mode can be enabled separately for reading and writing with
`blockread` and `blockwrite`, `block` is just a shortcut for both.

### Record configuration

See also the [regDev](https://github.com/paulscherrerinstitute/regDev)
documentation.

#### DTYP

```
field (DTYP, "regDev")
```

#### Format of INP / OUT link

```
field (INP, "@name:offset T=datatype [flags]")
field (OUT, "@name:offset[:initoffset] T=datatype [flags]")
```

Most Tosca resouces assume a 32 bit `datatype` like `int32` or `uint32`.
For other choices and `flags` see the regDev documentation.

#### Interrupt triggered processing

```
field (SCAN, "I/O Intr")
field (INP, "@name:offset T=datatype V=[vector] [flags]")
```

Here `vector` is the VME interrupt vector (1-254) for VME devices or
the USER1 or USER2 interrupt line (0-15) for FPGA devices.
If the device is a SMEM1 or SMEM2 map, the interrupt lines from
USER1 or USER2 respectively are used.

Output records can be triggered by interrupts as well but this is quite
uncommon.

### Transition from PEV to Tosca

For backward compatibility with the older pev driver, the old startup
script functions _pev(Asyn)Configure_ and _pev(Asyn)I2cConfigure_ are
still available but call Tosca or i2cDev configuration functions (and
print to the shell how they can be replaced).
Also the resource names used by pev (e.g. "SH_MEM") are still understood
by Tosca.
No change is needed in the record configuration because both use regDev.

#### pev(Asyn)Configure

The transition from _pev(Asyn)Configure_ to _toscaRegDevConfigure_ is quite
straight forward:

```
# pev(Asyn)Configure card name addrspace address DMA_mode vec size block swap vmePktSize
toscaRegDevConfigure name card:addrspace:address size flags
```

The `name` is now the first argument but the meaning stays the same.
Second argument is now `card:addrspace:address` as one string.
Card `0:` and address `:0` can be skipped.
Third argument is now `size`.
All further arguments are `flags` in arbitrary order.
The existing `DMA_modes` and `swap` choices are understood.
The `block` argument is either the string `block` or nothing.
The interrupt vector `vec` can be passed as `intr=vec`, but now it is
possible that different records use different interrupts vectors with the
option `V=` in the link.
Using this method, interrupt triggered records can now also be used on
block devices.
The `vmePktSize` option is not supported any more.

The pev driver automatically created a regDev device for a TSCR memory map
with the name "pev_csr".
The Tosca driver does the same to stay compatible.

#### pev(Asyn)I2cConfigure

The transition from _pev(Asyn)I2cConfigure_ to _i2cDev_ needs a bit more
work but also gives some benefit.

```
# pev(AsynI)2cConfigure card name i2cControlWord command
i2cDevConfigure name sysfspattern device muxdev=val
```

Again, `name` has the same meaning as before.

The PON FPGA to which the I²C buses are connected is connected to the
localbus of the processor.
Hence only I²C buses on card 0 are accessible and there is no `card`
argument any more.

The bus and device address of the I²C device used to be coded in the
`i2cControlWord`.
The highest 3 bits describe the bus number and the lowest 7 bits the
`device` address.
Now, an I²C bus can be identified using either a `/dev/i2c*` device file
or simply a number or, because it is hard to tell in advance which hardware
bus has which number, a sysfs pattern.
See the [I²C API](#ic-bus-access) for possible sysfs patterns.

The bus sysfs pattern can be derived from the highest hex digit of the
control word:

|  i2cControlWord         |  sysfspattern                                  | connected hardware      |
|-------------------------|------------------------------------------------|-------------------------|
| 0x00000000 - 0x1FFFFFFF | `/sys/devices/{,*/}*localbus/*80.pon-i2c/i2c*` | temperature sensors     |
| 0x40000000 - 0x5FFFFFFF | `/sys/devices/{,*/}*localbus/*a0.pon-i2c/i2c*` | power monitoring        |
| 0x60000000 - 0x7FFFFFFF | `/sys/devices/{,*/}*localbus/*b0.pon-i2c/i2c*` | transition card over P0 |
| 0x80000000 - 0x9FFFFFFF | `/sys/devices/{,*/}*localbus/*c0.pon-i2c/i2c*` | XMC1/FMC1 slot          |
| 0xA0000000 - 0xBFFFFFFF | `/sys/devices/{,*/}*localbus/*d0.pon-i2c/i2c*` | XMC2/FMC2 slot          |
| 0xC0000000 - 0xDFFFFFFF | `/sys/devices/{,*/}*localbus/*e0.pon-i2c/i2c*` | PCIe switch             |
| 0xE0000000 - 0xFFFFFFFF | `/sys/devices/{,*/}*localbus/*f0.pon-i2c/i2c*` | programmable oscillator |

The _i2cDevConfigure_ function allows definition of multiplexer (mux)
settings.
This ensures that the mux programming is integrated in the device access in
a thread safe way and cannot be changed accidentally by a different thread
trying to access another device with different mux settings on the same I²C
bus at the same time.
The mux devices in use are programmed with a single byte command, so that
they can be defined as `muxdev`val= with `muxdev` being the device
address of the mux device and `val` the value sent to the mux device.
Of course this change requires to modify the templates and to remove the
mux setting records.
The old method using separate records to program the mux devices still
works but is not thread save and thus not recommended.
However, be aware that this mechanism is not process save, only thread
save.
Other processes may re-program the mux while this driver uses it.
To get a process save way to program mux devices, they must be handled by
the Linux kernel and therefore need to be defined in the system device
tree.
This may be difficult for mux devices on pluggable hardware like
transition cards.

Example (a device that requires mux 0x70 to set be to 0 and mux 0x71 to 8):

```
# pevAsynI2cConfigure 0 gtx1143_A0 0x60000050
i2cDevConfigure gtx1143_A0 /sys/devices/{,*/}*localbus/*b0.pon-i2c/i2c* 0x50 0x70`0 0x71`0x8
```

#### ifc1210 device support

The `ifc1210` device support had covered access to PON and SRAM (under
the common name of ELB), to SMON, and the BMR&nbsp;463 DC/DC regulators.
Now all are accessible through regDev, using _toscaPonDevConfigure_,
_toscaSmonDevConfigure_, and _toscaRegDevConfigure_ with SRAM address
space.
While SRAM access through ELB needed an address offset of 0xe000, the
access through _toscaRegDevConfigure_ does not use such an offset.
The BMR&nbsp;463 DC/DC regulators are actually I²C devices and as such
accessible with _i2cDevConfigure_:

```
toscaPonDevConfigure PON
toscaSmonDevConfigure SMON
toscaRegDevConfigure SRAM SRAM 8k
i2cDevConfigure BMR0 "/sys/devices/{,*/}*localbus/*a0.pon-i2c/i2c*" 0x53
i2cDevConfigure BMR1 "/sys/devices/{,*/}*localbus/*a0.pon-i2c/i2c*" 0x5b
i2cDevConfigure BMR2 "/sys/devices/{,*/}*localbus/*a0.pon-i2c/i2c*" 0x63
i2cDevConfigure BMR3 "/sys/devices/{,*/}*localbus/*a0.pon-i2c/i2c*" 0x24
```

#### pevVmeSlave(Main|Target)Config

The split between "Main" and "Target" configuration has been removed.
VME SLAVE "Target" maps can be defined with 1MB granularity anywhere in the
first 256 MB of the A32 address space.
There are two methods to define SLAVE maps to USER or SMEM.
Either create a [SLAVE map](#vme-slave-maps) from the IOC shell that will be
deleted when the IOC terminates.
Or a create a persistent map from the Linux command line that cannot be
deleted.

In the IOC shell (e.g. IOC startup script) use the _toscaMap_ function:

```
# pevVmeSlaveMainConfig AM32 mainbase mainsize
# pevVmeSlaveTargetConfig AM32 targetbase targetsize "" resource resource_address [AUTO]
toscaMap SLAVE:mainbase+targetbase targetsize resource:resource_address [SWAP]
```

Simply add the `targetbase` to the `mainbase` to the VME address.
The `resource` names can be copied using the pev style names or replaced
with the Tosca style names, the driver understands both.

Example

```
# pevVmeSlaveMainConfig("AM32", 0x01000000, 0x01000000)
# pevVmeSlaveTargetConfig("AM32", 0x000000,  0x100000, "", "SH_MEM", 0x000000, "AUTO")
# pevVmeSlaveTargetConfig("AM32", 0x100000,  0x100000, "", "USR1"  , 0x000000, "AUTO")
toscaMap SLAVE:0x01000000 1M SMEM SWAP
toscaMap SLAVE:0x01100000 1M USER SWAP
```

On the Linux command line use the sys file system:

```
echo vme_address:size:resource_address:resource:swap > '/sys/class/vme_user/bus!vme!s0/add_slave_window'
```

Pass `vme_address`, `size` and `resource_address` in hex.
The `resource` name can be either `pci`, `vme`, `usr` or `shm`.
The `swap` argument can be either `yes` or `no`.
(Actually anything starting with `y` or `Y` means `yes`, anything
else means `no`.)

Example

```
echo 0x1000000:0x100000:0:shm:yes > '/sys/class/vme_user/bus!vme!s0/add_slave_window'
echo 0x1100000:0x100000:0:usr:yes > '/sys/class/vme_user/bus!vme!s0/add_slave_window' 
```
