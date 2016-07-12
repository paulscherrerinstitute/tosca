#ifndef toscaDma_h
#define toscaDma_h

extern int toscaDmaDebug;

const char* toscaDmaRouteToStr(int route);
const char* toscaDmaTypeToStr(int type, int cycle);
typedef void (*toscaDmaCallback)(void* usr, int status);

enum toscaDmaEndpt{ dmaPattern, dmaRAM, dmaUSER, dmaSHM, dmaVME, dmaBLT, dmaMBLT, dma2eVME, dma2eVMEFast, dma2eSST, dma2eSSTB, dma2eSST160, dma2eSST267, dma2eSST320 };
int toscaDmaTransfer(int route, size_t source_addr, size_t dest_addr, size_t size, unsigned int dwidth, unsigned int cycle);

/*
int toscaDmaTransfer(toscaDmaEndpt source, size_t source_addr, toscaDmaEndpt dest, size_t dst_addr, size_t size, unsigned int dwidth, unsigned int cycle);
*/
#endif

