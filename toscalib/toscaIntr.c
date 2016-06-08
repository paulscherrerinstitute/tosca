#include <sys/select.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include "toscaIntr.h"

int toscaIntrDebug;
FILE* toscaIntrDebugFile = NULL;

#define debug_internal(m, fmt, ...) if(m##Debug) fprintf(m##DebugFile?m##DebugFile:stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#define debug(fmt, ...) debug_internal(toscaIntr, fmt, ##__VA_ARGS__)

#define ADD_FD(fd, name, ...)                               \
do {                                                        \
    if (fd == 0) {                                          \
        sprintf(filename, name , ## __VA_ARGS__ );          \
        fd = open(filename, O_RDWR);                        \
        if (fd < 0) debug("open %s failed: %m", filename);  \
    }                                                       \
    if (fd >= 0) {                                          \
        FD_SET(fd, &readfs);                                \
        if (fd > fdmax) fdmax = fd;                         \
    }                                                       \
} while (0)

#define CHECK_FD(first, last, fd, retval)                        \
do { for (i=first; i <= last; i++)                               \
    if ((mask & (retval)) && fd > 0 && FD_ISSET(fd, &readfs)) {  \
        write(fd, NULL, 0);                                      \
        return retval;                                           \
    }                                                            \
} while (0)

int toscaWaitForIntr(uint64_t mask, uint8_t vector, struct timeval *timeout)
{
    static int fd_intr[7*256+3+32] = {0};
    #define FD_VME(i,v) fd_intr[(i<<8)+v]
    #define FD_ERR(i)   fd_intr[7*256+i]
    #define FD_USER(i)  fd_intr[7*256+3+i]

    fd_set readfs;
    int fdmax = -1;
    int i;
    char filename[30];
    
    FD_ZERO(&readfs);
    if (mask & (INTR_USER1_ANY | INTR_USER2_ANY))
        for (i = 0; i < 32; i++)
        {
            if (!(mask & INTR_USER1_INTR(i))) continue;
            ADD_FD(FD_USER(i), "/dev/toscauserevent%d.%d", i & INTR_USER2_ANY ? 2 : 1, i & 15);
        }
    if (mask & INTR_VME_LVL_ANY)
        for (i = 1; i <= 7; i++)
        {
            if (!(mask & INTR_VME_LVL(i))) continue;
            ADD_FD(FD_VME(i,vector), "/dev/toscavmeevent%d.%d", i, vector);
        }
    if (mask & INTR_VME_SYSFAIL)
        ADD_FD(FD_ERR(0), "/dev/toscavmesysfail");
    if (mask & INTR_VME_ACFAIL)
        ADD_FD(FD_ERR(1), "/dev/toscavmeacfail");
    if (mask & INTR_VME_ERROR)
        ADD_FD(FD_ERR(2), "/dev/toscavmeerror");
 
    if (select(fdmax + 1, &readfs, NULL, NULL, timeout) == 1)
        return -1; /* Error waiting, probably timeout. */
    
    /* handle VME_SYSFAIL, VME_ACFAIL, VME_ERROR */
    if (mask & (INTR_VME_SYSFAIL | INTR_VME_ACFAIL | INTR_VME_ERROR))
        CHECK_FD(0, 2, FD_ERR(i), INTR_VME_SYSFAIL<<i);
    /* handle VME_LVL */
    if (mask & INTR_VME_LVL_ANY)
        CHECK_FD(1, 7, FD_VME(i,vector), INTR_VME_LVL(i));
    /* handle USER */
    if (mask & (INTR_USER1_ANY | INTR_USER2_ANY))
        CHECK_FD(0, 31, FD_USER(i), INTR_USER1_INTR(i));
    /* should be unreachable */
    return 0;
}
