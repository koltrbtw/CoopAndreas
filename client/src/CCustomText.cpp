#include "stdafx.h"
#include "CCustomText.h"

namespace
{
struct SCustomText
{
    const char* szKey;
    char szText[128];  // CText::Get returns it writable
};

SCustomText s_texts[] = {
    // a meeting point the mission waits for all the players at, one of them is there first
    {"CO_WAIT", "Wait here for your ~b~homies~w~."},
};
}  // namespace

const char* CCustomText::Find(const char* szKey)
{
    if (!szKey || szKey[0] != 'C' || szKey[1] != 'O' || szKey[2] != '_')
    {
        return nullptr;
    }

    for (auto& text : s_texts)
    {
        if (_stricmp(text.szKey, szKey) == 0)
        {
            return text.szText;
        }
    }
    return nullptr;
}

static const char* __stdcall FindCustomText(const char* szKey)
{
    return CCustomText::Find(szKey);
}

// CText::Get (0x6A0050, thiscall, the key): ours first. The first 5 bytes (sub esp, 20h; push esi; push edi) are played
// here, then back to 0x6A0055
static void __declspec(naked) CText__Get_Hook()
{
    __asm
    {
        push ecx
        push dword ptr [esp + 8]  // the key
        call FindCustomText
        pop ecx
        test eax, eax
        jz original
        ret 4

    original:
        sub esp, 20h
        push esi
        push edi
        push 0x6A0055
        ret
    }
}

void CCustomText::InjectHooks()
{
    patch::RedirectJump(0x6A0050, CText__Get_Hook);
}
