#pragma once

#include <map>
#include <xivres/common.h>

#include "Enums.h"
#include "AudioResamplerConfigs.h"
#include "BaseConfigRepository.h"
#include "FontReplacementConfigs.h"
#include "ChoicesProfile.h"
#include "ResourceOverrideRules.h"

namespace XivAlexander {
	enum class LogCategory;

	class RuntimeConfigRepository : public BaseConfigRepository {
		friend class Config;
		using BaseConfigRepository::BaseConfigRepository;

	public:
		// Each group and item is saved under its member's name. Migrate moves the keys 1.14.9.3 saved at the top level.

		class UiGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			ConfigItem<Language> Language{this, "Language", Language::SystemDefault};
			ConfigItem<ThemeMode> ThemeMode{this, "ThemeMode", ThemeMode::System};

			class MainWindowGroup : public ConfigGroup {
			public:
				using ConfigGroup::ConfigGroup;

				ConfigItem<bool> Show{this, "Show", true};
				ConfigItem<bool> AlwaysOnTop{this, "AlwaysOnTop", true};
				ConfigItem<bool> HideOnMinimize{this, "HideOnMinimize", false};
				ConfigItem<int> SettingsTreeWidth{this, "SettingsTreeWidth", 220, [](const int& v) { return std::clamp(v, 80, 2000); }};  // In pixels at 96 DPI.
			} MainWindow{this, "MainWindow"};

			class LogWindowGroup : public ConfigGroup {
			public:
				using ConfigGroup::ConfigGroup;

				ConfigItem<bool> Show{this, "Show", true};
				ConfigItem<bool> AlwaysOnTop{this, "AlwaysOnTop", false};
				ConfigItem<bool> UseWordWrap{this, "UseWordWrap", false};
				ConfigItem<bool> UseMonospaceFont{this, "UseMonospaceFont", false};
			} LogWindow{this, "LogWindow"};

			class ConfigWindowGroup : public ConfigGroup {
			public:
				using ConfigGroup::ConfigGroup;

				ConfigItem<bool> UseWordWrap{this, "UseWordWrap", false};
			} ConfigWindow{this, "ConfigWindow"};
		} Ui{this, "Ui"};

