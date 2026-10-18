#pragma once

// the mod's own GXT entries (keys "CO_..."): what a coop mission tells its players and the game has no text for. Every
// CText::Get looks here first, so the scripts and the opcodes for the network players print them as any game key
class CCustomText
{
public:
    static void InjectHooks();
    // the text of a mod's key, nullptr for a game key
    static const char* Find(const char* szKey);
};
