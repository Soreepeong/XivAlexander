#include "pch.h"
#include "Game/SignatureDefinitions.h"

#include <algorithm>
#include <map>
#include <utility>

#include <xivres/pe_image.h>

#include "Game/ResolveContext.h"
#include "Game/Signatures.h"
#include "Misc/Logger.h"
#include "Utils/Win32/Process.h"

namespace XivAlexander::Game {
	const Signatures::RegexSignature SqPackIndexEntryCount(R"([\x48\x49\x4C\x4D]\x39[\x80-\xBF](....)\x75\x05\xC1[\xE8-\xEF]\x03\xEB\x03\xC1[\xE8-\xEF]\x04)");
	const Signatures::RegexSignature RipRelativeLea(R"([\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....))");
	const Signatures::RegexSignature StringIndirectionResolver(R"(\x8b\x01\x25\xff\xff\xff\x00\x48\x03\xc1)");
	const Signatures::RegexSignature CutSceneLanguageGetter(R"(\xE8(....)[\x48\x49]\x63[\x50-\x53\x55-\x57]\x1C)");

	const Signatures::RegexSignature OodleInit(R"(\x75.\x48\x8d\x15....\x48\x8d\x0d....\xe8(....)\xc6\x05....\x01.{0,256}\x75.\xb9(....)\xe8(....)(?:\x45\x33\xc0\x33\xd2|\x33\xd2\x45\x33\xc0)\x48\x8b\xc8\xe8.....{0,6}\x41\xb9(....)\xba.....{0,6}\x48\x8b\xc8\xe8(....))");
	const Signatures::RegexSignature OodleSetUpStatesAndTrain(R"(\x75\x04[\x48\x4C]\x89..\xe8(....)[\x4C\x48]..\xe8(....).{0,256}\x01\x75\x0a[\x48\x49]\x8b.\xe8(....)\xeb\x09[\x48\x49]\x8b.\x08\xe8(....))");
	const Signatures::RegexSignature OodleDecode(R"([\x48-\x4F]\x85[\xC0-\xFF]\x74\x0A[\x48-\x4F]\x8B[\xC8-\xCF]\xE8(....)\xEB\x09\x48\x8B\x49\x08\xE8(....))");
	const Signatures::RegexSignature OodleEncode(R"([\x48-\x4F]\x85[\xC0-\xFF]\x74\x0D[\x48-\x4F]\x8B[\xC8-\xCF]\xE8(....)[\x48-\x4F]..\xEB\x0B\x48\x8B\x49\x08\xE8(....))");

	const Signatures::RegexSignature OpcodeActionRequest(R"(\x48\x8D\x54\x24(.)\x45\x33\xC9\xC7\x44\x24\1(..[\x00-\x03]\x00))");
	const Signatures::RegexSignature OpcodeCaller(R"((?:\x48\x8B[\x40-\xBF].{0,5}(?:\x41)?\x66\x89[\x44-\x7C]\x24.|(?:\x41)?\x66\x89[\x44-\x7C]\x24.\x48\x8B[\x40-\xBF].{0,5})\xE8(....))");
	const Signatures::RegexSignature OpcodeGroundTargetedSender(R"(\x66[\x40-\x4F]?\x89[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24.\xF3[\x40-\x4F]?\x0F\x11[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24.\xF3[\x40-\x4F]?\x0F\x11[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24.\xC7\x44\x24.(....))");
	const Signatures::RegexSignature OpcodeSizeFirst(R"(\x48\x83\xEC\x38\x4D\x8B\xC8\x48\xC7\x44\x24\x20(....)\x41\xB8(....))"); // opcode != payload size
	const Signatures::RegexSignature OpcodeSizeFromRegister(R"(\x48\x83\xEC.\x4D\x8B\xC8\x41\xB8(....)\x4C\x89\x44\x24\x20)"); // opcode == payload size
	const Signatures::RegexSignature OpcodeConditional(R"((?:[\x70-\x7F].|\x0F[\x80-\x8F]....)\x41\xB8(....)(\x48\xC7\x44\x24\x20)(....)[\x48\x4C]\x8B.{1,5}\x41?\x8B.{1,5}\x48\x8B.{1,5}\xE8)"); // opcode != payload size, but conditional

	const Signatures::RegexSignature AudioSamplingRateImmediate(R"(\xC7(?:\x45.|\x44\x24.|\x85....|\x84\x24....)(\x80\xBB\x00\x00)\xC7(?:\x45.|\x44\x24.|\x85....|\x84\x24....)\x0F\x00\x00\x00)");
	const Signatures::RegexSignature AudioSamplingRateGetter(R"(\x8B\x05(....)\xC3)");

	const Signatures::RegexSignature MssAsiAttribute(R"(\x85\xD2\x74.\x83\xFA\x01\x74\x03\x33\xC0\xC3\xB8\x10\x00\x00\x00\xC3\x8B\x41\x28\xC3)");
	const Signatures::RegexSignature MssAsiOpen(R"([\x44\x45]\x8B[\x40-\x43\x45-\x47]\x20\x48\x8D\x15....[\x48\x49]\x8B[\xC8-\xCF]\xE8(....))");
	const Signatures::RegexSignature MssAsiSetUpDecoder(R"([\x48-\x4F]\x8B[\x00-\x3F][\x40-\x4F]?\x89[\x40-\x7F]\x38[\x48-\x4F]\x8B[\x00-\x3F][\x40-\x4F]?\x89[\x40-\x7F]\x34[\x48-\x4F]\x8B[\x00-\x3F]\xC7[\x40-\x47]\x30\x00\x00\x01\x00)");
	const Signatures::RegexSignature MssAsiProcess(R"(\xFF\x41\x3C[\x40-\x4F]?\x8B[\xC0-\xFF]\x83\x79\x3C\x01)");
	const Signatures::RegexSignature MssAsiResetPair(R"(\xE8(....)(?:[\x48\x49]\x8B[\x40-\x4F]\x08\x33\xD2|\x33\xD2[\x48\x49]\x8B[\x40-\x4F]\x08)\xE8(....))");

	const Signatures::RegexSignature SoundVoiceInit(R"([\x48-\x4F]\x8B[\xC0-\xFF]\x41\x83\xF8\x10(?:\x0F\x8F....|\x7F.|\x7E.\x83\xC8\xFF\xC3|\x7E.\xB8\xFF\xFF\xFF\xFF\xC3)\x45\x85\xC9(?:\x0F\x84....|\x74.)[\x48\x4C]\x8B[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24\x28[\x48-\x4F]\x89[\x80-\xBF])");
	const Signatures::RegexSignature SoundVoiceVtable(R"([\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....)[\x48-\x4F]\x8B[\xC0-\xFF][\x48-\x4F]\x89[\x00-\x3F](?:[\x48\x49]\x8D[\x50-\x57]\x30|[\x48\x49]\x83[\xC0-\xC7]\x30\xFF\x15))");
	const Signatures::RegexSignature SoundVoiceRender(R"([\x40-\x4F]?\x83[\xB8-\xBB\xBD-\xBF](....)\x03.{0,8}?(?:\x0F\x85....|\x75.)[\x40-\x4F]?\x83[\xB8-\xBB\xBD-\xBF](....)\x00(?:\x0F\x8E....|\x7E.)\xE8(....))");
	const Signatures::RegexSignature SoundVoiceSubmit(R"(\xF7[\x80-\x83\x85-\x87](....)\xFB\xFF\xFF\xFF.{0,24}?[\x40-\x4F]?\x8B[\x80-\xBF](....)[\x40-\x4F]?\x83[\xF8-\xFF]\x02(?:\x0F\x8D....|\x7D.))");
	const Signatures::RegexSignature SoundVoiceSetMarker(R"(\xF7[\x80-\x83\x85-\x87](....)\xFB\xFF\xFF\xFF\x74.\x89[\x90-\x93\x95-\x97](....)\x33\xC0\xC3)");
	const Signatures::RegexSignature SoundVoiceStoreImmediate(R"(\xC7[\x80-\x83\x85-\x87](....)(....))");
	const Signatures::RegexSignature RelativeCall(R"(\xE8(....))");
	const Signatures::RegexSignature SoundBufferEndHandler(R"([\x40\x41][\x50-\x57]\x48\x83\xEC.\x83\x79.\x07[\x48-\x4F]\x8B[\xC0-\xFF]\x74.\x48\x83\xC1\xF8\xE8....[\x40-\x4F]?\x80[\xB8-\xBB\xBD-\xBF](....)\x01)");

	const Signatures::RegexSignature MessageLoop(R"(\xE8(....)\x84\xc0\x75\xf7)");

	// The callers check that the lobby is in state 4 or 0x3B, then call the login. Holds since 6.10; the login itself
	// is compiled differently often enough that matching its own instructions broke between 7.25 and 7.30.
	const Signatures::RegexSignature LobbyLoginCaller(R"(\x8B[\x80-\xBF]....\x83\xF8\x04(?:\x74.|\x0F\x84....)\x83\xF8\x3B.{0,80}?\xE8(....))");
	// Near its start, the login tests its last argument to choose between two ways of logging in.
	const Signatures::RegexSignature LobbyLoginLastArgumentTest(R"(\x80\xBC\x24....\x00)");

	// Same as what NoKillPlugin (Bluefissure) hooks: reads the error code from the dialog result right away.
	const Signatures::RegexSignature LobbyErrorDialog(R"(\x40\x53\x48\x83\xEC\x30\x48\x8B\xD9\x49\x8B\xC8\xE8....\x8B\xD0)");

	// The IME mode getter starts by asking ImmGetOpenStatus, then ImmGetConversionStatus, about the HIMC in a global;
	// it returns early when closed with a jz, or with a jnz over the return before 7.0. Holds since 5.55.
	const Signatures::RegexSignature ImeStatusQueries(R"(\x48\x8B\x0D(....)(?:\xE8|\xFF\x15)(....)\x85\xC0(?:\x74.|\x0F\x84....|\x75.{1,16}?)\x48\x8B\x0D(....)(?:\x4C\x8D\x44\x24.\x48\x8D\x54\x24.|\x48\x8D\x54\x24.\x4C\x8D\x44\x24.)(?:\xE8|\xFF\x15)(....))");
	// bt eax, 8: tests IME_CMODE_NOCONVERSION, which only the IME mode getter does after those queries.
	const Signatures::RegexSignature ImeNoConversionTest(R"(\x0F\xBA\xE0\x08)");

	// DeviceDX11::PostTick with the render thread: mov rcx, [device+0x70]; call Kernel::SwapChain::Present; mov byte
	// [device+0x79], 0; then a jmp over the branch without the render thread, whose call is the same but not followed by
	// one. Holds since 7.0 (checked through 7.56h); before, PostTick has only the call without the jmp.
	const Signatures::RegexSignature SwapChainPresentCall(R"([\x48\x49]\x8B[\x48-\x4B\x4D-\x4F]\x70\xE8(....)\x41?\xC6[\x40-\x47]\x79\x00\xEB)");
	// The walk of the notifiers before Present, 117 bytes before that call since 7.0: lea rcx, [lock];
	// call [EnterCriticalSection]; mov rbx, [head]; test rbx, rbx; jz; mov rax, [rbx]; mov rcx, rbx; call [rax+0x10]
	// The walk as the game has it, or as CrowdFix's SkipIdleNotifiers rewrites it (mov rcx, imm64; mov rax, imm64; call rax;
	// jmp past the rest), after the lock is taken.
	const Signatures::RegexSignature PrePresentNotifierWalk(R"(\x48\x8D\x0D....\xFF\x15....(?:\x48\x8B\x1D....\x48\x85\xDB\x74.\x48\x8B\x03\x48\x8B\xCB\xFF\x50\x10|\x48\xB9.{8}\x48\xB8.{8}\xFF\xD0\xEB))");

	// The signatures below are from CrowdFix (SheepGoMeh), checked against 7.56h (2026.09.15).

	// Framework::Tick: lea rcx, [rbx+TaskManager]; ...; call ExecuteAllTasks; mov rcx, [rbx+...]; test rcx, rcx
	const Signatures::RegexSignature TaskManagerExecuteAllTasksCall(R"(\xE8(....)\x48\x8B\x8B....\x48\x85\xC9\x74.\xF3\x0F\x10\x8B)");
	// A call to the culling manager getter, which starts with mov rax, [g_CullingManager].
	const Signatures::RegexSignature CullingManagerGetterCall(R"(\xE8(....)\x48\x8B\x05....\x83\x60..\xE8)");
	const Signatures::RegexSignature CullingManagerGetter(R"(\x48\x8B\x05(....))");

	// lea rcx, [lock]; call [EnterCriticalSection]; mov rbx, [head]; test rbx, rbx; jz; mov rax, [rbx]; mov rcx, rbx; call [rax+0x10]
	const Signatures::RegexSignature NotifierListWalk(R"(\x48\x8D\x0D(....)\xFF\x15....\x48\x8B\x1D(....)\x48\x85\xDB\x74.\x48\x8B\x03\x48\x8B\xCB\xFF\x50\x10)");
	// The PostTick loops: mov rbx, [head]; test; jz; mov rax, [rbx]; mov rcx, rbx; call [rax+slot]; mov rbx, [rbx+0x10]; test; jnz
	const Signatures::RegexSignature NotifierPrePresentLoop(R"(\x48\x8B\x1D(....)\x48\x85\xDB\x74\x12\x48\x8B\x03\x48\x8B\xCB\xFF\x50\x10\x48\x8B\x5B\x10\x48\x85\xDB\x75\xEE)");
	const Signatures::RegexSignature NotifierPostKickLoop(R"(\x48\x8B\x1D(....)\x48\x85\xDB\x74\x14\x66\x90\x48\x8B\x03\x48\x8B\xCB\xFF\x50\x08\x48\x8B\x5B\x10\x48\x85\xDB\x75\xEE)");
	// Link and Unlink take the notifier lock right after saving the node: mov rbx, rcx; lea rcx, [lock]; call [EnterCriticalSection].
	// Link then reads the list head (mov rax, [head]; xor ecx, ecx), and Unlink the node's previous link (mov rcx, [rbx+8]).
	const Signatures::RegexSignature NotifierLinkBody(R"(\x48\x8B\xD9\x48\x8D\x0D(....)\xFF\x15....\x48\x8B\x05(....)\x33\xC9)");
	const Signatures::RegexSignature NotifierUnlinkBody(R"(\x48\x8B\xD9\x48\x8D\x0D(....)\xFF\x15....\x48\x8B\x4B\x08)");
	// Notifier callbacks of buffers, vertex buffers, index buffers, textures and constant buffers, up to the flag test
	// that sends them to their return. The texture's pre-present one also does work for 0x2000: bt edx, 13 (bt ecx, 13
	// before 7.20) where the jne goes; before 7.20 it starts with push rbx instead of mov rax, rsp; push rbx, and the
	// jne is short.
	const Signatures::RegexSignature BufferNotifierPostKick(R"(\x40\x53\x48\x83\xEC\x40\x8B\x05....\x48\x8B\xD9\x48\x8B\x54\xC1\x20\x48\x85\xD2\x74.\x8B\x49\x1C\xF6\xC1\x11\x74)");
	const Signatures::RegexSignature BufferNotifierPrePresent(R"(\x40\x53\x48\x83\xEC\x20\x8B\x05....\x48\x8B\xD9\x48\x8B\x54\xC1\x20\x48\x85\xD2\x74.\xF6\x41\x1C\x11\x74)");
	const Signatures::RegexSignature IndexBufferNotifierPostKick(R"(\x40\x53\x48\x83\xEC\x40\x8B\x05....\x48\x8B\xD9\x4C\x8B\x54\xC1\x28\x4D\x85\xD2\x74.\x8B\x51\x20\xF6\xC2\x11\x74.\xF6\xC2\x40\x75)");
	const Signatures::RegexSignature IndexBufferNotifierPrePresent(R"(\x40\x53\x48\x83\xEC\x20\x8B\x05....\x48\x8B\xD9\x48\x8B\x54\xC1\x28\x48\x85\xD2\x74.\x8B\x41\x20\xA8\x11\x74.\xA8\x40\x75)");
	const Signatures::RegexSignature TextureNotifierPostKick(R"(\x40\x53\x48\x83\xEC\x40\x8B\x41\x3C\x48\x8B\xD9\x25\x10\x00\x10\x00\x3D\x10\x00\x10\x00\x0F\x85)");
	const Signatures::RegexSignature TextureNotifierPrePresent(R"((?:\x40|\x48\x8B\xC4)\x53\x48\x83\xEC\x60\x48\x83\x79\x48\x00\x48\x8B\xD9\x0F\x84....\x8B[\x49\x51]\x3C.{0,32}?\x25\x10\x00\x10\x00.{0,24}?\x3D\x10\x00\x10\x00(?:\x0F\x85(....)|\x75(.)))");
	const Signatures::RegexSignature TextureNotifierUploadTest(R"(\x0F\xBA[\xE0-\xE7]\x0D)");
	const Signatures::RegexSignature ConstantBufferNotifierPrePresent(R"(\x48\x89\x5C\x24\x10\x57\x48\x83\xEC\x40\x8B\x05....\x48\x8B\xD9\x48\x8B\x7C\xC1\x18\x48\x85\xFF\x74.\x48\x83\x79\x30\x00\x74.\xF7\x41\xEC\x00\x40\x00\x00\x74)");

