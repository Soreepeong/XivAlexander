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

// One signature per feature; parts the feature can do without are optional, logged when missing while the rest still resolves.
namespace XivAlexander::Game::Resolved {
	using SqPackIndexLookupFn = bool(*)(void* sqpackManager, const char* path, uint32_t* outOffset, uint32_t* outDatIndex);

	struct SqpackLookupHooksFunctions {
		// The two that LoadSqPack sets, one per index kind.
		std::vector<SqPackIndexLookupFn> IndexLookups;
	};

	using StringIndirectionResolverFn = const char8_t*(*)(const char8_t* str);
	using CutSceneLanguageGetterFn = int(*)(void* p);

	struct TextHooksFunctions {
		std::optional<CutSceneLanguageGetterFn> CutSceneLanguageGetter;
		std::vector<StringIndirectionResolverFn> StringIndirectionResolvers;
	};

	struct OpcodeGuesserCandidates {
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

	struct AudioResamplerFunctions {
		uint32_t* MixRateSetup{};  // the immediate of the mix rate the sound engine is set up with
		SoundVoiceRenderInfo Render;
		// Voice resampling; the mix rate can still be changed without it.
		std::optional<SoundVoiceFunctions> Voice;
		std::optional<SoundBufferEndInfo> BufferEnd;
	};

	struct AltCodecMusicSupportFunctions {
		AsiStreamAttributeFn Attribute{};
		AsiStreamResetFn Reset{};
		AsiStreamOpenFn Open{};
		AsiStreamSetUpDecoderFn SetUpDecoder{};
		AsiStreamProcessFn Process{};
		uint32_t PeekBytes{};  // what the decoder set-up fetches into the source buffer first
	};

	using MessageLoopFn = bool(*)();

	struct MainThreadTimingHandlerFunctions {
		MessageLoopFn SingleMessageLoop{};
	};

	// Starts a lobby login; sessionId is AgentLobby's Utf8String holding DEV.TestSID, and is only copied from.
	using LobbyLoginFn = bool(*)(void* self, void* sessionId, void* arg3, void* arg4, void* arg5, void* arg6, uint8_t arg7, uint8_t arg8);
	// AgentLobby's handler for OK on a lobby error dialog; result is an AtkValue whose low 16 bits are the error code.
	using LobbyErrorDialogFn = uint64_t(*)(void* self, void* arg2, AtkValue& result);

	// Switching sessions and telling expired sessions apart work without each other.
	struct LoginSessionsFunctions {
		std::optional<LobbyLoginFn> LobbyLogin;
		std::optional<LobbyErrorDialogFn> LobbyErrorDialog;
	};

	// TextService's IME mode for the indicator in the chat input: 0 when closed, otherwise the indicator shows U+E01F + mode.
	using ImeModeGetterFn = uint32_t(*)(void* textService);

	struct ImeModeIndicatorFunctions {
		ImeModeGetterFn GetImeMode{};
		// The HIMC the game keeps for its window, which the getter queries.
		void* const* InputContext{};
	};

	// Kernel::SwapChain::Present, which times the frame and calls IDXGISwapChain::Present.
	using SwapChainPresentFn = void(*)(void* swapChain);

	struct FontReplacementFunctions {
		// The rel32 of DeviceDX11::PostTick's call of Present on the render thread path.
		uint8_t* PresentCall{};
		SwapChainPresentFn Present{};
	};

	// AtkServer::SetDrawState: stages the blend state, texture and texture size constant of a batch of UI draw commands.
	using AtkServerSetDrawStateFn = void(*)(void* server, void* state, uint8_t textureDivisor);

	struct SubpixelTextFunctions {
		AtkServerSetDrawStateFn SetDrawState{};
		size_t TextureWidth{};  // uint32_t, in Kernel::Texture
		size_t RenderingToTexture{};  // uint8_t, in AtkServer: drawing into a render texture
	};

