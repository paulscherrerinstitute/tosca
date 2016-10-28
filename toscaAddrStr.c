#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "toscaMap.h"
#include "toscaAddrStr.h"

#define TOSCA_DEBUG_NAME toscaAddrString
#include "toscaDebug.h"

vmeaddr_t toscaStrToOffset(const char* str)
{
    char* p = (char*)str, *q;
    vmeaddr_t size = 0, val;
    if (!str) return 0;
    while (1)
    {
        if (!*p) return size;
        val = strtoull(p, &q, 0);
        if (q == p) goto fail;
        switch (*q)
        {
            case 'e':
            case 'E':
                val <<= 60; break;
            case 'p':
            case 'P':
                val <<= 50; break;
            case 't':
            case 'T':
                val <<= 40; break;
            case 'g':
            case 'G':
                val <<= 30; break;
            case 'm':
            case 'M':
                val <<= 20; break;
            case 'k':
            case 'K':
                val <<= 10; break;
            case 0:
                return size + val;
            default:
                goto fail;
        }
        size += val;
        p = q+1;
        if (!*p) return size;
    }
fail:
    if (q > str) error("rubbish \"%s\" after %.*s", q, (int)(q-str), str);
    errno = EINVAL;
    return -1;
}

size_t toscaStrToSize(const char* str)
{
    vmeaddr_t size = toscaStrToOffset(str);
    if (size == -1) return -1;
    if (size & ~(vmeaddr_t)(size_t) -1)
    {
        error("%s too big for %u bit", str, (int)sizeof(size_t)*8);
        errno = EINVAL;
        return -1;
    }
    return size;
}

toscaMapAddr_t toscaStrToAddr(const char* str)
{
    toscaMapAddr_t result = {0};
    unsigned long card;
    char *s, *p;
    
    if (!str) return result;
    
    card = strtoul(str, &s, 0);
    if (*s == ':')
    {
        s++;
        result.aspace = card << 16;
    }
    else
        s = (char*) str;
    p = strchr(s, ':');
    result.address = toscaStrToOffset(p ? p+1 : s);
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
        (strncmp(s, "SHM", 3) == 0  && (s+=3)))
        result.aspace |= TOSCA_SHM;
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
    if (result.address == -1)
    {
        if (!p) result.address = 0;
        else return (toscaMapAddr_t){0};
    }
    debug("0x%x=%u:%s:0x%llx",
        result.aspace, result.aspace>>16,
        toscaAddrSpaceToStr(result.aspace),
        (unsigned long long) result.address);
    if (*s == 0) return result;
    return result;
}
