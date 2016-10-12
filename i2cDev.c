#include <glob.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <epicsTypes.h>
#include <iocsh.h>
#include <regDev.h>
#include <epicsExport.h>


int i2cDevDebug;
epicsExportAddress(int, i2cDevDebug);

struct regDevice
{
    int fd;
};

int i2cDevRead(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    int priority,
    regDevTransferComplete callback,
    const char* user)
{
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data;
    int i;
    
    if (dlen == 0) return 0; /* any way to check online status ? */
    if (dlen > 2)
    {
        if (i2cDevDebug) fprintf(stderr, "%s %s: only 1 or 2 bytes supported\n", user, regDevName(device));
        return -1;
    }
    args.read_write = I2C_SMBUS_READ;
    args.size = 1 + dlen;
    args.data = &data;
    
    for (i = 0; i < nelem; i++)
    {
        args.command = offset + i;
        if (ioctl(device->fd, I2C_SMBUS, &args) < 0)
        {
            if (i2cDevDebug) perror("ioctl I2C_SMBUS failed");
            return -1;
        }
        switch (dlen)
        {
            case 1:
                ((epicsUInt8*) pdata)[i] = data.byte;
                break;
            case 2:
                ((epicsUInt16*) pdata)[i] = data.word;
                break;
        }
    }
    return 0;
}

int i2cDevWrite(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    void* pmask,
    int priority,
    regDevTransferComplete callback,
    const char* user)
{
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data;
    int i;
    
    if (dlen > 2)
    {
        if (i2cDevDebug) fprintf(stderr, "%s %s: only 1 or 2 bytes supported\n", user, regDevName(device));
        return -1;
    }
    args.read_write = I2C_SMBUS_WRITE;
    args.size = 1 + dlen;
    args.data = &data;
    
    for (i = 0; i < nelem; i++)
    {
        args.command = offset + i;
        switch (dlen)
        {
            case 1:
                data.byte = ((epicsUInt8*) pdata)[i];
                break;
            case 2:
                data.word = ((epicsUInt16*) pdata)[i];
                break;
        }
        if (ioctl(device->fd, I2C_SMBUS, &args) < 0)
        {
            if (i2cDevDebug) perror("ioctl I2C_SMBUS failed");
            return -1;
        }
    }
    return 0;
}

struct regDevSupport i2cDevRegDev = {
    .read = i2cDevRead,
    .write = i2cDevWrite,
};

int i2cDevOpen(const char* busname)
{
    glob_t globinfo;
    char* p;
    int busnum;
    int fd;
    char filename[80];
    
    if (i2cDevDebug) printf("i2cDevOpen %s\n", busname);
    
    if (!busname || !busname[0])
    {
        errno = EINVAL;
        return -1;
    }
    /* maybe busname is a device file? */
    fd = open(busname, O_RDWR);
    if (i2cDevDebug) printf("i2cDevOpen: open %s returned %d\n", filename, fd);
    if (fd >= 0)
    {
        return fd;
    }

    /* maybe busname is a number? */
    busnum = strtol(busname, &p, 10);
    if (*p == 0)
    {
        if (i2cDevDebug) printf("i2cDevOpen: %d is bus number\n", busnum );
    }
    if (*p != 0)
    {
        /* maybe busname is a sysfs pattern? */
        if (glob(busname, 0, NULL, &globinfo) != 0)
        {
            if (i2cDevDebug) printf("i2cDevOpen: glob failed\n");
            return -1;
        }
        if (i2cDevDebug) printf("i2cDevOpen: glob found %s\n", globinfo.gl_pathv[0]);
        p = strrchr(globinfo.gl_pathv[0], '-');
        if (!p)
        {
            if (i2cDevDebug) printf("i2cDevOpen: no - found in %s\n", globinfo.gl_pathv[0]);
            return -1;
        }
        p++;
        if (i2cDevDebug) printf("i2cDevOpen: look up number in '%s'\n", p);
        busnum = strtol(p, &p, 10);
        globfree(&globinfo);
        if (*p != 0)
        {
            if (i2cDevDebug) printf("i2cDevOpen: %s does not end in number\n", globinfo.gl_pathv[0]);
            return -1;
        }
        if (i2cDevDebug) printf("i2cDevOpen: bus number is %d\n", busnum );
    }
    sprintf(filename, "/dev/i2c-%d", busnum);
    fd = open(filename, O_RDWR);
    if (i2cDevDebug) printf("i2cDevOpen: open %s returned %d\n", filename, fd);
    if (fd < 0)
    {
        sprintf(filename, "/dev/i2c/%d", busnum);
        fd = open(filename, O_RDWR);
        if (i2cDevDebug) printf("i2cDevOpen: open %s returned %d\n", filename, fd);
    }
    return fd;
}

int i2cDevConfigure(const char* name, const char* busname, int address, int maxreg)
{
    regDevice *device = NULL;
    int fd;
    
    if (!name || !busname || !name[0] || !busname[0])
    {
        printf("usage: i2cDevConfigure name busname address [maxreg]\n");
        return -1;
    }
    fd = i2cDevOpen(busname);
    if (fd == -1)
    {
        perror("i2c bus not found");
        return -1;
    }
    if (address > 0x3f)
    {
        if (ioctl(fd, I2C_TENBIT, 1))
        {
            perror("ioctl I2C_TENBIT failed");
            goto fail;
        }
    }   
    if (ioctl(fd, I2C_SLAVE_FORCE, address) < 0)
    {
        perror("ioctl I2C_SLAVE failed");
        goto fail;
    }
    device = malloc(sizeof(regDevice));
    if (!device)
    {
        perror("malloc regDevice");
        goto fail;
    }
    device->fd = fd;
    if (regDevRegisterDevice(name, &i2cDevRegDev, device, maxreg ? maxreg + 1 : 0) != SUCCESS)
    {
        perror("regDevRegisterDevice() failed");
        goto fail;
    }
    if (regDevInstallWorkQueue(device, 100) != SUCCESS)
    {
        perror("regDevInstallWorkQueue() failed");
        goto fail;
    }
    return 0;

fail:
    close(fd);
    free(device);
    return -1;
}

static const iocshFuncDef i2cDevConfigureDef =
    { "i2cDevConfigure", 4, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "i2cDev_bus", iocshArgString },
    &(iocshArg) { "address", iocshArgInt },
    &(iocshArg) { "maxreg", iocshArgInt },
}};

static void i2cDevConfigureFunc(const iocshArgBuf *args)
{
    i2cDevConfigure(args[0].sval, args[1].sval, args[2].ival, args[3].ival);
}

static void i2cDevRegistrar(void)
{
    iocshRegister(&i2cDevConfigureDef, i2cDevConfigureFunc);
}

epicsExportRegistrar(i2cDevRegistrar);