	// ExcelModuleInterface::GetLanguage: the 0-based language the sheets are loaded in, through a static it updates.
	using ExcelLanguageGetterFn = const int32_t*(*)(void* excelModuleInterface);
	// Localize::ProcessNoun: formats a noun with the sheet's handler, made for the language of the moment and kept per sheet name.
	using LocalizeProcessNounFn = bool(*)(void* localize, void* sheetName, void* output);
	// TextModule::ProcessNoun, the noun macros' handler: Localize for a macro in the sheets' language, the plain cell for any other.
	using TextModuleProcessNounFn = int32_t(*)(void* textModule, const uint8_t* macro, int32_t length, void* parameters, uint8_t hasCase, int32_t language, void* output);
	// The text wrapper's per-text entry, which also sets its language flags.
	using TextWrapFn = int32_t(*)(void* wrapper, void* text, void* lines, void* style, uint8_t paging);

	struct DisplayLanguageNouns {
		ExcelLanguageGetterFn GetLanguage{};
		LocalizeProcessNounFn ProcessNoun{};
		uint8_t* LanguageCall{};  // ProcessNoun's call of GetLanguage, which picks the handler
		size_t Interface{};  // the ExcelModuleInterface*, in Localize
		size_t Map{};  // std::map<Utf8String, handler*>: head node, then size
		size_t NodeIsNil{};  // uint8_t, after the colour
		size_t NodeValue{};  // the handler*, in a node
		size_t NodeSize{};

		struct MacroHandler {
			TextModuleProcessNounFn Function{};
			uint8_t* LocalizeCall{};  // its call of ProcessNoun, made only for a macro in the sheets' language
			size_t Interface{};  // the ExcelModuleInterface*, in the text module
		};

		std::vector<MacroHandler> MacroHandlers;
	};

	struct DisplayLanguageWrap {
		ExcelLanguageGetterFn GetLanguage{};
		TextWrapFn Wrap{};
		uint8_t* LanguageCall{};  // Wrap's call of GetLanguage, which sets the CJK flag
		size_t Interface{};  // the ExcelModuleInterface*, in the wrapper
		size_t CjkFlag{};  // uint8_t: chs/cht/ko/tc rules
		size_t KoreanFlag{};  // uint8_t: Korean breaks, set once by the set-up of AtkModule's wrapper
		size_t ModuleWrapper{};  // the wrapper, in AtkModule
		size_t ModuleFonts{};  // what the set-up stores in the wrapper, in AtkModule
		size_t WrapperFonts{};  // where it stores it, in the wrapper
	};

	// A two byte instruction before a language branch, replaced with a short jmp to either side.
	struct LanguageBranchPatch {
		uint8_t* Instruction{};
		uint16_t Original{};
		uint16_t JumpToFirst{};
		uint16_t JumpToSecond{};
	};

	struct DisplayLanguageFunctions {
		std::optional<DisplayLanguageNouns> Nouns;
		std::optional<DisplayLanguageWrap> Wrap;
		// AddonTalk::OnRefresh: first 4 lines at the fixed size and spacing, second 3 lines at the ULD's.
		std::optional<LanguageBranchPatch> TalkLines;
		// AddonTalkSubtitle: first the enlarged scale, second the normal one.
		std::optional<LanguageBranchPatch> SubtitleScale;
	};

	// PronounModule::ProcessShellCommandMessage: chat input to the form sent (flag 1) or logged (flag 0); returns a string the module keeps.
	using ProcessShellCommandMessageFn = void*(*)(void* pronounModule, void* input, uint8_t flag);

	struct OutgoingTextFunctions {
		ProcessShellCommandMessageFn ProcessShellCommandMessage{};
	};

	[[nodiscard]] std::string to_string(const OutgoingTextFunctions& value);

	[[nodiscard]] std::string to_string(const DisplayLanguageNouns::MacroHandler& value);
	[[nodiscard]] std::string to_string(const DisplayLanguageNouns& value);
	[[nodiscard]] std::string to_string(const DisplayLanguageWrap& value);
	[[nodiscard]] std::string to_string(const LanguageBranchPatch& value);
	[[nodiscard]] std::string to_string(const DisplayLanguageFunctions& value);

