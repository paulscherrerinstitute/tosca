#ifndef toscaElb_h
#define toscaElb_h

#ifdef __cplusplus
extern "C" {
#endif

const char* toscaElbAddrToRegname(int address);
int toscaElbRead(int address);
int toscaElbWrite(int address, int value);
int toscaElbDevConfigure(const char* name);

#ifdef __cplusplus
}
#endif
#endif