	// Every enqueue into the 128-entry job ring: mov eax, [write index]; inc eax; and eax, 0x7F; mov [write index], eax.
	// The dequeue does the same with the read index right after it, fewer times.
	const Signatures::RegexSignature JobRingIndexIncrement(R"(\x8B\x05(....)\xFF\xC0\x83\xE0\x7F\x89\x05(....))");
	// In the kick: lea rcx, [task manager+JobPool]; call WakeAll; mov rcx, [rsp+...]; mov edx, ...; call [...]
	const Signatures::RegexSignature JobPoolWakeAllCall(R"(\x48\x8D[\x48-\x4B\x4D-\x4F](.)\xE8(....)\x48\x8B\x4C\x24.\xBA....\xFF\x15)");
	// The wake-all's loop: cmp [pool+count], reg; jbe; mov reg, [pool+threads]; mov rcx, [reg+i*8]; movzx eax, [rcx+skip];
	// test al, al; jnz; xor eax, eax; lock xadd [rcx+wake count], eax; cmp eax, 2; jae; mov eax, 1; lock xadd [rcx+wake
	// count], eax; test eax, eax; jnz; mov rcx, [rcx+event]; call [SetEvent]
	const Signatures::RegexSignature JobPoolWakeAllBody(R"(\x39[\x40-\x7F](.)\x76.(?:\x0F\x1F[\x00-\x84].{0,6}?|\x66?\x90)*\x48\x8B[\x40-\x7F](.)\x48\x8B\x0C[\xC0-\xFF]\x0F\xB6\x41(.)\x84\xC0\x75.\x33\xC0\xF0\x0F\xC1\x41(.)\x83\xF8\x02\x73.\xB8\x01\x00\x00\x00\xF0\x0F\xC1\x41\4\x85\xC0\x75.\x48\x8B\x49(.)\xFF\x15(....))");

	const Signatures::RegexSignature SkeletonPoseSyncWalkCall(R"(\xE8(....)\xE8....\xE8....\x41\x8B\xFF)");
	// In that order in the walk: movzx eax, word [skeleton+count]; mov rdx, [skeleton+partials]; add rdx, rbx;
	// mov rcx, [rdx+pose]; test rcx, rcx; jz; call; ...; add rbx, stride; sub rdi, 1
	const Signatures::RegexSignature PoseSyncPartialCount(R"(\x0F\xB7[\x40-\x7F](.))");
	const Signatures::RegexSignature PoseSyncPartialArray(R"(\x48\x8B[\x40-\x7F](.)[\x48\x49]\x03[\xC0-\xFF])");
	const Signatures::RegexSignature PoseSyncPartialPose(R"(\x48\x8B[\x80-\xBF](....)[\x48\x4C]\x85[\xC0-\xFF]\x74.\xE8)");
	const Signatures::RegexSignature PoseSyncPartialStride(R"([\x48\x49]\x81[\xC0-\xC7](....)[\x48\x49]\x83[\xE8-\xEF]\x01)");

	// mov ecx, 0xA000 (40960 object slots, one 16 byte store each); register restores; then the loop:
	// movdqu [rax], xmm0; lea rax, [rax+0x10]; sub rcx, 1; jnz
	const Signatures::RegexSignature CullingVisibilityClearCount(R"(\xB9(\x00\xA0\x00\x00)(?:[\x48\x4C]\x8B[\x84-\xBC]\x24....|\x0F\x28[\x84-\xBC]\x24....|\x90|\x0F\x1F[\x00-\x84].{0,6}?){0,10}\xF3\x0F\x7F\x00\x48\x8D\x40\x10\x48\x83\xE9\x01\x75\xF2)");
	// Right before it: mov rax, [culling manager+table]; xorps xmm0, xmm0; ...; mov ecx, 0xA000
	const Signatures::RegexSignature CullingVisibilityTableLoad(R"([\x48\x49]\x8B[\x40-\x47](.)\x0F\x57\xC0.{0,12}?\xB9\x00\xA0\x00\x00)");
	// The object slot allocator: mov r11, [rcx+mask]; ...; mov eax, [r9]; cmp eax, -1; je (full word); the bit loop;
	// cmp r8d, words; jb; ...; or edx, eax; mov [r11+r8*4], edx; shl r8d, 5; lea ebx, [rcx+r8] (the slot);
	// shl rbx, entry shift; add rbx, [r10+objects]
	const Signatures::RegexSignature CullingSlotAlloc(R"([\x48\x4C]\x8B[\x41\x49\x51\x59\x61\x69\x71\x79](.).{0,16}?\x8B[\x00-\x3F]\x83\xF8\xFF\x74..{0,40}?\x81[\xF8-\xFF](....)\x72..{0,24}?\x0B[\xC0-\xFF][\x40-\x4F]?\x89[\x04-\x3C][\x80-\xBF][\x41]?\xC1[\xE0-\xE7]\x05.{0,8}?[\x48\x49]\xC1[\xE0-\xE7](.)[\x48\x49\x4C\x4D]\x03[\x40-\x7F](.))");
	// The release: sub rcx, [r10+objects]; mov rax, [r10+mask]; sar rcx, entry shift; ...; not eax; and [r9], eax
	const Signatures::RegexSignature CullingSlotRelease(R"([\x48\x49\x4C\x4D]\x2B[\x40-\x7F](.)[\x48\x49\x4C\x4D]\x8B[\x40-\x7F](.)[\x48\x49]\xC1[\xF8-\xFF](.).{0,24}?\xF7[\xD0-\xD7][\x40-\x4F]?\x21)");
	// mov rcx, [g_CullingManager]; call ...
	const Signatures::RegexSignature CullingManagerCall(R"(\x48\x8B\x0D(....)\xE8(....))");

	const Signatures::RegexSignature GraphicsAllocatorFreeStart(R"(\x48\x85\xD2\x0F\x84....\x48\x89\x74\x24.\x57\x48\x83\xEC.\x48\x8B\xF1\x48\x89\x5C\x24.\x48\x81\xC1)");
	// The slab check of Free: add rcx, lock; mov rdi, rdx; call [EnterCriticalSection]; mov r8, rdi; and r8, ~page mask;
	// mov eax, [r8+page index]; cmp [rsi+chunk count], eax; jbe; lea rcx, [rax+rax*2]; mov rax, [rsi+chunk table];
	// add rcx, rcx; mov rdx, [rax+rcx*8+chunk base]; test rdx, rdx; jz; mov rax, rdi; sub rax, rdx; cmp rax, chunk span
	const Signatures::RegexSignature GraphicsAllocatorFreeLayout(R"(\x48\x81\xC1(....)[\x48\x4C]\x8B[\xC0-\xFF]\xFF\x15(....)[\x48\x4C]\x8B[\xC0-\xFF][\x48\x49]\x81[\xE0-\xE7](....)[\x40-\x4F]?\x8B[\x40-\x7F](.)\x39[\x80-\xBF](....)\x0F\x86....([\x48\x4C]\x8D[\x00-\x3F][\x40-\x7F])[\x48\x4C]\x8B[\x80-\xBF](....)([\x48\x4C]\x03[\xC0-\xFF])[\x48\x4C]\x8B[\x40-\x7F]([\xC0-\xFF])(.)[\x48\x4C]\x85[\xC0-\xFF]\x0F\x84....[\x48\x4C]\x8B[\xC0-\xFF][\x48\x4C]\x2B[\xC0-\xFF](?:\x48\x3D|\x48\x81[\xF8-\xFF])(....))");
	// The backing frees: mov rcx, [rsi+backing]; (mov rdx, rdi;) mov rax, [rcx]; call [rax+free slot*8]
	const Signatures::RegexSignature GraphicsAllocatorBackingFree(R"(\x48\x8B[\x88-\x8F](....)(?:[\x48\x49]\x8B[\xD0-\xD7])?\x48\x8B\x01\xFF\x50(.))");
	// The unlock at the end: lea rcx, [rsi+lock]; call [LeaveCriticalSection]
	const Signatures::RegexSignature GraphicsAllocatorUnlock(R"(\x48\x8D[\x88-\x8F](....)\xFF\x15(....))");
	// In the buffer write lock: mov rax, [g_AllocatorManager]; ...; mov rcx, [rax+allocator]; mov rax, [rcx]; call [rax+0x10]
	const Signatures::RegexSignature GraphicsAllocatorManagerLoad(R"(\x48\x8B\x05(....)\x41\xB8....\x8B\xD7\x48\x8B\x48(.)\x48\x8B\x01\xFF\x50(.)\x48\x89\x83)");
	// The graphics allocator's destructor (and constructor) setting both vtables: lea rax, [vtable]; (mov rdi, rcx;)
	// mov [rcx], rax; (mov ebx, edx;) lea rax, [inner vtable]; mov [rcx+inner], rax
	const Signatures::RegexSignature GraphicsAllocatorVtables(R"(\x48\x8D\x05(....)(?:[\x48\x4C]\x8B[\xC0-\xFF])?\x48\x89\x01(?:\x8B[\xC0-\xFF])?\x48\x8D\x05(....)\x48\x89\x81(....))");
	// Its slots: the alloc counts, then forwards to the inner allocator: mov eax, 1; lock xadd [rcx+counter], eax;
	// add rcx, inner; mov rax, [rcx]; jmp [rax+slot]. Most others only forward.
	const Signatures::RegexSignature GraphicsAllocatorCountingWrapper(R"(\xB8\x01\x00\x00\x00\xF0\x0F\xC1\x81(....)\x48\x81\xC1(....)\x48\x8B\x01\x48\xFF\x60(.))");
	const Signatures::RegexSignature GraphicsAllocatorForwardingWrapper(R"(\x48\x81\xC1(....)\x48\x8B\x01\x48\xFF\x60(.))");
	// The terminate releases the inner allocator, then calls the base's: push rbx; sub rsp, ..; mov rbx, rcx;
	// add rcx, inner; mov rax, [rcx]; call [rax+slot]; mov rax, [rbx+base]; lea rcx, [rbx+base]; ...
	const Signatures::RegexSignature GraphicsAllocatorTerminateWrapper(R"(\x40\x53\x48\x83\xEC.\x48\x8B\xD9\x48\x81\xC1(....)\x48\x8B\x01\xFF\x50(.)\x48\x8B\x43(.)\x48\x8D\x4B(.))");
	// The inner allocator's size query: mov r9, rdx; and r9, ~page mask; mov eax, [r9+page index]; cmp [rcx+chunk count], eax
	const Signatures::RegexSignature SmallObjectAllocatorSize(R"([\x48\x4C]\x8B[\xC8-\xCF][\x48\x49]\x81[\xE0-\xE7](....)[\x40-\x4F]?\x8B[\x40-\x7F](.)\x39[\x80-\xBF](....))");

	// The tail jump to the follow AI at the end of Companion::Update.
	const Signatures::RegexSignature CompanionFollowJump(R"(\xE9(....)\x48\x8B\xCF\xE8....\xF3\x0F\x58\x87....\x0F\x2F\x05)");
	// Elsewhere in Companion::Update: call [rax+0x108] (draw-ready check); test or cmp al; jz or jnz; cmp qword [rdi+render flags], 0
	const Signatures::RegexSignature CompanionRenderFlagsTest(R"(\xFF\x90\x08\x01\x00\x00(?:\x3C\x01\x0F\x85....|\x84\xC0\x0F\x84....|\x3C\x01\x75.|\x84\xC0\x74.)\x48\x83[\xB8-\xBF](....)\x00)");

	// JobList::Prepare: call [rax+0x18] (wait for the previous run); reset counters; mov edx, -1; call [WaitForSingleObject]
	const Signatures::RegexSignature JobListArrayPrepare(R"(\x40\x53\x48\x83\xEC\x20\x48\x8B\x01\x48\x8B\xD9\xFF\x50\x18\x33\xD2\x8B\xC2\x87\x43\x7C\x87\x93\xA0\x00\x00\x00\x48\x8B\x4B\x10\xBA\xFF\xFF\xFF\xFF(\xFF\x15)(....))");
	const Signatures::RegexSignature JobListSingleItemPrepare(R"(\x40\x53\x48\x83\xEC\x20\x48\x8B\x01\x48\x8B\xD9\xFF\x50\x18\x33\xC0\xBA\xFF\xFF\xFF\xFF\x87\x43\x74\x48\x8B\x4B\x10(\xFF\x15)(....))");

	// Callers of the job list kick: mov rdx, [group+0x18]; mov rcx, [g_TaskManager]; call kick
	const Signatures::RegexSignature JobListKickCaller(R"(\x48\x8B[\x50-\x57]\x18\x48\x8B\x0D(....)\xE8(....))");
	const Signatures::RegexSignature RenderManagerLoad(R"(\x48\x8B\x0D(....)\xE8....\x84\xC0\x74.\x48\x8B\x0D....\xE8....\x33\xC9)");
	// Manager::RenderView: lea rdx, [manager+prep list]; add rcx, TaskManager (rcx: g_Framework); call kick
	const Signatures::RegexSignature BgInstancingPrepKickCall(R"(\x48\x8D\x93(....)\x48\x81\xC1(....)\xE8(....))");
	// The kick's calls on the job list (rbx): call [rax+count]; test eax, eax; jz; then call [rax+prepare];
	// call [rax+describe] (rdx: the descriptor); call [rax+count] again
	const Signatures::RegexSignature JobListKickCalls(R"(\xFF\x50(.)\x85\xC0(?:\x0F\x84....|\x74.)[\x48\x49]\x8B[\x00-\x3F][\x48\x49]\x8B[\xC8-\xCF]\xFF\x50(.)[\x48\x49]\x8B[\x00-\x3F](?:\x48\x8D\x54\x24.|[\x48\x49]\x8B[\xC8-\xCF]){2}\xFF\x50(.)[\x48\x49]\x8B[\x00-\x3F][\x48\x49]\x8B[\xC8-\xCF]\xFF\x50(.))");
	// InnerThread::Run running a claimed task: mov rdx, [thread+pool]; mov rcx, rdi (task); mov r8, [rsp+argument];
	// add rdx, context; mov rax, [rdi]; test r8, r8; jnz; call [rax+slot]; jmp; mov r8, [r8]; call [rax+slot]
	const Signatures::RegexSignature JobRunTask(R"(\x48\x8B[\x50-\x57](.)[\x48\x49]\x8B[\xC8-\xCF][\x4C\x48]\x8B[\x44\x4C]\x24.[\x48\x49]\x83\xC2(.)[\x48\x49]\x8B[\x00-\x07][\x4D\x48]\x85[\xC0-\xFF]\x75.\xFF\x50(.)\xE9....[\x4D\x49]\x8B[\x00-\x3F]\xFF\x50(.))");
	// And claiming it first, from the queue entry (rbx): mov rax, [rbx+claim]; lea rdx, [rbx+state];
	// mov rcx, [rbx+owner]; lea r9, [rsp+remaining]; lea r8, [rsp+argument]; ...; call rax. Before 7.30, mov rcx, [rbx+owner];
	// lea rdx, [rbx+state]; lea r9; ...; lea r8; call [rbx+claim].
	const Signatures::RegexSignature JobRunClaim(R"((?:\x48\x8B[\x40-\x47](.)\x48\x8D[\x50-\x57](.)\x48\x8B[\x48-\x4F](.)\x4C\x8D\x4C\x24.\x4C\x8D\x44\x24..{0,8}?\xFF\xD0|\x48\x8B[\x48-\x4F](.)\x48\x8D[\x50-\x57](.)\x4C\x8D\x4C\x24..{0,8}?\x4C\x8D\x44\x24.\xFF[\x50-\x57](.)))");

	// lea rcx, [tmp]; call HotbarUIIntermediate::ctor; mov rcx, [module]; lea r8, [tmp]; mov rdx, slot; call Prepare;
	// inc slot index; add slot id, 0x11
	const Signatures::RegexSignature HotbarPrepare(R"((\x48\x8D\x4C\x24(.)\xE8(....)[\x48\x49]\x8B[\x88-\x8F]....\x4C\x8D\x44\x24\2[\x48\x49]\x8B[\xD0-\xD7]\xE8(....))\xFF[\xC0-\xC7]\x83[\xC0-\xC7]\x11)");