	[[nodiscard]] std::string to_string(const SqpackLookupHooksFunctions& value);
	[[nodiscard]] std::string to_string(const TextHooksFunctions& value);
	[[nodiscard]] std::string to_string(const OpcodeGuesserCandidates& value);
	[[nodiscard]] std::string to_string(const AudioResamplerFunctions& value);
	[[nodiscard]] std::string to_string(const AltCodecMusicSupportFunctions& value);
	[[nodiscard]] std::string to_string(const MainThreadTimingHandlerFunctions& value);
	[[nodiscard]] std::string to_string(const LoginSessionsFunctions& value);
	[[nodiscard]] std::string to_string(const ImeModeIndicatorFunctions& value);
	[[nodiscard]] std::string to_string(const FontReplacementFunctions& value);
	[[nodiscard]] std::string to_string(const SubpixelTextFunctions& value);

	extern const Signatures::ComplexSignature<SqpackLookupHooksFunctions> SqpackLookupHooks;
	extern const Signatures::ComplexSignature<TextHooksFunctions> TextHooks;
	extern const Signatures::ComplexSignature<Oodle::OodleNetworkFunctions> OodleNetwork;
	extern const Signatures::ComplexSignature<OpcodeGuesserCandidates> OpcodeGuesser;
	extern const Signatures::ComplexSignature<AudioResamplerFunctions> AudioResampler;
	extern const Signatures::ComplexSignature<AltCodecMusicSupportFunctions> AltCodecMusicSupport;
	extern const Signatures::ComplexSignature<MainThreadTimingHandlerFunctions> MainThreadTimingHandler;
	extern const Signatures::ComplexSignature<LoginSessionsFunctions> LoginSessions;
	extern const Signatures::ComplexSignature<ImeModeIndicatorFunctions> ImeModeIndicator;
	extern const Signatures::ComplexSignature<FontReplacementFunctions> FontReplacement;
	extern const Signatures::ComplexSignature<SubpixelTextFunctions> SubpixelText;
	extern const Signatures::ComplexSignature<DisplayLanguageFunctions> DisplayLanguage;
	extern const Signatures::ComplexSignature<OutgoingTextFunctions> OutgoingText;
}

// Every CrowdFix fix is a feature of its own, which keeps working when what another fix needs is not found.
namespace XivAlexander::Game::Resolved::CrowdFix {
	// Runs every root task of the frame on the main thread, from Framework::Tick.
	using TaskManagerExecuteAllTasksFn = void(*)(void* taskManager, float* deltaTime);
	// Wakes every sleeping worker of TaskManager::JobPool.
	using JobPoolWakeAllFn = void(*)(void* jobPool);
	// Puts a job list on the queue of TaskManager::JobPool and wakes workers for it.
	using JobListKickFn = uint32_t(*)(void* taskManager, void* jobList);
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
	// Whether the animation tail casts a ground ray for a skeleton's ground state; only reads the state.
	using GroundRayActiveFn = bool(*)(const void* ground);
	using CommandListGatherFn = uint64_t(*)(void* device, uint32_t list, uint8_t** cursor, uint32_t* remaining, uint8_t** results, uint32_t* counts, uint32_t* total);
	using CommandListSortFn = void(*)(uint8_t* out, uint8_t* in, int32_t first, int32_t last);

	// What Kernel::Notifier callbacks test of their u32 flags before doing any work; one of them failing means the callback returns at once.
	enum class NotifierWorkTest {
		BufferFlags,
		IndexBufferFlags,
		TextureMappedFlags,
		TextureMappedOrUploadFlags,
		ConstantBufferFlags,
	};

	// Where TaskManager::JobPool and its InnerThreads keep what the wake-all reads, as read by the wake-all itself.
	struct JobPoolLayout {
		size_t TaskManagerJobPool{};  // the JobPool inside TaskManager
		size_t Threads{};  // InnerThread**
		size_t ThreadCount{};  // int32_t
		size_t ThreadSkip{};  // uint8_t: the wake-all leaves the thread alone when set
		size_t ThreadWakeCount{};  // int32_t: 0 when asleep
		size_t ThreadEvent{};  // HANDLE
		int32_t ThreadWakeLimit{};  // the wake-all leaves a thread alone once its wake count reaches this
	};

