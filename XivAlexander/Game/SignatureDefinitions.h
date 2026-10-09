#pragma once

#include <optional>
#include <vector>

#include "Game/AsiStream.h"
#include "Game/ComplexSignature.h"
#include "Game/Oodle.h"
#include "Game/SoundEngine.h"

namespace XivAlexander::Game {
	struct AtkValue;
}

namespace XivAlexander::Game::Resolved {
	struct MssAsiFunctions {
		AsiStreamAttributeFn Attribute{};
		AsiStreamResetFn Reset{};
		AsiStreamOpenFn Open{};
		AsiStreamSetUpDecoderFn SetUpDecoder{};
		AsiStreamProcessFn Process{};
	};

	struct IpcTypeCandidates {
		struct PayloadWriter {
			size_t Offset;
			uint32_t PayloadSize;
			uint16_t Opcode;
		};

		std::vector<PayloadWriter> PayloadWriters;
		std::optional<uint16_t> S2C_ActionEffects[5];
		std::optional<uint16_t> S2C_ActorControl;
		std::optional<uint16_t> S2C_ActorControlSelf;
		std::optional<uint16_t> S2C_ActorCast;
		std::optional<uint16_t> C2S_ActionRequest;
		std::optional<uint16_t> C2S_ActionRequestGroundTargeted;
	};

	// TextService's IME mode for the indicator in the chat input: 0 when closed, otherwise the indicator shows U+E01F + mode.
	using ImeModeGetterFn = uint32_t(*)(void* textService);

	struct ImeModeGetter {
		ImeModeGetterFn Function{};
		// The HIMC the game keeps for its window, which the getter queries.
		void* const* InputContext{};
	};

	// Runs every root task of the frame on the main thread, from Framework::Tick.
	using TaskManagerExecuteAllTasksFn = void(*)(void* taskManager, float* deltaTime);
	// Wakes every sleeping worker of TaskManager::JobPool.
	using JobPoolWakeAllFn = void(*)(void* jobPool);
	// Puts a job list on the queue of TaskManager::JobPool and wakes workers for it.
	using JobListKickFn = uint32_t(*)(void* taskManager, void* jobList);
	// Waits until every task of a job list has run.
	using JobListWaitFn = void(*)(void* jobList);
	// Joins in on a parallel-for group: claims its work in blocks of items, or one item at a time.
	using ParallelForHelpFn = void(*)(void* group);
	using NotifierLinkFn = void(*)(void* notifier);
	using SkeletonPoseSyncWalkFn = void(*)(void* skeletonList);
	using GraphicsAllocatorFreeFn = void(*)(void* allocator, void* block);
	using CompanionFollowFn = void(*)(void* companion);
	using AnimationUpdateFn = void(*)(void* skeletons, float deltaTime);
	using AnimationTailFn = void(*)(void* skeleton, float deltaTime);
	using AnimationTailAppendFn = void(*)(void* submitBase, void* skeleton);
	using CameraCullJobFn = int64_t(*)(void* cullingManager, uint8_t* item);
	using CommandListGatherFn = uint64_t(*)(void* device, uint32_t list, uint8_t** cursor, uint32_t* remaining, uint8_t** results, uint32_t* counts, uint32_t* total);
	using CommandListSortFn = void(*)(uint8_t* out, uint8_t* in, int32_t first, int32_t last);

	// What Kernel::Notifier callbacks test before doing any work; one of them failing means the callback returns at once.
	enum class NotifierWorkTest {
		BufferFlags,  // the u32 at +0x1C has 0x11
		IndexBufferFlags,  // the u32 at +0x20 has 0x11 but not 0x40
		TextureMappedFlags,  // the u32 at +0x3C has all of 0x100010
		TextureMappedOrUploadFlags,  // the u32 at +0x3C has all of 0x100010, or 0x2000
		ConstantBufferFlags,  // the u32 at -0x14 has 0x4000
	};

	struct NotifierCallbackTest {
		const void* Function{};
		NotifierWorkTest Test{};
	};

