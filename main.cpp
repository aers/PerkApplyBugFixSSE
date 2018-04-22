﻿#include "../skse64/PluginAPI.h"
#include "../skse64_common/skse_version.h"
#include "../skse64_common/Relocation.h"
#include "../skse64_common/SafeWrite.h"
#include "../skse64_common/BranchTrampoline.h"
#include "../xbyak/xbyak.h"
#include <shlobj.h>

#include "offsets.h"

uint32_t next_formid = 0;

typedef void(*_UnknownAddFunc)(int64_t taskPool, int64_t actorPtr, int64_t perkPtr, uint32_t val, int32_t unk1);
RelocAddr<_UnknownAddFunc> UnknownAddFunc(off_UNKNOWN_ADD_FUNCTION);
RelocAddr<uintptr_t *> TaskPool(off_TASKPOOL);

void do_add(int64_t actorPtr, int64_t perkPtr, int32_t unk1)
{
	uint32_t val = 0;

	uint32_t formid = *(uint32_t *)(actorPtr + 0x14); // formid 0x14 in SSE TESForm

	if (formid == next_formid)
	{
		_MESSAGE("perk loop in formid %08X", formid);
		next_formid = 0;
		if (formid != 0x14) // player formid = 0x14
			val |= 0x100;
	}

	UnknownAddFunc(*TaskPool, actorPtr, perkPtr, val, unk1);
}

typedef void(*_HandleAddRf)(int64_t apm);
RelocAddr<_HandleAddRf> HandleAddRf(off_HANDLEADDRF);

void do_handle(int64_t actorPtr, uint32_t val)
{
	bool shouldClear = (val & 0x100) != 0;

	if (shouldClear)
	{
		int64_t apm = *((int64_t*)(actorPtr + 0xF0)); // actorprocessmanager 0xF0 in SSE Actor
		if (apm != 0)
			HandleAddRf(apm);
	}
}

// .text:00000001405C8FDE                 movzx   r8d, byte ptr [rdi+18h]
RelocAddr<uintptr_t> SwitchFunctionMovzx(off_SWITCHMOVZX);
// .text:00000001405C6C6A                 movzx   edi, r9b
RelocAddr<uintptr_t> UnknownAddFuncMovzx1(off_ADDFUNCMOVZX1);
// .text:00000001405C6C96                 movzx   eax, dil
RelocAddr<uintptr_t> UnknownAddFuncMovzx2(off_ADDFUNCMOVZX2);
// next form id get
// .text:000000014068249B                 mov     [rsp+38h+var_10], rdx
// .text:00000001406824A0                 lea     rdx, [rsp + 38h + var_18]
RelocAddr<uintptr_t> NextFormIdGetHook(off_NEXTFORMIDGETHOOK);
// do_handle hook
// .text:000000014033891B                 add     rcx, 60h
// .text:000000014033891F                 movzx   ebp, r9b
// .text:0000000140338923                 movzx   r14d, r8b
// length 0x8
RelocAddr<uintptr_t> DoHandleHook(off_DOHANDLEHOOK);
// do_add hook
// .text:0000000140338CC1                 movzx   eax, byte ptr [rdx+8]
// .text:0000000140338CC5                 xor     r9d, r9d
// .text:0000000140338CC8                 mov     rdx, [rcx + 8]
// .text:0000000140338CCC                 mov     rcx, cs:qword_142F5F978
// .text:0000000140338CD3                 mov[rsp + 38h + var_18], al
// .text:0000000140338CD7                 call    sub_1405C6C50
// .text:0000000140338CDC loc_140338CDC:    
// length 0x1B
RelocAddr<uintptr_t> DoAddHook(off_DOADDHOOK);