	const Signatures::RegexSignature AnimationUpdateCall(R"(\xE8(....)\x48\x8B\x0D....\x48\x8B\x6C\x24\x58)");
	const Signatures::RegexSignature AnimationTailStart(R"(\x48\x89\x5C\x24\x18\x55\x48\x83\xEC\x30\x48\x8B\xE9)");
	// In the animation update: mov [entry count], reg; lea reg, [entries]; cmp reg, 1
	const Signatures::RegexSignature AnimationTailEntries(R"(\x89[\x05\x0D\x15\x1D\x2D\x35\x3D](....)\x48\x8D[\x05\x0D\x15\x1D\x2D\x35\x3D](....)\x83[\xF8-\xFF]\x01)");
	const Signatures::RegexSignature AnimationSubmit(R"(\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x20\x48\x8B\x05(....)\x48\x8B\xD9\xF3\x0F\x11\x88\xF0\x00\x00\x00)");
	// The animation submit, in this order. Its group: mov r9, [submit base]; xor esi, esi; mov edi, [r9+writer count];
	// lea rbx, [r9+group]
	const Signatures::RegexSignature AnimationSubmitGroup(R"([\x48\x4C]\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....)(?:[\x40-\x4F]?\x33[\xC0-\xFF])?[\x40-\x4F]?\x8B[\x40-\x7F](.)[\x48\x49\x4C\x4D]\x8D[\x40-\x7F](.))");
	// Publishing every writer's item count to its block: mov rax, [rbx+writers]; mov r8, [rcx+rax+block];
	// test r8, r8; jz; mov eax, [rcx+rax+items]; mov [r8], eax; add rcx, writer size; sub rdx, 1; jnz
	const Signatures::RegexSignature ParallelForWriterFlush(R"([\x48\x49]\x8B[\x40-\x7F](.)[\x48\x4C]\x8B[\x44\x4C\x54\x5C\x64\x6C\x74\x7C].(.)[\x48\x4D]\x85[\xC0-\xFF]\x74.\x8B[\x44\x4C\x54\x5C\x64\x6C\x74\x7C].(.)[\x40-\x4F]?\x89[\x00-\x3F][\x48\x49]\x83[\xC0-\xC7](.)[\x48\x49]\x83[\xE8-\xEF]\x01\x75.)");
	// Arming: lea rax, [job]; mov [rbx+context], r9; mov [rbx+job], rax; mov eax, esi; xchg [rbx+counter], eax (twice);
	// mov eax, esi; lock xadd [rbx+blocks claimed], eax; test eax, eax; jz (nothing appended)
	const Signatures::RegexSignature ParallelForArm(R"(\x48\x8D\x05(....)[\x48\x4C]\x89[\x40-\x7F](.)\x48\x89[\x40-\x7F](.)\x8B[\xC0-\xFF]\x87[\x80-\xBF](....)\x8B[\xC0-\xFF]\x87[\x80-\xBF](....)\x8B[\xC0-\xFF]\xF0\x0F\xC1[\x80-\xBF](....)\x85\xC0\x74.)");
	// mov rdx, [group+job list]; mov rcx, [g_TaskManager]; call kick; mov rcx, group; cmp [group+per-item claims], <zero>;
	// je; call help; jmp; call help; then the wait: mov rcx, [group+job list]; mov rax, [rcx]; call [rax+wait]
	const Signatures::RegexSignature AnimationSubmitKick(R"(\x48\x8B[\x50-\x57](.)\x48\x8B\x0D(....)\xE8(....)[\x48\x49]\x8B[\xC8-\xCF][\x40-\x47]?\x38[\x80-\xBF](....)\x74\x07\xE8(....)\xEB\x05\xE8(....)[\x48\x49]\x8B[\x48-\x4F](.)\x48\x8B\x01\xFF\x50(.))");
	// Disarming: mov [rbx+context], rsi; mov [rbx+job], rsi; then emptying every writer: mov rax, [rbx+writers];
	// lea rcx, [rcx+writer size]; mov dword [rcx+rax+items-size], items per block; mov [rcx+rax+block-size], rsi
	const Signatures::RegexSignature ParallelForDisarm(R"([\x48\x4C]\x89[\x40-\x7F](.)[\x48\x4C]\x89[\x40-\x7F](.).{0,40}?[\x48\x49]\x8B[\x40-\x7F](.)[\x48\x49]\x8D[\x40-\x7F](.)\xC7\x44[\x00-\x3F](.)(....)[\x48\x4C]\x89[\x44\x4C\x54\x5C\x64\x6C\x74\x7C][\x00-\x3F](.))");
	// Emptying the chunks, from the submit base: lea rax, [r8+chunks]; mov edx, chunk count; mov rcx, [rax];
	// test rcx, rcx; jz; mov [rcx], esi; add rax, 8; sub rdx, 1; jnz; ...; mov [r8+blocks claimed], esi
	const Signatures::RegexSignature ParallelForChunkReset(R"([\x48\x49]\x8D[\x40-\x47](.)\xBA(....)[\x48\x49]\x8B[\x00-\x3F][\x48\x49]\x85[\xC0-\xFF]\x74\x02\x89[\x00-\x3F][\x48\x49]\x83[\xC0-\xC7]\x08[\x48\x49]\x83[\xE8-\xEF]\x01\x75..{0,8}?[\x40-\x4F]?\x89[\x80-\xBF](....))");
	// The append's prologue, up to mov ebp, TLS slot of the thread's parallel-for writer
	const Signatures::RegexSignature AnimationTailAppend(R"(\x40\x53\x55\x41\x54\x48\x83\xEC\x20\x8B\x0D....\x4C\x8B\xE2\x65\x48\x8B\x04\x25\x58\x00\x00\x00\xBD(....))");
	// Then claiming a block when the writer's is full: mov ebx, [rbp+items]; cmp ebx, items per block - 1; jbe;
	// mov rax, [rbp+block]; test rax, rax; jz; mov [rax], ebx; mov rdx, [rbp+group]; ...; lock xadd [rdx+blocks claimed],
	// ecx; ...; shr r8d, chunk shift; cmp r8d, chunk count; jb (past the last chunk, it stores the item through null);
	// ...; lock xadd [r15+chunks], rax
	const Signatures::RegexSignature ParallelForBlockClaim(R"(\x8B[\x40-\x7F](.)\x83[\xF8-\xFF](.)(?:\x0F\x86....|\x76.)[\x48\x4C]\x8B[\x40-\x7F](.)[\x48\x4D]\x85[\xC0-\xFF]\x74\x02\x89[\x00-\x3F][\x48\x4C]\x8B[\x40-\x7F](.).{0,8}?\xF0[\x40-\x4F]?\x0F\xC1[\x80-\xBF](....).{0,8}?[\x41]?\xC1[\xE8-\xEF](.)[\x41]?\x83[\xF8-\xFF](.)\x72..{0,64}?\xF0[\x48\x49\x4C\x4D]\x0F\xC1[\x40-\x7F](.))");
	// The tail's ground ray: mov rcx, [skeleton+ground]; test rcx, rcx; jz; call is active; test al, al; jz
	const Signatures::RegexSignature AnimationTailGround(R"([\x48\x49]\x8B[\x88-\x8F](....)[\x48\x4D]\x85\xC9\x74.\xE8(....)\x84\xC0\x74.)");
	// That test, which only reads the ground state: test byte [rcx+flags], 1; jz; cmp qword [rcx+..], 0; jz; (again); mov al, 1; ret
	const Signatures::RegexSignature GroundRayActiveBody(R"(\xF6\x41.\x01\x74.\x48\x83\x79.\x00\x74.\x48\x83\x79.\x00\x74.\xB0\x01\xC3)");
	// Then every partial skeleton: cmp qword [partial+pose], 0; jz; (movaps xmm1, xmm6;) mov rcx, partial; call update
	const Signatures::RegexSignature AnimationTailPartialUpdate(R"([\x48\x49]\x83[\xB8-\xBF](....)\x00\x74.(?:\x0F\x28[\xC8-\xCF]|[\x48\x49]\x8B[\xC8-\xCF]){1,3}\xE8(....))");
	// The partial skeleton update ends by applying every pending animation control removal: ...; lea rcx, [partial+list];
	// call erase; cmp qword [partial+count], 0; jnz
	const Signatures::RegexSignature PartialPendingRemovals(R"([\x48\x49\x4C\x4D]\x8D[\x88-\x8F](....)\xE8....[\x48\x49]\x83[\xB8-\xBF](....)\x00\x75.)");

	// A parallel-for group being armed: lea rax, [job]; mov [group+0x20], reg; mov [group+0x28], rax
	const Signatures::RegexSignature ParallelForJobStore(R"(\x48\x8D\x05(....)[\x4C\x48]\x89[\x40-\x7F]\x20\x48\x89[\x40-\x7F]\x28)");
	// What the camera culling job reads of its item (rdx) first: the type byte, the start at +0x30 and the count at +0x34
	const Signatures::RegexSignature CameraCullItemType(R"([\x40-\x4F]?\x0F\xB6[\x02\x0A\x12\x1A\x32\x3A])");
	const Signatures::RegexSignature CameraCullItemStart(R"([\x40-\x4F]?\x8B[\x42\x4A\x52\x5A\x62\x6A\x72\x7A]\x30)");
	const Signatures::RegexSignature CameraCullItemCount(R"([\x40-\x4F]?\x8B[\x42\x4A\x52\x5A\x62\x6A\x72\x7A]\x34)");
	// Where the culling builds its items: object type tests (cmp r11b, type) sort the visible objects into lists, then
	// every list but the BG objects' becomes one item: call allocate item; ...; mov [rax+8], list; mov byte [rax], type
	const Signatures::RegexSignature CameraCullObjectTypeTest(R"(\x41\x80[\xF8-\xFF](.)\x75)");
	const Signatures::RegexSignature CameraCullSingleItem(R"(\xE8(....).{0,32}?\x48\x89[\x40-\x7F]\x08\xC6[\x00-\x03\x06\x07](.))");
	// The item allocator returns the item at the writer's cursor: lea rax, [block+header]; mov [writer+cursor], rax;
	// ...; mov eax, ebx (index); shl rax, item shift; add rax, [writer+cursor]
	const Signatures::RegexSignature CameraCullItemAddress(R"([\x48\x49]\x8D[\x40-\x47](.)[\x48\x4C]\x89[\x40-\x7F](.).{0,24}?[\x48\x49]\xC1[\xE0-\xE7](.)[\x48\x49]\x03[\x40-\x7F](.))");

	// add rcx, cell group (or lea); lea r8, [cell job]; movzx r9d, r12b; mov rdx, rsi (in any order); register restores;
	// call or jmp parallel-for
	const Signatures::RegexSignature CullingCellParallelFor(R"([\x48\x49](?:\x81[\xC0-\xC7]|\x8D[\x80-\xBF])(....)(?:\x45\x0F\xB6[\xC8-\xCF]|\x4C\x8D\x05....|[\x48\x49]\x8B[\xD0-\xD7]){3}(?:[\x48\x4C]\x8B[\x40-\x7F]\x24.|\x48\x83\xC4.|\x41?[\x58-\x5F])*[\xE8\xE9](....))");
	// lea rcx, [rsi+setup group]; mov r9b, 1; lea r8, [job]; mov rdx, rsi; call parallel-for
	const Signatures::RegexSignature CullingSetupParallelFor(R"([\x48\x49]\x8D[\x80-\xBF](....)\x41\xB1\x01\x4C\x8D\x05....[\x48\x49]\x8B[\xD0-\xD7]\xE8(....))");
	// cmp [group+0xBC], <zero> (per-item claims); je; call per-item help; jmp; call block help
	const Signatures::RegexSignature ParallelForHelpChoice(R"(\x40\x38[\x80-\xBF]\xBC\x00\x00\x00\x74\x07\xE8(....)\xEB\x05\xE8(....))");

	// The gather is compiled without optimizations, which keeps it stable.
	const Signatures::RegexSignature CommandListGatherDriver(R"(\x4C\x89\x4C\x24\x20\x4C\x89\x44\x24\x18\x89\x54\x24\x10\x48\x89\x4C\x24\x08\x48\x83\xEC\x48\x48\x8B\x44\x24\x50)");
	// In the gather: mov rax, [rsp+device]; mov eax, [rax+context count]; mov [rsp+...], eax
	const Signatures::RegexSignature CommandListContextCount(R"(\x8B\x40(.)\x89\x44\x24.)");
	// imul rax, rax, context size; mov rcx, [rsp+device]; add rax, [rcx+contexts]
	const Signatures::RegexSignature CommandListContextArray(R"([\x48\x4C]\x69[\xC0-\xFF](....)[\x48\x4C]\x8B[\x44\x4C\x54\x5C]\x24.[\x48\x4C]\x03[\x40-\x7F](.))");
	// The gather of one context's blocks starts with: inc rax (list + 1); lea rax, [rax+rax*2]; lea r12, [rcx+rax*8];
	// mov eax, [rcx+rax*8+blocks]; mov ebp, eax; shl ebp, block shift
	const Signatures::RegexSignature CommandListBlocks(R"(\x48\xFF\xC0.{0,12}?\x48\x8D\x04\x40.{0,8}?\x4C\x8D([\x04\x0C\x14\x1C\x24\x2C\x34\x3C])\xC1\x8B\x44\xC1(.)\x8B[\xE8-\xEF]\xC1[\xE0-\xE7](.))");
	// Then, before copying, it loads the first block from that list: mov rdi, [r12] (or r13, in 7.20)
	const Signatures::RegexSignature CommandListFirstBlock(R"([\x48-\x4F]\x8B)");
	// Its copy of every full block: call memcpy; mov rdi, [rdi+next block]; add rbx, block size; sub rsi, 1; jnz
	const Signatures::RegexSignature CommandListBlockCopy(R"(\xE8....[\x48\x4C]\x8B[\x80-\xBF](....)[\x48\x49]\x81[\xC0-\xC7](....)[\x48\x49]\x83[\xE8-\xEF]\x01\x75.)");
	// Then the entry count: shr ebp, entry shift; sub ebp, [r12+free slots]
	const Signatures::RegexSignature CommandListEntryCount(R"(\xC1[\xE8-\xEF](.)[\x40-\x4F]?\x2B(?:[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24|[\x40-\x7F])(.))");
}

namespace XivAlexander::Game::Resolved {
	namespace {
		using Signatures::Address;
		using Signatures::RegexSignature;
		using Signatures::ResolveContext;
		using Signatures::ResolveError;
		using Signatures::ScanResult;

		template<typename T>
		std::vector<T> AllMatchStarts(const ResolveContext& ctx, const RegexSignature& signature) {
			std::vector<T> result;
			for (const auto& m : ctx.All(signature, ctx.Text()))
				result.push_back(Address(m.begin(0)));
			return result;
		}

		size_t FieldOffset(const ResolveContext& ctx, const ScanResult& m, size_t group, std::string_view what) {
			return static_cast<size_t>(ctx.InRange<int32_t>(m.Get<int32_t>(group), 1, 0x10000, what));
		}

		/// Resolves a part that its feature can do without. A part that fails is logged as a failed signature is, and left
		/// empty, so that the feature's signature still resolves everything else.
		template<typename TFn>
		auto OptionalPart(std::string_view feature, std::string_view part, TFn&& resolve) -> std::optional<std::invoke_result_t<TFn&>> {
			try {
				return resolve();
			} catch (const std::exception& e) {
				Misc::Logger::Acquire()->Format<LogLevel::Warning>(LogCategory::Signatures, "{}: {}: {}", feature, part, e.what());
				return std::nullopt;
			}
		}

		bool InOpcodeRange(uint32_t v) {
			return 0x0001 <= v && v <= 0x1000;
		}

		std::optional<uint16_t> FindActionRequest(std::span<const uint8_t> text) {
			for (const auto& m : OpcodeActionRequest.Lookup(text, RegexSignature::FromNextByte)) {
				if (const auto opcode = static_cast<uint16_t>(m.Get<uint32_t>(2)); InOpcodeRange(opcode))
					return opcode;
			}
			return std::nullopt;
		}

		std::optional<uint16_t> FindActionRequestGroundTargeted(const ResolveContext& ctx, std::span<const uint8_t> text) {
			for (const auto& call : OpcodeCaller.Lookup(text, RegexSignature::FromNextByte)) {
				const auto body = ctx.Module().FunctionAt(call.ResolveAddress<const uint8_t*>(1));
				if (body.empty())
					continue;

				if (const auto m = ctx.Find(OpcodeGroundTargetedSender, body)) {
					if (const auto opcode = static_cast<uint16_t>(m->Get<uint32_t>(1)); InOpcodeRange(opcode))
						return opcode;
				}
			}
			return std::nullopt;
		}

		std::vector<OpcodeGuesserCandidates::PayloadWriter> FindDutyRecorderPayloadWriters(const ResolveContext& ctx, std::span<const uint8_t> text) {
			const auto base = ctx.Image().data();
			const auto offsetOf = [base](const ScanResult& m) {
				return static_cast<size_t>(static_cast<const uint8_t*>(m.begin(0)) - base);
			};

			std::vector<OpcodeGuesserCandidates::PayloadWriter> result;
			for (const auto& m : OpcodeSizeFirst.Lookup(text, RegexSignature::FromNextByte)) {
				if (const auto opcode = static_cast<uint16_t>(m.Get<uint32_t>(2)); InOpcodeRange(opcode))
					result.push_back({offsetOf(m), m.Get<uint32_t>(1), opcode});
			}

			for (const auto& m : OpcodeSizeFromRegister.Lookup(text, RegexSignature::FromNextByte)) {
				const auto value = m.Get<uint32_t>(1);
				if (const auto opcode = static_cast<uint16_t>(value); InOpcodeRange(opcode))
					result.push_back({offsetOf(m), value, opcode});
			}

			for (const auto& m : OpcodeConditional.Lookup(text, RegexSignature::FromNextByte)) {
				if (const auto opcode = static_cast<uint16_t>(m.Get<uint32_t>(1)); InOpcodeRange(opcode))
					result.push_back({offsetOf(m), m.Get<uint32_t>(3), opcode});
			}

			std::ranges::sort(result, [](const auto& a, const auto& b) { return a.Offset < b.Offset; });
			return result;
		}

		const void* ImportSlot(const ResolveContext& ctx, const char* dllName, const char* functionName) {
			const auto slot = Utils::Win32::Process::Current().FindImportedFunction(*ctx.Module(), dllName, functionName).first;
			ctx.Require(slot != nullptr, ResolveError::NotFound, "{}!{} is not imported", dllName, functionName);
			return slot;
		}

		// The import slot a call reads its target from: call [rip+slot], or call to a jmp [rip+slot] thunk.
		const void* ImportSlotCalledAt(const ResolveContext& ctx, const ScanResult& m, size_t group) {
			const auto target = m.ResolveAddress<const uint8_t*>(group);
			if (static_cast<const uint8_t*>(m.begin(group))[-1] != 0xE8)
				return target;
			if (!ctx.InSection(target, ".text"))
				return nullptr;

			const auto jump = target[0] == 0x48 ? target + 1 : target;
			if (jump[0] != 0xFF || jump[1] != 0x25)
				return nullptr;
			return jump + 6 + *reinterpret_cast<const int32_t*>(jump + 2);
		}

		const void* UniqueCallTarget(const ResolveContext& ctx, const RegexSignature& signature, std::string_view what) {
			return ctx.RequireInSection(ctx.Unique(signature, ctx.Text(), what).ResolveAddress<const void*>(1), ".text", what);
		}

		const void* UniqueFunctionStart(const ResolveContext& ctx, const RegexSignature& signature, std::string_view what) {
			return ctx.FunctionStartingAt(ctx.Unique(signature, ctx.Text(), what).begin(0), what).data();
		}

		bool Calls(std::span<const uint8_t> fn, const void* target) {
			for (const auto& m : RelativeCall.Lookup(fn, RegexSignature::FromNextByte)) {
				if (m.ResolveAddress<const void*>(1) == target)
					return true;
			}
			return false;
		}

