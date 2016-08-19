#include <stdio.h>
#include <string.h>

#include "toscaMap.h"

#include <iocsh.h>
#include <epicsExport.h>

int pevDebug;
epicsExportAddress(int, pevDebug);
FILE* pevDebugFile = NULL;

#define debug_internal(m, fmt, ...) if(m##Debug) fprintf(m##DebugFile?m##DebugFile:stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#define debugErrno(fmt, ...) debug(fmt " failed: %m", ##__VA_ARGS__)
#define debug(fmt, ...) debug_internal(pev, fmt, ##__VA_ARGS__)
#define error(fmt, ...) fprintf(stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)

/* pev compatibility mode */
extern int toscaRegDevConfigure(const char* name, const char* resource, size_t addr, size_t size, const char* flags);

static const iocshArg * const pevConfigureArgs[] = {
    &(iocshArg) { "card", iocshArgInt },
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "resource", iocshArgString },
    &(iocshArg) { "offset", iocshArgInt },
    &(iocshArg) { "protocol", iocshArgString },
    &(iocshArg) { "intrVec", iocshArgInt },
    &(iocshArg) { "mapSize", iocshArgInt },
    &(iocshArg) { "blockMode", iocshArgInt },
    &(iocshArg) { "swap", iocshArgString },
    &(iocshArg) { "vmePktSize", iocshArgInt },
};

static const iocshFuncDef pevConfigureDef =
    { "pevConfigure", 10, pevConfigureArgs };
static const iocshFuncDef pevAsynConfigureDef =
    { "pevAsynConfigure", 10, pevConfigureArgs };

static void pevConfigureFunc(const iocshArgBuf *args)
{
    char flags[40] = "";
    int l = 0;
    const char *resource;
    unsigned int aspace;

    debug("card=%d, name=%s, resource=%s, offset=0x%x, protocol=%s, intrVec=%d, mapSize=0x%x, blockMode=%d, swap=%s, vmePktSize=%d",
        args[0].ival, args[1].sval, args[2].sval, args[3].ival, args[4].sval, args[5].ival, args[6].ival, args[7].ival, args[8].sval, args[9].ival);

    if (args[0].ival != 0)
    {
        printf ("Only card 0 is supported\n");
        return;
    }
    resource = args[2].sval;
    aspace = toscaStrToAddrSpace(resource);
    if (aspace) resource = toscaAddrSpaceToStr(aspace);
    
    if (args[5].ival) /* intrVec */
        l += sprintf(flags+l, "intr=%d ", args[5].ival - (aspace & (VME_A16|VME_A24|VME_A32|VME_A64) ? 0 : 1));
    if (args[7].ival && l < sizeof(flags) - 6) /* blockMode */
        l += sprintf(flags+l, "block ");
    if (args[4].sval && l < sizeof(flags)) /* protocol */
    {
        if (strstr(args[4].sval, "BLT") || strstr(args[4].sval, "2e"))
            l += sprintf(flags+l, "%.*s ", sizeof(flags)-1-l, args[4].sval);
    }
    if (args[8].sval && l < sizeof(flags)) /* swap */
        l += sprintf(flags+l, "%.*s ", sizeof(flags)-1-l, args[8].sval);
    if (args[9].ival && l < sizeof(flags)-10) /* vmePktSize */
        l += sprintf(flags+l, "bs=%d ", args[9].ival);
    if (l) flags[l-1] = 0;
    
    printf("Compatibility mode! pevConfigure call replaced by:\ntoscaRegDevConfigure %s %s 0x%x 0x%x %s\n",
                      args[1].sval, resource, args[3].ival, args[6].ival, flags);
    toscaRegDevConfigure(args[1].sval, resource, args[3].ival, args[6].ival, flags);
}

static void pevRegistrar(void)
{
    iocshRegister(&pevConfigureDef, pevConfigureFunc);
    iocshRegister(&pevAsynConfigureDef, pevConfigureFunc);
}

epicsExportRegistrar(pevRegistrar);

void* pev_init(int x)
{
    printf("pev compatibility mode\n");
    return (void*) -1;
}

void* pevx_init(int x)
{
    printf("pevx compatibility mode\n");
    return (void*) -1;
}

int pev_csr_rd(int addr)
{
    return toscaCsrRead(addr & 0x7FFFFFFF);
}

void pev_csr_wr(int addr, int val)
{
    debug ("pev_csr_wr(0x%x,0x%x)  -> toscaCsrWrite(0x%x,0x%x)", addr, val, addr & 0x7FFFFFFF, val);
    toscaCsrWrite(addr & 0x7FFFFFFF, val);  
}

void pev_csr_set(int addr, int val)
{
    toscaCsrSet(addr & 0x7FFFFFFF, val);  
}

int pev_elb_rd(int addr)
{
    fprintf(stderr, "%s not implemented\n", __FUNCTION__);
    return -1;
}

int pev_smon_rd(int addr)
{
    fprintf(stderr, "%s not implemented\n", __FUNCTION__);
    return -1;
}

int pev_bmr_read(unsigned int card, unsigned int addr, unsigned int *val, unsigned int count)
{
    fprintf(stderr, "%s not implemented\n", __FUNCTION__);
    return -1;
}

float pev_bmr_conv_11bit_u(unsigned short val)
{
    fprintf(stderr, "%s not implemented\n", __FUNCTION__);
    return 0.0/0.0;
}

float pev_bmr_conv_11bit_s(unsigned short val)
{
    fprintf(stderr, "%s not implemented\n", __FUNCTION__);
    return 0.0/0.0;
}

float pev_bmr_conv_16bit_u(unsigned short val)
{
    fprintf(stderr, "%s not implemented\n", __FUNCTION__);
    return 0.0/0.0;
}

int pev_elb_wr(int addr, int val)
{
    fprintf(stderr, "%s not implemented\n", __FUNCTION__);
    return -1;
}

void pev_smon_wr(int addr, int val)
{
    fprintf(stderr, "%s not implemented\n", __FUNCTION__);
}

int pev_bmr_write(unsigned int card, unsigned int addr, unsigned int val, unsigned int count)
{
    fprintf(stderr, "%s not implemented\n", __FUNCTION__);
    return -1;
}

