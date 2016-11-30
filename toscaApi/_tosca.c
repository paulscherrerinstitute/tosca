#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <byteswap.h>
#include "toscaApi.h"

int main(int argc, char** argv)
{
    int wordsize = 0;
    size_t size = 0;
    size_t mapsize;
    size_t cur;
    char* end;
    void* buffer = NULL;
    size_t buffersize = 0;
    
    if (argc < 2)
    {
        fprintf(stderr, "usage: tosca addrspace:address [wordsize] [size]\n");
        return 1;
    }
    toscaMapAddr_t addr = toscaStrToAddr(argv[1], NULL);
    if (argc >= 3)
    {
        wordsize = strtol(argv[2], &end, 0);
        if (*end)
        {
            fprintf(stderr, "rubbish \"%s\" at end of wordsize \"%s\"\n", end, argv[2]);
            return 1;
        }
        switch (wordsize)
        {
            case 1: case 2: case 4: case 8: case -2: case -4: case -8: break;
            default:
                fprintf(stderr, "wordzize must be 1, 2, 4, 8, -2, -4 or -8\n");
                return 1;
        }
    }
    if (argc >= 4)
    {
        size = strToSize(argv[3], &end);
        if (*end)
        {
            fprintf(stderr, "rubbish \"%s\" at end of size \"%s\"\n", end, argv[3]);
            return 1;
        }
    }
    
    volatile void* map = toscaMap(addr.addrspace, addr.address, size ? size : 1, 0);
    if (!map)
    {
        perror(NULL);
        return 1;
    }

    toscaMapInfo_t mapinfo = toscaMapFind(map);
    mapsize = mapinfo.size - (mapinfo.baseptr - map);
    if (size) mapsize = size;
    
    if (wordsize < 0)
    {
        buffersize = 0x1000;
        if (buffersize > mapsize) buffersize = mapsize;
        buffer = malloc(buffersize);
    }
    
    if (!isatty(0))
    {
        cur = 0;
        if (wordsize >= 0)
        {
            while (cur < mapsize)
            {
                ssize_t n = read(0, (char*) map + cur, mapsize - cur);
                if (n < 0) perror(NULL);
                if (n < 1) break;
                cur += n;
            }
        }
        else
        {
            unsigned int i = 0;
            while (i < mapsize / -wordsize)
            {
                unsigned int j = 0;
                ssize_t n = read(0, buffer + cur, buffersize - cur);
                if (n < 0) perror(NULL);
                if (n < 1) break;
                switch (wordsize)
                {
                    case -2:
                        while ((n -= 2) >= 0)
                        {
                            uint16_t x = ((uint16_t*)buffer)[j++];
                            ((uint16_t*)map)[i++] = bswap_16(x);
                        }
                        break;
                    case -4:
                        while ((n -= 4) >= 0)
                        {
                            uint32_t x = ((uint32_t*)buffer)[j++];
                            ((uint32_t*)map)[i++] = bswap_32(x);
                        }
                        break;
                    case -8:
                        while ((n -= 8) >= 0)
                        {
                            uint64_t x = ((uint64_t*)buffer)[j++];
                            ((uint64_t*)map)[i++] = bswap_64(x);
                        }
                        break;
                }
                /* move incomplete words to buffer start for next turn */
                cur = n - wordsize;
                if (n < 0) memcpy(buffer, buffer + j * -wordsize, cur);
            }
            /* write last incomplete word filled with 0 */
            if (cur > 0 && i * -wordsize < mapsize)
            {
                memset(buffer + cur, 0, -wordsize - cur);
                switch (wordsize)
                {
                    case -2:
                    {
                        uint16_t x = ((uint16_t*)buffer)[0];
                        ((uint16_t*)map)[i++] = bswap_16(x);
                    }
                    break;
                    case -4:
                    {
                        uint32_t x = ((uint32_t*)buffer)[0];
                        ((uint32_t*)map)[i++] = bswap_32(x);
                    }
                    break;
                    case -8:
                    {
                        uint64_t x = ((uint64_t*)buffer)[0];
                        ((uint64_t*)map)[i++] = bswap_64(x);
                    }
                    break;
                }
            }
            cur = i * -wordsize;
        }
        if (cur < size)
        {
            /* fill the rest with 0 */
            memset((char*) map + cur, 0, size - cur);
        }
    }
    if (!isatty(1))
    {
        cur = 0;
        if (wordsize >= 0)
        {
            while (cur < mapsize)
            {
                ssize_t n = write(1, (char*) map + cur, mapsize - cur);
                if (n < 0) perror(NULL);
                if (n < 1) return 0;
                cur += n;
            }
        }
        else
        {
            int i = 0;
            while (i * -wordsize < mapsize)
            {
                int j = 0;
                if (mapsize - i * -wordsize < buffersize) buffersize = mapsize - i * -wordsize;
                switch( wordsize)
                {
                    case -2:
                        while (j <= (buffersize-1) / 2)
                        {
                            uint16_t x = ((uint16_t*)map)[i++];
                            ((uint16_t*)buffer)[j++] = bswap_16(x);
                        }
                        break;
                    case -4:
                        while (j <= (buffersize-1) / 4)
                        {
                            uint32_t x = ((uint32_t*)map)[i++];
                            ((uint32_t*)buffer)[j++] = bswap_32(x);
                        }
                        break;
                    case -8:
                        while (j <= (buffersize-1) / 8)
                        {
                            uint64_t x = ((uint64_t*)map)[i++];
                            ((uint64_t*)buffer)[j++] = bswap_64(x);
                        }
                        break;
                }
                if (j == 0) return 0;
                while (cur < buffersize)
                {
                    ssize_t n = write(1, buffer + cur, buffersize - cur);
                    if (n < 0) perror(NULL);
                    if (n < 1) return 0;
                    cur += n;
                }
            }
        }
    }
    if (isatty(0) && isatty(1))
    {
        if (!size) size = 0x100;
        if (!wordsize) wordsize = 2;
        if (size > mapsize) size = mapsize;
        memDisplay(addr.address, map, wordsize, size);
    }
    return 0;
}