		class GameWindowGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			ConfigItem<bool> AlwaysOnTop{this, "AlwaysOnTop", false};
			ConfigItem<GameWindowTitleMode> TitleMode{this, "TitleMode", GameWindowTitleMode::None};
			ConfigItem<std::string> TitlePrefixFormat{this, "TitlePrefixFormat", std::string("{alias_or_pid} - {title}")};
			ConfigItem<std::string> TitleSuffixFormat{this, "TitleSuffixFormat", std::string("{title} ({alias_or_pid})")};
			ConfigItem<bool> DisableGhosting{this, "DisableGhosting", false};
			ConfigItem<bool> UseImeModeIndicator{this, "UseImeModeIndicator", false};
		} GameWindow{this, "GameWindow"};

		class NetworkTimingGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			ConfigItem<bool> Enabled{this, "Enabled", true};
			ConfigItem<HighLatencyMitigationMode> HighLatencyMitigationMode{this, "HighLatencyMitigationMode", HighLatencyMitigationMode::SimulateNormalizedRttAndLatency};
			ConfigItem<bool> UseHighLatencyMitigationLogging{this, "UseHighLatencyMitigationLogging", true};
			ConfigItem<bool> UseHighLatencyMitigationPreviewMode{this, "UseHighLatencyMitigationPreviewMode", false};

			// Troubleshooting purposes. If you think it's not working, change this to zero, and see if anything's different.
			// * If you find nothing has changed, try checking stuff in "Network > Troubleshooting".
			// * If you still find nothing has changed, make an issue with a log from the log window.
			// Revert before doing anything other than hitting striking dummies.
			// If you lower this value to an unreasonable extent, expect to get called out and banned from ranking communities.
			// SE probably doesn't care enough, but other players will.
			ConfigItem<int64_t> ExpectedAnimationLockDurationUs{this, "ExpectedAnimationLockDurationUs", 75000LL};

			// Should be the doubled value of the above.
			ConfigItem<int64_t> MaximumAnimationLockDurationUs{this, "MaximumAnimationLockDurationUs", 150000LL};
		} NetworkTiming{this, "NetworkTiming"};

		class SocketGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			ConfigItem<bool> ReducePacketDelay{this, "ReducePacketDelay", false};
			ConfigItem<bool> TakeOverLoopbackAddresses{this, "TakeOverLoopbackAddresses", false};
			ConfigItem<bool> TakeOverPrivateAddresses{this, "TakeOverPrivateAddresses", false};
			ConfigItem<bool> TakeOverAllAddresses{this, "TakeOverAllAddresses", false};
			ConfigItem<bool> TakeOverAllPorts{this, "TakeOverAllPorts", false};
		} Socket{this, "Socket"};

		class FramerateControlGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			ConfigItem<bool> UseMoreCpuTime{this, "UseMoreCpuTime", false};
			ConfigItem<bool> SynchronizeProcessing{this, "SynchronizeProcessing", false};

			class LockGroup : public ConfigGroup {
			public:
				using ConfigGroup::ConfigGroup;

				ConfigItem<uint64_t> Interval{
					this, "Interval", 0, [](const uint64_t& val) {
						return std::min<uint64_t>(std::max<uint64_t>(0, val), 1000000);
					}
				};
				ConfigItem<bool> Automatic{this, "Automatic", false};
				ConfigItem<double> TargetFramerateRangeFrom{this, "TargetFramerateRangeFrom", 50.};
				ConfigItem<double> TargetFramerateRangeTo{this, "TargetFramerateRangeTo", 60.};
				ConfigItem<uint64_t> MaximumRenderIntervalDeviation{
					this, "MaximumRenderIntervalDeviation", 100, [](const uint64_t& val) {
						return std::min<uint64_t>(std::max<uint64_t>(0, val), 1000000);
					}
				};
				ConfigItem<uint64_t> GlobalCooldown{this, "GlobalCooldown", 250};
			} Lock{this, "Lock"};

			ConfigItem<bool> UseBackgroundLimit{this, "UseBackgroundLimit", false};
			ConfigItem<double> BackgroundLimit{
				this, "BackgroundLimit", 5.0, [](const double& val) {
					return std::min(1000.0, std::max(0.1, val));
				}
			};

			/// Whether any of the above needs the main thread timing handler; kept up to date by the repository.
			ConfigItem<bool> UseMainThreadTimingHandler{this, "UseMainThreadTimingHandler", false};
		} FramerateControl{this, "FramerateControl"};

		class OpcodesGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			ConfigItem<std::vector<std::string>> EnabledPatchCodes{this, "EnabledPatchCodes", std::vector<std::string>()};
			ConfigItem<std::string> VersionSensitiveFeaturesAllowedGameVersion{this, "VersionSensitiveFeaturesAllowedGameVersion"};
			ConfigItem<bool> CheckForUpdatesOnStartup{this, "CheckForUpdatesOnStartup", true};
			ConfigItem<bool> UseOpcodeFinder{this, "UseOpcodeFinder", false};
			ConfigItem<bool> UseAllIpcMessageLogger{this, "UseAllIpcMessageLogger", false};
		} Opcodes{this, "Opcodes"};

		class LaunchGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			ConfigItem<bool> UseLoginSessionSwitching{this, "UseLoginSessionSwitching", false};
			ConfigItem<xivres::game_language> RememberedLanguage{this, "RememberedLanguage", xivres::game_language::Unspecified};
			ConfigItem<xivres::game_publisher> RememberedRegion{this, "RememberedRegion", xivres::game_publisher::Unspecified};
			ConfigItem<uint32_t> ClearCopiedCommandLineSeconds{this, "ClearCopiedCommandLineSeconds", 30U, [](const uint32_t& val) { return std::min<uint32_t>(val, 86400); }};
		} Launch{this, "Launch"};

		// If not set, default to files in System32 (SysWOW64) in %WINDIR% (GetSystemDirectory)
		// If set but invalid, show errors.
		class ChainLoadGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			ConfigItem<std::vector<std::filesystem::path>> D3d11{this, "D3d11"};
			ConfigItem<std::vector<std::filesystem::path>> Dxgi{this, "Dxgi"};
			ConfigItem<std::vector<std::filesystem::path>> Dinput8{this, "Dinput8"};
		} ChainLoad{this, "ChainLoad"};

		class ModdingGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			// The directories of the lists below that are there until removed; they may start with a token of Config.
			static std::vector<std::filesystem::path> DefaultGameResourceFileEntryRootDirectories() { return {LR"(<config>\ReplacementFileEntries)"}; }
			static std::vector<std::filesystem::path> DefaultTtmpSearchDirectories() { return {LR"(<config>\TexToolsMods)", LR"(<sqpack>\TexToolsMods)"}; }

			ConfigItem<bool> Enabled{this, "Enabled", false};
			ConfigItem<std::vector<std::filesystem::path>> AdditionalSqpackRootDirectories{this, "AdditionalSqpackRootDirectories"};
			ConfigItem<std::vector<std::filesystem::path>> GameResourceFileEntryRootDirectories{this, "GameResourceFileEntryRootDirectories", DefaultGameResourceFileEntryRootDirectories()};
			ConfigItem<std::vector<PathReplacementRule>> PathReplacements{this, "PathReplacements"};
			ConfigItem<bool> UseAltCodecMusicSupport{this, "UseAltCodecMusicSupport", false};

			class LanguagesGroup : public ConfigGroup {
			public:
				using ConfigGroup::ConfigGroup;

				ConfigItem<xivres::game_language> ResourceOverride{this, "ResourceOverride", xivres::game_language::Unspecified};
				ConfigItem<xivres::game_language> VoiceResourceOverride{this, "VoiceResourceOverride", xivres::game_language::Unspecified};
				ConfigItem<std::vector<xivres::game_language>> FallbackPriority{this, "FallbackPriority"};
				ConfigItem<std::vector<ForcedCharacterLanguage>> ForcedCharacterLanguages{this, "ForcedCharacterLanguages"};
				ConfigItem<int> ForcedCharacterLipSync{this, "ForcedCharacterLipSync", -1};
			} Languages{this, "Languages"};

			class TtmpGroup : public ConfigGroup {
			public:
				using ConfigGroup::ConfigGroup;

				ConfigItem<std::vector<ChoicesProfile>> ChoicesFiles{this, "ChoicesFiles", std::vector<ChoicesProfile>{{.Name = "Default", .FileName = "choices.json"}}};
				ConfigItem<std::vector<std::filesystem::path>> SearchDirectories{this, "SearchDirectories", DefaultTtmpSearchDirectories()};
			} Ttmp{this, "Ttmp"};

			class LoggingGroup : public ConfigGroup {
			public:
				using ConfigGroup::ConfigGroup;

				ConfigItem<bool> AllDataFileRead{this, "AllDataFileRead", false};
				ConfigItem<bool> HashTrackerKeys{this, "HashTrackerKeys", false};
				ConfigItem<bool> AllPaths{this, "AllPaths", false};
				ConfigItem<bool> ReplacedPaths{this, "ReplacedPaths", false};
				ConfigItem<bool> DialogueCharacterNames{this, "DialogueCharacterNames", false};
				ConfigItem<std::vector<LogPathFilter>> PathFilters{this, "PathFilters"};
			} Logging{this, "Logging"};

			class MuteVoiceGroup : public ConfigGroup {
			public:
				using ConfigGroup::ConfigGroup;

				ConfigItem<bool> Battle{this, "Battle", false};
				ConfigItem<bool> Cm{this, "Cm", false};
				ConfigItem<bool> Emote{this, "Emote", false};
				ConfigItem<bool> Line{this, "Line", false};
			} MuteVoice{this, "MuteVoice"};
		} Modding{this, "Modding"};

		class AudioGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			ConfigItem<uint32_t> OutputSamplingRate{this, "OutputSamplingRate", 48000U};
			SoxrResamplerConfigGroup SoxrResampler{this, "SoxrResampler"};
		} Audio{this, "Audio"};

		class CrowdFixGroup : public ConfigGroup {
		public:
			using ConfigGroup::ConfigGroup;

			ConfigItem<bool> Enabled{this, "Enabled", false};

			class FixesGroup : public ConfigGroup {
			public:
				using ConfigGroup::ConfigGroup;

				ConfigItem<bool> SkipIdleNotifiers{this, "SkipIdleNotifiers", true};
				ConfigItem<bool> ChainWorkerWakeups{this, "ChainWorkerWakeups", true};
				ConfigItem<bool> DedupeSkeletonSyncs{this, "DedupeSkeletonSyncs", true};
				ConfigItem<bool> TrimCullingClear{this, "TrimCullingClear", true};
				ConfigItem<bool> ShortenAllocatorLock{this, "ShortenAllocatorLock", false};
				ConfigItem<bool> PoolStagingBlocks{this, "PoolStagingBlocks", false};
				ConfigItem<bool> FreezeHiddenMinions{this, "FreezeHiddenMinions", true};
				ConfigItem<bool> SkipPrepareWait{this, "SkipPrepareWait", false};
				ConfigItem<bool> InlineBgPrep{this, "InlineBgPrep", false};
				ConfigItem<bool> SkipHiddenHotbars{this, "SkipHiddenHotbars", true};
				ConfigItem<bool> ParallelAnimTail{this, "ParallelAnimTail", true};
				ConfigItem<bool> SplitCharacterCulling{this, "SplitCharacterCulling", false};
				ConfigItem<bool> PerItemCullingClaims{this, "PerItemCullingClaims", false};
				ConfigItem<bool> GatherUsedCommands{this, "GatherUsedCommands", true};
			} Fixes{this, "Fixes"};
		} CrowdFix{this, "CrowdFix"};

		FontReplacementConfigGroup FontReplacement{this, "FontReplacement"};

		RuntimeConfigRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey);
		~RuntimeConfigRepository() override;

		void Reload(const std::filesystem::path& from = {}) override;

		[[nodiscard]] WORD GetLangId() const;
		[[nodiscard]] LPCWSTR GetStringRes(UINT uId) const;

		template<typename... Args>
		[[nodiscard]] std::wstring FormatStringRes(UINT uId, Args&&... args) const {
			return std::vformat(GetStringRes(uId), std::make_wformat_args(std::forward<Args&>(args)...));
		}

		[[nodiscard]] std::wstring GetLanguageNameLocalized(xivres::game_language gameLanguage) const;
		[[nodiscard]] std::wstring GetRegionNameLocalized(xivres::game_publisher gameRegion) const;
		[[nodiscard]] std::vector<xivres::game_language> GetFallbackLanguageList() const;
		[[nodiscard]] std::vector<std::pair<WORD, std::string>> GetDisplayLanguagePriorities() const;

		enum class VersionSensitiveFeaturesDecision {
			Keep,
			KeepTemporarily,
			DisableTemporarily,
			Disable,
		};

		xivres::util::listener_manager<RuntimeConfigRepository, void> OnVersionSensitiveFeaturesAllowedChange;

		[[nodiscard]] bool IsVersionSensitiveFeaturesDecided() const;
		[[nodiscard]] bool AreVersionSensitiveFeaturesAllowed(LogCategory logCategory, const std::string& featureName) const;
		[[nodiscard]] bool IsCurrentGameVersionAllowed() const;
		[[nodiscard]] bool AreVersionSensitiveFeaturesDisabledTemporarily() const { return m_versionSensitiveFeaturesAllowedTemporarily == 0; }
		void DecideVersionSensitiveFeatures(VersionSensitiveFeaturesDecision decision);

	protected:
		void Migrate(nlohmann::json& config) const override;

	private:
		std::atomic_int m_versionSensitiveFeaturesAllowedTemporarily = -1;

		void AllowCurrentGameVersion();

	public:
		/// The render interval within the framerate range that delays the end of the cooldown the least, or the shortest of
		/// those within a millisecond of that. See LockedCooldownDelayUs in the source for the delay.
		[[nodiscard]] static uint64_t CalculateLockFramerateIntervalUs(double fromFps, double toFps, uint64_t gcdUs, uint64_t maximumRenderIntervalDeviation);

		/// The shortest and the longest a cooldown takes when frames are rendered every intervalUs, give or take the deviation:
		/// until the frame after its end, or the one after that, if that frame may come too early to see it.
		[[nodiscard]] static std::pair<uint64_t, uint64_t> EstimateLockedCooldownUs(uint64_t cooldownUs, uint64_t intervalUs, uint64_t maximumRenderIntervalDeviation);

	private:
		std::map<std::string, std::map<std::string, std::string>> m_musicDirectoryPurchaseWebsites;

	public:
		[[nodiscard]] const std::map<std::string, std::string>& GetMusicDirectoryPurchaseWebsites(std::string name) const;
	};
}
