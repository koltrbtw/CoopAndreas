#include "stdafx.h"
#include "CommandHooks.h"
#include "CCustomCommandMgr.h"
#include "Commands/CCommandAddChatMessage.h"
#include "CScriptEntityAdoption.h"

uint16_t nCommand = 0x0;
CRunningScript* pScript = nullptr;

// 0 the game runs the command, 1 we did, 2 the script waits at it until the next frame
int ProcessCustomCommand()
{
	// if is in custom command range
	uint16_t commandNormalised = (nCommand & 0x7FFF);
	if (commandNormalised >= CCustomCommandMgr::MIN_CUSTOM_COMMAND && commandNormalised <= CCustomCommandMgr::MAX_CUSTOM_COMMAND)
	{
		pScript->m_pCurrentIP += 2;
		pScript->m_bNotFlag = (nCommand & 0x8000) != 0;
		CCustomCommandMgr::ProcessCommand(commandNormalised, pScript); // process it
		return 1;
	}

	switch (CScriptEntityAdoption::ProcessCommand(nCommand, pScript))
	{
	case CScriptEntityAdoption::eResult::HANDLED:
		return 1;
	case CScriptEntityAdoption::eResult::WAIT:
		return 2;
	default:
		return 0;
	}
}


void __declspec(naked) CRunningScript__Process_Hook() 
{
	__asm
	{
		mov ax, [ecx]

		mov nCommand, ax
		mov pScript, esi

		push eax
		push ecx
		push esi

		call ProcessCustomCommand

		test eax, eax
		jz process_orig_opcode 
		cmp eax, 2
		je wait_next_frame

		pop esi
		pop ecx
		pop eax
		push 0x469FB0
		ret

	// out of CRunningScript::Process with the IP at the command, as after a wait
	wait_next_frame:
		pop esi
		pop ecx
		pop eax
		push 0x469FFB
		ret

	process_orig_opcode:
		pop esi
		pop ecx
		pop eax
		push 0x469FBF
		ret
	}
}

//uintptr_t CRunningScript__ProcessOneCommand_Ret = 0x469EBF;
//uintptr_t CRunningScript__ProcessOneCommand_Exit = 0x469EF8;
//void __declspec(naked) CRunningScript__ProcessOneCommand_Hook()
//{
//	__asm
//	{
//		xor eax, eax
//		mov ax, [edx]
//
//		mov nCommand, ax
//		mov pScript, ecx
//
//		pushad
//		pushfd
//	}
//
//	if (ProcessCustomCommand(nCommand, pScript))
//	{
//		__asm
//		{
//			popfd
//			popad
//
//			jmp CRunningScript__ProcessOneCommand_Exit
//		}
//	}
//
//	__asm
//	{
//		popfd
//		popad
//
//		jmp CRunningScript__ProcessOneCommand_Ret
//	}
//}

void CommandHooks::InjectHooks()
{
	patch::RedirectJump(0x469FBA, CRunningScript__Process_Hook);
	///patch::RedirectJump(0x469EBA, CRunningScript__ProcessOneCommand_Hook);
}