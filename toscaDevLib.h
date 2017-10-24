#ifndef toscaDevLib_h
#define toscaDevLib_h

#ifdef __cplusplus
extern "C" {
#endif

/* Functions to start the tosca handler loops as EPICS threads.
   These functions are called automatically
   at startup by toscaDevLib but are exported to
   be accessible from the ioc shell for debugging
   purposes.
*/ 
int toscaIntrLoopStart(void);
int toscaDmaLoopsStart(unsigned int number_of_threads);

#ifdef __cplusplus
}
#endif

#endif