extern "C" {
	bool SKSEPlugin_Query(const SKSEInterface * skse, PluginInfo * info)
	{
		gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim Special Edition\\SKSE\\PerkFix.log");
		gLog.SetPrintLevel(IDebugLog::kLevel_Error);
		gLog.SetLogLevel(IDebugLog::kLevel_DebugMessage);

		_MESSAGE("Double Perk Apply On Load Fix");

		// populate info structure
		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "double perk apply fix plugin";
		info->version = 1;

		if (skse->isEditor)
		{
			_MESSAGE("loaded in editor, marking as incompatible");
			return false;
		}
		else if (skse->runtimeVersion != RUNTIME_VERSION_1_5_39)
		{
			_MESSAGE("unsupported runtime version %08X", skse->runtimeVersion);
			return false;
		}

		return true;
	}

	bool SKSEPlugin_Load(const SKSEInterface * skse) {
		_MESSAGE("Patching double perk bug...");

		if (!g_branchTrampoline.Create(1024 * 64))
		{
			_ERROR("couldn't create branch trampoline. this is fatal. skipping remainder of init process.");
			return false;
		}

		if (!g_localTrampoline.Create(1024 * 64, nullptr))
		{
			_ERROR("couldn't create codegen buffer. this is fatal. skipping remainder of init process.");
			return false;
		}

		_MESSAGE("Patching val movzx");
		// .text:00000001405C8FDE                 movzx   r8d, byte ptr [rdi+18h]
		// 44 0F B6 47 18
		// ->
		// mov r8d, dword ptr [rdi+18h]
		// 44 8b 47 18 90
		unsigned char first_movzx_patch [] = { 0x44, 0x8b, 0x47, 0x18, 0x90 };
		SafeWriteBuf(SwitchFunctionMovzx.GetUIntPtr(), first_movzx_patch, 5);
		
		// .text:00000001405C6C6A                 movzx   edi, r9b
		// 41 0F B6 F9
		// ->
		// mov edi, r9d
		// 44 89 CF 90
		unsigned char second_movzx_patch[] = { 0x44, 0x89, 0xCF, 0x90 };
		SafeWriteBuf(UnknownAddFuncMovzx1.GetUIntPtr(), second_movzx_patch, 4);

		// .text:00000001405C6C96                 movzx   eax, dil
		// 40 0F B6 C7
		// ->
		// mov eax, edi
		// 89 F8 90 90
		unsigned char third_movzx_patch[] = { 0x89, 0xF8, 0x90, 0x90 };
		SafeWriteBuf(UnknownAddFuncMovzx2.GetUIntPtr(), third_movzx_patch, 4);

		_MESSAGE("Hooking for next form ID");
		{
			struct GetNextFormId_Code : Xbyak::CodeGenerator
			{
				GetNextFormId_Code(void * buf) : Xbyak::CodeGenerator(4096, buf)
				{
					Xbyak::Label retnLabel;

					// enter 68249B
					//.text:000000014068249B                 mov[rsp + 38h + var_10], rdx
					mov(ptr[rsp + 0x28], rdx);
					// store formid
					push(rax);
					mov(rdx, qword [rdx + 0x14]);
					mov(rax, (uintptr_t)&next_formid);
					mov(dword [rax], edx);
					pop(rax);
					//.text:00000001406824A0                 lea     rdx, [rsp + 38h + var_18]
					lea(rdx, ptr[rsp + 0x20]);
					// exit 6824A5
					jmp(ptr[rip + retnLabel]);

					L(retnLabel);
					dq(NextFormIdGetHook.GetUIntPtr() + 0xA);
				}
			};

			void * codeBuf = g_localTrampoline.StartAlloc();
			GetNextFormId_Code code(codeBuf);
			g_localTrampoline.EndAlloc(code.getCurr());

			g_branchTrampoline.Write6Branch(NextFormIdGetHook.GetUIntPtr(), uintptr_t(code.getCode()));
		}

		_MESSAGE("Hooking handle function");
		{
			struct DoHandleHook_Code : Xbyak::CodeGenerator
			{
				DoHandleHook_Code(void * buf, UInt64 doHandleAddr) : Xbyak::CodeGenerator(4096, buf)
				{
					Xbyak::Label retnLabel;
					Xbyak::Label funcLabel;

					// enter 33891B  
					// .text:000000014033891B                 add     rcx, 60h
					add(rcx, 0x60);
					// .text:000000014033891F                 movzx   ebp, r9b
					movzx(ebp, r9b);
					// .text:0000000140338923                 movzx   r14d, r8b
					mov(r14d, r8d); // preserve val

					// call do_handle
					
					push(rdx); // save rdx+rcx
					push(rcx);
					mov(edx, r14d); // val
					mov(rcx, rsi); // actorPtr
					sub(rsp, 0x20); // parameter stack space 
					//void do_handle(int64_t actorPtr, uint32_t val)
					call(ptr[rip + funcLabel]);
					add(rsp, 0x20);
					pop(rcx);
					pop(rdx);
					

					// exit 338927
					jmp(ptr[rip + retnLabel]);

					L(funcLabel);
					dq(doHandleAddr);

					L(retnLabel);
					dq(DoHandleHook.GetUIntPtr() + 0x8);
				}
			};

			void *codeBuf = g_localTrampoline.StartAlloc();
			DoHandleHook_Code code(codeBuf, uintptr_t(do_handle));
			g_localTrampoline.EndAlloc(code.getCurr());

			g_branchTrampoline.Write6Branch(DoHandleHook.GetUIntPtr(), uintptr_t(code.getCode()));
		}

		_MESSAGE("Hooking add function");
		{
			struct DoAddHook_Code : Xbyak::CodeGenerator
			{
				DoAddHook_Code(void * buf, UInt64 doAddAddr) : Xbyak::CodeGenerator(4096, buf)
				{
					Xbyak::Label retnLabel;
					Xbyak::Label funcLabel;

					// .text:0000000140338CB9                 mov     r8, [rdx]			             perk ptr
					// enter 338CC1
					// .text:0000000140338CC1                 movzx   eax, byte ptr[rdx + 8]		 do_add unk1
					// .text:0000000140338CC5                 xor     r9d, r9d                       val = 0
					// .text:0000000140338CC8                 mov     rdx, [rcx + 8]                 actor ptr
					// .text:0000000140338CCC                 mov     rcx, cs:qword_142F5F978        taskpool
					// .text:0000000140338CD3                 mov[rsp + 38h + var_18], al            do_add unk1

					movzx(eax, byte[rdx + 0x8]);
					mov(rcx, ptr[rcx + 0x8]); // actorPtr
					mov(rdx, r8); //perkPtr
					mov(r8d, eax); //unk1

					// void do_add(int64_t actorPtr, int64_t perkPtr, int32_t unk1)
					call(ptr[rip + funcLabel]);

					// exit 338CDC
					jmp(ptr[rip + retnLabel]);

					L(funcLabel);
					dq(doAddAddr);

					L(retnLabel);
					dq(DoAddHook.GetUIntPtr() + 0x1B);
				
				}
			};

			void *codeBuf = g_localTrampoline.StartAlloc();
			DoAddHook_Code code(codeBuf, uintptr_t(do_add));
			g_localTrampoline.EndAlloc(code.getCurr());

			g_branchTrampoline.Write6Branch(DoAddHook.GetUIntPtr(), uintptr_t(code.getCode()));
		}

		_MESSAGE("Patched");
		return true;
	}
};

