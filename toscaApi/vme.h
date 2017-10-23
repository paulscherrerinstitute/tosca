#ifndef _VME_H_
#define _VME_H_

/* Resource Type */
enum vme_resource_type {
	VME_MASTER,
	VME_SLAVE,
	VME_DMA,
	VME_LM
};

/* VME Address Spaces */
#define VME_A16		0x1
#define VME_A24		0x2
#define	VME_A32		0x4
#define VME_A64		0x8
#define VME_CRCSR	0x10
#define VME_USER1	0x20
#define VME_USER2	0x40
#define VME_USER3	0x80
#define VME_USER4	0x100

#define VME_SHM     VME_USER3

#define VME_A16_MAX		0x10000ULL
#define VME_A24_MAX		0x1000000ULL
#define VME_A32_MAX		0x100000000ULL
#define VME_A64_MAX		0x10000000000000000ULL
#define VME_CRCSR_MAX	0x1000000ULL

/* VME Cycle Types */
#define VME_SCT			0x1
#define VME_BLT			0x2
#define VME_MBLT		0x4
#define VME_2eVME		0x8
#define VME_2eSST		0x10
#define VME_2eSSTB		0x20
#define VME_2eVMEFast	0x40
#define VME_2eSST160	0x100
#define VME_2eSST267	0x200
#define VME_2eSST320	0x400

#define	VME_SUPER	0x1000
#define	VME_USER	0x2000
#define	VME_PROG	0x4000
#define	VME_DATA	0x8000

/* VME Data Widths */
#define VME_D8		0x1
#define VME_D16		0x2
#define VME_D32		0x4
#define VME_D64		0x8

#define VME_NO_SWAP	     0
#define VME_LE_TO_BE	(1 << 8)
#define VME_DW_BW_SWAP	(2 << 8)
#define VME_DW_QW_SWAP  (4 << 8)

/* Arbitration Scheduling Modes */
#define VME_R_ROBIN_MODE	0x1
#define VME_PRIORITY_MODE	0x2

#define VME_DMA_PATTERN				(1<<0)
#define VME_DMA_PCI					(1<<1)
#define VME_DMA_VME					(1<<2)
#define VME_DMA_SHM1                (1<<3)
#define VME_DMA_USER1               (1<<4)
#define VME_DMA_PATTERN_BYTE		(1<<0)
#define VME_DMA_PATTERN_WORD		(1<<1)
#define VME_DMA_PATTERN_INCREMENT	(1<<2)
#define VME_DMA_SHM2                (1<<5)
#define VME_DMA_USER2               (1<<6)

#define VME_DMA_VME_TO_MEM			(1ULL<<0)
#define VME_DMA_MEM_TO_VME			(1ULL<<1)
#define VME_DMA_VME_TO_VME			(1ULL<<2)
#define VME_DMA_MEM_TO_MEM			(1ULL<<3)
#define VME_DMA_PATTERN_TO_VME		(1ULL<<4)
#define VME_DMA_PATTERN_TO_MEM		(1ULL<<5)
#define VME_DMA_MEM_TO_USER1		(1ULL<<6)
#define VME_DMA_USER1_TO_MEM		(1ULL<<7)
#define VME_DMA_VME_TO_USER1		(1ULL<<8)
#define VME_DMA_USER1_TO_VME		(1ULL<<9)
#define VME_DMA_VME_TO_SHM1			(1ULL<<10)
#define VME_DMA_SHM1_TO_VME			(1ULL<<11)
#define VME_DMA_MEM_TO_SHM1			(1ULL<<12)
#define VME_DMA_SHM1_TO_MEM			(1ULL<<13)
#define VME_DMA_USER1_TO_SHM1		(1ULL<<14)
#define VME_DMA_SHM1_TO_USER1		(1ULL<<15)
#define VME_DMA_MEM_TO_USER2		(1ULL<<16)
#define VME_DMA_MEM_TO_SHM2			(1ULL<<17)
#define VME_DMA_VME_TO_USER2		(1ULL<<18)
#define VME_DMA_VME_TO_SHM2			(1ULL<<19)
#define VME_DMA_SHM1_TO_USER2		(1ULL<<20)
#define VME_DMA_SHM1_TO_SHM2		(1ULL<<21)
#define VME_DMA_USER1_TO_USER2		(1ULL<<22)
#define VME_DMA_USER1_TO_SHM2		(1ULL<<23)
#define VME_DMA_USER2_TO_VME		(1ULL<<24)
#define VME_DMA_USER2_TO_SHM1		(1ULL<<25)
#define VME_DMA_USER2_TO_SHM2		(1ULL<<26)
#define VME_DMA_USER2_TO_MEM		(1ULL<<27)
#define VME_DMA_USER2_TO_USER1		(1ULL<<28)
#define VME_DMA_SHM2_TO_VME			(1ULL<<29)
#define VME_DMA_SHM2_TO_SHM1		(1ULL<<30)
#define VME_DMA_SHM2_TO_MEM			(1ULL<<31)
#define VME_DMA_SHM2_TO_USER1		(1ULL<<32)
#define VME_DMA_SHM2_TO_USER2		(1ULL<<33)

#endif /* _VME_H_ */
