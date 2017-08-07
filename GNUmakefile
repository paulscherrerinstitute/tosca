include /ioc/tools/driver.makefile

EXCLUDE_VERSIONS=3.13 3.14.8
BUILDCLASSES=Linux
ARCH_FILTER=eldk52-e500v2 fslqoriq20-e6500_64
CMPLR=STD
USR_CFLAGS += -I /opt/eldk-5.2/ifc/include/

DIRS = toscaApi .
SOURCES = $(wildcard $(DIRS:%=%/[^_]*.c))
HEADERS = $(wildcard $(DIRS:%=%/tosca*.h))
