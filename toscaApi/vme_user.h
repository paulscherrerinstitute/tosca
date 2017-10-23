#ifndef _VME_USER_H_
#define _VME_USER_H_

#define VME_USER_BUS_MAX	1

/*
 * VMEbus Master Window Configuration Structure
 */
struct vme_master {
	__u32 enable;			/* State of Window */
	__u64 vme_addr;			/* Starting Address on the VMEbus */
	__u64 size;				/* Window Size */
	__u32 aspace;			/* Address Space */
	__u32 cycle;			/* Cycle properties */
	__u32 dwidth;			/* Maximum Data Width */
#if 0
	char prefetchenable;	/* Prefetch Read Enable State */
	int prefetchsize;		/* Prefetch Read Size (Cache Lines) */
	char wrpostenable;		/* Write Post State */
#endif
};

/*
 * IOCTL Commands and structures
 */

/* Magic number for use in ioctls */
#define VME_IOC_MAGIC 0xAE
#define VME_DMA_IOC_MAGIC 0xAF

/* VMEbus Slave Window Configuration Structure */
struct vme_slave {
	__u32 enable;			/* State of Window */
	__u64 vme_addr;			/* Starting Address on the VMEbus */
	__u64 size;				/* Window Size */
	__u32 aspace;			/* Address Space */
	__u32 cycle;			/* Cycle properties */
	__u32 resource_offset;
#if 0
	char wrpostenable;		/* Write Post State */
	char rmwlock;			/* Lock PCI during RMW Cycles */
	char data64bitcapable;	/* non-VMEbus capable of 64-bit Data */
#endif
};

struct vme_irq_id {
	__u8 level;
	__u8 statid;
};

/* DMA data structures */
struct dma_execute {
	__u32 wait;
	__u32 execute;
};

struct dma_request {
	__u64 vme_addr;
	__u64 src_addr;	/* Starting Address */
	__u64 dst_addr;	/* Destination address */
	__u32 src_type;
	__u32 dst_type;
	__u32 size;
	__u32 route;    /* depreciated */
	__u32 aspace;
	__u32 dwidth;	/* it contains also endianess */
	__u32 cycle;
};

/*
 * Todo : this is duplicated from vme.h
 * One file should be exported via uapi
 */
#define VME_DMA_PATTERN				(1<<0)
#define VME_DMA_PCI					(1<<1)
#define VME_DMA_VME					(1<<2)
#define VME_DMA_SHM1				(1<<3)
#define VME_DMA_USER1				(1<<4)
#define VME_DMA_SHM2                (1<<5)
#define VME_DMA_USER2               (1<<6)

#define VME_DMA_PATTERN_BYTE		(1<<0)
#define VME_DMA_PATTERN_WORD		(1<<1)
#define VME_DMA_PATTERN_INCREMENT	(1<<2)

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

#define VME_NO_SWAP     0
#define VME_LE_TO_BE    (1 << 8)
#define VME_DW_BW_SWAP  (2 << 8)

#define VME_GET_SLAVE _IOR(VME_IOC_MAGIC, 1, struct vme_slave)
#define VME_SET_SLAVE _IOW(VME_IOC_MAGIC, 2, struct vme_slave)
#define VME_GET_MASTER _IOR(VME_IOC_MAGIC, 3, struct vme_master)
#define VME_SET_MASTER _IOW(VME_IOC_MAGIC, 4, struct vme_master)
#define VME_IRQ_GEN _IOW(VME_IOC_MAGIC, 5, struct vme_irq_id)

#define VME_DMA_SET _IOR(VME_DMA_IOC_MAGIC, 1, struct dma_request)
#define VME_DMA_EXECUTE _IOR(VME_DMA_IOC_MAGIC, 2, struct dma_execute)
#define VME_DMA_RESERVE _IOR(VME_DMA_IOC_MAGIC, 3, struct dma_request)
#define VME_DMA_FREE  _IO(VME_DMA_IOC_MAGIC, 4)
#define VME_DMA_ALLOC_BUFFER  _IOR(VME_DMA_IOC_MAGIC, 5, size_t)
#define VME_DMA_FREE_BUFFER  _IO(VME_DMA_IOC_MAGIC, 6)
#define VME_DMA_TIMEOUT  _IOR(VME_DMA_IOC_MAGIC, 7, long)

#endif /* _VME_USER_H_ */
