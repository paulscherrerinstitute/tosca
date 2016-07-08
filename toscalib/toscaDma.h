#ifndef toscaDma_h
#define toscaDma_h

const char* toscaDmaRouteToStr(int route);
const char* toscaDmaTypeToStr(int type, int cycle);
typedef void (*toscaDmaCallback)(void* usr, int status);
int toscaDmaTransferWait(int route, size_t source_addr, size_t dst_addr, size_t size, unsigned int dwidth, unsigned int cycle, toscaDmaCallback callback, void *usr);

#endif
