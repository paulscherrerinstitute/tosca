CONFIG=/usr/local/epics/base/configure
include $(CONFIG)/CONFIG.CrossCommon
include $(CONFIG)/os/CONFIG.Common.eldk52-e500v2

vpath %.c ../toscalib

CFLAGS = -Wall -I. -I../toscalib

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROG): $(PROG).o toscaMap.o
	$(CC) $(LDLAGS) -o $@ $^

clean:
	rm -rf *.o $(PROG)