	// As InnerThread::Run does: claim(owner, &state, &argument, &remaining) from the queue entry, then the task's vtable call with the pool context.
	struct JobRunLayout {
		size_t ThreadPool{};  // the JobPool*, in an InnerThread
		size_t PoolContext{};  // what tasks are called with, in the JobPool
		size_t TaskRunSlot{};  // task vtable slot called when the claim gives no argument
		size_t TaskRunWithArgumentSlot{};
	};

	// As the animation submit arms/joins/resets it and append fills it: items in blocks, blocks in chunks; each thread's writer owns one block at a time.
	struct ParallelForGroupLayout {
		size_t Writers{};  // pointer to the writers, one per thread
		size_t WriterCount{};  // uint32_t
		size_t JobList{};  // the job list the group is kicked with
		size_t JobContext{};  // what the job is called with
		size_t Job{};  // void(*)(void* context, void** item)
		size_t Chunks{};  // the first of ChunkCount chunk pointers
		size_t BlocksClaimed{};  // uint32_t, incremented by every block claim; zero when nothing was appended
		size_t ClaimCounters[2]{};  // uint32_t each, zeroed when the group is armed
		size_t PerItemClaims{};  // uint8_t: the helps claim single items rather than blocks
		size_t WriterSize{};
		size_t WriterItems{};  // uint32_t, in a writer: items in its block, BlockItems when it has none
		size_t WriterBlock{};  // pointer to the item count of its block, in a writer
		size_t ChunkCount{};
		size_t ChunkBlocks{};
		size_t BlockItems{};
		size_t JobListWaitSlot{};  // vtable slot of the job list's wait
	};

	// Graphics::SmallObjectAllocator, as its Free reads it: the slab chunk table and the backing allocator.
	struct GraphicsAllocatorLayout {
		size_t Lock{};  // CRITICAL_SECTION
		size_t ChunkTable{};  // pointer to the chunk entries
		size_t ChunkCount{};  // uint32_t
		size_t ChunkStride{};  // size of a chunk entry
		size_t ChunkBase{};  // the chunk's memory, in a chunk entry
		size_t ChunkSpan{};  // bytes of a chunk
		uint64_t PageMask{};  // block & ~PageMask is the page header of a slab block
		size_t PageIndex{};  // uint32_t chunk index, in the page header
		size_t Backing{};  // the backing allocator
		size_t BackingFreeSlot{};  // vtable slot of the backing allocator's free
	};

	// Where a skeleton keeps its partial skeletons, as the pose sync walk reads them.
	struct PartialSkeletonLayout {
		size_t Count{};  // uint16_t, in the skeleton
		size_t Array{};  // pointer to the partial skeletons, in the skeleton
		size_t Stride{};  // size of a partial skeleton
		size_t Pose{};  // hkaPose*, in a partial skeleton
	};

	// Kernel::Device's per-context command lists, as the gather reads them.
	struct CommandListLayout {
		size_t ContextArray{};  // pointer to the contexts, in the device
		size_t ContextCount{};  // uint32_t, in the device
		size_t ContextSize{};  // sizeof(Kernel::Context)
		size_t Lists{};  // the first list, in a context
		size_t ListSize{};  // per list: first block, write pointer, u32 free slots, u32 blocks
		size_t ListFirstBlock{};  // pointer to the first block, in a list
		size_t ListFreeSlots{};  // uint32_t, in a list
		size_t ListBlocks{};  // uint32_t, in a list
		size_t BlockSize{};
		size_t NextBlock{};  // the link entry in the last slot of a full block
		size_t EntrySize{};  // u32 sort key, 4 bytes, command pointer
	};

	struct NotifierCallbackTest {
		const void* Function{};
		NotifierWorkTest Test{};
		ptrdiff_t FlagsOffset{};  // the u32 flags, from the notifier
		uint32_t Mask{};
		uint32_t SecondMask{};
	};

	// Where the fixes are toggled and updated from; without it, every fix stays off.
	struct FixDriverFunctions {
		TaskManagerExecuteAllTasksFn ExecuteAllTasks{};
	};