	// The list every Kernel::Notifier is linked into, and the two walks over it in DeviceDX11::PostTick, which call
	// vtable+0x10 on every notifier before Present and vtable+0x08 after kicking the render thread.
	struct GraphicsNotifiers {
		CRITICAL_SECTION* Lock{};
		void* const* Head{};  // linked through +0x10
		NotifierLinkFn Link{};
		NotifierLinkFn Unlink{};
		uint8_t* PrePresentLoop{};
		size_t PrePresentLoopLength{};
		uint8_t* PostKickLoop{};
		size_t PostKickLoopLength{};
		std::vector<NotifierCallbackTest> CallbackTests;
	};

	struct JobPoolWake {
		const uint32_t* QueueIndices{};  // write index, then read index
		CRITICAL_SECTION* QueueLock{};
		JobPoolWakeAllFn WakeAll{};
	};

	struct CullingVisibilityClear {
		uint32_t* ClearCount{};  // the immediate of the loop that clears the visibility table 16 bytes at a time
		void* const* CullingManager{};
	};

	// Both JobList::Prepare variants call WaitForSingleObject a second time on an event only they reset.
	struct JobListPrepareWaits {
		uint8_t* ArrayList{};  // call [WaitForSingleObject], 6 bytes
		uint8_t* SingleItemList{};
	};

	struct BgInstancingPrep {
		JobListKickFn Kick{};
		void* const* RenderManager{};
		size_t PrepListOffset{};  // the single-item job list inside Render::Manager that RenderView kicks
	};

	// The hotbar update's prepare of every slot of a hidden bar: lea rcx, [intermediate], ..., call Prepare; then inc esi.
	struct HiddenHotbarPrepares {
		uint8_t* Bar{};
		uint8_t* CrossBar{};
		size_t Length{};  // up to the inc esi
	};

	struct AnimationTail {
		AnimationUpdateFn Update{};
		AnimationTailFn Tail{};
		int32_t* EntryCount{};
		void* Entries{};  // { void* skeleton; int32_t depth; } x EntryCount, sorted by depth
		void* const* SubmitBase{};  // the animation submit's parallel-for group is at +0x30
		void* const* TaskManager{};
		JobListKickFn Kick{};
		ParallelForHelpFn HelpPerItem{};
		ParallelForHelpFn HelpBlocks{};
		AnimationTailAppendFn Append{};
	};

	struct CullingParallelFors {
		void* const* CullingManager{};
		size_t CellGroupOffset{};
		ParallelForHelpFn CellHelpPerItem{};
		ParallelForHelpFn CellHelpBlocks{};
		size_t SetupGroupOffset{};
		ParallelForHelpFn SetupHelpPerItem{};
		ParallelForHelpFn SetupHelpBlocks{};
	};

	struct CommandListGather {
		CommandListGatherFn Gather{};
		CommandListSortFn Sort{};
	};

	[[nodiscard]] std::string to_string(const MssAsiFunctions& value);
	[[nodiscard]] std::string to_string(const IpcTypeCandidates& value);
	[[nodiscard]] std::string to_string(const ImeModeGetter& value);
	[[nodiscard]] std::string to_string(const GraphicsNotifiers& value);
	[[nodiscard]] std::string to_string(const JobPoolWake& value);
	[[nodiscard]] std::string to_string(const CullingVisibilityClear& value);
	[[nodiscard]] std::string to_string(const JobListPrepareWaits& value);
	[[nodiscard]] std::string to_string(const BgInstancingPrep& value);
	[[nodiscard]] std::string to_string(const HiddenHotbarPrepares& value);
	[[nodiscard]] std::string to_string(const AnimationTail& value);
	[[nodiscard]] std::string to_string(const CullingParallelFors& value);
	[[nodiscard]] std::string to_string(const CommandListGather& value);

