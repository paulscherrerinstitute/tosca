#ifndef toscaPon_h
#define toscaPon_h

#ifdef __cplusplus
extern "C" {
#endif
extern int toscaPonDebug;

const char* toscaPonAddrToRegname(unsigned int address);
unsigned int toscaPonRead(unsigned int address);
int toscaPonWrite(unsigned int address, unsigned int value);

#ifdef __cplusplus
}
#endif
#endif