	// The Kernel::Notifier list; DeviceDX11::PostTick walks it calling one vtable slot before Present and another after, right before the render kick.
	struct SkipIdleNotifiersFunctions {
		CRITICAL_SECTION* Lock{};
		void* const* Head{};
		size_t NextOffset{};  // the next notifier, in a notifier
		size_t PrePresentSlot{};
		size_t PostKickSlot{};
		NotifierLinkFn Link{};
		NotifierLinkFn Unlink{};
		uint8_t* PrePresentLoop{};
		size_t PrePresentLoopLength{};
		uint8_t* PostKickLoop{};
		size_t PostKickLoopLength{};
		std::vector<NotifierCallbackTest> CallbackTests;
	};

	struct ChainWorkerWakeupsFunctions {
		const uint32_t* QueueIndices{};  // write index, then read index
		JobPoolWakeAllFn WakeAll{};
		JobPoolLayout Layout;
	};

	struct DedupeSkeletonSyncsFunctions {
		SkeletonPoseSyncWalkFn SyncWalk{};
	};

	struct TrimCullingClearFunctions {
		uint32_t* ClearCount{};  // the immediate of the loop that clears the visibility table 16 bytes at a time
		uint32_t FullCount{};  // that immediate as the game has it: every visibility slot
		size_t TableOffset{};  // the visibility table inside the culling manager
		// Object slot bitmask (u32 words) in the culling manager: bit set while the slot (= visibility table index) is used; lowest clear is allocated.
		size_t ObjectMask{};
		uint32_t ObjectMaskWords{};
		void* const* CullingManager{};
	};

	struct ShortenAllocatorLockFunctions {
		GraphicsAllocatorFreeFn Free{};
		GraphicsAllocatorLayout Layout;
	};

	// The graphics allocator's class: a wrapper whose vtable slots forward to the small-object allocator inside it.
	struct PoolStagingBlocksFunctions {
		void* const* AllocatorManager{};
		size_t AllocatorOffset{};  // the graphics allocator that dynamic buffer writes use, inside the manager
		const void* const* Vtable{};  // the graphics allocator's own
		size_t TerminateSlot{};  // releases everything at once
		size_t AllocSlot{};  // (size, alignment); also counts the allocation
		size_t FreeSlot{};  // (block)
		size_t SizeSlot{};  // (block): the size of a block it handed out
		size_t AllocCounter{};  // int32_t, in the graphics allocator: what the alloc counts
	};

	struct FreezeHiddenMinionsFunctions {
		CompanionFollowFn Follow{};
		size_t RenderFlagsOffset{};  // the u64 render flags of the game object, as Companion::Update tests them
	};

	// Both JobList::Prepare variants call WaitForSingleObject a second time on an event only they reset.
	struct SkipPrepareWaitFunctions {
		uint8_t* ArrayList{};  // call [WaitForSingleObject], 6 bytes
		uint8_t* SingleItemList{};
	};

	struct InlineBgPrepFunctions {
		JobListKickFn Kick{};
		void* const* RenderManager{};
		size_t PrepListOffset{};  // the single-item job list inside Render::Manager that RenderView kicks
		size_t FrameworkTaskManagerOffset{};  // the TaskManager inside Framework that RenderView kicks it on
		// Job list vtable slots the kick calls in order: item count, prepare, describe (fills { claim fn, its object, 16-byte claim state }).
		size_t ListCountSlot{};
		size_t ListPrepareSlot{};
		size_t ListDescribeSlot{};
		JobPoolLayout Pool;
		JobRunLayout Run;
	};

	// The hotbar update's per-slot prepare of a hidden bar, then the slot index increment.
	struct SkipHiddenHotbarsFunctions {
		uint8_t* Bar{};
		uint8_t* CrossBar{};
		size_t Length{};  // up to the increment, the same at both sites
	};

