#ifndef toscaPev_h
#define toscaPev_h

#include <pevulib.h>
#include <pevxulib.h>

#ifdef __cplusplus
extern "C" {
#endif

volatile void* pevMapExt(unsigned int card, unsigned int sg_id, unsigned int map_mode, size_t logicalAddress, size_t size, unsigned int flags, size_t localAddress);

#define pevMap(card, sg_id, map_mode, logicalAddress, size) \
    pevMapExt((card), (sg_id), (map_mode), (logicalAddress), (size), 0, 0)

#define pevMapToAddr(card, sg_id, map_mode, logicalAddress, size, localAddress) \
    pevMapExt((card), (sg_id), (map_mode), (logicalAddress), (size), MAP_FLAG_FORCE, (localAddress))


#define EVT_SRC_VME_ANY_LEVEL ((EVT_SRC_VME+1)|((EVT_SRC_VME+7)<<8))

int pevIntrConnect(unsigned int card, unsigned int src_id, unsigned int vec_id, void (*func)(), void* usr);

int pevIntrDisconnect(unsigned int card, unsigned int src_id, unsigned int vec_id, void (*func)(), void* usr);

int pevIntrEnable(unsigned int card, unsigned int src_id);

int pevIntrDisable(unsigned int card, unsigned int src_id);


#define DMA_SPACE_BUF DMA_SPACE_MASK

void* pevDmaAlloc(unsigned int card, size_t size);

void* pevDmaFree(unsigned int card, void* oldptr);

void* pevDmaRealloc(unsigned int card, void* oldptr, size_t size);

typedef void (*pevDmaCallback)(void* usr, int status);

int pevDmaTransfer(unsigned int card, unsigned int src_space, size_t src_addr, unsigned int des_space, size_t des_addr, size_t size, unsigned int dont_use,
    unsigned int priority, pevDmaCallback callback, void *usr);

#define pevDmaTransferWait(card, src_space, src_addr, des_space, des_addr, size, dont_use) \
    pevDmaTransfer((card), (src_space), (src_addr), (des_space), (des_addr), (size), 0, 0, NULL, NULL)

#define pevDmaFromBuffer(card, buffer, des_space, des_addr, size, dont_use, priority, callback, usr) \
    pevDmaTransfer((card), DMA_SPACE_BUF, (size_t)(void*)(buffer), (des_space), (des_addr), (size), (dont_use), (priority), (callback), (usr))

#define pevDmaToBuffer(card, src_space, src_addr, buffer, size, dont_use, priority, callback, usr) \
    pevDmaTransfer((card), (src_space), (src_addr), DMA_SPACE_BUF, (size_t)(void*)(buffer), (size), (dont_use), (priority), (callback), (usr))

#define pevDmaFromBufferWait(card, buffer, des_space, des_addr, size, dont_use) \
    pevDmaTransfer((card), DMA_SPACE_BUF, (size_t)(void*)(buffer), (des_space), (des_addr), (size), (dont_use), 0, NULL, NULL)

#define pevDmaToBufferWait(card, src_space, src_addr, buffer, size, dont_use) \
    pevDmaTransfer((card), (src_space), (src_addr), DMA_SPACE_BUF, (size_t)(void*)(buffer), (size), (dont_use), 0, NULL, NULL)


#ifdef __cplusplus
}
#endif

#endif /* toscaPev_h */
