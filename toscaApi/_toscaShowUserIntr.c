#include <time.h>
#include "toscaIntr.h"

void handler(void* arg, int inum) {
    struct timespec tp;
    char buffer[30];
    clock_gettime(CLOCK_REALTIME, &tp);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&tp.tv_sec));
    printf("%s.%09ld intr %d\n", buffer, tp.tv_nsec, inum);
}

int main() {
    toscaIntrConnectHandler(TOSCA_USER_INTR_ANY, handler, NULL);
    toscaIntrLoop(NULL);
    return 0;
}
