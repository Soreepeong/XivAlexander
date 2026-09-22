#pragma once

#include <map>
#include <xivres/common.h>

#include "Enums.h"
#include "AudioResamplerConfigs.h"
#include "BaseConfigRepository.h"
#include "ChoicesProfile.h"
#include "ResourceOverrideRules.h"

namespace XivAlexander {
	class RuntimeConfigRepository : public BaseConfigRepository {
		friend class Config;
		using BaseConfigRepository::BaseConfigRepository;

	public:
		// Miscellaneous configuration
		ConfigItem<bool> AlwaysOnTop_GameMainWindow{this, "AlwaysOnTop_GameMainWindow", false};

		ConfigItem<bool> AlwaysOnTop_XivAlexMainWindow{this, "AlwaysOnTop_XivAlexMainWindow", true};
		ConfigItem<bool> HideOnMinimize_XivAlexMainWindow{this, "HideOnMinimize_XivAlexMainWindow", false};

		ConfigItem<bool> AlwaysOnTop_XivAlexLogWindow{this, "AlwaysOnTop_XivAlexLogWindow", false};
		ConfigItem<bool> UseWordWrap_XivAlexLogWindow{this, "UseWordWrap_XivAlexLogWindow", false};
		ConfigItem<bool> UseMonospaceFont_XivAlexLogWindow{this, "UseMonospaceFont_XivAlexLogWindow", false};

		ConfigItem<bool> UseWordWrap_ConfigWindow{this, "UseWordWrap_ConfigWindow", false};

		ConfigItem<bool> UseNetworkTimingHandler{this, "UseNetworkTimingHandler", true};
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

		ConfigItem<bool> ReducePacketDelay{this, "ReducePacketDelay", false};
		ConfigItem<bool> TakeOverLoopbackAddresses{this, "TakeOverLoopback", false};
		ConfigItem<bool> TakeOverPrivateAddresses{this, "TakeOverPrivateAddresses", false};
		ConfigItem<bool> TakeOverAllAddresses{this, "TakeOverAllAddresses", false};
		ConfigItem<bool> TakeOverAllPorts{this, "TakeOverAllPorts", false};

		ConfigItem<bool> UseOpcodeFinder{this, "UseOpcodeFinder", false};
		ConfigItem<bool> ShowLoggingWindow{this, "ShowLoggingWindow", true};
		ConfigItem<bool> ShowControlWindow{this, "ShowControlWindow", true};
		ConfigItem<bool> UseAllIpcMessageLogger{this, "UseAllIpcMessageLogger", false};

		ConfigItem<std::vector<std::string>> EnabledPatchCodes{this, "EnabledPatchCodes", std::vector<std::string>()};

		ConfigItem<bool> LogAllDataFileRead{this, "LogAllDataFileRead", false};

		ConfigItem<xivres::game_language> RememberedGameLaunchLanguage{this, "RememberedGameLaunchLanguage", xivres::game_language::Unspecified};
		ConfigItem<xivres::game_publisher> RememberedGameLaunchRegion{this, "RememberedGameLaunchRegion", xivres::game_publisher::Unspecified};

		ConfigItem<bool> CheckForUpdatedOpcodesOnStartup{this, "CheckForUpdatedOpcodesOnStartup", true};

		ConfigItem<bool> UseMoreCpuTime{this, "UseMoreCpuPower", false};
		ConfigItem<bool> SynchronizeProcessing{this, "SynchronizeProcessing", false};
		ConfigItem<GameWindowTitleMode> GameWindowTitleMode{this, "AddProcessIDToGameWindowTitle", GameWindowTitleMode::None};

		ConfigItem<uint64_t> LockFramerateInterval{
			this, "LockFramerate", 0, [](const uint64_t& val) {
				return std::min<uint64_t>(std::max<uint64_t>(0, val), 1000000);
			}
		};
		ConfigItem<bool> LockFramerateAutomatic{this, "LockFramerateAutomatic", false};
		ConfigItem<double> LockFramerateTargetFramerateRangeFrom{this, "LockFramerateTargetFramerateRangeFrom", 50.};
		ConfigItem<double> LockFramerateTargetFramerateRangeTo{this, "LockFramerateTargetFramerateRangeTo", 60.};
		ConfigItem<uint64_t> LockFramerateMaximumRenderIntervalDeviation{
			this, "LockFramerateMaximumRenderIntervalDeviation", 100, [](const uint64_t& val) {
				return std::min<uint64_t>(std::max<uint64_t>(0, val), 1000000);
			}
		};
		ConfigItem<uint64_t> LockFramerateGlobalCooldown{this, "LockFramerateGlobalCooldown", 250};

		ConfigItem<bool> UseMainThreadTimingHandler{this, "UseMainThreadTimingHandler", false};
		ConfigItem<bool> UseBackgroundFramerateLimit{this, "UseBackgroundFramerateLimit", false};
		ConfigItem<double> BackgroundFramerateLimit{
			this, "BackgroundFramerateLimit", 5.0, [](const double& val) {
				return std::min(1000.0, std::max(0.1, val));
			}
		};

