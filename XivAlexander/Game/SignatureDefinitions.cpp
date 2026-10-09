#include "pch.h"
#include "Game/SignatureDefinitions.h"

#include <algorithm>
#include <map>
#include <utility>

#include "Game/ResolveContext.h"
#include "Game/Signatures.h"
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
	const Signatures::RegexSignature NotifierLinkCall(R"(\xE8(....)\x41\x0F\xB6\xC5\xE9)");
	const Signatures::RegexSignature NotifierUnlinkCall(R"(\xE8(....)\x45\x33\xF6\x48\x8D\x5E)");
	// Notifier callbacks of buffers, vertex buffers, index buffers, textures and constant buffers, up to the flag test
	// that sends them to their return. The texture's pre-present one also does work for 0x2000: bt edx, 13 at the jne.
	const Signatures::RegexSignature BufferNotifierPostKick(R"(\x40\x53\x48\x83\xEC\x40\x8B\x05....\x48\x8B\xD9\x48\x8B\x54\xC1\x20\x48\x85\xD2\x74.\x8B\x49\x1C\xF6\xC1\x11\x74)");
	const Signatures::RegexSignature BufferNotifierPrePresent(R"(\x40\x53\x48\x83\xEC\x20\x8B\x05....\x48\x8B\xD9\x48\x8B\x54\xC1\x20\x48\x85\xD2\x74.\xF6\x41\x1C\x11\x74)");
	const Signatures::RegexSignature IndexBufferNotifierPostKick(R"(\x40\x53\x48\x83\xEC\x40\x8B\x05....\x48\x8B\xD9\x4C\x8B\x54\xC1\x28\x4D\x85\xD2\x74.\x8B\x51\x20\xF6\xC2\x11\x74.\xF6\xC2\x40\x75)");
	const Signatures::RegexSignature IndexBufferNotifierPrePresent(R"(\x40\x53\x48\x83\xEC\x20\x8B\x05....\x48\x8B\xD9\x48\x8B\x54\xC1\x28\x48\x85\xD2\x74.\x8B\x41\x20\xA8\x11\x74.\xA8\x40\x75)");
	const Signatures::RegexSignature TextureNotifierPostKick(R"(\x40\x53\x48\x83\xEC\x40\x8B\x41\x3C\x48\x8B\xD9\x25\x10\x00\x10\x00\x3D\x10\x00\x10\x00\x0F\x85)");
	const Signatures::RegexSignature TextureNotifierPrePresent(R"(\x48\x8B\xC4\x53\x48\x83\xEC\x60\x48\x83\x79\x48\x00\x48\x8B\xD9\x0F\x84....\x8B\x51\x3C.{0,32}?\x25\x10\x00\x10\x00.{0,8}?\x3D\x10\x00\x10\x00\x0F\x85(....))");
	const Signatures::RegexSignature TextureNotifierUploadTest(R"(\x0F\xBA\xE2\x0D)");
	const Signatures::RegexSignature ConstantBufferNotifierPrePresent(R"(\x48\x89\x5C\x24\x10\x57\x48\x83\xEC\x40\x8B\x05....\x48\x8B\xD9\x48\x8B\x7C\xC1\x18\x48\x85\xFF\x74.\x48\x83\x79\x30\x00\x74.\xF7\x41\xEC\x00\x40\x00\x00\x74)");

	// lea rcx, [queue lock]; call [EnterCriticalSection]; mov eax, [queue write index]; ...
	const Signatures::RegexSignature JobQueueLock(R"(\x48\x8D\x0D....\xFF\x15....\x8B\x05(....)\x4C\x8D\x0D....\x48\x8B\x0B\x0F\x57\xC9)");
	const Signatures::RegexSignature JobPoolWakeAllCall(R"(\xE8(....)\x48\x8B\x4C\x24.\xBA....\xFF\x15)");

	const Signatures::RegexSignature SkeletonPoseSyncWalkCall(R"(\xE8(....)\xE8....\xE8....\x41\x8B\xFF)");

	// mov ecx, 0xA000 (40960 object slots, one 16 byte store each); mov r14, [rsp+...]; mov r13, [rsp+...]; mov r12, [rsp+...]
	const Signatures::RegexSignature CullingVisibilityClearCount(R"(\xB9(\x00\xA0\x00\x00)\x4C\x8B\xB4\x24....\x4C\x8B\xAC\x24....\x4C\x8B\xA4\x24)");

	const Signatures::RegexSignature GraphicsAllocatorFree(R"(\x48\x85\xD2\x0F\x84....\x48\x89\x74\x24.\x57\x48\x83\xEC.\x48\x8B\xF1\x48\x89\x5C\x24.\x48\x81\xC1)");
	// In the buffer write lock: mov rax, [g_AllocatorManager]; ...; mov rcx, [rax+0x10]; mov rax, [rcx]; call [rax+0x10]
	const Signatures::RegexSignature GraphicsAllocatorManagerLoad(R"(\x48\x8B\x05(....)\x41\xB8....\x8B\xD7\x48\x8B\x48.\x48\x8B\x01\xFF\x50.\x48\x89\x83)");

	// The tail jump to the follow AI at the end of Companion::Update.
	const Signatures::RegexSignature CompanionFollowJump(R"(\xE9(....)\x48\x8B\xCF\xE8....\xF3\x0F\x58\x87....\x0F\x2F\x05)");

	// JobList::Prepare: call [rax+0x18] (wait for the previous run); reset counters; mov edx, -1; call [WaitForSingleObject]
	const Signatures::RegexSignature JobListArrayPrepare(R"(\x40\x53\x48\x83\xEC\x20\x48\x8B\x01\x48\x8B\xD9\xFF\x50\x18\x33\xD2\x8B\xC2\x87\x43\x7C\x87\x93\xA0\x00\x00\x00\x48\x8B\x4B\x10\xBA\xFF\xFF\xFF\xFF(\xFF\x15)(....))");
	const Signatures::RegexSignature JobListSingleItemPrepare(R"(\x40\x53\x48\x83\xEC\x20\x48\x8B\x01\x48\x8B\xD9\xFF\x50\x18\x33\xC0\xBA\xFF\xFF\xFF\xFF\x87\x43\x74\x48\x8B\x4B\x10(\xFF\x15)(....))");

	const Signatures::RegexSignature JobListKick(R"(\x40\x53\x57\x48\x83\xEC\x58\x48\x8B\x05....\x48\x33\xC4\x48\x89\x44\x24\x40\x48\x8B\x02)");
	const Signatures::RegexSignature RenderManagerLoad(R"(\x48\x8B\x0D(....)\xE8....\x84\xC0\x74.\x48\x8B\x0D....\xE8....\x33\xC9)");
	// Manager::RenderView: lea rdx, [manager+prep list]; add rcx, TaskManager (rcx: g_Framework); call kick
	const Signatures::RegexSignature BgInstancingPrepKickCall(R"(\x48\x8D\x93(....)\x48\x81\xC1\x00\x17\x00\x00\xE8(....))");

	// lea rcx, [tmp]; call HotbarUIIntermediate::ctor; mov rcx, [module]; lea r8, [tmp]; mov rdx, slot; call Prepare; inc esi
	const Signatures::RegexSignature HotbarBarPrepare(R"((\x48\x8D\x4C\x24.\xE8....\x48\x8B\x8F....\x4C\x8D\x44\x24.\x48\x8B\xD3\xE8....)\xFF\xC6\x83\xC5\x11)");
	const Signatures::RegexSignature HotbarCrossBarPrepare(R"((\x48\x8D\x4C\x24.\xE8....\x48\x8B\x8F....\x4C\x8D\x44\x24.\x49\x8B\xD6\xE8....)\xFF\xC6\x83\xC3\x11)");

	const Signatures::RegexSignature AnimationUpdateCall(R"(\xE8(....)\x48\x8B\x0D....\x48\x8B\x6C\x24\x58)");
	const Signatures::RegexSignature AnimationTailStart(R"(\x48\x89\x5C\x24\x18\x55\x48\x83\xEC\x30\x48\x8B\xE9)");
	// In the animation update: mov [entry count], esi; lea rdi, [entries]; cmp esi, 1
	const Signatures::RegexSignature AnimationTailEntries(R"(\x89\x35(....)\x48\x8D\x3D(....)\x83\xFE\x01)");
	const Signatures::RegexSignature AnimationSubmit(R"(\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x20\x48\x8B\x05(....)\x48\x8B\xD9\xF3\x0F\x11\x88\xF0\x00\x00\x00)");
	// mov rdx, [group+0x18]; mov rcx, [g_TaskManager]; call kick; mov rcx, rbx; cmp [group+0xBC], sil; je; call help; jmp; call help
	const Signatures::RegexSignature AnimationSubmitKick(R"(\x48\x8B\x53\x18\x48\x8B\x0D(....)\xE8(....)\x48\x8B\xCB\x40\x38\xB3\xBC\x00\x00\x00\x74\x07\xE8(....)\xEB\x05\xE8(....))");
	const Signatures::RegexSignature AnimationTailAppend(R"(\x40\x53\x55\x41\x54\x48\x83\xEC\x20\x8B\x0D....\x4C\x8B\xE2\x65\x48\x8B\x04\x25....\xBD\x98\x02\x00\x00)");

	const Signatures::RegexSignature CameraCullJob(R"(\x40\x53\x56\x41\x54\x41\x55\x41\x56\x48\x81\xEC\x90\x00\x00\x00)");

	// add rcx, cell group; lea r8, [cell job]; movzx r9d, r12b; mov rdx, rsi; call parallel-for
	const Signatures::RegexSignature CullingCellParallelFor(R"(\x48\x81\xC1(....)\x4C\x8D\x05....\x45\x0F\xB6\xCC\x48\x8B\xD6\xE8(....))");
	// lea rcx, [rsi+setup group]; mov r9b, 1; lea r8, [job]; mov rdx, rsi; call parallel-for
	const Signatures::RegexSignature CullingSetupParallelFor(R"(\x48\x8D\x8E(....)\x41\xB1\x01\x4C\x8D\x05....\x48\x8B\xD6\xE8(....))");
	// cmp [group+0xBC], <zero> (per-item claims); je; call per-item help; jmp; call block help
	const Signatures::RegexSignature ParallelForHelpChoice(R"(\x40\x38[\x80-\xBF]\xBC\x00\x00\x00\x74\x07\xE8(....)\xEB\x05\xE8(....))");

	const Signatures::RegexSignature CommandListGatherDriver(R"(\x4C\x89\x4C\x24\x20\x4C\x89\x44\x24\x18\x89\x54\x24\x10\x48\x89\x4C\x24\x08\x48\x83\xEC\x48\x48\x8B\x44\x24\x50)");
	const Signatures::RegexSignature CommandListGatherBlocks(R"(\x48\x89\x54\x24\x10\x55\x41\x54\x41\x56\x48\x83\xEC\x30\x41\x8B\xC0\x4D\x8B\xF1\x48\xFF\xC0\xC7\x02\x00\x00\x00\x00)");
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

		std::vector<IpcTypeCandidates::PayloadWriter> FindDutyRecorderPayloadWriters(const ResolveContext& ctx, std::span<const uint8_t> text) {
			const auto base = ctx.Image().data();
			const auto offsetOf = [base](const ScanResult& m) {
				return static_cast<size_t>(static_cast<const uint8_t*>(m.begin(0)) - base);
			};

			std::vector<IpcTypeCandidates::PayloadWriter> result;
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

		// The target of the call at p.
		const void* CallTargetAt(const ResolveContext& ctx, const void* p, std::string_view what) {
			const auto call = ctx.MatchAt(RelativeCall, std::span(static_cast<const uint8_t*>(p), 5), what);
			return ctx.RequireInSection(call.ResolveAddress<const void*>(1), ".text", what);
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

		// Both helps of a parallel-for group: per-item claims when its +0xBC flag is set, blocks of items otherwise.
		std::pair<ParallelForHelpFn, ParallelForHelpFn> ParallelForHelps(const ResolveContext& ctx, const void* forkJoin, std::string_view what) {
			const auto m = ctx.Unique(ParallelForHelpChoice, ctx.FunctionStartingAt(forkJoin, what), what);
			return {
				Address(ctx.RequireInSection(m.ResolveAddress<const void*>(1), ".text", what)),
				Address(ctx.RequireInSection(m.ResolveAddress<const void*>(2), ".text", what)),
			};
		}
	}

	std::string to_string(const MssAsiFunctions& value) {
		return std::format("attribute {}, reset {}, open {}, set up {}, process {}",
			Signatures::Describe(value.Attribute),
			Signatures::Describe(value.Reset),
			Signatures::Describe(value.Open),
			Signatures::Describe(value.SetUpDecoder),
			Signatures::Describe(value.Process));
	}

	std::string to_string(const IpcTypeCandidates& value) {
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

	std::string to_string(const ImeModeGetter& value) {
		return std::format("function {}, input context {}",
			Signatures::Describe(value.Function),
			Signatures::Describe(value.InputContext));
	}

	std::string to_string(const GraphicsNotifiers& value) {
		return std::format("lock {}, head {}, link {}, unlink {}, pre-present loop {}, post-kick loop {}, {} callback tests",
			Signatures::Describe(value.Lock),
			Signatures::Describe(value.Head),
			Signatures::Describe(value.Link),
			Signatures::Describe(value.Unlink),
			Signatures::Describe(value.PrePresentLoop),
			Signatures::Describe(value.PostKickLoop),
			value.CallbackTests.size());
	}

	std::string to_string(const JobPoolWake& value) {
		return std::format("queue indices {}, wake all {}",
			Signatures::Describe(value.QueueIndices),
			Signatures::Describe(value.WakeAll));
	}

	std::string to_string(const CullingVisibilityClear& value) {
		return std::format("clear count {}, culling manager {}",
			Signatures::Describe(value.ClearCount),
			Signatures::Describe(value.CullingManager));
	}

	std::string to_string(const JobListPrepareWaits& value) {
		return std::format("array list {}, single-item list {}",
			Signatures::Describe(value.ArrayList),
			Signatures::Describe(value.SingleItemList));
	}

	std::string to_string(const BgInstancingPrep& value) {
		return std::format("kick {}, render manager {}, prep list +{}",
			Signatures::Describe(value.Kick),
			Signatures::Describe(value.RenderManager),
			Signatures::Describe(value.PrepListOffset));
	}

	std::string to_string(const HiddenHotbarPrepares& value) {
		return std::format("bar {}, cross bar {}, length {}",
			Signatures::Describe(value.Bar),
			Signatures::Describe(value.CrossBar),
			Signatures::Describe(value.Length));
	}

	std::string to_string(const AnimationTail& value) {
		return std::format("update {}, tail {}, entry count {}, entries {}, submit base {}, task manager {}, kick {}, help per item {}, help blocks {}, append {}",
			Signatures::Describe(value.Update),
			Signatures::Describe(value.Tail),
			Signatures::Describe(value.EntryCount),
			Signatures::Describe(value.Entries),
			Signatures::Describe(value.SubmitBase),
			Signatures::Describe(value.TaskManager),
			Signatures::Describe(value.Kick),
			Signatures::Describe(value.HelpPerItem),
			Signatures::Describe(value.HelpBlocks),
			Signatures::Describe(value.Append));
	}

	std::string to_string(const CullingParallelFors& value) {
		return std::format("culling manager {}, cell group +{} (per item {}, blocks {}), setup group +{} (per item {}, blocks {})",
			Signatures::Describe(value.CullingManager),
			Signatures::Describe(value.CellGroupOffset),
			Signatures::Describe(value.CellHelpPerItem),
			Signatures::Describe(value.CellHelpBlocks),
			Signatures::Describe(value.SetupGroupOffset),
			Signatures::Describe(value.SetupHelpPerItem),
			Signatures::Describe(value.SetupHelpBlocks));
	}

	std::string to_string(const CommandListGather& value) {
		return std::format("gather {}, sort {}",
			Signatures::Describe(value.Gather),
			Signatures::Describe(value.Sort));
	}

	const Signatures::ComplexSignature<std::vector<SqPackIndexLookupFn>> SqPackIndexLookupFunctions("SqPackIndexLookupFunctions", [](ResolveContext& ctx) {
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
		return result;
	});

	const Signatures::ComplexSignature<std::vector<StringIndirectionResolverFn>> StringIndirectionResolverFunctions("StringIndirectionResolverFunctions", [](ResolveContext& ctx) {
		return AllMatchStarts<StringIndirectionResolverFn>(ctx, StringIndirectionResolver);
	});

	const Signatures::ComplexSignature<CutSceneLanguageGetterFn> CutSceneLanguageGetterFunction("CutSceneLanguageGetterFunction", [](ResolveContext& ctx) -> CutSceneLanguageGetterFn {
		return Address(ctx.First(CutSceneLanguageGetter, ctx.Text(), "call").ResolveAddress<const void*>(1));
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

	const Signatures::ComplexSignature<IpcTypeCandidates> IpcTypes("IpcTypes", [](ResolveContext& ctx) {
		IpcTypeCandidates r;
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

	const Signatures::ComplexSignature<uint32_t*> MixRateSetup("MixRateSetup", [](ResolveContext& ctx) {
		return ctx.Unique(AudioSamplingRateImmediate, ctx.Text(), "setup").begin<uint32_t>(1);
	});

	const Signatures::ComplexSignature<SoundVoiceRenderInfo> VoiceRender("VoiceRender", [](ResolveContext& ctx) {
		const auto m = ctx.Unique(SoundVoiceRender, ctx.Text(), "render");
		const auto getter = ctx.RequireInSection(m.ResolveAddress<const void*>(3), ".text", "mix rate getter");
		const auto g = ctx.MatchAt(AudioSamplingRateGetter, ctx.FunctionStartingAt(getter, "mix rate getter"), "mix rate getter");
		return SoundVoiceRenderInfo{
			.State = FieldOffset(ctx, m, 1, "state offset"),
			.QueuedBuffers = FieldOffset(ctx, m, 2, "queued buffers offset"),
			.MixRate = g.ResolveAddress<uint32_t*>(1),
		};
	});

	const Signatures::ComplexSignature<SoundVoiceFunctions> VoiceFunctions("VoiceFunctions", [](ResolveContext& ctx) {
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

		if (const auto render = ctx.TryGet(VoiceRender)) {
			ctx.Require(render->State == r.Layout.State && render->QueuedBuffers == r.Layout.QueuedBuffers, ResolveError::Mismatch,
				"submit state +0x{:X} and queued buffers +0x{:X} != render state +0x{:X} and queued buffers +0x{:X}",
				r.Layout.State, r.Layout.QueuedBuffers, render->State, render->QueuedBuffers);
		}

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
	});

	const Signatures::ComplexSignature<SoundBufferEndInfo> BufferEndHandler("BufferEndHandler", [](ResolveContext& ctx) {
		const auto m = ctx.Unique(SoundBufferEndHandler, ctx.Text(), "handler");
		return SoundBufferEndInfo{
			.Handler = Address(m.begin(0)),
			.CallbackLayout = {.EndOfData = FieldOffset(ctx, m, 1, "end of data offset")},
		};
	});

	const Signatures::ComplexSignature<MssAsiFunctions> MssAsiStream("MssAsiStream", [](ResolveContext& ctx) {
		MssAsiFunctions r;
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

	const Signatures::ComplexSignature<MessageLoopFn> MessageLoopFunction("MessageLoopFunction", [](ResolveContext& ctx) -> MessageLoopFn {
		return Address(ctx.First(MessageLoop, ctx.Image(), "message loop").ResolveAddress<const void*>(1));
	});

	const Signatures::ComplexSignature<LobbyLoginFn> LobbyLoginFunction("LobbyLoginFunction", [](ResolveContext& ctx) -> LobbyLoginFn {
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
	});

	const Signatures::ComplexSignature<LobbyErrorDialogFn> LobbyErrorDialogFunction("LobbyErrorDialogFunction", [](ResolveContext& ctx) -> LobbyErrorDialogFn {
		const auto match = ctx.Unique(LobbyErrorDialog, ctx.Text(), "lobby error dialog");
		return Address(ctx.FunctionStartingAt(&match.Get<const uint8_t>(0), "lobby error dialog").data());
	});

	const Signatures::ComplexSignature<ImeModeGetter> ImeModeGetterFunction("ImeModeGetterFunction", [](ResolveContext& ctx) {
		const auto openStatus = ImportSlot(ctx, "imm32.dll", "ImmGetOpenStatus");
		const auto conversionStatus = ImportSlot(ctx, "imm32.dll", "ImmGetConversionStatus");

		std::optional<ImeModeGetter> found;
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
				Signatures::Describe(found ? found->Function : nullptr), Signatures::Describe(fn.data()));
			found = ImeModeGetter{
				.Function = Address(fn.data()),
				.InputContext = static_cast<void* const*>(ctx.RequireInSection(inputContext, ".data", "input context")),
			};
		}
		ctx.Require(found.has_value(), ResolveError::NotFound, "IME mode getter not found");
		return *found;
	});

	const Signatures::ComplexSignature<TaskManagerExecuteAllTasksFn> TaskManagerExecuteAllTasksFunction("TaskManagerExecuteAllTasksFunction", [](ResolveContext& ctx) -> TaskManagerExecuteAllTasksFn {
		return Address(UniqueCallTarget(ctx, TaskManagerExecuteAllTasksCall, "ExecuteAllTasks call"));
	});

	const Signatures::ComplexSignature<void* const*> CullingManagerInstance("CullingManagerInstance", [](ResolveContext& ctx) {
		const auto getter = UniqueCallTarget(ctx, CullingManagerGetterCall, "culling manager getter call");
		const auto load = ctx.MatchAt(CullingManagerGetter, ctx.FunctionStartingAt(getter, "culling manager getter"), "culling manager getter");
		return static_cast<void* const*>(ctx.RequireInSection(load.ResolveAddress<const void*>(1), ".data", "culling manager"));
	});

	const Signatures::ComplexSignature<GraphicsNotifiers> GraphicsNotifierList("GraphicsNotifierList", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto walk = ctx.Unique(NotifierListWalk, text, "notifier list walk");
		GraphicsNotifiers r{
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

		r.Link = Address(UniqueCallTarget(ctx, NotifierLinkCall, "notifier link call"));
		r.Unlink = Address(UniqueCallTarget(ctx, NotifierUnlinkCall, "notifier unlink call"));

		// Base and derived classes share their callbacks, so each signature may find a callback used by several vtables.
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
			const auto before = r.CallbackTests.size();
			for (const auto& m : ctx.All(*signature, text)) {
				if (ctx.FunctionStartingAt(m.begin(0)).empty())
					continue;
				if (test == NotifierWorkTest::TextureMappedOrUploadFlags
					&& !ctx.TryMatchAt(TextureNotifierUploadTest, std::span(m.ResolveAddress<const uint8_t*>(1), 4)))
					continue;
				r.CallbackTests.push_back({m.begin(0), test});
			}
			ctx.Require(r.CallbackTests.size() > before, ResolveError::NotFound, "notifier callback for test {} not found", static_cast<int>(test));
		}
		return r;
	});

	const Signatures::ComplexSignature<JobPoolWake> JobPoolWakeFunctions("JobPoolWakeFunctions", [](ResolveContext& ctx) {
		const auto queue = ctx.Unique(JobQueueLock, ctx.Text(), "job queue lock");
		return JobPoolWake{
			.QueueIndices = static_cast<const uint32_t*>(ctx.RequireInSection(queue.ResolveAddress<const void*>(1), ".data", "job queue indices")),
			.WakeAll = Address(UniqueCallTarget(ctx, JobPoolWakeAllCall, "job pool wake-all call")),
		};
	});

	const Signatures::ComplexSignature<JobListKickFn> JobListKickFunction("JobListKickFunction", [](ResolveContext& ctx) -> JobListKickFn {
		return Address(UniqueFunctionStart(ctx, JobListKick, "job list kick"));
	});

	const Signatures::ComplexSignature<SkeletonPoseSyncWalkFn> SkeletonPoseSyncWalkFunction("SkeletonPoseSyncWalkFunction", [](ResolveContext& ctx) -> SkeletonPoseSyncWalkFn {
		return Address(UniqueCallTarget(ctx, SkeletonPoseSyncWalkCall, "pose sync walk call"));
	});

	const Signatures::ComplexSignature<CullingVisibilityClear> CullingVisibilityClearLoop("CullingVisibilityClearLoop", [](ResolveContext& ctx) {
		return CullingVisibilityClear{
			.ClearCount = ctx.Unique(CullingVisibilityClearCount, ctx.Text(), "visibility clear count").begin<uint32_t>(1),
			.CullingManager = ctx.Get(CullingManagerInstance),
		};
	});

	const Signatures::ComplexSignature<GraphicsAllocatorFreeFn> GraphicsAllocatorFreeFunction("GraphicsAllocatorFreeFunction", [](ResolveContext& ctx) -> GraphicsAllocatorFreeFn {
		return Address(UniqueFunctionStart(ctx, GraphicsAllocatorFree, "graphics allocator free"));
	});

	const Signatures::ComplexSignature<void* const*> GraphicsAllocatorManagerInstance("GraphicsAllocatorManagerInstance", [](ResolveContext& ctx) {
		const auto load = ctx.Unique(GraphicsAllocatorManagerLoad, ctx.Text(), "allocator manager load");
		return static_cast<void* const*>(ctx.RequireInSection(load.ResolveAddress<const void*>(1), ".data", "allocator manager"));
	});

	const Signatures::ComplexSignature<CompanionFollowFn> CompanionFollowFunction("CompanionFollowFunction", [](ResolveContext& ctx) -> CompanionFollowFn {
		return Address(UniqueCallTarget(ctx, CompanionFollowJump, "companion follow jump"));
	});

	const Signatures::ComplexSignature<JobListPrepareWaits> JobListPrepareWaitCalls("JobListPrepareWaitCalls", [](ResolveContext& ctx) {
		const auto wait = ImportSlot(ctx, "kernel32.dll", "WaitForSingleObject");
		const auto site = [&](const RegexSignature& signature, std::string_view what) {
			const auto m = ctx.Unique(signature, ctx.Text(), what);
			ctx.Require(ImportSlotCalledAt(ctx, m, 2) == wait, ResolveError::Mismatch, "{} does not call WaitForSingleObject", what);
			return m.begin<uint8_t>(1);
		};
		return JobListPrepareWaits{
			.ArrayList = site(JobListArrayPrepare, "array job list prepare"),
			.SingleItemList = site(JobListSingleItemPrepare, "single-item job list prepare"),
		};
	});

	const Signatures::ComplexSignature<BgInstancingPrep> BgInstancingPrepJob("BgInstancingPrepJob", [](ResolveContext& ctx) {
		const auto kick = UniqueFunctionStart(ctx, JobListKick, "job list kick");

		std::optional<size_t> offset;
		for (const auto& m : ctx.All(BgInstancingPrepKickCall, ctx.Text())) {
			if (m.ResolveAddress<const void*>(2) != kick)
				continue;
			ctx.Require(!offset, ResolveError::Ambiguous, "BG instancing prep kicked more than once");
			offset = static_cast<size_t>(ctx.InRange<int32_t>(m.Get<int32_t>(1), 1, 0x100000, "prep list offset"));
		}
		ctx.Require(offset.has_value(), ResolveError::NotFound, "BG instancing prep kick not found");

		const auto load = ctx.Unique(RenderManagerLoad, ctx.Text(), "render manager load");
		return BgInstancingPrep{
			.Kick = Address(kick),
			.RenderManager = static_cast<void* const*>(ctx.RequireInSection(load.ResolveAddress<const void*>(1), ".data", "render manager")),
			.PrepListOffset = *offset,
		};
	});

	const Signatures::ComplexSignature<HiddenHotbarPrepares> HiddenHotbarPrepareCalls("HiddenHotbarPrepareCalls", [](ResolveContext& ctx) {
		const auto bar = ctx.Unique(HotbarBarPrepare, ctx.Text(), "hotbar prepare");
		const auto crossBar = ctx.Unique(HotbarCrossBarPrepare, ctx.Text(), "cross hotbar prepare");
		return HiddenHotbarPrepares{
			.Bar = bar.begin<uint8_t>(1),
			.CrossBar = crossBar.begin<uint8_t>(1),
			.Length = static_cast<size_t>(static_cast<uint8_t*>(bar.end(1)) - bar.begin<uint8_t>(1)),
		};
	});

	const Signatures::ComplexSignature<AnimationTail> AnimationTailFunctions("AnimationTailFunctions", [](ResolveContext& ctx) {
		const auto update = UniqueCallTarget(ctx, AnimationUpdateCall, "animation update call");
		const auto updateFn = ctx.FunctionStartingAt(update, "animation update");
		const auto tail = UniqueFunctionStart(ctx, AnimationTailStart, "animation tail");
		ctx.Require(Calls(updateFn, tail), ResolveError::Mismatch, "the animation update does not call the tail");
		const auto entries = ctx.Unique(AnimationTailEntries, updateFn, "tail entries");

		const auto submit = ctx.Unique(AnimationSubmit, ctx.Text(), "animation submit");
		const auto submitFn = ctx.FunctionStartingAt(submit.begin(0), "animation submit");
		const auto kick = ctx.Unique(AnimationSubmitKick, submitFn, "animation submit kick");
		ctx.Require(kick.ResolveAddress<const void*>(2) == UniqueFunctionStart(ctx, JobListKick, "job list kick"), ResolveError::Mismatch, "the animation submit kicks with another function");
		const auto append = UniqueFunctionStart(ctx, AnimationTailAppend, "animation append");
		ctx.Require(Calls(submitFn, append), ResolveError::Mismatch, "the animation submit does not call the append");

		AnimationTail r{
			.Update = Address(update),
			.Tail = Address(tail),
			.EntryCount = entries.ResolveAddress<int32_t*>(1),
			.Entries = entries.ResolveAddress<void*>(2),
			.SubmitBase = submit.ResolveAddress<void* const*>(1),
			.TaskManager = kick.ResolveAddress<void* const*>(1),
			.Kick = Address(kick.ResolveAddress<const void*>(2)),
			.HelpPerItem = Address(ctx.RequireInSection(kick.ResolveAddress<const void*>(3), ".text", "per-item help")),
			.HelpBlocks = Address(ctx.RequireInSection(kick.ResolveAddress<const void*>(4), ".text", "block help")),
			.Append = Address(append),
		};
		ctx.RequireInSection(r.EntryCount, ".data", "tail entry count");
		ctx.RequireInSection(r.Entries, ".data", "tail entries");
		ctx.RequireInSection(r.SubmitBase, ".data", "submit base");
		ctx.RequireInSection(r.TaskManager, ".data", "task manager");
		return r;
	});

	const Signatures::ComplexSignature<CameraCullJobFn> CameraCullJobFunction("CameraCullJobFunction", [](ResolveContext& ctx) -> CameraCullJobFn {
		return Address(UniqueFunctionStart(ctx, CameraCullJob, "camera cull job"));
	});

	const Signatures::ComplexSignature<CullingParallelFors> CullingParallelForGroups("CullingParallelForGroups", [](ResolveContext& ctx) {
		const auto cell = ctx.Unique(CullingCellParallelFor, ctx.Text(), "cell culling parallel-for");
		const auto setup = ctx.Unique(CullingSetupParallelFor, ctx.Text(), "culling setup parallel-for");
		const auto [cellPerItem, cellBlocks] = ParallelForHelps(ctx, cell.ResolveAddress<const void*>(2), "cell culling parallel-for");
		const auto [setupPerItem, setupBlocks] = ParallelForHelps(ctx, setup.ResolveAddress<const void*>(2), "culling setup parallel-for");
		return CullingParallelFors{
			.CullingManager = ctx.Get(CullingManagerInstance),
			.CellGroupOffset = static_cast<size_t>(ctx.InRange<int32_t>(cell.Get<int32_t>(1), 1, 0x10000, "cell group offset")),
			.CellHelpPerItem = cellPerItem,
			.CellHelpBlocks = cellBlocks,
			.SetupGroupOffset = static_cast<size_t>(ctx.InRange<int32_t>(setup.Get<int32_t>(1), 1, 0x10000, "setup group offset")),
			.SetupHelpPerItem = setupPerItem,
			.SetupHelpBlocks = setupBlocks,
		};
	});

	const Signatures::ComplexSignature<CommandListGather> CommandListGatherFunctions("CommandListGatherFunctions", [](ResolveContext& ctx) {
		// The gather of one context's blocks inlines the top level of a merge sort, and calls the sort itself for the rest.
		const auto blocks = static_cast<const uint8_t*>(UniqueFunctionStart(ctx, CommandListGatherBlocks, "command list gather of blocks"));
		return CommandListGather{
			.Gather = Address(UniqueFunctionStart(ctx, CommandListGatherDriver, "command list gather")),
			.Sort = Address(CallTargetAt(ctx, blocks + 0x147, "merge sort call")),
		};
	});
}
