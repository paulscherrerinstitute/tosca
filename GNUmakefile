include /ioc/tools/driver.makefile

BUILDCLASSES=Linux
ARCH_FILTER=eldk52-e500v2
CMPLR=STD
USR_CFLAGS += -I /opt/eldk-5.2/ifc/include/

DIRS = toscalib .
SOURCES = $(wildcard $(DIRS:%=%/*.c))