	struct ParallelAnimTailFunctions {
		AnimationUpdateFn Update{};
		AnimationTailFn Tail{};
		int32_t* EntryCount{};
		void* Entries{};  // { void* skeleton; int32_t depth; } x EntryCount, sorted by depth
		void* const* SubmitBase{};
		size_t GroupOffset{};  // the animation submit's parallel-for group, in the submit base
		ParallelForGroupLayout Group;
		void* const* TaskManager{};
		JobListKickFn Kick{};
		ParallelForHelpFn HelpPerItem{};
		ParallelForHelpFn HelpBlocks{};
		AnimationTailAppendFn Append{};
		size_t AppendTlsSlot{};  // the thread's parallel-for writer, in the game's TLS block
		PartialSkeletonLayout Partials;
		size_t SkeletonGround{};  // the ground ray state, in a skeleton
		GroundRayActiveFn GroundRayActive{};
		size_t PartialPendingRemovals{};  // the count of pending animation control removals, in a partial skeleton
	};

	struct SplitCharacterCullingFunctions {
		CameraCullJobFn CullJob{};
		uint8_t CharacterItemType{};  // the type byte of the item that holds every character
		size_t ItemSize{};  // the stride the culling item allocator hands items out at
		size_t StartOffset{};  // uint32_t, in an item: the first object the job culls
		size_t CountOffset{};  // uint32_t, in an item: how many objects the job culls
	};

	struct PerItemCullingClaimsFunctions {
		void* const* CullingManager{};
		size_t CellGroupOffset{};
		ParallelForHelpFn CellHelpPerItem{};
		ParallelForHelpFn CellHelpBlocks{};
		size_t SetupGroupOffset{};
		ParallelForHelpFn SetupHelpPerItem{};
		ParallelForHelpFn SetupHelpBlocks{};
	};

	struct GatherUsedCommandsFunctions {
		CommandListGatherFn Gather{};
		CommandListSortFn Sort{};
		CommandListLayout Layout;
	};

	[[nodiscard]] std::string to_string(const PartialSkeletonLayout& value);
	[[nodiscard]] std::string to_string(const ParallelForGroupLayout& value);
	[[nodiscard]] std::string to_string(const FixDriverFunctions& value);
	[[nodiscard]] std::string to_string(const SkipIdleNotifiersFunctions& value);
	[[nodiscard]] std::string to_string(const ChainWorkerWakeupsFunctions& value);
	[[nodiscard]] std::string to_string(const DedupeSkeletonSyncsFunctions& value);
	[[nodiscard]] std::string to_string(const TrimCullingClearFunctions& value);
	[[nodiscard]] std::string to_string(const ShortenAllocatorLockFunctions& value);
	[[nodiscard]] std::string to_string(const PoolStagingBlocksFunctions& value);
	[[nodiscard]] std::string to_string(const FreezeHiddenMinionsFunctions& value);
	[[nodiscard]] std::string to_string(const SkipPrepareWaitFunctions& value);
	[[nodiscard]] std::string to_string(const InlineBgPrepFunctions& value);
	[[nodiscard]] std::string to_string(const SkipHiddenHotbarsFunctions& value);
	[[nodiscard]] std::string to_string(const ParallelAnimTailFunctions& value);
	[[nodiscard]] std::string to_string(const SplitCharacterCullingFunctions& value);
	[[nodiscard]] std::string to_string(const PerItemCullingClaimsFunctions& value);
	[[nodiscard]] std::string to_string(const GatherUsedCommandsFunctions& value);