		// The code from p on: up to length bytes, and not past the end of .text.
		std::span<const uint8_t> CodeFrom(const ResolveContext& ctx, const void* p, size_t length) {
			const auto text = ctx.Text();
			const auto begin = static_cast<const uint8_t*>(ctx.RequireInSection(p, ".text", "code"));
			return {begin, (std::min)(length, static_cast<size_t>(text.data() + text.size() - begin))};
		}

		// What follows a match within data.
		std::span<const uint8_t> After(std::span<const uint8_t> data, const ScanResult& m) {
			return data.subspan(static_cast<size_t>(static_cast<const uint8_t*>(m.end(0)) - data.data()));
		}

		size_t ByteAt(const ScanResult& m, size_t group) {
			return m.Get<uint8_t>(group);
		}

		// The parts of the function that starts at primary, within 16 KB after it: the first part, and those whose unwind
		// info chains back to it, which is where the compiler moves the rarely run code of a function.
		std::vector<std::span<const uint8_t>> FunctionParts(const ResolveContext& ctx, const void* primary) {
			const auto base = reinterpret_cast<const uint8_t*>(*ctx.Module());
			const auto image = xivres::pe_image::from_loaded(base);
			const auto rva = static_cast<uint32_t>(static_cast<const uint8_t*>(primary) - base);
			const auto table = image.function_table();

			std::vector<std::span<const uint8_t>> parts;
			for (auto it = std::ranges::lower_bound(table, rva, {}, &xivres::pe_image::runtime_function::BeginAddress);
				it != table.end() && it->BeginAddress < rva + 0x4000; ++it) {
				if (image.primary_of(*it).BeginAddress == rva)
					parts.emplace_back(base + it->BeginAddress, it->EndAddress - it->BeginAddress);
			}
			ctx.Require(!parts.empty(), ResolveError::NotFound, "no function starts at {}", Signatures::Describe(primary));
			return parts;
		}

		uint32_t* FindMixRateSetup(const ResolveContext& ctx) {
			return ctx.Unique(AudioSamplingRateImmediate, ctx.Text(), "setup").begin<uint32_t>(1);
		}

		SoundVoiceRenderInfo FindVoiceRender(const ResolveContext& ctx) {
			const auto m = ctx.Unique(SoundVoiceRender, ctx.Text(), "render");
			const auto getter = ctx.RequireInSection(m.ResolveAddress<const void*>(3), ".text", "mix rate getter");
			const auto g = ctx.MatchAt(AudioSamplingRateGetter, ctx.FunctionStartingAt(getter, "mix rate getter"), "mix rate getter");
			return SoundVoiceRenderInfo{
				.State = FieldOffset(ctx, m, 1, "state offset"),
				.QueuedBuffers = FieldOffset(ctx, m, 2, "queued buffers offset"),
				.MixRate = g.ResolveAddress<uint32_t*>(1),
			};
		}

		// The voice's vtable functions, checked against the fields the render reads.
		SoundVoiceFunctions FindVoiceFunctions(const ResolveContext& ctx, const SoundVoiceRenderInfo& render) {
			SoundVoiceFunctions r;
			const auto text = ctx.Text();
			r.Init = Address(ctx.Unique(SoundVoiceInit, text, "initialiser").begin(0));

			const auto vt = ctx.Unique(SoundVoiceVtable, text, "vtable");
			const auto slots = ctx.Vtable(vt.ResolveAddress<const void* const*>(1), SoundVoice::MaxVtblSlots, "vtable");
			const auto destructorBody = ctx.FunctionContaining(vt.begin(0), "destructor").data();

			// every slot is told apart by what it does, so that a reordered vtable is still understood
			const auto submit = ctx.UniqueSlot(slots, "submit", [&ctx](size_t, std::span<const uint8_t> fn) -> std::optional<SoundVoice::Layout> {
				const auto m = ctx.Find(SoundVoiceSubmit, fn);
				if (!m)
					return std::nullopt;
				return SoundVoice::Layout{
					.State = FieldOffset(ctx, *m, 1, "submit state offset"),
					.QueuedBuffers = FieldOffset(ctx, *m, 2, "submit queued buffers offset"),
				};
			});
			r.Layout = submit.second;
			r.Layout.SubmitSlot = submit.first;

			const auto setMarker = ctx.UniqueSlot(slots, "set marker", [&ctx](size_t, std::span<const uint8_t> fn) -> std::optional<size_t> {
				const auto m = ctx.TryMatchAt(SoundVoiceSetMarker, fn);
				if (!m)
					return std::nullopt;
				return static_cast<size_t>(m->Get<int32_t>(1));
			});
			ctx.Require(setMarker.second == r.Layout.State, ResolveError::Mismatch,
				"submit state +0x{:X} != set marker state +0x{:X}", r.Layout.State, setMarker.second);
			r.Layout.SetMarkerSlot = setMarker.first;

			ctx.Require(render.State == r.Layout.State && render.QueuedBuffers == r.Layout.QueuedBuffers, ResolveError::Mismatch,
				"submit state +0x{:X} and queued buffers +0x{:X} != render state +0x{:X} and queued buffers +0x{:X}",
				r.Layout.State, r.Layout.QueuedBuffers, render.State, render.QueuedBuffers);

			// the deleting destructor calls the destructor proper within its first few instructions
			r.Layout.DestructorSlot = ctx.UniqueSlotIndex(slots, "destructor", [destructorBody](size_t, std::span<const uint8_t> fn) {
				for (const auto& m : RelativeCall.Lookup(fn.first((std::min)(fn.size(), static_cast<size_t>(48))), RegexSignature::FromNextByte)) {
					if (m.ResolveAddress<const uint8_t*>(1) == destructorBody)
						return true;
				}
				return false;
			});

			// the flush is what puts the voice in the flushed state
			const auto state = r.Layout.State;
			r.Layout.FlushSlot = ctx.UniqueSlotIndex(slots, "flush", [state](size_t, std::span<const uint8_t> fn) {
				for (const auto& m : SoundVoiceStoreImmediate.Lookup(fn)) {
					if (std::cmp_equal(m.Get<int32_t>(1), state) && m.Get<int32_t>(2) == static_cast<int32_t>(SoundVoiceState::Flushed))
						return true;
				}
				return false;
			});

			r.Submit = Address(slots[r.Layout.SubmitSlot]);
			r.SetMarker = Address(slots[r.Layout.SetMarkerSlot]);
			r.Destructor = Address(slots[r.Layout.DestructorSlot]);
			r.Flush = Address(slots[r.Layout.FlushSlot]);
			return r;
		}

		SoundBufferEndInfo FindBufferEndHandler(const ResolveContext& ctx) {
			const auto m = ctx.Unique(SoundBufferEndHandler, ctx.Text(), "handler");
			return SoundBufferEndInfo{
				.Handler = Address(m.begin(0)),
				.CallbackLayout = {.EndOfData = FieldOffset(ctx, m, 1, "end of data offset")},
			};
		}

		LobbyLoginFn FindLobbyLogin(const ResolveContext& ctx) {
			const void* target = nullptr;
			for (const auto& call : ctx.All(LobbyLoginCaller, ctx.Text())) {
				const auto p = call.ResolveAddress<const void*>(1);
				ctx.Require(!target || target == p, ResolveError::Ambiguous,
					"lobby login callers call both {} and {}", Signatures::Describe(target), Signatures::Describe(p));
				target = p;
			}
			ctx.Require(target != nullptr, ResolveError::NotFound, "lobby login caller not found");
			const auto fn = ctx.FunctionStartingAt(target, "lobby login");

			// The hook relies on the argument layout: 8 arguments, the last picking one of two branches that each copy
			// the three Utf8String arguments with the same function.
			ctx.Require(ctx.Find(LobbyLoginLastArgumentTest, fn.first((std::min)(fn.size(), static_cast<size_t>(0x60)))).has_value(), ResolveError::Mismatch,
				"lobby login {} does not test its last argument", Signatures::Describe(target));
			std::map<const void*, size_t> callees;
			for (const auto& call : RelativeCall.Lookup(fn))
				++callees[call.ResolveAddress<const void*>(1)];
			ctx.Require(std::ranges::any_of(callees, [](const auto& callee) { return callee.second == 6; }), ResolveError::Mismatch,
				"lobby login {} does not copy three strings on both branches", Signatures::Describe(target));
			return Address(fn.data());
		}

		LobbyErrorDialogFn FindLobbyErrorDialog(const ResolveContext& ctx) {
			const auto match = ctx.Unique(LobbyErrorDialog, ctx.Text(), "lobby error dialog");
			return Address(ctx.FunctionStartingAt(&match.Get<const uint8_t>(0), "lobby error dialog").data());
		}
	}

	std::string to_string(const SqpackLookupHooksFunctions& value) {
		return std::format("index lookups {}", Signatures::Describe(value.IndexLookups));
	}

	std::string to_string(const TextHooksFunctions& value) {
		return std::format("cut scene language getter {}, string indirection resolvers {}",
			Signatures::Describe(value.CutSceneLanguageGetter),
			Signatures::Describe(value.StringIndirectionResolvers));
	}

	std::string to_string(const AudioResamplerFunctions& value) {
		return std::format("mix rate setup {}, render: {}, voice: {}, buffer end: {}",
			Signatures::Describe(value.MixRateSetup),
			Signatures::Describe(value.Render),
			Signatures::Describe(value.Voice),
			Signatures::Describe(value.BufferEnd));
	}

	std::string to_string(const MainThreadTimingHandlerFunctions& value) {
		return std::format("single message loop {}", Signatures::Describe(value.SingleMessageLoop));
	}

	std::string to_string(const LoginSessionsFunctions& value) {
		return std::format("lobby login {}, lobby error dialog {}",
			Signatures::Describe(value.LobbyLogin),
			Signatures::Describe(value.LobbyErrorDialog));
	}

	std::string to_string(const AltCodecMusicSupportFunctions& value) {
		return std::format("attribute {}, reset {}, open {}, set up {}, process {}",
			Signatures::Describe(value.Attribute),
			Signatures::Describe(value.Reset),
			Signatures::Describe(value.Open),
			Signatures::Describe(value.SetUpDecoder),
			Signatures::Describe(value.Process));
	}

	std::string to_string(const OpcodeGuesserCandidates& value) {
		return std::format(
			"ActionEffect01 {}, ActionEffect08 {}, ActionEffect16 {}, ActionEffect24 {}, ActionEffect32 {}, "
			"ActorControl {}, ActorControlSelf {}, ActorCast {}, ActionRequest {}, ActionRequestGroundTargeted {}, "
			"{} payload writers",
			Signatures::Describe(value.S2C_ActionEffects[0]),
			Signatures::Describe(value.S2C_ActionEffects[1]),
			Signatures::Describe(value.S2C_ActionEffects[2]),
			Signatures::Describe(value.S2C_ActionEffects[3]),
			Signatures::Describe(value.S2C_ActionEffects[4]),
			Signatures::Describe(value.S2C_ActorControl),
			Signatures::Describe(value.S2C_ActorControlSelf),
			Signatures::Describe(value.S2C_ActorCast),
			Signatures::Describe(value.C2S_ActionRequest),
			Signatures::Describe(value.C2S_ActionRequestGroundTargeted),
			value.PayloadWriters.size());
	}

	std::string to_string(const ImeModeIndicatorFunctions& value) {
		return std::format("IME mode getter {}, input context {}",
			Signatures::Describe(value.GetImeMode),
			Signatures::Describe(value.InputContext));
	}

	std::string to_string(const FontReplacementFunctions& value) {
		return std::format("Present call {}, Present {}",
			Signatures::Describe(value.PresentCall),
			Signatures::Describe(value.Present));
	}

	const Signatures::ComplexSignature<SqpackLookupHooksFunctions> SqpackLookupHooks("SqpackLookupHooks", [](ResolveContext& ctx) {
		// Each SqPackManager keeps the lookup for its kind of index in a member, which LoadSqPack sets with the
		// address of either function and then compares against one of them.
		const auto count = ctx.Unique(SqPackIndexEntryCount, ctx.Text(), "index entry count");
		const auto member = FieldOffset(ctx, count, 1, "lookup member");
		const auto loadSqPack = ctx.FunctionContaining(count.begin(0), "LoadSqPack");

		// lea reg, [rip+fn], then mov [base+member], reg within the next few instructions.
		std::vector<SqPackIndexLookupFn> result;
		for (const auto& lea : ctx.All(RipRelativeLea, loadSqPack)) {
			const auto leaBytes = static_cast<const uint8_t*>(lea.begin(0));
			const auto leaReg = ((leaBytes[2] >> 3) & 7) | (leaBytes[0] & 4 ? 8 : 0);
			const auto after = static_cast<const uint8_t*>(lea.end(0));
			const auto limit = (std::min)(after + 24, loadSqPack.data() + loadSqPack.size());
			for (auto p = after; p + 7 <= limit; ++p) {
				if ((p[0] & 0xF0) != 0x40 || (p[0] & 0x08) == 0 || p[1] != 0x89 || (p[2] & 0xC0) != 0x80 || (p[2] & 7) == 4)
					continue;
				if (const auto movReg = ((p[2] >> 3) & 7) | (p[0] & 4 ? 8 : 0); movReg != leaReg)
					continue;
				if (static_cast<size_t>(*reinterpret_cast<const int32_t*>(p + 3)) != member)
					continue;

				const auto fn = lea.ResolveAddress<SqPackIndexLookupFn>(1);
				if (std::ranges::find(result, fn) == result.end()) {
					ctx.Require(!ctx.FunctionStartingAt(reinterpret_cast<const void*>(fn)).empty(), ResolveError::Mismatch,
						"index lookup {} is not where a function starts", Signatures::Describe(reinterpret_cast<const void*>(fn)));
					result.push_back(fn);
				}
				break;
			}
		}

		ctx.Require(result.size() == 2, ResolveError::Mismatch, "LoadSqPack sets {} lookup functions instead of 2", result.size());
		return SqpackLookupHooksFunctions{.IndexLookups = std::move(result)};
	});

	const Signatures::ComplexSignature<TextHooksFunctions> TextHooks("TextHooks", [](ResolveContext& ctx) {
		return TextHooksFunctions{
			.CutSceneLanguageGetter = OptionalPart("TextHooks", "cut scene language getter", [&ctx]() -> CutSceneLanguageGetterFn {
				return Address(ctx.First(CutSceneLanguageGetter, ctx.Text(), "call").ResolveAddress<const void*>(1));
			}),
			.StringIndirectionResolvers = AllMatchStarts<StringIndirectionResolverFn>(ctx, StringIndirectionResolver),
		};
	});

	const Signatures::ComplexSignature<Oodle::OodleNetworkFunctions> OodleNetwork("OodleNetwork", [](ResolveContext& ctx) {
		Oodle::OodleNetworkFunctions r;
		const auto text = ctx.Text();

		const auto init = ctx.First(OodleInit, text, "OodleInit");
		init.ResolveAddressInto(r.SetMallocFree, 1);
		init.GetInto(r.HtBits, 2);
		init.ResolveAddressInto(r.SharedSize, 3);
		init.GetInto(r.WindowSize, 4);
		init.ResolveAddressInto(r.SharedSetWindow, 5);

		const auto train = ctx.First(OodleSetUpStatesAndTrain, text, "OodleSetUpStatesAndTrain");
		train.ResolveAddressInto(r.UdpStateSize, 1);
		train.ResolveAddressInto(r.TcpStateSize, 2);
		train.ResolveAddressInto(r.TcpTrain, 3);
		train.ResolveAddressInto(r.UdpTrain, 4);

		const auto decode = ctx.First(OodleDecode, text, "OodleDecode");
		decode.ResolveAddressInto(r.TcpDecode, 1);
		decode.ResolveAddressInto(r.UdpDecode, 2);

		const auto encode = ctx.First(OodleEncode, text, "OodleEncode");
		encode.ResolveAddressInto(r.TcpEncode, 1);
		encode.ResolveAddressInto(r.UdpEncode, 2);
		return r;
	});

	const Signatures::ComplexSignature<OpcodeGuesserCandidates> OpcodeGuesser("OpcodeGuesser", [](ResolveContext& ctx) {
		OpcodeGuesserCandidates r;
		const auto text = ctx.Text();

		r.C2S_ActionRequest = FindActionRequest(text);
		r.C2S_ActionRequestGroundTargeted = FindActionRequestGroundTargeted(ctx, text);

		// (ActionEffect01,) 08, 16, 24, 32, ActorCast, ActorControl, ActorControlTarget, ActorControlSelf
		// (AE01->08: 0x200,) 08->...->32: 0x240
		r.PayloadWriters = FindDutyRecorderPayloadWriters(ctx, text);
		const auto& writers = r.PayloadWriters;
		const auto opcodeAt = [&writers](size_t i) -> std::optional<uint16_t> {
			if (i >= writers.size())
				return std::nullopt;
			return writers[i].Opcode;
		};

		for (size_t i = 2, streak = 0; i < writers.size(); ++i) {
			if (writers[i].PayloadSize == writers[i - 1].PayloadSize * 2 - writers[i - 2].PayloadSize
				&& writers[i].PayloadSize >= 0x200)
				++streak;
			else
				streak = 1;

			if (streak == 3) {
				r.S2C_ActionEffects[0] = opcodeAt(i - 4);
				r.S2C_ActionEffects[1] = opcodeAt(i - 3);
				r.S2C_ActionEffects[2] = opcodeAt(i - 2);
				r.S2C_ActionEffects[3] = opcodeAt(i - 1);
				r.S2C_ActionEffects[4] = opcodeAt(i + 0);
				r.S2C_ActorCast = opcodeAt(i + 1);
				r.S2C_ActorControl = opcodeAt(i + 2);
				// r.S2C_ActorControlTarget = opcodeAt(i + 3);
				r.S2C_ActorControlSelf = opcodeAt(i + 4);
				break;
			}
		}
		return r;
	});

