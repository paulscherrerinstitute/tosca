#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "memDisplay.h"

#include "toscaMap.h"
#include "toscaAddrStr.h"

#define TOSCA_EXTERN_DEBUG
#define TOSCA_DEBUG_NAME toscaMap
#include "toscaDebug.h"

size_t toscaStrToSize(const char* str)
{
    char *q;
    if (!str) return 0;
    vmeaddr_t size = strToSize(str, &q);
    if (*q)
    {
        errno = EINVAL;
        return -1;
    }
    if (size & ~(vmeaddr_t)(size_t) -1)
    {
        error("%s too big for %u bit", str, (int)sizeof(size_t)*8);
        errno = EFAULT;
        return -1;
    }
    return size;
}

toscaMapAddr_t toscaStrToAddr(const char* str)
{
    toscaMapAddr_t result = {0};
    unsigned long card;
    char *s;
    
    if (!str) return (toscaMapAddr_t){0};
    
    card = strtoul(str, &s, 0);
    if (*s == ':')
    {
        s++;
        result.aspace = card << 16;
    }
    else
        s = (char*) str;
    if ((strncmp(s, "USR", 3) == 0 && (s+=3)) ||
        (strncmp(s, "USER", 4) == 0 && (s+=4)))
    {
        if (*s == '2')
        {
            result.aspace |= TOSCA_USER2;
            s++;
        }
        else
        {
            result.aspace |= TOSCA_USER1;
            if (*s == '1') s++;
        }
    }
    else
    if ((strncmp(s, "SH_MEM", 6) == 0 && (s+=6)) ||
        (strncmp(s, "SHMEM", 5) == 0 && (s+=5)) ||
        (strncmp(s, "SMEM", 4) == 0 && (s+=4)) ||
        (strncmp(s, "SHM", 3) == 0 && (s+=3)))
        result.aspace |= TOSCA_SMEM;
    else
    if (strncmp(s, "TCSR", 4) == 0 && (s+=4))
        result.aspace |= TOSCA_CSR;
    else
    if (strncmp(s, "TIO", 3) == 0 && (s+=3))
        result.aspace |= TOSCA_IO;
    else
    if (strncmp(s, "SRAM", 4) == 0 && (s+=4))
        result.aspace |= TOSCA_SRAM;
    else
    {
        if (strncmp(s, "VME_", 4) == 0) s+=4;
        if ((strncmp(s, "CRCSR", 5) == 0 && (s+=5)) || 
            (strncmp(s, "CSR", 3) == 0 && (s+=3)))
            result.aspace |= VME_CRCSR;
        else
        if (strncmp(s, "SLAVE", 5) == 0 && (s+=5))
            result.aspace |= VME_SLAVE;
        else
        if (*s == 'A')
        {
            switch (strtol(++s, &s, 10))
            {
                case 16:
                    result.aspace |= VME_A16; break;
                case 24:
                    result.aspace |= VME_A24; break;
                case 32:
                    result.aspace |= VME_A32; break;
                default:
                    return (toscaMapAddr_t){0};
            }
            do
            {
                if (*s == '*') result.aspace |= VME_SUPER;
                else
                if (*s == '#') result.aspace |= VME_PROG;
                else
                break;
            } while (s++);
        }
    }
    if (s > str && *s != 0 && *s != ':') return (toscaMapAddr_t){0};
    if (*s == ':') s++;
    result.address = strToSize(s, &s);
    if (*s) return (toscaMapAddr_t){0};
    return result;
}