	using SqPackIndexLookupFn = bool(*)(void* sqpackManager, const char* path, uint32_t* outOffset, uint32_t* outDatIndex);
	using StringIndirectionResolverFn = const char8_t*(*)(const char8_t* str);
	using CutSceneLanguageGetterFn = int(*)(void* p);
	using MessageLoopFn = bool(*)();
	// Starts a lobby login; sessionId is AgentLobby's Utf8String holding DEV.TestSID, and is only copied from.
	using LobbyLoginFn = bool(*)(void* self, void* sessionId, void* arg3, void* arg4, void* arg5, void* arg6, uint8_t arg7, uint8_t arg8);
	// AgentLobby's handler for OK on a lobby error dialog; result is an AtkValue whose low 16 bits are the error code.
	using LobbyErrorDialogFn = uint64_t(*)(void* self, void* arg2, AtkValue& result);

	extern const Signatures::ComplexSignature<std::vector<SqPackIndexLookupFn>> SqPackIndexLookupFunctions;
	extern const Signatures::ComplexSignature<std::vector<StringIndirectionResolverFn>> StringIndirectionResolverFunctions;
	extern const Signatures::ComplexSignature<CutSceneLanguageGetterFn> CutSceneLanguageGetterFunction;

	extern const Signatures::ComplexSignature<Oodle::OodleNetworkFunctions> OodleNetwork;

	extern const Signatures::ComplexSignature<IpcTypeCandidates> IpcTypes;

	extern const Signatures::ComplexSignature<uint32_t*> MixRateSetup;
	extern const Signatures::ComplexSignature<SoundVoiceRenderInfo> VoiceRender;
	extern const Signatures::ComplexSignature<SoundVoiceFunctions> VoiceFunctions;
	extern const Signatures::ComplexSignature<SoundBufferEndInfo> BufferEndHandler;

	extern const Signatures::ComplexSignature<MssAsiFunctions> MssAsiStream;

	extern const Signatures::ComplexSignature<MessageLoopFn> MessageLoopFunction;

	extern const Signatures::ComplexSignature<LobbyLoginFn> LobbyLoginFunction;
	extern const Signatures::ComplexSignature<LobbyErrorDialogFn> LobbyErrorDialogFunction;

	extern const Signatures::ComplexSignature<ImeModeGetter> ImeModeGetterFunction;

	extern const Signatures::ComplexSignature<TaskManagerExecuteAllTasksFn> TaskManagerExecuteAllTasksFunction;
	extern const Signatures::ComplexSignature<void* const*> CullingManagerInstance;
	extern const Signatures::ComplexSignature<GraphicsNotifiers> GraphicsNotifierList;
	extern const Signatures::ComplexSignature<JobPoolWake> JobPoolWakeFunctions;
	extern const Signatures::ComplexSignature<JobListKickFn> JobListKickFunction;
	extern const Signatures::ComplexSignature<JobListWaitFn> JobListJoinWaitFunction;
	extern const Signatures::ComplexSignature<SkeletonPoseSyncWalkFn> SkeletonPoseSyncWalkFunction;
	extern const Signatures::ComplexSignature<CullingVisibilityClear> CullingVisibilityClearLoop;
	extern const Signatures::ComplexSignature<GraphicsAllocatorFreeFn> GraphicsAllocatorFreeFunction;
	extern const Signatures::ComplexSignature<void* const*> GraphicsAllocatorManagerInstance;
	extern const Signatures::ComplexSignature<CompanionFollowFn> CompanionFollowFunction;
	extern const Signatures::ComplexSignature<JobListPrepareWaits> JobListPrepareWaitCalls;
	extern const Signatures::ComplexSignature<BgInstancingPrep> BgInstancingPrepJob;
	extern const Signatures::ComplexSignature<HiddenHotbarPrepares> HiddenHotbarPrepareCalls;
	extern const Signatures::ComplexSignature<AnimationTail> AnimationTailFunctions;
	extern const Signatures::ComplexSignature<CameraCullJobFn> CameraCullJobFunction;
	extern const Signatures::ComplexSignature<CullingParallelFors> CullingParallelForGroups;
	extern const Signatures::ComplexSignature<CommandListGather> CommandListGatherFunctions;
}
