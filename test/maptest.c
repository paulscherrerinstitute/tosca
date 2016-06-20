#include <stdio.h>
#include <toscaMap.h>

int main()
{
    if (toscaMapVme(VME_A32 | VME_SUPER, 0x000000, 0x900000) == NULL) { perror ("map 1"); return 1; }
    if (toscaMapVme(VME_A32 | VME_SUPER, 0x500000, 0x100000) == NULL) { perror ("map 2"); return 1; }
    if (toscaMapVme(VME_A32 | VME_SUPER | VME_DATA, 0x500000, 0x100000) == NULL) { perror ("map 3"); return 1; }
//    if (toscaMapVme(VME_A32, 0xfff00000, 0x10000000) == NULL) { perror ("map 4"); return 1; }

    getchar();
    return 0;
}
