#include <stdio.h>
#include <string.h>

#include "i2cDev.h"

#include "toscaMap.h"

#include <iocsh.h>
#include <epicsExport.h>

#define TOSCA_EXTERN_DEBUG
#define TOSCA_DEBUG_NAME pev
#include "toscaDebug.h"
epicsExportAddress(int, pevDebug);

/** MASTER WINDOW ******************************************/

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
    
    printf("Compatibility mode! pev[Asyn]Configure replaced by:\n"
        "toscaRegDevConfigure %s, %s%s:0x%x, 0x%x %s\n",
        args[1].sval, cardstr, toscaAddrSpaceToStr(addr.aspace), args[3].ival, args[6].ival, flags);
    if (toscaRegDevConfigure(args[1].sval, addr.aspace, args[3].ival, args[6].ival, flags) != 0)
    {
        perror("toscaRegDevConfigure failed");
    }
}

/** VME SLAVE WINDOW ***************************************/

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
        error("usage: pevVmeSlaveMainConfig (\"AM24\"|\"AM32\", base, size)");
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
        error("usage: pevVmeSlaveTargetConfig (\"AM32\", base, size, \"BLT\"|\"MBLT\"|\"2eVME\"|\"2eSST160\"|\"2eSST233\"|\"2eSST320\", \"SH_MEM\"|\"PCIE\"|\"USR1/2\", offset, \"WS\"|\"DS\"|\"QS\")");
        return;
    }

    if (strcmp(slaveAddrSpace, "AM32") != 0)
    {
        error("pevVmeSlaveTargetConfig(): ERROR, can map to AM32 only");
        return;
    }
    addr = toscaStrToAddr(target);
    swap = swapping && strcmp(swapping, "AUTO") == 0;
    printf("Compatibility mode! pevVmeSlaveMainConfig and pevVmeSlaveTargetConfig replaced by:\n"
        "toscaMapVMESlave %s:0x%x, 0x%x, 0x%x%s\n",
        toscaAddrSpaceToStr(addr.aspace), targetOffset, winSize, mainBase+winBase, swap ? " 1" : "");
    if (toscaMapVMESlave(addr.aspace, targetOffset, winSize, mainBase+winBase, swap) != 0)
    {
        perror("toscaMapVMESlave failed");
    }
}

/** I2C ****************************************************/

static const iocshArg * const pevI2cConfigureArgs[] = {
    &(iocshArg) { "crate", iocshArgInt },
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "i2cControlWord", iocshArgInt },
    &(iocshArg) { "command", iocshArgInt },
};

static const iocshFuncDef pevI2cConfigureDef =
    { "pevI2cConfigure", 4, pevI2cConfigureArgs };
static const iocshFuncDef pevAsynI2cConfigureDef =
    { "pevAsynI2cConfigure", 4, pevI2cConfigureArgs };

static void pevI2cConfigureFunc(const iocshArgBuf *args)
{
    unsigned int card = args[0].ival;
    const char* name = args[1].sval;
    unsigned int controlword = args[2].ival;
    int i2c_addr = (controlword & 0x7f) | ((controlword & 0x70) >> 8);
    int pev_i2c_bus = controlword >> 29;
    int pon_addr = 0x80 + (pev_i2c_bus << 4);
    char sysfspattern[80];
    
    debug("card=%d, name=%s, controlword=0x%08x bus=elb-%d ponaddr=0x%02x addr=0x%02x", 
        card, name, controlword, pev_i2c_bus, pon_addr, i2c_addr);

    /* pev i2c adapters are the ones on localbus/pon */
    sprintf(sysfspattern, "/sys/devices/*.localbus/*%02x.pon-i2c/i2c-*", pon_addr);
    printf("Compatibility mode! pev[Asyn]I2cConfigure replaced by:\n"
        "toscaI2cConfigure %s, 0x%x, 0x%x\n", name, pon_addr, i2c_addr);
    i2cDevConfigure(name, sysfspattern, i2c_addr, 0);
}

/***********************************************************/

static void pevRegistrar(void)
{
    iocshRegister(&pevConfigureDef, pevConfigureFunc);
    iocshRegister(&pevAsynConfigureDef, pevConfigureFunc);
    iocshRegister(&pevVmeSlaveMainConfigDef, pevVmeSlaveMainConfigFunc);
    iocshRegister(&pevVmeSlaveTargetConfigDef, pevVmeSlaveTargetConfigFunc);
    iocshRegister(&pevI2cConfigureDef, pevI2cConfigureFunc);
    iocshRegister(&pevAsynI2cConfigureDef, pevI2cConfigureFunc);

    toscaRegDevConfigure("pev_csr", TOSCA_CSR, 0, 0x2000, NULL);
}

epicsExportRegistrar(pevRegistrar);

