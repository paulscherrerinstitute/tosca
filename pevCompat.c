#include <stdio.h>
#include <string.h>

#include "toscaMap.h"

#include <iocsh.h>
#include <epicsExport.h>

#define TOSCA_DEBUG_NAME pev
#include "toscaDebug.h"
epicsExportAddress(int, pevDebug);

/* pev compatibility mode */
int toscaRegDevConfigure(const char* name, unsigned int aspace, size_t address, size_t size, const char* flags);

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
    unsigned int card;
    toscaMapAddr_t addr;
    char cardstr[10] = "";

    debug("card=%d, name=%s, resource=%s, offset=0x%x, protocol=%s, intrVec=%d, mapSize=0x%x, blockMode=%d, swap=%s, vmePktSize=%d",
        args[0].ival, args[1].sval, args[2].sval, args[3].ival, args[4].sval, args[5].ival, args[6].ival, args[7].ival, args[8].sval, args[9].ival);

    card = args[0].ival;
    resource = args[2].sval;
    addr = toscaStrToAddr(resource);
    
    if (args[5].ival) /* intrVec */
        l += sprintf(flags+l, "intr=%d ", args[5].ival - (addr.aspace & (VME_A16|VME_A24|VME_A32|VME_A64) ? 0 : 1));
    if (args[7].ival & 1 && l < sizeof(flags) - 6) /* blockMode */
    {
        l += sprintf(flags+l, "block ");
    }
    if (args[7].ival & 2 && l < sizeof(flags) - 5) /* DMA-only mode */
    {
        l += sprintf(flags+l, "dmaonly ");
    }
    if (args[4].sval && l < sizeof(flags)) /* protocol */
    {
        if (strstr(args[4].sval, "BLT") || strstr(args[4].sval, "2e"))
            l += sprintf(flags+l, "%.*s ", (int)sizeof(flags)-1-l, args[4].sval);
    }
    if (args[8].sval && l < sizeof(flags)) /* swap */
        l += sprintf(flags+l, "%.*s ", (int)sizeof(flags)-1-l, args[8].sval);
    /* args[9] = vmePktSize ignored */
    if (l) flags[l-1] = 0;
    
    if (card) sprintf(cardstr, "%u:", card);
    
    printf("Compatibility mode! pevConfigure replaced by:\n"
        "toscaRegDevConfigure %s %s%s:0x%x 0x%x %s\n",
        args[1].sval, cardstr, toscaAddrSpaceToStr(addr.aspace), args[3].ival, args[6].ival, flags);
    if (toscaRegDevConfigure(args[1].sval, addr.aspace, args[3].ival, args[6].ival, flags) != 0)
    {
        fprintf(stderr, "toscaRegDevConfigure failed: %m\n");
    }
}

static const iocshFuncDef pevVmeSlaveMainConfigDef =
    { "pevVmeSlaveMainConfig", 3, (const iocshArg *[]) {
    &(iocshArg) { "addrSpace", iocshArgString },
    &(iocshArg) { "mainBase", iocshArgInt },
    &(iocshArg) { "mainSize", iocshArgInt },
}};

static unsigned int mainBase;

static void pevVmeSlaveMainConfigFunc (const iocshArgBuf *args)
{
    const char* addrSpace = args[0].sval;

    if (!addrSpace)
    {
        fprintf(stderr, "usage: pevVmeSlaveMainConfig (\"AM24\"|\"AM32\", base, size)\n");
        return;
    }
    mainBase = args[1].ival;
}
		
static const iocshFuncDef pevVmeSlaveTargetConfigDef =
    { "pevVmeSlaveTargetConfig", 7, (const iocshArg *[]) {
    &(iocshArg) { "slaveAddrSpace", iocshArgString },
    &(iocshArg) { "winBase", iocshArgInt },
    &(iocshArg) { "winSize", iocshArgInt },
    &(iocshArg) { "protocol", iocshArgString },
    &(iocshArg) { "target", iocshArgString },
    &(iocshArg) { "targetOffset", iocshArgInt },
    &(iocshArg) { "swapping", iocshArgString },
}};
    
static void pevVmeSlaveTargetConfigFunc (const iocshArgBuf *args)
{
    char* slaveAddrSpace = args[0].sval;
    unsigned int winBase = args[1].ival;
    unsigned int winSize = args[2].ival;
    const char* target = args[4].sval;
    unsigned int targetOffset = args[5].ival;
    const char* swapping = args[6].sval;
    toscaMapAddr_t addr;
    int swap;

    if (!slaveAddrSpace)
    {
        fprintf(stderr, "usage: pevVmeSlaveTargetConfig (\"AM32\", base, size, \"BLT\"|\"MBLT\"|\"2eVME\"|\"2eSST160\"|\"2eSST233\"|\"2eSST320\", \"SH_MEM\"|\"PCIE\"|\"USR1/2\", offset, \"WS\"|\"DS\"|\"QS\")\n");
        return;
    }

    if (strcmp(slaveAddrSpace, "AM32") != 0)
    {
        fprintf(stderr, "pevVmeSlaveTargetConfig(): ERROR, can map to AM32 only\n");
        return;
    }
    addr = toscaStrToAddr(target);
    swap = swapping && strcmp(swapping, "AUTO") == 0;
    printf("Compatibility mode! pevVmeSlaveMainConfig and pevVmeSlaveTargetConfig replaced by:\n"
        "toscaMapVMESlave %s:0x%x 0x%x 0x%x%s\n",
        toscaAddrSpaceToStr(addr.aspace), targetOffset, winSize, mainBase+winBase, swap ? " 1" : "");
    if (toscaMapVMESlave(addr.aspace, targetOffset, winSize, mainBase+winBase, swap) != 0)
    {
        fprintf(stderr, "toscaMapVMESlave failed: %m\n");
    }
}

static void pevRegistrar(void)
{
    iocshRegister(&pevConfigureDef, pevConfigureFunc);
    iocshRegister(&pevAsynConfigureDef, pevConfigureFunc);
    iocshRegister(&pevVmeSlaveMainConfigDef, pevVmeSlaveMainConfigFunc);
    iocshRegister(&pevVmeSlaveTargetConfigDef, pevVmeSlaveTargetConfigFunc);

    toscaRegDevConfigure("pev_csr", TOSCA_CSR, 0, 0x2000, NULL);
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