	const Signatures::ComplexSignature<AudioResamplerFunctions> AudioResampler("AudioResampler", [](ResolveContext& ctx) {
		// The voices are resampled to the mix rate, so nothing works without it.
		AudioResamplerFunctions r{
			.MixRateSetup = FindMixRateSetup(ctx),
			.Render = FindVoiceRender(ctx),
		};
		r.Voice = OptionalPart("AudioResampler", "voice functions", [&] { return FindVoiceFunctions(ctx, r.Render); });
		r.BufferEnd = OptionalPart("AudioResampler", "buffer end handler", [&] { return FindBufferEndHandler(ctx); });
		return r;
	});

	const Signatures::ComplexSignature<AltCodecMusicSupportFunctions> AltCodecMusicSupport("AltCodecMusicSupport", [](ResolveContext& ctx) {
		AltCodecMusicSupportFunctions r;
		const auto text = ctx.Text();

		const Address attribute = ctx.First(MssAsiAttribute, text, "attribute").begin(0);
		r.Attribute = attribute;

		for (const auto& m : MssAsiResetPair.Lookup(text, RegexSignature::FromNextByte)) {
			if (m.ResolveAddress<const void*>(2) == attribute.Get()) {
				r.Reset = Address(m.ResolveAddress<const void*>(1));
				break;
			}
		}
		ctx.Require(r.Reset != nullptr, ResolveError::NotFound, "reset not found");

		r.Open = Address(ctx.First(MssAsiOpen, text, "open").ResolveAddress<const void*>(1));
		r.SetUpDecoder = Address(ctx.FunctionContaining(ctx.First(MssAsiSetUpDecoder, text, "decoder set-up").begin(0), "decoder set-up").data());
		r.Process = Address(ctx.FunctionContaining(ctx.First(MssAsiProcess, text, "process").begin(0), "process").data());
		return r;
	});

	const Signatures::ComplexSignature<MainThreadTimingHandlerFunctions> MainThreadTimingHandler("MainThreadTimingHandler", [](ResolveContext& ctx) {
		return MainThreadTimingHandlerFunctions{
			.SingleMessageLoop = Address(ctx.First(MessageLoop, ctx.Image(), "message loop").ResolveAddress<const void*>(1)),
		};
	});

	const Signatures::ComplexSignature<LoginSessionsFunctions> LoginSessions("LoginSessions", [](ResolveContext& ctx) {
		return LoginSessionsFunctions{
			.LobbyLogin = OptionalPart("LoginSessions", "lobby login", [&ctx] { return FindLobbyLogin(ctx); }),
			.LobbyErrorDialog = OptionalPart("LoginSessions", "lobby error dialog", [&ctx] { return FindLobbyErrorDialog(ctx); }),
		};
	});

	const Signatures::ComplexSignature<ImeModeIndicatorFunctions> ImeModeIndicator("ImeModeIndicator", [](ResolveContext& ctx) {
		const auto openStatus = ImportSlot(ctx, "imm32.dll", "ImmGetOpenStatus");
		const auto conversionStatus = ImportSlot(ctx, "imm32.dll", "ImmGetConversionStatus");

		std::optional<ImeModeIndicatorFunctions> found;
		for (const auto& m : ctx.All(ImeStatusQueries, ctx.Text())) {
			const auto inputContext = m.ResolveAddress<void* const*>(1);
			if (inputContext != m.ResolveAddress<void* const*>(3)
				|| ImportSlotCalledAt(ctx, m, 2) != openStatus
				|| ImportSlotCalledAt(ctx, m, 4) != conversionStatus)
				continue;

			// the queries are the first thing it does, right after reserving its stack
			const auto fn = ctx.FunctionContaining(m.begin(0), "IME mode getter");
			if (static_cast<const uint8_t*>(m.begin(0)) - fn.data() > 0x10 || !ctx.Find(ImeNoConversionTest, fn))
				continue;

			ctx.Require(!found, ResolveError::Ambiguous, "IME mode getters at {} and {}",
				Signatures::Describe(found ? found->GetImeMode : nullptr), Signatures::Describe(fn.data()));
			found = ImeModeIndicatorFunctions{
				.GetImeMode = Address(fn.data()),
				.InputContext = static_cast<void* const*>(ctx.RequireInSection(inputContext, ".data", "input context")),
			};
		}
		ctx.Require(found.has_value(), ResolveError::NotFound, "IME mode getter not found");
		return *found;
	});

	const Signatures::ComplexSignature<FontReplacementFunctions> FontReplacement("FontReplacement", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto call = ctx.Unique(SwapChainPresentCall, text, "Present call");
		const auto callInstruction = static_cast<const uint8_t*>(call.begin(1)) - 1;

		// Only PostTick walks the notifiers right before calling Present.
		const auto before = (std::min)(static_cast<size_t>(0x100), static_cast<size_t>(callInstruction - text.data()));
		ctx.Require(ctx.Find(PrePresentNotifierWalk, std::span(callInstruction - before, before)).has_value(), ResolveError::Mismatch,
			"the Present call at {} does not follow the pre-present notifier walk", Signatures::Describe(callInstruction));

		const auto present = call.ResolveAddress<const void*>(1);
		ctx.Require(!ctx.FunctionStartingAt(present).empty(), ResolveError::Mismatch,
			"Present {} is not where a function starts", Signatures::Describe(present));
		return FontReplacementFunctions{
			.PresentCall = Address(callInstruction),
			.Present = Address(present),
		};
	});
}

namespace XivAlexander::Game::Resolved::CrowdFix {
	namespace {
		// Both helps of a parallel-for group: per-item claims when its +0xBC flag is set, blocks of items otherwise.
		std::pair<ParallelForHelpFn, ParallelForHelpFn> ParallelForHelps(const ResolveContext& ctx, const void* forkJoin, std::string_view what) {
			const auto m = ctx.Unique(ParallelForHelpChoice, ctx.FunctionStartingAt(forkJoin, what), what);
			return {
				Address(ctx.RequireInSection(m.ResolveAddress<const void*>(1), ".text", what)),
				Address(ctx.RequireInSection(m.ResolveAddress<const void*>(2), ".text", what)),
			};
		}

		struct JobListKickCallers {
			void* const* TaskManager{};
			const void* Kick{};
		};

		// The job list kick, as what most of its callers call: mov rdx, [group+0x18]; mov rcx, [g_TaskManager]; call kick.
		// Used by InlineBgPrep and ParallelAnimTail.
		JobListKickCallers FindJobListKick(const ResolveContext& ctx) {
			std::map<std::pair<const void*, const void*>, size_t> counts;
			for (const auto& m : ctx.All(JobListKickCaller, ctx.Text()))
				++counts[{m.ResolveAddress<const void*>(1), m.ResolveAddress<const void*>(2)}];
			ctx.Require(!counts.empty(), ResolveError::NotFound, "job list kick callers not found");

			const auto top = std::ranges::max_element(counts, {}, [](const auto& item) { return item.second; });
			size_t runnerUp = 0;
			for (const auto& [key, count] : counts) {
				if (key != top->first)
					runnerUp = (std::max)(runnerUp, count);
			}
			ctx.Require(top->second >= 3 && top->second > runnerUp * 2, ResolveError::Ambiguous,
				"job list kick callers disagree: {} call one function and {} another", top->second, runnerUp);

			const auto [taskManager, kick] = top->first;
			ctx.Require(!ctx.FunctionStartingAt(kick).empty(), ResolveError::Invalid, "no function starts at job list kick {}", Signatures::Describe(kick));
			return {static_cast<void* const*>(taskManager), kick};
		}

		// Used by TrimCullingClear and PerItemCullingClaims.
		void* const* FindCullingManager(const ResolveContext& ctx) {
			const auto getter = UniqueCallTarget(ctx, CullingManagerGetterCall, "culling manager getter call");
			const auto load = ctx.MatchAt(CullingManagerGetter, ctx.FunctionStartingAt(getter, "culling manager getter"), "culling manager getter");
			return static_cast<void* const*>(ctx.RequireInSection(load.ResolveAddress<const void*>(1), ".data", "culling manager"));
		}

		// Used by DedupeSkeletonSyncs, and by ParallelAnimTail for the partial skeleton layout.
		SkeletonPoseSyncWalkFn FindSkeletonPoseSyncWalk(const ResolveContext& ctx) {
			return Address(UniqueCallTarget(ctx, SkeletonPoseSyncWalkCall, "pose sync walk call"));
		}

		PartialSkeletonLayout FindPartialSkeletonLayout(const ResolveContext& ctx) {
			// The walk reads the count, the array, then each partial skeleton's pose, then steps to the next one.
			auto code = CodeFrom(ctx, reinterpret_cast<const void*>(FindSkeletonPoseSyncWalk(ctx)), 0x180);
			const auto count = ctx.First(PoseSyncPartialCount, code, "partial skeleton count");
			code = After(code, count);
			const auto array = ctx.First(PoseSyncPartialArray, code, "partial skeleton array");
			code = After(code, array);
			const auto pose = ctx.First(PoseSyncPartialPose, code, "partial skeleton pose");
			code = After(code, pose);
			const auto stride = ctx.First(PoseSyncPartialStride, code, "partial skeleton stride");

			return PartialSkeletonLayout{
				.Count = ByteAt(count, 1),
				.Array = ByteAt(array, 1),
				.Stride = FieldOffset(ctx, stride, 1, "partial skeleton stride"),
				.Pose = FieldOffset(ctx, pose, 1, "partial skeleton pose"),
			};
		}

		struct JobPoolWakeAll {
			const void* WakeAll{};
			JobPoolLayout Layout;
		};

		// The job pool's wake-all, as what the kick calls, and the layout as the wake-all reads it. Used by
		// ChainWorkerWakeups and InlineBgPrep.
		JobPoolWakeAll FindJobPoolWakeAll(const ResolveContext& ctx) {
			const auto call = ctx.Unique(JobPoolWakeAllCall, ctx.Text(), "job pool wake-all call");
			const auto wakeAll = ctx.RequireInSection(call.ResolveAddress<const void*>(2), ".text", "job pool wake-all");
			const auto body = ctx.Find(JobPoolWakeAllBody, ctx.FunctionStartingAt(wakeAll, "job pool wake-all"));
			ctx.Require(body.has_value(), ResolveError::Mismatch, "job pool wake-all {} does not match", Signatures::Describe(wakeAll));
			ctx.Require(ImportSlotCalledAt(ctx, *body, 6) == ImportSlot(ctx, "kernel32.dll", "SetEvent"), ResolveError::Mismatch,
				"job pool wake-all {} does not call SetEvent", Signatures::Describe(wakeAll));

			return {
				.WakeAll = wakeAll,
				.Layout = {
					.TaskManagerJobPool = ByteAt(call, 1),
					.Threads = ByteAt(*body, 2),
					.ThreadCount = ByteAt(*body, 1),
					.ThreadSkip = ByteAt(*body, 3),
					.ThreadWakeCount = ByteAt(*body, 4),
					.ThreadEvent = ByteAt(*body, 5),
				},
			};
		}

		// A disp8 operand, which is signed.
		ptrdiff_t SignedByteAt(const ScanResult& m, size_t group) {
			return m.Get<int8_t>(group);
		}

		// The function p is in, from the start of its first part to the end of its last. The compiler moves the code after
		// a shrink-wrapped register save, or rarely run code, into parts of their own, which only chain to the first one in
		// their unwind info; a pattern may well span parts.
		std::span<const uint8_t> WholeFunctionContaining(const ResolveContext& ctx, const void* p, std::string_view what) {
			const auto base = reinterpret_cast<const uint8_t*>(*ctx.Module());
			const auto image = xivres::pe_image::from_loaded(base);
			const auto rva = static_cast<uint32_t>(static_cast<const uint8_t*>(p) - base);
			const auto table = image.function_table();
			auto it = std::ranges::upper_bound(table, rva, {}, &xivres::pe_image::runtime_function::BeginAddress);
			ctx.Require(it != table.begin() && rva < (it - 1)->EndAddress, ResolveError::NotFound, "{}: no function contains {}", what, Signatures::Describe(p));

			const auto parts = FunctionParts(ctx, base + image.primary_of(*(it - 1)).BeginAddress);
			const auto begin = std::ranges::min(parts, {}, [](const auto& part) { return part.data(); }).data();
			const auto& last = std::ranges::max(parts, {}, [](const auto& part) { return part.data(); });
			return {begin, last.data() + last.size()};
		}

		// The base register and displacement of mov r64, [base+disp] (no index, not rip relative) at the start of code.
		std::optional<std::pair<uint8_t, int32_t>> LoadFrom(std::span<const uint8_t> code) {
			if (code.size() < 3 || (code[0] & 0xF8) != 0x48 || (code[0] & 0x02) || code[1] != 0x8B)
				return std::nullopt;

			const auto mod = code[2] >> 6;
			auto rm = code[2] & 7;
			size_t next = 3;
			if (mod == 3)
				return std::nullopt;
			if (rm == 4) {
				// SIB without an index
				if (code.size() < 4 || ((code[3] >> 3) & 7) != 4)
					return std::nullopt;
				rm = code[3] & 7;
				next = 4;
			}
			if (mod == 0 && rm == 5)
				return std::nullopt;

			const auto baseRegister = static_cast<uint8_t>(rm | (code[0] & 1 ? 8 : 0));
			if (mod == 0)
				return std::make_pair(baseRegister, 0);
			if (mod == 1)
				return code.size() > next ? std::optional(std::make_pair(baseRegister, static_cast<int32_t>(static_cast<int8_t>(code[next])))) : std::nullopt;
			return code.size() >= next + 4 ? std::optional(std::make_pair(baseRegister, *reinterpret_cast<const int32_t*>(&code[next]))) : std::nullopt;
		}
	}

	std::string to_string(const FixDriverFunctions& value) {
		return std::format("execute all tasks {}", Signatures::Describe(value.ExecuteAllTasks));
	}

	std::string to_string(const SkipIdleNotifiersFunctions& value) {
		return std::format("lock {}, head {}, link {}, unlink {}, pre-present loop {}, post-kick loop {}, {} callback tests",
			Signatures::Describe(value.Lock),
			Signatures::Describe(value.Head),
			Signatures::Describe(value.Link),
			Signatures::Describe(value.Unlink),
			Signatures::Describe(value.PrePresentLoop),
			Signatures::Describe(value.PostKickLoop),
			value.CallbackTests.size());
	}

	std::string to_string(const ChainWorkerWakeupsFunctions& value) {
		return std::format("queue indices {}, wake all {}, job pool +{} (threads +{}, count +{}), thread skip +{}, wake count +{}, event +{}",
			Signatures::Describe(value.QueueIndices),
			Signatures::Describe(value.WakeAll),
			Signatures::Describe(value.Layout.TaskManagerJobPool),
			Signatures::Describe(value.Layout.Threads),
			Signatures::Describe(value.Layout.ThreadCount),
			Signatures::Describe(value.Layout.ThreadSkip),
			Signatures::Describe(value.Layout.ThreadWakeCount),
			Signatures::Describe(value.Layout.ThreadEvent));
	}

	std::string to_string(const PartialSkeletonLayout& value) {
		return std::format("count +{}, array +{}, stride {}, pose +{}",
			Signatures::Describe(value.Count),
			Signatures::Describe(value.Array),
			Signatures::Describe(value.Stride),
			Signatures::Describe(value.Pose));
	}

	std::string to_string(const ParallelForGroupLayout& value) {
		return std::format("writers +{} (count +{}, size {}, items +{}, block +{}), job list +{} (wait slot {}), context +{}, job +{}, chunks +{} ({} x {} blocks x {} items), blocks claimed +{}, claim counters +{} +{}, per-item claims +{}",
			Signatures::Describe(value.Writers),
			Signatures::Describe(value.WriterCount),
			Signatures::Describe(value.WriterSize),
			Signatures::Describe(value.WriterItems),
			Signatures::Describe(value.WriterBlock),
			Signatures::Describe(value.JobList),
			Signatures::Describe(value.JobListWaitSlot),
			Signatures::Describe(value.JobContext),
			Signatures::Describe(value.Job),
			Signatures::Describe(value.Chunks),
			Signatures::Describe(value.ChunkCount),
			Signatures::Describe(value.ChunkBlocks),
			Signatures::Describe(value.BlockItems),
			Signatures::Describe(value.BlocksClaimed),
			Signatures::Describe(value.ClaimCounters[0]),
			Signatures::Describe(value.ClaimCounters[1]),
			Signatures::Describe(value.PerItemClaims));
	}

	std::string to_string(const DedupeSkeletonSyncsFunctions& value) {
		return std::format("pose sync walk {}", Signatures::Describe(value.SyncWalk));
	}

	std::string to_string(const TrimCullingClearFunctions& value) {
		return std::format("clear count {}, table +{}, object mask +{} ({} words), culling manager {}",
			Signatures::Describe(value.ClearCount),
			Signatures::Describe(value.TableOffset),
			Signatures::Describe(value.ObjectMask),
			Signatures::Describe(value.ObjectMaskWords),
			Signatures::Describe(value.CullingManager));
	}

	std::string to_string(const ShortenAllocatorLockFunctions& value) {
		const auto& l = value.Layout;
		return std::format("free {}, lock +{}, chunk table +{} (count +{}, stride {}, base +{}, span {}), page mask {} (index +{}), backing +{} (free slot {})",
			Signatures::Describe(value.Free),
			Signatures::Describe(l.Lock),
			Signatures::Describe(l.ChunkTable),
			Signatures::Describe(l.ChunkCount),
			Signatures::Describe(l.ChunkStride),
			Signatures::Describe(l.ChunkBase),
			Signatures::Describe(l.ChunkSpan),
			Signatures::Describe(l.PageMask),
			Signatures::Describe(l.PageIndex),
			Signatures::Describe(l.Backing),
			Signatures::Describe(l.BackingFreeSlot));
	}

