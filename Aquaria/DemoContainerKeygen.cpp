#include "AquariaCompileConfig.h"

#include "DemoContainerKeygen.h"
#include "lvpa/LVPAInternal.h"
#include "LVPAFile.h"

using namespace LVPA_NAMESPACE;

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

void DemoKeygen_MakeKey(const char *exename, LVPAFile *lvpa)
{
    uint8 key[] =
    {
        0x45, 0x7d, 0x2a, 0x03, 0x56, 0x21, 0x67, 0x92, 0x85, 0x60, 0x07, 0x21, 0x54, 0x83, 0x89, 0x12, 0x00, 0x68
    };
    uint8 buf[400 + sizeof(key)];

    if(!exename || !*exename)
    {
#if _WIN32  
        uint32 exepathlen = GetModuleFileName( NULL, (char*)&buf[0], 1024 );
        buf[exepathlen] = 0; // according to MSDN not null-terminated if <= win XP
        exename = (char*)&buf[0];
#else
        return;
#endif
    }

    FILE *f = fopen(exename, "rb");
    if(!f)
        return;
    fread(buf, 1, 400, f);
    fclose(f);
    memcpy(&buf[400], key, sizeof(key));
    lvpa->SetMasterKey(buf, sizeof(buf));
}
