#include <time.h>
#include "toscaIntr.h"

void handler(void* arg, int inum, int ivec) {
    struct timespec tp;
    char timestr[30];
    clock_gettime(CLOCK_REALTIME, &tp);
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", localtime(&tp.tv_sec));
    if (ivec)
        printf("%s.%09ld %s-%d.%d\n", timestr, tp.tv_nsec, (char*) arg, inum, ivec);
    else
        printf("%s.%09ld %s-%d\n", timestr, tp.tv_nsec, (char*) arg, inum);
}

int main() {
    toscaIntrConnectHandler(TOSCA_USER1_INTR_ANY, handler, "USER1");
    toscaIntrConnectHandler(TOSCA_USER1_INTR_ANY, handler, "USER2");
    toscaIntrConnectHandler(TOSCA_VME_INTR_ANY_VECS(0,255), handler, "VME");
    toscaIntrLoop(NULL);
    return 0;
}