	extern const Signatures::ComplexSignature<FixDriverFunctions> FixDriver;
	extern const Signatures::ComplexSignature<SkipIdleNotifiersFunctions> SkipIdleNotifiers;
	extern const Signatures::ComplexSignature<ChainWorkerWakeupsFunctions> ChainWorkerWakeups;
	extern const Signatures::ComplexSignature<DedupeSkeletonSyncsFunctions> DedupeSkeletonSyncs;
	extern const Signatures::ComplexSignature<TrimCullingClearFunctions> TrimCullingClear;
	extern const Signatures::ComplexSignature<ShortenAllocatorLockFunctions> ShortenAllocatorLock;
	extern const Signatures::ComplexSignature<PoolStagingBlocksFunctions> PoolStagingBlocks;
	extern const Signatures::ComplexSignature<FreezeHiddenMinionsFunctions> FreezeHiddenMinions;
	extern const Signatures::ComplexSignature<SkipPrepareWaitFunctions> SkipPrepareWait;
	extern const Signatures::ComplexSignature<InlineBgPrepFunctions> InlineBgPrep;
	extern const Signatures::ComplexSignature<SkipHiddenHotbarsFunctions> SkipHiddenHotbars;
	extern const Signatures::ComplexSignature<ParallelAnimTailFunctions> ParallelAnimTail;
	extern const Signatures::ComplexSignature<SplitCharacterCullingFunctions> SplitCharacterCulling;
	extern const Signatures::ComplexSignature<PerItemCullingClaimsFunctions> PerItemCullingClaims;
	extern const Signatures::ComplexSignature<GatherUsedCommandsFunctions> GatherUsedCommands;
}

// What font replacement reads and hooks, one signature per part of it; offsets are as the game's code says, so they follow the patches.
namespace XivAlexander::Game::Resolved::Fonts {
	struct StageLayout {
		void* const* Instance{};  // AtkStage*
		int32_t FontManager{};  // AtkFontManager*, in AtkStage
	};

	struct FontManagerLayout {
		int32_t Fonts{};  // GameFont*
		int32_t FontCount{};  // uint16_t
		int32_t FontSetCount{};
		int32_t FontSetSize{};
		int32_t FontSize{};
	};

	struct FontSetLayout {
		void* PickFont{};
		int32_t ScaledSizeY{};  // float
		int32_t NodeScaleX{};  // float
		int32_t NodeScaleY{};  // float
		int32_t CurrentFont{};  // GameFont*
		int32_t CurrentFontSize{};  // float
		std::optional<int32_t> DrawFlags;  // uint32_t
	};

	struct FontLayout {
		void* GetGlyph{};
		void* LayOutCharacter{};
		void* BuildFont{};
		int32_t MaxTextures{};
		int32_t Textures{};  // Kernel::Texture*[MaxTextures]
		int32_t KerningCount{};  // uint32_t
		int32_t TextureWidth{};  // uint16_t
		int32_t Size{};  // float
		int32_t Ascent{};  // int
		int32_t GlyphMap{};  // GameGlyphMap*
		int32_t Secondary{};  // GameFont*
		int32_t SecondaryRatio{};  // float
		int32_t TextureCount{};  // uint16_t
		int32_t Flags{};  // uint8_t
		std::optional<int32_t> XShift;  // int8_t
	};

	struct UiLayout {
		int32_t StageUnitManager{};  // RaptureAtkUnitManager*, in AtkStage
		int32_t LoadedUnitsCount{};  // uint16_t
		int32_t LoadedUnitsEntries{};  // AtkUnitBase*[]
		int32_t UnitUld{};  // AtkUldManager, in AtkUnitBase
		int32_t UldNodeList{};  // AtkResNode**
		int32_t UldNodeListCount{};  // uint16_t
		int32_t NodeType{};  // uint16_t
		int32_t ComponentNodeComponent{};  // AtkComponentBase*
		int32_t ComponentUld{};  // AtkUldManager, in AtkComponentBase
	};

	// FileName is an MSVC std::string.
	struct ResourceHandleLayout {
		int32_t FileName{};
		int32_t FileNameLength{};
		int32_t FileNameCapacity{};
	};

	// Device::CreateTexture2D(device, int* size, uint8_t mipLevels, uint32_t format, uint32_t flags, uint32_t unknown) -> Kernel::Texture*.
	struct TextureFunctions {
		void* const* Device{};
		void* CreateTexture2D{};
		int32_t D3D11Texture2D{};  // ID3D11Texture2D*, in Kernel::Texture
	};

	struct ReplacerFunctions {
		void* BuildFontCache{};
		void* FreeFontCache{};
		void* const* RendererVtable{};  // AtkFontAnalyzerRenderer
		void* const* RenderCountVtable{};  // AtkFontAnalyzerRenderCount
		int32_t FontCacheSlot{};  // in AtkTextNode
		int32_t FontCacheFlags{};  // uint8_t, in AtkTextNode
		std::optional<int32_t> StateFlags;  // uint32_t, in the font analyzer's state
	};