	std::string to_string(const PoolStagingBlocksFunctions& value) {
		return std::format("allocator manager {}, allocator +{} (vtable {}, slots: terminate {}, alloc {}, free {}, size {}; alloc counter +{})",
			Signatures::Describe(value.AllocatorManager),
			Signatures::Describe(value.AllocatorOffset),
			Signatures::Describe(value.Vtable),
			Signatures::Describe(value.TerminateSlot),
			Signatures::Describe(value.AllocSlot),
			Signatures::Describe(value.FreeSlot),
			Signatures::Describe(value.SizeSlot),
			Signatures::Describe(value.AllocCounter));
	}

	std::string to_string(const FreezeHiddenMinionsFunctions& value) {
		return std::format("follow {}, render flags +{}",
			Signatures::Describe(value.Follow),
			Signatures::Describe(value.RenderFlagsOffset));
	}

	std::string to_string(const SkipPrepareWaitFunctions& value) {
		return std::format("array list {}, single-item list {}",
			Signatures::Describe(value.ArrayList),
			Signatures::Describe(value.SingleItemList));
	}

	std::string to_string(const InlineBgPrepFunctions& value) {
		return std::format("kick {}, render manager {}, prep list +{}, framework task manager +{}, list slots: count {}, prepare {}, describe {}, job pool +{} (threads +{}, count +{}), thread pool +{}, pool context +{}, task slots {} and {}",
			Signatures::Describe(value.Kick),
			Signatures::Describe(value.RenderManager),
			Signatures::Describe(value.PrepListOffset),
			Signatures::Describe(value.FrameworkTaskManagerOffset),
			Signatures::Describe(value.ListCountSlot),
			Signatures::Describe(value.ListPrepareSlot),
			Signatures::Describe(value.ListDescribeSlot),
			Signatures::Describe(value.Pool.TaskManagerJobPool),
			Signatures::Describe(value.Pool.Threads),
			Signatures::Describe(value.Pool.ThreadCount),
			Signatures::Describe(value.Run.ThreadPool),
			Signatures::Describe(value.Run.PoolContext),
			Signatures::Describe(value.Run.TaskRunSlot),
			Signatures::Describe(value.Run.TaskRunWithArgumentSlot));
	}

	std::string to_string(const SkipHiddenHotbarsFunctions& value) {
		return std::format("bar {}, cross bar {}, length {}",
			Signatures::Describe(value.Bar),
			Signatures::Describe(value.CrossBar),
			Signatures::Describe(value.Length));
	}

	std::string to_string(const ParallelAnimTailFunctions& value) {
		return std::format("update {}, tail {}, entry count {}, entries {}, submit base {} (group +{}: {}), task manager {}, kick {}, help per item {}, help blocks {}, append {} (TLS +{}), partial skeletons: {} (pending removals +{}), ground +{} (active {})",
			Signatures::Describe(value.Update),
			Signatures::Describe(value.Tail),
			Signatures::Describe(value.EntryCount),
			Signatures::Describe(value.Entries),
			Signatures::Describe(value.SubmitBase),
			Signatures::Describe(value.GroupOffset),
			to_string(value.Group),
			Signatures::Describe(value.TaskManager),
			Signatures::Describe(value.Kick),
			Signatures::Describe(value.HelpPerItem),
			Signatures::Describe(value.HelpBlocks),
			Signatures::Describe(value.Append),
			Signatures::Describe(value.AppendTlsSlot),
			to_string(value.Partials),
			Signatures::Describe(value.PartialPendingRemovals),
			Signatures::Describe(value.SkeletonGround),
			Signatures::Describe(value.GroundRayActive));
	}

	std::string to_string(const SplitCharacterCullingFunctions& value) {
		return std::format("camera cull job {}, character item type {}, item size {}",
			Signatures::Describe(value.CullJob),
			Signatures::Describe(static_cast<size_t>(value.CharacterItemType)),
			Signatures::Describe(value.ItemSize));
	}

	std::string to_string(const PerItemCullingClaimsFunctions& value) {
		return std::format("culling manager {}, cell group +{} (per item {}, blocks {}), setup group +{} (per item {}, blocks {})",
			Signatures::Describe(value.CullingManager),
			Signatures::Describe(value.CellGroupOffset),
			Signatures::Describe(value.CellHelpPerItem),
			Signatures::Describe(value.CellHelpBlocks),
			Signatures::Describe(value.SetupGroupOffset),
			Signatures::Describe(value.SetupHelpPerItem),
			Signatures::Describe(value.SetupHelpBlocks));
	}

	std::string to_string(const GatherUsedCommandsFunctions& value) {
		const auto& l = value.Layout;
		return std::format("gather {}, sort {}, contexts +{} (count +{}, size {}), lists +{} (size {}, first block +{}, free slots +{}, blocks +{}), block size {} (next +{}), entry size {}",
			Signatures::Describe(value.Gather),
			Signatures::Describe(value.Sort),
			Signatures::Describe(l.ContextArray),
			Signatures::Describe(l.ContextCount),
			Signatures::Describe(l.ContextSize),
			Signatures::Describe(l.Lists),
			Signatures::Describe(l.ListSize),
			Signatures::Describe(l.ListFirstBlock),
			Signatures::Describe(l.ListFreeSlots),
			Signatures::Describe(l.ListBlocks),
			Signatures::Describe(l.BlockSize),
			Signatures::Describe(l.NextBlock),
			Signatures::Describe(l.EntrySize));
	}

	const Signatures::ComplexSignature<FixDriverFunctions> FixDriver("CrowdFix::FixDriver", [](ResolveContext& ctx) {
		return FixDriverFunctions{
			.ExecuteAllTasks = Address(UniqueCallTarget(ctx, TaskManagerExecuteAllTasksCall, "ExecuteAllTasks call")),
		};
	});

