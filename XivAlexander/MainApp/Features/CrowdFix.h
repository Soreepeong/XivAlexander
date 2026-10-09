#pragma once

namespace XivAlexander::Apps::MainApp::Features {
	/// Port of CrowdFix (SheepGoMeh) fixes for per-frame work that scales with nearby objects; toggles apply at the next frame start on the main thread.
	class CrowdFix {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		enum class Fix : size_t {
			SkipIdleNotifiers,
			ChainWorkerWakeups,
			DedupeSkeletonSyncs,
			TrimCullingClear,
			ShortenAllocatorLock,
			PoolStagingBlocks,
			FreezeHiddenMinions,
			SkipPrepareWait,
			InlineBgPrep,
			SkipHiddenHotbars,
			ParallelAnimTail,
			SplitCharacterCulling,
			PerItemCullingClaims,
			GatherUsedCommands,
			Count,
		};

		/// Starts with the fixes CrowdFix enables by default.
		CrowdFix();
		~CrowdFix();

		void SetEnabled(Fix fix, bool enabled);
		/// Whether the fix is set to be enabled; GetStatus tells whether it is in effect.
		[[nodiscard]] bool IsEnabled(Fix fix) const;
		[[nodiscard]] bool IsAvailable(Fix fix) const;
		[[nodiscard]] std::string GetStatus(Fix fix) const;
	};
}
