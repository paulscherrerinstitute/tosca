#ifndef toscaPon_h
#define toscaPon_h

#ifdef __cplusplus
extern "C" {
#endif
extern int toscaPonDebug;

const char* toscaPonAddrToRegname(int address);
int toscaPonRead(int address);
int toscaPonWrite(int address, int value);

#ifdef __cplusplus
}
#endif
#endif
