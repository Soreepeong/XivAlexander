#pragma once

#include <xivres/util.listener_manager.h>

namespace XivAlexander {
	class RuntimeConfigRepository;
	template<typename T>
	class ConfigItem;
}

namespace XivAlexander::Apps::MainApp::Features {
	/// Port of CrowdFix (SheepGoMeh) fixes for per-frame work that scales with nearby objects; toggles apply at the next frame start on the main thread.
	class CrowdFix {
	public:
		/// When a fix's state (not only its detail) changes; on game threads too, so listeners should only post to their own.
		xivres::util::listener_manager<CrowdFix, void> OnStatusChange;

	private:
		struct Implementation;
		// Declared after OnStatusChange: undoing the fixes on destruction fires it.
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

		enum class FixState {
			Off,
			On,
			Waiting,  // For what the game makes later, such as after logging in.
			Unavailable,
			Failed,
		};

		struct FixStatus {
			FixState State = FixState::Off;
			std::string Detail;  // For developers; English.
		};

		explicit CrowdFix(const std::function<bool(Fix)>& initiallyEnabled);
		~CrowdFix();

		[[nodiscard]] static ConfigItem<bool>& ConfigItemOf(RuntimeConfigRepository& runtime, Fix fix);

		void SetEnabled(Fix fix, bool enabled);
		/// Whether the fix is set to be enabled; GetStatus tells whether it is in effect.
		[[nodiscard]] bool IsEnabled(Fix fix) const;
		[[nodiscard]] bool IsAvailable(Fix fix) const;
		[[nodiscard]] FixStatus GetStatus(Fix fix) const;
		/// Unavailable if none of the fixes can be applied on this version of the game.
		[[nodiscard]] FixStatus GetStatus() const;
	};
}