		ConfigItem<Language> Language{this, "Language", Language::SystemDefault};
		ConfigItem<ThemeMode> ThemeMode{this, "ThemeMode", ThemeMode::System};

		// If not set, default to files in System32 (SysWOW64) in %WINDIR% (GetSystemDirectory)
		// If set but invalid, show errors.
		ConfigItem<std::vector<std::filesystem::path>> ChainLoadPath_d3d11{this, "ChainLoadPath_d3d11"};
		ConfigItem<std::vector<std::filesystem::path>> ChainLoadPath_dxgi{this, "ChainLoadPath_dxgi"};
		ConfigItem<std::vector<std::filesystem::path>> ChainLoadPath_dinput8{this, "ChainLoadPath_dinput8"};

		ConfigItem<bool> UseModding{this, "UseModding", false};
		ConfigItem<bool> UseHashTrackerKeyLogging{this, "UseHashTrackerKeyLogging", false};
		ConfigItem<xivres::game_language> ResourceLanguageOverride{this, "ResourceLanguageOverride", xivres::game_language::Unspecified};
		ConfigItem<xivres::game_language> VoiceResourceLanguageOverride{this, "VoiceResourceLanguageOverride", xivres::game_language::Unspecified};
		ConfigItem<std::vector<xivres::game_language>> FallbackLanguagePriority =
			{this, "FallbackLanguagePriority"};
		ConfigItem<std::vector<std::filesystem::path>> AdditionalSqpackRootDirectories =
			{this, "AdditionalSqpackRootDirectories"};
		ConfigItem<bool> TtmpFlattenSubdirectoryDisplay{this, "TtmpFlattenSubdirectoryDisplay", false};
		ConfigItem<bool> TtmpUseSubdirectoryTogglingOnFlattenedView{this, "", false};
		ConfigItem<bool> TtmpShowDedicatedMenu{this, "TtmpShowDedicatedMenu", false};
		ConfigItem<std::vector<std::filesystem::path>> AdditionalTexToolsModPackSearchDirectories =
			{this, "AdditionalTexToolsModPackSearchDirectories"};
		ConfigItem<std::vector<std::filesystem::path>> AdditionalGameResourceFileEntryRootDirectories =
			{this, "AdditionalGameResourceFileEntryRootDirectories"};
		ConfigItem<std::vector<ChoicesProfile>> TtmpChoicesFiles{this, "TtmpChoicesFiles", std::vector<ChoicesProfile>{{.Name = "Default", .FileName = "choices.json"}}};
		ConfigItem<std::vector<PathReplacementRule>> PathReplacements{this, "PathReplacements"};
		ConfigItem<std::vector<LogPathFilter>> LogPathFilters{this, "LogPathFilters"};
		ConfigItem<std::map<std::string, std::string>> ForcedCharacterLanguages{this, "ForcedCharacterLanguages"};
		ConfigItem<int> ForcedCharacterLanguageLipSync{this, "ForcedCharacterLanguageLipSync", -1};
		ConfigItem<bool> LogAllPaths{this, "LogAllPaths", false};
		ConfigItem<bool> LogReplacedPaths{this, "LogReplacedPaths", false};
		ConfigItem<bool> LogDialogueCharacterNames{this, "LogDialogueCharacterNames", false};


		ConfigItem<bool> MuteVoice_Battle{this, "MuteVoice_Battle", false};
		ConfigItem<bool> MuteVoice_Cm{this, "MuteVoice_Cm", false};
		ConfigItem<bool> MuteVoice_Emote{this, "MuteVoice_Emote", false};
		ConfigItem<bool> MuteVoice_Line{this, "MuteVoice_Line", false};

		ConfigItem<bool> UseAltCodecMusicSupport{this, "UseAltCodecMusicSupport", false};
		ConfigItem<AudioResamplerEngine> VoiceResampler{this, "VoiceResampler", AudioResamplerEngine::Disabled};
		ConfigItem<SoxrResamplerConfig> SoxrResampler{
			this, "SoxrResampler", {}, [](const SoxrResamplerConfig& v) { return v.Sanitized(); }};
		ConfigItem<WindowedSincResamplerConfig> WindowedSincResampler{
			this, "WindowedSincResampler", {}, [](const WindowedSincResamplerConfig& v) { return v.Sanitized(); }};
		ConfigItem<R8brainResamplerConfig> R8brainResampler{
			this, "R8brainResampler", {}, [](const R8brainResamplerConfig& v) { return v.Sanitized(); }};
		ConfigItem<ArtResamplerConfig> ArtResampler{
			this, "ArtResampler", {}, [](const ArtResamplerConfig& v) { return v.Sanitized(); }};
		ConfigItem<uint32_t> AudioOutputSamplingRate{this, "AudioOutputSamplingRate", 48000U};

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

		[[nodiscard]] static uint64_t CalculateLockFramerateIntervalUs(double fromFps, double toFps, uint64_t gcdUs, uint64_t maximumRenderIntervalDeviation);

	private:
		std::map<std::string, std::map<std::string, std::string>> m_musicDirectoryPurchaseWebsites;

	public:
		[[nodiscard]] const std::map<std::string, std::string>& GetMusicDirectoryPurchaseWebsites(std::string name) const;
	};
}
