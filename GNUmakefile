include /ioc/tools/driver.makefile

EXCLUDE_VERSIONS=3.13 3.14.8
BUILDCLASSES=Linux
#ARCH_FILTER=eldk52-e500v2 fslqoriq20-e6500_64

# The essentials
HEADERS += toscaApi/toscaDebug.h 
SOURCES += toscaApi/toscaMap.c
HEADERS += toscaApi/toscaMap.h
SOURCES += toscaApi/toscaDma.c
HEADERS += toscaApi/toscaDma.h
SOURCES += toscaApi/toscaIntr.c
HEADERS += toscaApi/toscaIntr.h
SOURCES += toscaApi/toscaReg.c
HEADERS += toscaApi/toscaReg.h
HEADERS += toscaApi/toscaApi.h
SOURCES += toscaInit.c
HEADERS += toscaInit.h
DBDS    += toscaInit.dbd

# iocsh access to tosca API functions
SOURCES += toscaIocsh.c
DBDS    += toscaIocsh.dbd

# iocsh access to system monitoring (optional)
SOURCES += toscaSmon.c
DBDS    += toscaSmon.dbd

# regDev device support (optional)
SOURCES += toscaRegDev.c
DBDS    += toscaRegDev.dbd

# regDev and iocsh access to PON FPGA registers (optional)
SOURCES += toscaPon.c
DBDS    += toscaPon.dbd

# EPICS VME support (optional, not needed for non-VME boards)
SOURCES += toscaDevLib.c
DBDS    += toscaDevLib.dbd

# Debug utilities (optional)
SOURCES += toscaUtils.c
DBDS    += toscaUtils.dbd

# Test scripts (optional)
SCRIPTS += $(wildcard *.test)

# Backward compatibility to pev driver (optional)
# requires toscaRegDev
USR_CFLAGS += -I /opt/eldk-5.2/ifc/include/
SOURCES += toscaApi/toscaPev.c
HEADERS += toscaApi/toscaPev.h
SOURCES += pevCompat.c
DBDS    += pevCompat.dbd
SOURCES += ifcDev.c
DBDS    += ifc.dbd