	const Signatures::ComplexSignature<SkipIdleNotifiersFunctions> SkipIdleNotifiers("CrowdFix::SkipIdleNotifiers", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto walk = ctx.Unique(NotifierListWalk, text, "notifier list walk");
		SkipIdleNotifiersFunctions r{
			.Lock = walk.ResolveAddress<CRITICAL_SECTION*>(1),
			.Head = walk.ResolveAddress<void* const*>(2),
		};
		ctx.RequireInSection(r.Lock, ".data", "notifier lock");
		ctx.RequireInSection(r.Head, ".data", "notifier list head");

		const auto loop = [&](const RegexSignature& signature, std::string_view what, uint8_t*& at, size_t& length) {
			const auto m = ctx.Unique(signature, text, what);
			ctx.Require(m.ResolveAddress<void* const*>(1) == r.Head, ResolveError::Mismatch, "{} walks another list", what);
			at = m.begin<uint8_t>(0);
			length = static_cast<size_t>(static_cast<uint8_t*>(m.end(0)) - at);
		};
		loop(NotifierPrePresentLoop, "pre-present loop", r.PrePresentLoop, r.PrePresentLoopLength);
		loop(NotifierPostKickLoop, "post-kick loop", r.PostKickLoop, r.PostKickLoopLength);

		// Of the functions that take the same lock, only Link and Unlink read these right after.
		const auto lockUser = [&](const RegexSignature& signature, bool readsHead, std::string_view what) {
			const void* found = nullptr;
			for (const auto& m : ctx.All(signature, text)) {
				if (m.ResolveAddress<const void*>(1) != r.Lock || (readsHead && m.ResolveAddress<const void*>(2) != r.Head))
					continue;
				ctx.Require(!found, ResolveError::Ambiguous, "{} found more than once", what);
				found = ctx.FunctionContaining(m.begin(0), what).data();
			}
			ctx.Require(found != nullptr, ResolveError::NotFound, "{} not found", what);
			return found;
		};
		r.Link = Address(lockUser(NotifierLinkBody, true, "notifier link"));
		r.Unlink = Address(lockUser(NotifierUnlinkBody, false, "notifier unlink"));

		// Base and derived classes share their callbacks, so each signature may find a callback used by several vtables.
		// A callback without a test is treated as one that may always do work, so a test that is not found only makes the
		// filter keep more notifiers.
		const std::pair<const RegexSignature*, NotifierWorkTest> tests[]{
			{&BufferNotifierPostKick, NotifierWorkTest::BufferFlags},
			{&BufferNotifierPrePresent, NotifierWorkTest::BufferFlags},
			{&IndexBufferNotifierPostKick, NotifierWorkTest::IndexBufferFlags},
			{&IndexBufferNotifierPrePresent, NotifierWorkTest::IndexBufferFlags},
			{&TextureNotifierPostKick, NotifierWorkTest::TextureMappedFlags},
			{&TextureNotifierPrePresent, NotifierWorkTest::TextureMappedOrUploadFlags},
			{&ConstantBufferNotifierPrePresent, NotifierWorkTest::ConstantBufferFlags},
		};
		for (const auto& [signature, test] : tests) {
			for (const auto& m : ctx.All(*signature, text)) {
				if (ctx.FunctionStartingAt(m.begin(0)).empty())
					continue;
				if (test == NotifierWorkTest::TextureMappedOrUploadFlags) {
					// where the jne goes: jne rel32, or jne rel8 before 7.20
					const auto target = m.Match()[1].matched
						? m.ResolveAddress<const uint8_t*>(1)
						: m.begin<uint8_t>(2) + 1 + m.Get<int8_t>(2);
					if (!ctx.InSection(target, ".text") || !ctx.TryMatchAt(TextureNotifierUploadTest, CodeFrom(ctx, target, 4)))
						continue;
				}
				r.CallbackTests.push_back({m.begin(0), test});
			}
		}
		return r;
	});

	const Signatures::ComplexSignature<ChainWorkerWakeupsFunctions> ChainWorkerWakeups("CrowdFix::ChainWorkerWakeups", [](ResolveContext& ctx) {
		const auto text = ctx.Text();

		// The write index is the global the enqueues (9 of them in 7.x) increment; the read index follows it.
		std::map<const uint32_t*, size_t> increments;
		for (const auto& m : ctx.All(JobRingIndexIncrement, text)) {
			if (const auto index = m.ResolveAddress<const uint32_t*>(1); index == m.ResolveAddress<const uint32_t*>(2))
				++increments[index];
		}
		ctx.Require(increments.size() == 2, ResolveError::Mismatch, "job ring index increments write {} globals instead of 2", increments.size());
		const auto [writeIndex, writes] = *increments.begin();
		const auto [readIndex, reads] = *std::next(increments.begin());
		ctx.Require(readIndex == writeIndex + 1 && writes > reads, ResolveError::Mismatch,
			"job ring indices {} ({} increments) and {} ({} increments) are not a write and a read index",
			Signatures::Describe(writeIndex), writes, Signatures::Describe(readIndex), reads);

		const auto [wakeAll, layout] = FindJobPoolWakeAll(ctx);
		return ChainWorkerWakeupsFunctions{
			.QueueIndices = static_cast<const uint32_t*>(ctx.RequireInSection(writeIndex, ".data", "job queue indices")),
			.WakeAll = Address(wakeAll),
			.Layout = layout,
		};
	});

	const Signatures::ComplexSignature<DedupeSkeletonSyncsFunctions> DedupeSkeletonSyncs("CrowdFix::DedupeSkeletonSyncs", [](ResolveContext& ctx) {
		return DedupeSkeletonSyncsFunctions{.SyncWalk = FindSkeletonPoseSyncWalk(ctx)};
	});

	const Signatures::ComplexSignature<TrimCullingClearFunctions> TrimCullingClear("CrowdFix::TrimCullingClear", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto loop = ctx.Unique(CullingVisibilityClearCount, text, "visibility clear loop");
		const auto site = loop.begin<const uint8_t>(0);
		ctx.Require(site - text.data() >= 0x30, ResolveError::Invalid, "visibility clear loop at the start of .text");

		// the table pointer is loaded into rax, which the loop stores through, right before the count
		const auto load = ctx.Find(CullingVisibilityTableLoad, std::span(site - 0x30, 0x35));
		ctx.Require(load && load->end(0) == site + 5, ResolveError::NotFound, "visibility table load not found");
		TrimCullingClearFunctions r{
			.ClearCount = loop.begin<uint32_t>(1),
			.TableOffset = ByteAt(*load, 1),
			.CullingManager = FindCullingManager(ctx),
		};

		// The object slots, as the slot allocator and release that the game calls on the culling manager use them. The
		// camera culling indexes the visibility table with the same slots.
		std::set<const void*> callees;
		for (const auto& m : ctx.All(CullingManagerCall, text)) {
			if (m.ResolveAddress<void* const*>(1) == r.CullingManager)
				callees.insert(m.ResolveAddress<const void*>(2));
		}

		// mask, objects, entry shift; and the words the allocator scans
		std::optional<std::tuple<size_t, size_t, size_t>> allocator, release;
		uint32_t words = 0;
		for (const auto callee : callees) {
			const auto fn = ctx.InSection(callee, ".text") ? ctx.FunctionStartingAt(callee) : std::span<const uint8_t>();
			if (fn.empty())
				continue;

			if (const auto m = ctx.Find(CullingSlotAlloc, fn)) {
				const auto current = std::make_tuple(ByteAt(*m, 1), ByteAt(*m, 4), ByteAt(*m, 3));
				ctx.Require(!allocator || *allocator == current, ResolveError::Ambiguous, "culling slot allocators disagree");
				allocator = current;
				words = m->Get<uint32_t>(2);
			}
			if (const auto m = ctx.Find(CullingSlotRelease, fn)) {
				const auto current = std::make_tuple(ByteAt(*m, 2), ByteAt(*m, 1), ByteAt(*m, 3));
				ctx.Require(!release || *release == current, ResolveError::Ambiguous, "culling slot releases disagree");
				release = current;
			}
		}
		ctx.Require(allocator.has_value(), ResolveError::NotFound, "culling slot allocator not found");
		ctx.Require(release.has_value(), ResolveError::NotFound, "culling slot release not found");

		const auto [mask, objects, shift] = *allocator;
		ctx.Require(*release == *allocator, ResolveError::Mismatch,
			"the culling slot allocator (mask +0x{:X}, objects +0x{:X}) and release (mask +0x{:X}, objects +0x{:X}) disagree",
			mask, objects, std::get<0>(*release), std::get<1>(*release));
		ctx.Require(std::cmp_equal(static_cast<uint64_t>(words) * 32, *r.ClearCount), ResolveError::Mismatch,
			"the culling slot allocator scans {} mask words for 0x{:X} visibility slots", words, *r.ClearCount);
		ctx.Require(mask != r.TableOffset && mask != objects, ResolveError::Invalid, "the object mask +0x{:X} overlaps the visibility table or the objects", mask);
		r.ObjectMask = mask;
		r.ObjectMaskWords = words;
		return r;
	});

	const Signatures::ComplexSignature<ShortenAllocatorLockFunctions> ShortenAllocatorLock("CrowdFix::ShortenAllocatorLock", [](ResolveContext& ctx) {
		const auto function = UniqueFunctionStart(ctx, GraphicsAllocatorFreeStart, "graphics allocator free");
		const auto code = CodeFrom(ctx, function, 0x200);
		const auto m = ctx.Find(GraphicsAllocatorFreeLayout, code);
		ctx.Require(m.has_value(), ResolveError::Mismatch, "graphics allocator free {} does not match", Signatures::Describe(function));

		GraphicsAllocatorLayout r{
			.Lock = FieldOffset(ctx, *m, 1, "lock"),
			.ChunkTable = FieldOffset(ctx, *m, 7, "chunk table"),
			.ChunkCount = FieldOffset(ctx, *m, 5, "chunk count"),
			.ChunkBase = ByteAt(*m, 10),
			.ChunkSpan = static_cast<size_t>(ctx.InRange<int32_t>(m->Get<int32_t>(11), 1, 0x1000000, "chunk span")),
			.PageMask = ~static_cast<uint64_t>(static_cast<int64_t>(m->Get<int32_t>(3))),
			.PageIndex = ByteAt(*m, 4),
		};
		ctx.Require(std::has_single_bit(r.PageMask + 1) && r.PageMask >= 0xFF && r.PageMask < 0x100000, ResolveError::Invalid,
			"page mask 0x{:X} is not a power of two minus one", r.PageMask);

		// The chunk entry address: lea rcx, [rax+rax*2] (x3); add rcx, rcx (x2); then [table+rcx*scale+base].
		const auto lea = m->begin<const uint8_t>(6);
		const auto add = m->begin<const uint8_t>(8);
		const auto sib = m->Get<uint8_t>(9);
		const auto leaTriples = (lea[2] & 0xC7) == 0x04 && (lea[3] >> 6) == 1 && ((lea[3] >> 3) & 7) == (lea[3] & 7) && !!(lea[0] & 2) == !!(lea[0] & 1);
		const auto addDoubles = ((add[2] >> 3) & 7) == (add[2] & 7) && !!(add[0] & 4) == !!(add[0] & 1);
		ctx.Require(leaTriples && addDoubles, ResolveError::Mismatch, "chunk entry address is not computed as expected");
		r.ChunkStride = size_t{6} << (sib >> 6);

		// It frees blocks of other sizes through the backing allocator, and unlocks the same lock it took.
		const auto rest = After(code, *m);
		std::optional<std::pair<size_t, size_t>> backing;
		for (const auto& f : ctx.All(GraphicsAllocatorBackingFree, rest)) {
			const auto current = std::make_pair(FieldOffset(ctx, f, 1, "backing allocator"), ByteAt(f, 2));
			ctx.Require(!backing || *backing == current, ResolveError::Mismatch, "backing frees disagree");
			backing = current;
		}
		ctx.Require(backing.has_value(), ResolveError::NotFound, "backing free not found");
		ctx.Require(backing->second % 8 == 0, ResolveError::Invalid, "backing free slot offset 0x{:X}", backing->second);
		r.Backing = backing->first;
		r.BackingFreeSlot = backing->second / 8;

		const auto enter = ImportSlotCalledAt(ctx, *m, 2);
		const auto leave = ImportSlot(ctx, "kernel32.dll", "LeaveCriticalSection");
		ctx.Require(enter == ImportSlot(ctx, "kernel32.dll", "EnterCriticalSection"), ResolveError::Mismatch, "graphics allocator free does not lock with EnterCriticalSection");
		ctx.Require(std::ranges::any_of(ctx.All(GraphicsAllocatorUnlock, rest), [&](const ScanResult& u) {
			return std::cmp_equal(u.Get<int32_t>(1), r.Lock) && ImportSlotCalledAt(ctx, u, 2) == leave;
		}), ResolveError::Mismatch, "graphics allocator free does not unlock +0x{:X}", r.Lock);

		return ShortenAllocatorLockFunctions{.Free = Address(function), .Layout = r};
	});

	const Signatures::ComplexSignature<PoolStagingBlocksFunctions> PoolStagingBlocks("CrowdFix::PoolStagingBlocks", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto load = ctx.Unique(GraphicsAllocatorManagerLoad, text, "allocator manager load");
		PoolStagingBlocksFunctions r{
			.AllocatorManager = static_cast<void* const*>(ctx.RequireInSection(load.ResolveAddress<const void*>(1), ".data", "allocator manager")),
			.AllocatorOffset = ByteAt(load, 2),
		};

		const auto slotsOf = [&ctx](const void* const* vtable) {
			std::vector<const void*> slots;
			for (size_t i = 0; i < 32 && ctx.InSection(vtable[i], ".text"); i++)
				slots.push_back(vtable[i]);
			return slots;
		};
		const auto matchingSlots = [&ctx](const std::vector<const void*>& slots, const RegexSignature& signature, auto&& predicate) {
			std::vector<std::pair<size_t, ScanResult>> found;
			for (size_t i = 0; i < slots.size(); i++) {
				if (const auto m = ctx.TryMatchAt(signature, CodeFrom(ctx, slots[i], 0x40)); m && predicate(*m))
					found.emplace_back(i, *m);
			}
			return found;
		};

		// The allocator's class is the one whose vtable has the counting alloc that forwards to the small-object allocator
		// set up next to it. Its other slots are told apart by which of that allocator's slots they forward to, and those
		// by their code. A pooled block's size class must come from the real size query, and pooled blocks must still be
		// freed by the real free when the pool is turned off, so a slot guessed wrong would corrupt the heap.
		std::optional<std::tuple<const void* const*, const void* const*, int32_t>> classes;
		for (const auto& m : ctx.All(GraphicsAllocatorVtables, text)) {
			const auto vtable = m.ResolveAddress<const void* const*>(1);
			const auto inner = m.ResolveAddress<const void* const*>(2);
			const auto innerOffset = m.Get<int32_t>(3);
			if (!ctx.InSection(vtable, ".rdata") || !ctx.InSection(inner, ".rdata") || (classes && std::get<0>(*classes) == vtable))
				continue;
			if (matchingSlots(slotsOf(vtable), GraphicsAllocatorCountingWrapper, [innerOffset](const ScanResult& w) { return w.Get<int32_t>(2) == innerOffset; }).empty())
				continue;

			ctx.Require(!classes, ResolveError::Ambiguous, "graphics allocator vtables at {} and {}", Signatures::Describe(std::get<0>(*classes)), Signatures::Describe(vtable));
			classes.emplace(vtable, inner, innerOffset);
		}
		ctx.Require(classes.has_value(), ResolveError::NotFound, "graphics allocator vtable not found");
		const auto [vtable, innerVtable, innerOffset] = *classes;
		const auto slots = slotsOf(vtable);
		const auto innerSlots = slotsOf(innerVtable);

		const auto unique = [&ctx](const auto& found, std::string_view what) {
			ctx.Require(found.size() == 1, ResolveError::Mismatch, "{} graphics allocator {} slots instead of 1", found.size(), what);
			return found.front();
		};
		const auto [innerFree, innerFreeMatch] = unique(matchingSlots(innerSlots, GraphicsAllocatorFreeStart, [](const ScanResult&) { return true; }), "inner free");
		const auto [innerSize, innerSizeMatch] = unique(matchingSlots(innerSlots, SmallObjectAllocatorSize, [](const ScanResult&) { return true; }), "inner size");
		const auto forwardingTo = [&](size_t innerSlot) {
			return [innerOffset, innerSlot](const ScanResult& w) {
				return w.Get<int32_t>(1) == innerOffset && w.Get<uint8_t>(2) == innerSlot * 8;
			};
		};

		const auto [allocSlot, alloc] = unique(matchingSlots(slots, GraphicsAllocatorCountingWrapper, [innerOffset](const ScanResult& w) { return w.Get<int32_t>(2) == innerOffset; }), "alloc");
		r.Vtable = vtable;
		r.AllocSlot = allocSlot;
		r.FreeSlot = unique(matchingSlots(slots, GraphicsAllocatorForwardingWrapper, forwardingTo(innerFree)), "free").first;
		r.SizeSlot = unique(matchingSlots(slots, GraphicsAllocatorForwardingWrapper, forwardingTo(innerSize)), "size").first;
		r.TerminateSlot = unique(matchingSlots(slots, GraphicsAllocatorTerminateWrapper, [innerOffset](const ScanResult& w) {
			return w.Get<int32_t>(1) == innerOffset && w.Get<uint8_t>(3) == w.Get<uint8_t>(4);
		}), "terminate").first;
		r.AllocCounter = FieldOffset(ctx, alloc, 1, "alloc counter");

		// what the game itself calls to allocate through the manager
		ctx.Require(ByteAt(load, 3) == r.AllocSlot * 8, ResolveError::Mismatch,
			"the game allocates through slot +0x{:X}, but the counting alloc is slot {}", ByteAt(load, 3), r.AllocSlot);
		return r;
	});

	const Signatures::ComplexSignature<FreezeHiddenMinionsFunctions> FreezeHiddenMinions("CrowdFix::FreezeHiddenMinions", [](ResolveContext& ctx) {
		const auto jump = ctx.Unique(CompanionFollowJump, ctx.Text(), "companion follow jump");

		// Companion::Update tests the render flags itself after the draw-ready check. Both the tests and the jump may be
		// in any part of it.
		std::optional<size_t> renderFlags;
		for (const auto part : FunctionParts(ctx, ctx.FunctionContaining(jump.begin(0), "Companion::Update").data())) {
			for (const auto& m : ctx.All(CompanionRenderFlagsTest, part)) {
				const auto offset = FieldOffset(ctx, m, 1, "render flags");
				ctx.Require(!renderFlags || *renderFlags == offset, ResolveError::Mismatch, "render flag tests disagree: +0x{:X} and +0x{:X}", renderFlags.value_or(0), offset);
				renderFlags = offset;
			}
		}
		ctx.Require(renderFlags.has_value(), ResolveError::NotFound, "render flags test not found");

		return FreezeHiddenMinionsFunctions{
			.Follow = Address(ctx.RequireInSection(jump.ResolveAddress<const void*>(1), ".text", "companion follow")),
			.RenderFlagsOffset = *renderFlags,
		};
	});

	const Signatures::ComplexSignature<SkipPrepareWaitFunctions> SkipPrepareWait("CrowdFix::SkipPrepareWait", [](ResolveContext& ctx) {
		const auto wait = ImportSlot(ctx, "kernel32.dll", "WaitForSingleObject");
		const auto site = [&](const RegexSignature& signature, std::string_view what) {
			const auto m = ctx.Unique(signature, ctx.Text(), what);
			ctx.Require(ImportSlotCalledAt(ctx, m, 2) == wait, ResolveError::Mismatch, "{} does not call WaitForSingleObject", what);
			return m.begin<uint8_t>(1);
		};
		return SkipPrepareWaitFunctions{
			.ArrayList = site(JobListArrayPrepare, "array job list prepare"),
			.SingleItemList = site(JobListSingleItemPrepare, "single-item job list prepare"),
		};
	});

	const Signatures::ComplexSignature<InlineBgPrepFunctions> InlineBgPrep("CrowdFix::InlineBgPrep", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto kick = FindJobListKick(ctx).Kick;

		// RenderView may kick it from more than one place, but always the same list on the same task manager.
		std::optional<std::pair<int32_t, int32_t>> offsets;
		for (const auto& m : ctx.All(BgInstancingPrepKickCall, text)) {
			if (m.ResolveAddress<const void*>(3) != kick)
				continue;
			const auto current = std::make_pair(m.Get<int32_t>(1), m.Get<int32_t>(2));
			ctx.Require(!offsets || *offsets == current, ResolveError::Ambiguous, "BG instancing prep kicks disagree");
			offsets = current;
		}
		ctx.Require(offsets.has_value(), ResolveError::NotFound, "BG instancing prep kick not found");

		const auto load = ctx.Unique(RenderManagerLoad, text, "render manager load");
		InlineBgPrepFunctions r{
			.Kick = Address(kick),
			.RenderManager = static_cast<void* const*>(ctx.RequireInSection(load.ResolveAddress<const void*>(1), ".data", "render manager")),
			.PrepListOffset = static_cast<size_t>(ctx.InRange<int32_t>(offsets->first, 1, 0x100000, "prep list offset")),
			.FrameworkTaskManagerOffset = static_cast<size_t>(ctx.InRange<int32_t>(offsets->second, 1, 0x100000, "framework task manager offset")),
		};

		// The inline run does what the kick and a worker would: the kick's job list calls, then the worker's claim and
		// task calls, with the context the worker passes.
		const auto calls = ctx.Find(JobListKickCalls, CodeFrom(ctx, kick, 0x80));
		ctx.Require(calls.has_value(), ResolveError::Mismatch, "job list kick {} does not call the job list as expected", Signatures::Describe(kick));
		ctx.Require(ByteAt(*calls, 1) == ByteAt(*calls, 4), ResolveError::Mismatch, "the job list kick counts the items with two different slots");
		for (const auto group : {1, 2, 3})
			ctx.Require(ByteAt(*calls, group) % 8 == 0, ResolveError::Invalid, "job list slot offset 0x{:X}", ByteAt(*calls, group));
		r.ListCountSlot = ByteAt(*calls, 1) / 8;
		r.ListPrepareSlot = ByteAt(*calls, 2) / 8;
		r.ListDescribeSlot = ByteAt(*calls, 3) / 8;

		const auto task = ctx.Unique(JobRunTask, text, "job run task call");
		const auto claim = ctx.Find(JobRunClaim, WholeFunctionContaining(ctx, task.begin(0), "job run"));
		ctx.Require(claim.has_value(), ResolveError::NotFound, "job run claim call not found");
		// The descriptor the describe fills is copied as it is into the queue entry, which the worker reads as these.
		const auto throughRax = claim->Match()[1].matched;
		const auto claimFunction = ByteAt(*claim, throughRax ? 1 : 6);
		const auto claimState = ByteAt(*claim, throughRax ? 2 : 5);
		const auto claimOwner = ByteAt(*claim, throughRax ? 3 : 4);
		ctx.Require(claimOwner == claimFunction + 8 && claimState == claimFunction + 0x10, ResolveError::Mismatch,
			"the job run claims with the function at +0x{:X}, its object at +0x{:X} and its state at +0x{:X}", claimFunction, claimOwner, claimState);
		ctx.Require(ByteAt(task, 3) % 8 == 0 && ByteAt(task, 4) % 8 == 0, ResolveError::Invalid, "task slot offsets 0x{:X} and 0x{:X}", ByteAt(task, 3), ByteAt(task, 4));
		r.Run = {
			.ThreadPool = ByteAt(task, 1),
			.PoolContext = ByteAt(task, 2),
			.TaskRunSlot = ByteAt(task, 3) / 8,
			.TaskRunWithArgumentSlot = ByteAt(task, 4) / 8,
		};
		r.Pool = FindJobPoolWakeAll(ctx).Layout;
		return r;
	});

	const Signatures::ComplexSignature<SkipHiddenHotbarsFunctions> SkipHiddenHotbars("CrowdFix::SkipHiddenHotbars", [](ResolveContext& ctx) {
		// The bar and the cross bar site: the same constructor and Prepare, the same temporary, the same length.
		const auto sites = ctx.All(HotbarPrepare, ctx.Text());
		ctx.Require(sites.size() == 2, ResolveError::Mismatch, "{} hidden hotbar prepare sites instead of 2", sites.size());
		const auto length = [](const ScanResult& m) {
			return static_cast<size_t>(static_cast<uint8_t*>(m.end(1)) - m.begin<uint8_t>(1));
		};
		const auto& bar = sites[0];
		const auto& crossBar = sites[1];
		ctx.Require(bar.ResolveAddress<const void*>(3) == crossBar.ResolveAddress<const void*>(3)
			&& bar.ResolveAddress<const void*>(4) == crossBar.ResolveAddress<const void*>(4), ResolveError::Mismatch,
			"the hidden hotbar prepare sites call different functions");
		ctx.Require(bar.Get<uint8_t>(2) == crossBar.Get<uint8_t>(2), ResolveError::Mismatch, "the hidden hotbar prepare sites use different temporaries");
		ctx.Require(length(bar) == length(crossBar), ResolveError::Mismatch, "the hidden hotbar prepare sites differ in length");
		return SkipHiddenHotbarsFunctions{
			.Bar = bar.begin<uint8_t>(1),
			.CrossBar = crossBar.begin<uint8_t>(1),
			.Length = length(bar),
		};
	});

	const Signatures::ComplexSignature<ParallelAnimTailFunctions> ParallelAnimTail("CrowdFix::ParallelAnimTail", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto update = UniqueCallTarget(ctx, AnimationUpdateCall, "animation update call");
		const auto updateFn = ctx.FunctionStartingAt(update, "animation update");

		// The tail's prologue is not unique in every build; the tail is the one of them the update calls.
		const void* tail = nullptr;
		for (const auto& m : ctx.All(AnimationTailStart, text)) {
			if (ctx.FunctionStartingAt(m.begin(0)).empty() || !Calls(updateFn, m.begin(0)))
				continue;
			ctx.Require(!tail, ResolveError::Ambiguous, "the animation update calls more than one tail candidate");
			tail = m.begin(0);
		}
		ctx.Require(tail != nullptr, ResolveError::NotFound, "animation tail not found");
		const auto entries = ctx.Unique(AnimationTailEntries, updateFn, "tail entries");

		const auto submit = ctx.Unique(AnimationSubmit, text, "animation submit");
		const auto submitFn = ctx.FunctionStartingAt(submit.begin(0), "animation submit");
		const auto jobListKick = FindJobListKick(ctx);
		const auto kick = ctx.Unique(AnimationSubmitKick, submitFn, "animation submit kick");
		ctx.Require(kick.ResolveAddress<const void*>(3) == jobListKick.Kick, ResolveError::Mismatch, "the animation submit kicks with another function");
		ctx.Require(kick.ResolveAddress<void* const*>(2) == jobListKick.TaskManager, ResolveError::Mismatch, "the animation submit kicks on another task manager");

		// The append is the function with this prologue that the submit calls; its TLS slot moves from build to build.
		const ScanResult* append = nullptr;
		const auto appends = ctx.All(AnimationTailAppend, text);
		for (const auto& m : appends) {
			if (!Calls(submitFn, m.begin(0)))
				continue;
			ctx.Require(!append, ResolveError::Ambiguous, "the animation submit calls more than one append candidate");
			append = &m;
		}
		ctx.Require(append != nullptr && !ctx.FunctionStartingAt(append->begin(0)).empty(), ResolveError::NotFound, "animation append not found");

		ParallelAnimTailFunctions r{
			.Update = Address(update),
			.Tail = Address(tail),
			.EntryCount = entries.ResolveAddress<int32_t*>(1),
			.Entries = entries.ResolveAddress<void*>(2),
			.SubmitBase = submit.ResolveAddress<void* const*>(1),
			.TaskManager = kick.ResolveAddress<void* const*>(2),
			.Kick = Address(kick.ResolveAddress<const void*>(3)),
			.HelpPerItem = Address(ctx.RequireInSection(kick.ResolveAddress<const void*>(5), ".text", "per-item help")),
			.HelpBlocks = Address(ctx.RequireInSection(kick.ResolveAddress<const void*>(6), ".text", "block help")),
			.Append = Address(append->begin(0)),
			.AppendTlsSlot = static_cast<size_t>(ctx.InRange<int32_t>(append->Get<int32_t>(1), 1, 0x10000, "append TLS slot")),
			.Partials = FindPartialSkeletonLayout(ctx),
		};
		ctx.RequireInSection(r.EntryCount, ".data", "tail entry count");
		ctx.RequireInSection(r.Entries, ".data", "tail entries");
		ctx.RequireInSection(r.SubmitBase, ".data", "submit base");
		ctx.RequireInSection(r.TaskManager, ".data", "task manager");

		// The fix repeats the submit's whole arm, kick, help, wait and reset sequence on the same group, so all of the
		// group is read from the submit, and every field it touches more than once has to agree. The writer and block
		// fields also have to agree with the append, which crashes once the claimed blocks pass the last chunk.
		const auto group = ctx.First(AnimationSubmitGroup, submitFn, "animation submit group");
		ctx.Require(group.ResolveAddress<void* const*>(1) == r.SubmitBase, ResolveError::Mismatch, "the animation submit group is not in the submit base");
		const auto flush = ctx.First(ParallelForWriterFlush, After(submitFn, group), "parallel-for writer flush");
		const auto arm = ctx.First(ParallelForArm, After(submitFn, flush), "parallel-for arming");
		ctx.Require(static_cast<const uint8_t*>(arm.end(0)) <= kick.begin<const uint8_t>(0), ResolveError::Mismatch, "the animation submit arms its group after kicking it");
		const auto disarm = ctx.First(ParallelForDisarm, After(submitFn, kick), "parallel-for disarming");
		const auto chunkReset = ctx.First(ParallelForChunkReset, After(submitFn, disarm), "parallel-for chunk reset");
		const auto claim = ctx.First(ParallelForBlockClaim, WholeFunctionContaining(ctx, append->begin(0), "animation append"), "parallel-for block claim");

		r.GroupOffset = ByteAt(group, 3);
		auto& g = r.Group;
		ctx.Require(ByteAt(group, 2) > r.GroupOffset, ResolveError::Invalid, "the writer count +0x{:X} is before the group +0x{:X}", ByteAt(group, 2), r.GroupOffset);
		g.WriterCount = ByteAt(group, 2) - r.GroupOffset;
		g.Writers = ByteAt(flush, 1);
		g.WriterBlock = ByteAt(flush, 2);
		g.WriterItems = ByteAt(flush, 3);
		g.WriterSize = ByteAt(flush, 4);
		g.JobContext = ByteAt(arm, 2);
		g.Job = ByteAt(arm, 3);
		g.ClaimCounters[0] = FieldOffset(ctx, arm, 4, "claim counter");
		g.ClaimCounters[1] = FieldOffset(ctx, arm, 5, "claim counter");
		g.BlocksClaimed = FieldOffset(ctx, arm, 6, "blocks claimed");
		g.JobList = ByteAt(kick, 1);
		g.PerItemClaims = FieldOffset(ctx, kick, 4, "per-item claims");
		g.BlockItems = static_cast<size_t>(ctx.InRange<int32_t>(disarm.Get<int32_t>(6), 1, 0x100, "items per block"));
		g.ChunkCount = static_cast<size_t>(ctx.InRange<int32_t>(chunkReset.Get<int32_t>(2), 1, 0x100, "chunk count"));
		g.ChunkBlocks = size_t{1} << ctx.InRange<uint8_t>(claim.Get<uint8_t>(6), 1, 16, "chunk shift");
		ctx.Require(ByteAt(chunkReset, 1) > r.GroupOffset, ResolveError::Invalid, "the chunks +0x{:X} are before the group +0x{:X}", ByteAt(chunkReset, 1), r.GroupOffset);
		g.Chunks = ByteAt(chunkReset, 1) - r.GroupOffset;
		ctx.Require(ByteAt(kick, 8) % 8 == 0, ResolveError::Invalid, "job list wait slot offset 0x{:X}", ByteAt(kick, 8));
		g.JobListWaitSlot = ByteAt(kick, 8) / 8;

		ctx.Require(ByteAt(kick, 7) == g.JobList, ResolveError::Mismatch, "the animation submit waits on +0x{:X}, but kicks +0x{:X}", ByteAt(kick, 7), g.JobList);
		ctx.Require(ByteAt(disarm, 1) == g.JobContext && ByteAt(disarm, 2) == g.Job, ResolveError::Mismatch,
			"the animation submit arms +0x{:X} and +0x{:X}, but disarms +0x{:X} and +0x{:X}", g.JobContext, g.Job, ByteAt(disarm, 1), ByteAt(disarm, 2));
		ctx.Require(ByteAt(disarm, 3) == g.Writers && ByteAt(disarm, 4) == g.WriterSize
			&& std::cmp_equal(SignedByteAt(disarm, 5) + static_cast<ptrdiff_t>(g.WriterSize), g.WriterItems)
			&& std::cmp_equal(SignedByteAt(disarm, 7) + static_cast<ptrdiff_t>(g.WriterSize), g.WriterBlock), ResolveError::Mismatch,
			"the animation submit publishes and empties its writers differently");
		ctx.Require(FieldOffset(ctx, chunkReset, 3, "blocks claimed") == r.GroupOffset + g.BlocksClaimed, ResolveError::Mismatch,
			"the animation submit counts claimed blocks at +0x{:X}, but resets +0x{:X}", r.GroupOffset + g.BlocksClaimed, FieldOffset(ctx, chunkReset, 3, "blocks claimed"));
		ctx.Require(ByteAt(claim, 1) == g.WriterItems && ByteAt(claim, 2) + 1 == g.BlockItems && ByteAt(claim, 3) == g.WriterBlock, ResolveError::Mismatch,
			"the animation append fills its writer differently from the submit");
		ctx.Require(FieldOffset(ctx, claim, 5, "blocks claimed") == g.BlocksClaimed && ByteAt(claim, 7) == g.ChunkCount && ByteAt(claim, 8) == g.Chunks, ResolveError::Mismatch,
			"the animation append claims blocks differently from the submit");
		ctx.Require(g.WriterItems + 4 <= g.WriterSize && g.WriterBlock + 8 <= g.WriterSize, ResolveError::Invalid, "writer fields outside a writer of {} bytes", g.WriterSize);

		// The tail only casts a ground ray for a skeleton whose ground state passes this test.
		const auto tailFn = WholeFunctionContaining(ctx, tail, "animation tail");
		const auto ground = ctx.First(AnimationTailGround, tailFn, "animation tail ground ray");
		const auto groundRayActive = ctx.RequireInSection(ground.ResolveAddress<const void*>(2), ".text", "ground ray test");
		ctx.Require(ctx.TryMatchAt(GroundRayActiveBody, CodeFrom(ctx, groundRayActive, 0x20)).has_value(), ResolveError::Mismatch,
			"the ground ray test {} does more than read the ground state", Signatures::Describe(groundRayActive));
		r.SkeletonGround = FieldOffset(ctx, ground, 1, "skeleton ground");
		r.GroundRayActive = Address(groundRayActive);

		// Then it updates every partial skeleton with a pose, which ends by applying its pending animation control removals.
		const auto partialUpdate = ctx.First(AnimationTailPartialUpdate, After(tailFn, ground), "animation tail partial skeleton update");
		ctx.Require(FieldOffset(ctx, partialUpdate, 1, "partial skeleton pose") == r.Partials.Pose, ResolveError::Mismatch,
			"the animation tail tests the pose at +0x{:X}, the pose sync walk at +0x{:X}", FieldOffset(ctx, partialUpdate, 1, "partial skeleton pose"), r.Partials.Pose);
		std::optional<size_t> removals;
		const auto partialUpdateFn = WholeFunctionContaining(ctx, ctx.RequireInSection(partialUpdate.ResolveAddress<const void*>(2), ".text", "partial skeleton update"), "partial skeleton update");
		for (const auto& m : ctx.All(PartialPendingRemovals, partialUpdateFn)) {
			// a list: its head, then its count
			const auto count = FieldOffset(ctx, m, 2, "pending removals");
			if (count != FieldOffset(ctx, m, 1, "pending removals list") + 8)
				continue;
			ctx.Require(!removals || *removals == count, ResolveError::Ambiguous, "pending removals at +0x{:X} and +0x{:X}", removals.value_or(0), count);
			removals = count;
		}
		ctx.Require(removals.has_value(), ResolveError::NotFound, "pending animation control removals not found");
		ctx.Require(*removals + 8 <= r.Partials.Stride, ResolveError::Invalid, "pending removals +0x{:X} outside a partial skeleton of 0x{:X} bytes", *removals, r.Partials.Stride);
		r.PartialPendingRemovals = *removals;
		return r;
	});

	const Signatures::ComplexSignature<SplitCharacterCullingFunctions> SplitCharacterCulling("CrowdFix::SplitCharacterCulling", [](ResolveContext& ctx) {
		// Of the jobs stored into parallel-for groups, the camera culling job is the one that starts by reading its
		// item's type, start and count.
		const void* found = nullptr;
		std::set<const void*> builders;
		for (const auto& m : ctx.All(ParallelForJobStore, ctx.Text())) {
			const auto job = m.ResolveAddress<const void*>(1);
			if (job != found) {
				if (!ctx.InSection(job, ".text") || ctx.FunctionStartingAt(job).empty())
					continue;

				const auto head = CodeFrom(ctx, job, 0x60);
				if (!ctx.Find(CameraCullItemType, head) || !ctx.Find(CameraCullItemStart, head) || !ctx.Find(CameraCullItemCount, head))
					continue;

				ctx.Require(!found, ResolveError::Ambiguous, "camera cull jobs at {} and {}", Signatures::Describe(found), Signatures::Describe(job));
				found = job;
			}
			builders.insert(WholeFunctionContaining(ctx, m.begin(0), "camera culling").data());
		}
		ctx.Require(found != nullptr, ResolveError::NotFound, "camera cull job not found");
		ctx.Require(builders.size() == 1, ResolveError::Mismatch, "{} functions arm the camera cull job instead of 1", builders.size());
		const auto builder = WholeFunctionContaining(ctx, *builders.begin(), "camera culling");

		// The function that arms the job builds its items: it sorts the visible objects by type, then puts every list but
		// the BG objects' into a single item of the same type. The character type has to be one of those single items,
		// and the item size is the stride the item allocator hands items out at.
		std::set<uint8_t> objectTypes;
		for (const auto& m : ctx.All(CameraCullObjectTypeTest, builder))
			objectTypes.insert(m.Get<uint8_t>(1));
		std::set<uint8_t> singleItemTypes;
		const void* allocator = nullptr;
		for (const auto& m : ctx.All(CameraCullSingleItem, builder)) {
			const auto target = m.ResolveAddress<const void*>(1);
			ctx.Require(!allocator || allocator == target, ResolveError::Mismatch, "the camera culling allocates its items with {} and {}", Signatures::Describe(allocator), Signatures::Describe(target));
			allocator = target;
			singleItemTypes.insert(m.Get<uint8_t>(2));
		}
		ctx.Require(allocator != nullptr, ResolveError::NotFound, "camera culling item allocation not found");
		constexpr uint8_t characterType = 2;
		ctx.Require(objectTypes.contains(characterType) && singleItemTypes.contains(characterType), ResolveError::Mismatch,
			"the camera culling does not put objects of type {} into a single item of their own", characterType);

		const auto address = ctx.First(CameraCullItemAddress, WholeFunctionContaining(ctx, ctx.RequireInSection(allocator, ".text", "camera culling item allocator"), "camera culling item allocator"), "camera culling item address");
		ctx.Require(ByteAt(address, 2) == ByteAt(address, 4), ResolveError::Mismatch, "the camera culling item allocator returns items from another cursor");
		const auto itemSize = size_t{1} << ctx.InRange<uint8_t>(address.Get<uint8_t>(3), 6, 8, "camera culling item shift");
		return SplitCharacterCullingFunctions{
			.CullJob = Address(found),
			.CharacterItemType = characterType,
			.ItemSize = itemSize,
		};
	});

	const Signatures::ComplexSignature<PerItemCullingClaimsFunctions> PerItemCullingClaims("CrowdFix::PerItemCullingClaims", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto offsetAndTarget = [](const ScanResult& m) {
			return std::make_pair(m.Get<int32_t>(1), m.ResolveAddress<const void*>(2));
		};

		std::set<std::pair<int32_t, const void*>> setups;
		for (const auto& m : ctx.All(CullingSetupParallelFor, text))
			setups.insert(offsetAndTarget(m));
		ctx.Require(setups.size() == 1, ResolveError::Mismatch, "{} culling setup parallel-fors instead of 1", setups.size());

		// The cell group is the other group handed to a parallel-for this way.
		std::set<std::pair<int32_t, const void*>> cells;
		for (const auto& m : ctx.All(CullingCellParallelFor, text)) {
			if (const auto site = offsetAndTarget(m); 0 < site.first && site.first < 0x10000 && !setups.contains(site))
				cells.insert(site);
		}
		ctx.Require(cells.size() == 1, ResolveError::Mismatch, "{} cell culling parallel-for candidates instead of 1", cells.size());

		const auto [cellOffset, cellForkJoin] = *cells.begin();
		const auto [setupOffset, setupForkJoin] = *setups.begin();
		const auto [cellPerItem, cellBlocks] = ParallelForHelps(ctx, ctx.RequireInSection(cellForkJoin, ".text", "cell culling parallel-for"), "cell culling parallel-for");
		const auto [setupPerItem, setupBlocks] = ParallelForHelps(ctx, ctx.RequireInSection(setupForkJoin, ".text", "culling setup parallel-for"), "culling setup parallel-for");
		return PerItemCullingClaimsFunctions{
			.CullingManager = FindCullingManager(ctx),
			.CellGroupOffset = static_cast<size_t>(ctx.InRange<int32_t>(cellOffset, 1, 0x10000, "cell group offset")),
			.CellHelpPerItem = cellPerItem,
			.CellHelpBlocks = cellBlocks,
			.SetupGroupOffset = static_cast<size_t>(ctx.InRange<int32_t>(setupOffset, 1, 0x10000, "setup group offset")),
			.SetupHelpPerItem = setupPerItem,
			.SetupHelpBlocks = setupBlocks,
		};
	});

	const Signatures::ComplexSignature<GatherUsedCommandsFunctions> GatherUsedCommands("CrowdFix::GatherUsedCommands", [](ResolveContext& ctx) {
		const auto gather = UniqueFunctionStart(ctx, CommandListGatherDriver, "command list gather");
		const auto gatherFn = ctx.FunctionStartingAt(gather, "command list gather");

		// The gather of one context's blocks is the only function the gather calls.
		std::set<const void*> callees;
		for (const auto& m : RelativeCall.Lookup(gatherFn)) {
			if (const auto target = m.ResolveAddress<const void*>(1); ctx.InSection(target, ".text") && !ctx.FunctionStartingAt(target).empty())
				callees.insert(target);
		}
		ctx.Require(callees.size() == 1, ResolveError::Mismatch, "the command list gather calls {} functions instead of 1", callees.size());
		const auto blocks = *callees.begin();
		const auto blocksFn = ctx.FunctionStartingAt(blocks, "command list gather of blocks");

		// It inlines the top level of a merge sort, calling the sort for both halves; the sort calls itself for its halves.
		std::map<const void*, size_t> blocksCallees;
		for (const auto& m : RelativeCall.Lookup(blocksFn))
			++blocksCallees[m.ResolveAddress<const void*>(1)];
		const void* sort = nullptr;
		for (const auto& [target, count] : blocksCallees) {
			if (count != 2 || !ctx.InSection(target, ".text") || ctx.FunctionStartingAt(target).empty())
				continue;

			size_t selfCalls = 0;
			for (const auto part : FunctionParts(ctx, target)) {
				for (const auto& m : RelativeCall.Lookup(part))
					selfCalls += m.ResolveAddress<const void*>(1) == target;
			}
			if (selfCalls < 2)
				continue;

			ctx.Require(!sort, ResolveError::Ambiguous, "merge sorts at {} and {}", Signatures::Describe(sort), Signatures::Describe(target));
			sort = target;
		}
		ctx.Require(sort != nullptr, ResolveError::NotFound, "merge sort not found");

		// The layout: the context array from the gather, and the lists and blocks from the gather of blocks.
		const auto contextCount = ctx.First(CommandListContextCount, gatherFn, "context count");
		const auto contextArray = ctx.First(CommandListContextArray, gatherFn, "context array");
		const auto prologue = ctx.Find(CommandListBlocks, blocksFn.first((std::min)(blocksFn.size(), static_cast<size_t>(0x40))));
		ctx.Require(prologue.has_value(), ResolveError::Mismatch, "command list gather of blocks {} does not start as expected", Signatures::Describe(blocks));
		const auto copy = ctx.First(CommandListBlockCopy, After(blocksFn, *prologue), "block copy");
		const auto entryCount = ctx.First(CommandListEntryCount, After(blocksFn, copy), "entry count");
		// The first block, loaded from the list (lea reg, [rcx+rax*8]) before the copy.
		const auto list = static_cast<uint8_t>(((prologue->Get<uint8_t>(1) >> 3) & 7) | 8);
		const auto beforeCopy = After(blocksFn, *prologue).first(static_cast<size_t>(static_cast<const uint8_t*>(copy.begin(0)) - static_cast<const uint8_t*>(prologue->end(0))));
		std::optional<int32_t> firstBlock;
		for (const auto& m : ctx.All(CommandListFirstBlock, beforeCopy)) {
			if (const auto load = LoadFrom(std::span(m.begin<const uint8_t>(0), beforeCopy.data() + beforeCopy.size())); load && load->first == list) {
				firstBlock = load->second;
				break;
			}
		}
		ctx.Require(firstBlock.has_value() && *firstBlock >= 0, ResolveError::NotFound, "the command list gather of blocks does not load the first block");

		// The list descriptor is found at (list + 1) * 24 inside the context: the lists start one descriptor in.
		CommandListLayout r{
			.ContextArray = ByteAt(contextArray, 2),
			.ContextCount = ByteAt(contextCount, 1),
			.ContextSize = static_cast<size_t>(ctx.InRange<int32_t>(contextArray.Get<int32_t>(1), 0x100, 0x100000, "context size")),
			.Lists = 24,
			.ListSize = 24,
			.ListFirstBlock = static_cast<size_t>(*firstBlock),
			.ListFreeSlots = ByteAt(entryCount, 2),
			.ListBlocks = ByteAt(*prologue, 2),
			.BlockSize = size_t{1} << ctx.InRange<uint8_t>(prologue->Get<uint8_t>(3), 8, 24, "block shift"),
			.NextBlock = FieldOffset(ctx, copy, 1, "next block"),
			.EntrySize = size_t{1} << ctx.InRange<uint8_t>(entryCount.Get<uint8_t>(1), 2, 8, "entry shift"),
		};
		ctx.Require(std::cmp_equal(copy.Get<int32_t>(2), r.BlockSize) && r.NextBlock == r.BlockSize - r.EntrySize, ResolveError::Mismatch,
			"blocks of 0x{:X} bytes are copied 0x{:X} bytes at a time and linked at +0x{:X}", r.BlockSize, copy.Get<int32_t>(2), r.NextBlock);
		ctx.Require(r.ListFreeSlots + 4 <= r.ListSize && r.ListBlocks + 4 <= r.ListSize && r.ListFirstBlock + 8 <= r.ListSize, ResolveError::Invalid,
			"list fields +0x{:X}, +0x{:X} and +0x{:X} are outside a list", r.ListFirstBlock, r.ListFreeSlots, r.ListBlocks);

		return GatherUsedCommandsFunctions{
			.Gather = Address(gather),
			.Sort = Address(sort),
			.Layout = r,
		};
	});
}
