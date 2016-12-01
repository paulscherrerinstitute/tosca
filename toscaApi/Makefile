LIB=toscaApi
PROGS=$(patsubst _%.c,%,$(wildcard _*.c))

#LINKTYPE=STATIC
LINKTYPE=SHARED

SOURCES+=$(wildcard *.c)
SOURCES+=../../symbolname/symbolname_GNU.c
SOURCES+=../../i2cDev/i2c.c
SOURCES+=../../sysfs/sysfs.c
SOURCES+=../../memDisplay/memDisplay.c

HEADERS+=tosca*.h
HEADERS+=../../symbolname/symbolname.h
HEADERS+=../../i2cDev/i2c.h
HEADERS+=../../sysfs/sysfs.h
HEADERS+=../../memDisplay/memDisplay.h

INSTALLDIR=/usr/local
ROOTFS=/net/gfa-ifc-fs/export/eldk52-e500v2

SRCDIRS=$(sort $(dir $(SOURCES)))
vpath %.c $(SRCDIRS)

INCLUDES+=/opt/eldk-5.2/ifc/include
INCLUDES+=$(sort $(dir $(HEADERS)))

OBJS=$(notdir $(SOURCES:%.c=%.o))
LIBNAME=lib$(LIB).a
SHLIBNAME=lib$(LIB).so$(addprefix .,$(shell git describe --tags HEAD))

CROSSPREFIX=/opt/eldk-5.2/powerpc-e500v2/sysroots/i686-eldk-linux/usr/bin/ppce500v2-linux-gnuspe/powerpc-e500v2-

CC=$(CROSSPREFIX)gcc
LD=$(CROSSPREFIX)g++
AR=$(CROSSPREFIX)ar

CFLAGS= -fPIC -O3 -g -Wall $(addprefix -I,$(INCLUDES))
LDFLAGS = -lpthread -lrt -ldl

LDSTATIC = $(LIBNAME)
LDSHARED = -L . -l $(LIB) -Wl,-rpath,$(INSTALLDIR)/lib

build: $(OBJS) $(LIBNAME) $(SHLIBNAME) $(PROGS)

$(SHLIBNAME): $(filter-out _%,$(OBJS))
	@echo making shared lib $@
	$(LD) -shared -Wl,-soname=$@ -o $@ $(LDFLAGS) $^
	rm -f lib$(LIB).so
	ln -s $(SHLIBNAME) lib$(LIB).so

$(LIBNAME): $(filter-out _%,$(OBJS))
	@echo making static lib $@
	$(AR) -rcs $@ $^

%.o: %.c
	@echo compiling $<
	$(CC) -c -o $@ $(CFLAGS) $<

$(PROGS): $(OBJS)
	@echo making program $@
	$(LD) -o $@ $(@:%=_%.o) $(LD$(LINKTYPE)) $(LDFLAGS)

clean:
	rm -rf *.i *.s *.o *.a *.so* $(PROGS) include bin lib

install: build
	mkdir -p include bin lib
	rm -rf includ/* bin/* lib/*
	cp $(PROGS) bin
	cp $(LIBNAME) $(SHLIBNAME) lib
	ln -s $(SHLIBNAME) lib/lib$(LIB).so
	cp $(HEADERS) include
	tar cfz - bin lib include | ssh -q root@gfa-eldk tar xfz - -C $(ROOTFS)/$(INSTALLDIR) 
