#pragma once

namespace XivAlexander::Apps::MainApp::Features {
	/// The fixes of CrowdFix (SheepGoMeh): per-frame work that grows with the number of objects around, done once
	/// instead of many times, spread over the job workers, or skipped where nothing reads its result.
	/// Toggles take effect at the start of the next frame, on the game's main thread.
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
