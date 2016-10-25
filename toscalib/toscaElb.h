#ifndef toscaElb_h
#define toscaElb_h

#ifdef __cplusplus
extern "C" {
#endif
extern int toscaElbDebug;

const char* toscaElbAddrToRegname(int address);
int toscaElbRead(int address);
int toscaElbWrite(int address, int value);

#ifdef __cplusplus
}
#endif
#endif