	// Tables of {uint64_t texture count, char* texture name format, char* FDT name}.
	struct FontTablesLayout {
		void* Game{};
		void* LobbyA{};
		void* LobbyB{};
		int32_t Count{};
		int32_t EntrySize{};
		int32_t FdtName{};
	};

	struct NamePlateFunctions {
		void* AllocateBake{};
		void* BakePrepare{};
		void* DrawBaked{};
		int32_t AddonBakePlate{};
		int32_t AddonObjects{};
		int32_t ObjectSize{};
		int32_t ObjectCount{};
		int32_t ObjectNameText{};
		int32_t ObjectTextW{};
		int32_t ObjectTextH{};
		int32_t ObjectNeedsToBeBaked{};
		int32_t BakeTextYOffset{};
		int32_t BakeAlpha{};
		int32_t RendererCurrentBakeData{};
		int32_t NodeTransform{};
		int32_t NodeWidth{};
		int32_t NodeHeight{};
		int32_t NodeParent{};
		int32_t NodeScaleX{};
	};

	// AtkServer's shader pairs, by pass.
	struct ShaderTablesLayout {
		void* BindFontShaders{};  // AtkServer::BindShaders(server, pass, alternate)
		int32_t PixelShaderFiles{};  // ShaderCodeResourceHandle*[], in AtkServer
		int32_t PixelShaders{};  // Kernel::PixelShader*[], in AtkServer
		int32_t D3DShader{};  // ID3D11PixelShader*, in Kernel::PixelShader
	};

	struct EdgeShaderFunctions {
		void* LoadShaders{};  // AtkServer::LoadShaders(server)
		int32_t ModuleServer{};  // AtkServer, in AtkModule
		int32_t ModuleFontManager{};  // AtkFontManager, in AtkModule
	};

	struct LineBreakerFunctions {
		void* SplitWord{};
		void* FitUnits{};
		int32_t ConsumedUnits{};
		int32_t MaxUnits{};
		int32_t UsedUnits{};
	};

	[[nodiscard]] std::string to_string(const StageLayout& value);
	[[nodiscard]] std::string to_string(const FontManagerLayout& value);
	[[nodiscard]] std::string to_string(const FontSetLayout& value);
	[[nodiscard]] std::string to_string(const FontLayout& value);
	[[nodiscard]] std::string to_string(const UiLayout& value);
	[[nodiscard]] std::string to_string(const ResourceHandleLayout& value);
	[[nodiscard]] std::string to_string(const TextureFunctions& value);
	[[nodiscard]] std::string to_string(const ReplacerFunctions& value);
	[[nodiscard]] std::string to_string(const FontTablesLayout& value);
	[[nodiscard]] std::string to_string(const NamePlateFunctions& value);
	[[nodiscard]] std::string to_string(const ShaderTablesLayout& value);
	[[nodiscard]] std::string to_string(const EdgeShaderFunctions& value);
	[[nodiscard]] std::string to_string(const LineBreakerFunctions& value);

	extern const Signatures::ComplexSignature<StageLayout> Stage;
	extern const Signatures::ComplexSignature<FontManagerLayout> FontManager;
	extern const Signatures::ComplexSignature<FontSetLayout> FontSet;
	extern const Signatures::ComplexSignature<FontLayout> Font;
	extern const Signatures::ComplexSignature<UiLayout> Ui;
	extern const Signatures::ComplexSignature<ResourceHandleLayout> ResourceHandles;
	extern const Signatures::ComplexSignature<TextureFunctions> Textures;
	extern const Signatures::ComplexSignature<ReplacerFunctions> Replacer;
	extern const Signatures::ComplexSignature<FontTablesLayout> FontTables;
	extern const Signatures::ComplexSignature<NamePlateFunctions> NamePlates;
	extern const Signatures::ComplexSignature<ShaderTablesLayout> ShaderTables;
	extern const Signatures::ComplexSignature<EdgeShaderFunctions> EdgeShader;
	extern const Signatures::ComplexSignature<LineBreakerFunctions> LineBreaker;
}
