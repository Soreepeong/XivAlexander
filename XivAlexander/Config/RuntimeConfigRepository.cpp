#include "pch.h"
#include "Config.h"

#include "Utils/Win32/Process.h"
#include "Utils/Win32/Resource.h"
#include "resource.h"
#include "XivAlexander.h"

#include "Misc/GameInstallationDetector.h"
#include "Misc/Logger.h"

namespace {
	const std::map<XivAlexander::Language, WORD> LanguageIdMap{
		{XivAlexander::Language::SystemDefault, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)},
		{XivAlexander::Language::Japanese, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)},
		{XivAlexander::Language::English, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)},
		{XivAlexander::Language::Korean, MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)},
	};

	const std::map<xivres::game_language, WORD> GameLanguageIdMap{
		{xivres::game_language::Unspecified, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)},
		{xivres::game_language::Japanese, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)},
		{xivres::game_language::English, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)},
		{xivres::game_language::German, MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)},
		{xivres::game_language::French, MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH)},
		{xivres::game_language::ChineseSimplified, MAKELANGID(LANG_CHINESE_SIMPLIFIED, SUBLANG_CHINESE_SIMPLIFIED)},
		{xivres::game_language::ChineseTraditional, MAKELANGID(LANG_CHINESE_TRADITIONAL, SUBLANG_CHINESE_TRADITIONAL)},
		{xivres::game_language::Korean, MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)},
		{xivres::game_language::TraditionalChinese, MAKELANGID(LANG_CHINESE_TRADITIONAL, SUBLANG_CHINESE_TRADITIONAL)},
	};

	const std::map<WORD, int> LanguageIdNameResourceIdMap{
		{MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), IDS_LANGUAGE_NAME_UNSPECIFIED},
		{MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN), IDS_LANGUAGE_NAME_JAPANESE},
		{MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), IDS_LANGUAGE_NAME_ENGLISH},
		{MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), IDS_LANGUAGE_NAME_GERMAN},
		{MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), IDS_LANGUAGE_NAME_FRENCH},
		{MAKELANGID(LANG_CHINESE_SIMPLIFIED, SUBLANG_CHINESE_SIMPLIFIED), IDS_LANGUAGE_NAME_CHINESE_SIMPLIFIED},
		{MAKELANGID(LANG_CHINESE_TRADITIONAL, SUBLANG_CHINESE_TRADITIONAL), IDS_LANGUAGE_NAME_CHINESE_TRADITIONAL},
		{MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN), IDS_LANGUAGE_NAME_KOREAN},
	};

	const std::map<xivres::game_publisher, int> RegionResourceIdMap{
		{xivres::game_publisher::Unspecified, IDS_REGION_NAME_UNSPECIFIED},
		{xivres::game_publisher::SquareEnixJapan, IDS_REGION_NAME_JAPAN},
		{xivres::game_publisher::SquareEnixAmerica, IDS_REGION_NAME_NORTH_AMERICA},
		{xivres::game_publisher::SquareEnixEurope, IDS_REGION_NAME_EUROPE},
		{xivres::game_publisher::ShengquGames, IDS_REGION_NAME_CHINA},
		{xivres::game_publisher::ActozSoft, IDS_REGION_NAME_KOREA},
		{xivres::game_publisher::UserjoyGames, IDS_REGION_NAME_CHINATRAD},
	};

	const std::string& CurrentGameVersion() {
		static const auto version = [] {
			try {
				return XivAlexander::Misc::GameInstallationDetector::GetGameReleaseInfo().GameVersion;
			} catch (...) {
				return std::string();
			}
		}();
		return version;
	}
}


XivAlexander::RuntimeConfigRepository::RuntimeConfigRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey)
	: BaseConfigRepository(pConfig, std::move(path), std::move(parentKey)) {
	m_cleanup += Ui.Language.AddAndCallOnChange([&] {
		Utils::Win32::Error::SetDefaultLanguageId(GetLangId());
	});

	m_cleanup += Opcodes.VersionSensitiveFeaturesAllowedGameVersion.OnChange([&] { OnVersionSensitiveFeaturesAllowedChange(); });

	const auto updateUseMainThreadTimingHandler = [&] {
		FramerateControl.UseMainThreadTimingHandler = FramerateControl.SynchronizeProcessing || FramerateControl.Lock.Automatic || FramerateControl.Lock.Interval || FramerateControl.UseBackgroundLimit || FramerateControl.UseMoreCpuTime;
	};
	m_cleanup += FramerateControl.AddAndCallOnChange(updateUseMainThreadTimingHandler);
}

XivAlexander::RuntimeConfigRepository::~RuntimeConfigRepository() {
	m_cleanup.clear();
}

void XivAlexander::RuntimeConfigRepository::Reload(const std::filesystem::path& from) {
	BaseConfigRepository::Reload(from);
	Utils::Win32::Error::SetDefaultLanguageId(GetLangId());
}

void XivAlexander::RuntimeConfigRepository::Migrate(nlohmann::json& config) const {
	// The keys 1.14.9.3 saved, all at the top level.
	const std::pair<const char*, const ConfigItemBase*> moved[]{
		{"Language", &Ui.Language},
		{"ThemeMode", &Ui.ThemeMode},
		{"ShowControlWindow", &Ui.MainWindow.Show},
		{"AlwaysOnTop_XivAlexMainWindow", &Ui.MainWindow.AlwaysOnTop},
		{"HideOnMinimize_XivAlexMainWindow", &Ui.MainWindow.HideOnMinimize},
		{"ShowLoggingWindow", &Ui.LogWindow.Show},
		{"AlwaysOnTop_XivAlexLogWindow", &Ui.LogWindow.AlwaysOnTop},
		{"UseWordWrap_XivAlexLogWindow", &Ui.LogWindow.UseWordWrap},
		{"UseMonospaceFont_XivAlexLogWindow", &Ui.LogWindow.UseMonospaceFont},
		{"AlwaysOnTop_GameMainWindow", &GameWindow.AlwaysOnTop},
		{"AddProcessIDToGameWindowTitle", &GameWindow.TitleMode},
		{"UseNetworkTimingHandler", &NetworkTiming.Enabled},
		{"HighLatencyMitigationMode", &NetworkTiming.HighLatencyMitigationMode},
		{"UseHighLatencyMitigationLogging", &NetworkTiming.UseHighLatencyMitigationLogging},
		{"UseHighLatencyMitigationPreviewMode", &NetworkTiming.UseHighLatencyMitigationPreviewMode},
		{"ExpectedAnimationLockDurationUs", &NetworkTiming.ExpectedAnimationLockDurationUs},
		{"MaximumAnimationLockDurationUs", &NetworkTiming.MaximumAnimationLockDurationUs},
		{"ReducePacketDelay", &Socket.ReducePacketDelay},
		{"TakeOverLoopback", &Socket.TakeOverLoopbackAddresses},
		{"TakeOverPrivateAddresses", &Socket.TakeOverPrivateAddresses},
		{"TakeOverAllAddresses", &Socket.TakeOverAllAddresses},
		{"TakeOverAllPorts", &Socket.TakeOverAllPorts},
		{"UseMoreCpuPower", &FramerateControl.UseMoreCpuTime},
		{"SynchronizeProcessing", &FramerateControl.SynchronizeProcessing},
		{"LockFramerate", &FramerateControl.Lock.Interval},
		{"LockFramerateAutomatic", &FramerateControl.Lock.Automatic},
		{"LockFramerateTargetFramerateRangeFrom", &FramerateControl.Lock.TargetFramerateRangeFrom},
		{"LockFramerateTargetFramerateRangeTo", &FramerateControl.Lock.TargetFramerateRangeTo},
		{"LockFramerateMaximumRenderIntervalDeviation", &FramerateControl.Lock.MaximumRenderIntervalDeviation},
		{"LockFramerateGlobalCooldown", &FramerateControl.Lock.GlobalCooldown},
		{"UseMainThreadTimingHandler", &FramerateControl.UseMainThreadTimingHandler},
		{"EnabledPatchCodes", &Opcodes.EnabledPatchCodes},
		{"CheckForUpdatedOpcodesOnStartup", &Opcodes.CheckForUpdatesOnStartup},
		{"UseOpcodeFinder", &Opcodes.UseOpcodeFinder},
		{"UseAllIpcMessageLogger", &Opcodes.UseAllIpcMessageLogger},
		{"RememberedGameLaunchLanguage", &Launch.RememberedLanguage},
		{"RememberedGameLaunchRegion", &Launch.RememberedRegion},
		{"ChainLoadPath_d3d11", &ChainLoad.D3d11},
		{"ChainLoadPath_dxgi", &ChainLoad.Dxgi},
		{"ChainLoadPath_dinput8", &ChainLoad.Dinput8},
		{"UseModding", &Modding.Enabled},
		{"AdditionalGameResourceFileEntryRootDirectories", &Modding.GameResourceFileEntryRootDirectories},
		{"AdditionalTexToolsModPackSearchDirectories", &Modding.Ttmp.SearchDirectories},
		{"LogAllDataFileRead", &Modding.Logging.AllDataFileRead},
		{"MuteVoice_Battle", &Modding.MuteVoice.Battle},
		{"MuteVoice_Cm", &Modding.MuteVoice.Cm},
		{"MuteVoice_Emote", &Modding.MuteVoice.Emote},
		{"MuteVoice_Line", &Modding.MuteVoice.Line},
	};

	// These were searched in addition to the defaults, which are now listed explicitly so that they can be removed.
	const std::pair<const char*, std::vector<std::filesystem::path>> listedDefaults[]{
		{"AdditionalGameResourceFileEntryRootDirectories", ModdingGroup::DefaultGameResourceFileEntryRootDirectories()},
		{"AdditionalTexToolsModPackSearchDirectories", ModdingGroup::DefaultTtmpSearchDirectories()},
	};
	if (config.is_object()) {
		for (const auto& [from, defaults] : listedDefaults) {
			if (const auto it = config.find(from); it != config.end() && it->is_array()) {
				auto value = nlohmann::json::array();
				for (const auto& dir : defaults)
					value.emplace_back(xivres::util::unicode::convert<std::string>(dir.wstring()));
				for (auto& dir : *it)
					value.emplace_back(std::move(dir));
				*it = std::move(value);
			}
		}
	}

	for (const auto& [from, item] : moved)
		MoveKey(config, from, *item);
}

WORD XivAlexander::RuntimeConfigRepository::GetLangId() const {
	if (const auto i = LanguageIdMap.find(Ui.Language); i != LanguageIdMap.end())
		return i->second;

	return MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
}

LPCWSTR XivAlexander::RuntimeConfigRepository::GetStringRes(UINT uId) const {
	return FindStringResourceEx(Dll::Module(), uId, GetLangId()) + 1;
}

std::wstring XivAlexander::RuntimeConfigRepository::GetLanguageNameLocalized(xivres::game_language gameLanguage) const {
	const auto langNameInUserLang = std::wstring(GetStringRes(LanguageIdNameResourceIdMap.at(GameLanguageIdMap.at(gameLanguage))));
	auto langNameInGameLang = std::wstring(FindStringResourceEx(Dll::Module(), LanguageIdNameResourceIdMap.at(GameLanguageIdMap.at(gameLanguage)), GameLanguageIdMap.at(gameLanguage)) + 1);
	if (langNameInUserLang == langNameInGameLang)
		return langNameInGameLang;
	else
		return std::format(L"{} ({})", langNameInUserLang, langNameInGameLang);
}

std::vector<xivres::game_language> XivAlexander::RuntimeConfigRepository::GetFallbackLanguageList() const {
	std::vector<xivres::game_language> result;
	for (const auto lang : Modding.Languages.FallbackPriority.Value()) {
		if (std::ranges::find(result, lang) == result.end() && lang != xivres::game_language::Unspecified)
			result.push_back(lang);
	}
	for (const auto lang : {
			xivres::game_language::Japanese,
			xivres::game_language::English,
			xivres::game_language::German,
			xivres::game_language::French,
			xivres::game_language::ChineseSimplified,
			xivres::game_language::Korean,
		}) {
		if (std::ranges::find(result, lang) == result.end())
			result.push_back(lang);
	}
	return result;
}

bool XivAlexander::RuntimeConfigRepository::IsCurrentGameVersionAllowed() const {
	const auto& gameVersion = CurrentGameVersion();
	return !gameVersion.empty() && Opcodes.VersionSensitiveFeaturesAllowedGameVersion.Value() == gameVersion;
}

void XivAlexander::RuntimeConfigRepository::AllowCurrentGameVersion() {
	if (const auto& gameVersion = CurrentGameVersion(); !gameVersion.empty())
		Opcodes.VersionSensitiveFeaturesAllowedGameVersion = gameVersion;
}

bool XivAlexander::RuntimeConfigRepository::IsVersionSensitiveFeaturesDecided() const {
	return m_versionSensitiveFeaturesAllowedTemporarily != -1 || IsCurrentGameVersionAllowed();
}

bool XivAlexander::RuntimeConfigRepository::AreVersionSensitiveFeaturesAllowed(LogCategory logCategory, const std::string& featureName) const {
	const auto temporary = m_versionSensitiveFeaturesAllowedTemporarily.load();
	if (temporary == -1 ? IsCurrentGameVersionAllowed() : temporary != 0)
		return true;

	Misc::Logger::Acquire()->Format<LogLevel::Warning>(logCategory, "{} is disabled on this game version.", featureName);
	return false;
}

void XivAlexander::RuntimeConfigRepository::DecideVersionSensitiveFeatures(VersionSensitiveFeaturesDecision decision) {
	switch (decision) {
		case VersionSensitiveFeaturesDecision::Keep:
			m_versionSensitiveFeaturesAllowedTemporarily = -1;
			if (IsCurrentGameVersionAllowed())
				OnVersionSensitiveFeaturesAllowedChange();
			else
				AllowCurrentGameVersion();
			break;

		case VersionSensitiveFeaturesDecision::KeepTemporarily:
			m_versionSensitiveFeaturesAllowedTemporarily = 1;
			OnVersionSensitiveFeaturesAllowedChange();
			break;

		case VersionSensitiveFeaturesDecision::DisableTemporarily:
			m_versionSensitiveFeaturesAllowedTemporarily = 0;
			OnVersionSensitiveFeaturesAllowedChange();
			break;

		case VersionSensitiveFeaturesDecision::Disable: {
			Launch.UseLoginSessionSwitching = false;
			Modding.Enabled = false;
			Opcodes.EnabledPatchCodes = std::vector<std::string>();
			Modding.UseAltCodecMusicSupport = false;
			GameWindow.UseImeModeIndicator = false;
			CrowdFix.Enabled = false;
			Audio.OutputSamplingRate = 48000U;
			Audio.SoxrResampler.Enabled = false;
			FontReplacement.Enabled = false;

			m_versionSensitiveFeaturesAllowedTemporarily = -1;
			if (IsCurrentGameVersionAllowed())
				OnVersionSensitiveFeaturesAllowedChange();
			else
				AllowCurrentGameVersion();
			break;
		}
	}
}

std::wstring XivAlexander::RuntimeConfigRepository::GetRegionNameLocalized(xivres::game_publisher gameRegion) const {
	return GetStringRes(RegionResourceIdMap.at(gameRegion));
}

std::vector<std::pair<WORD, std::string>> XivAlexander::RuntimeConfigRepository::GetDisplayLanguagePriorities() const {
	std::vector<std::pair<WORD, std::string>> res;
	if (Ui.Language != Language::SystemDefault) {
		wchar_t buf[64];
		LCIDToLocaleName(LanguageIdMap.at(Ui.Language), &buf[0], 64, 0);
		res.emplace_back(LanguageIdMap.at(Ui.Language), xivres::util::unicode::convert<std::string>(buf));
	}
	try {
		ULONG num = 0, bufSize = 0;
		if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, nullptr, &bufSize))
			throw Utils::Win32::Error("GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, nullptr, &bufSize)");
		std::wstring buf(bufSize, L'\0');
		if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, buf.data(), &bufSize))
			throw Utils::Win32::Error("GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, &buf[0], &bufSize)");
		buf.resize(bufSize);
		auto ptr = buf.data();
		while (*ptr) {
			const auto len = wcslen(ptr);
			res.emplace_back(LANGIDFROMLCID(LocaleNameToLCID(ptr, 0)), xivres::util::unicode::convert<std::string>(ptr));
			ptr += len + 1;
		}
	} catch (...) {
		// pass
	}
	for (const auto& [language, languageId] : LanguageIdMap) {
		if (language == Language::SystemDefault || language == Ui.Language)
			continue;
		wchar_t buf[64];
		LCIDToLocaleName(languageId, &buf[0], 64, 0);
		res.emplace_back(languageId, xivres::util::unicode::convert<std::string>(buf));
	}
	return res;
}

namespace {
	// The shortest interval within this of the least delay wins: a higher framerate is worth more than 1 ms of a multi-second cooldown.
	constexpr uint64_t PreferHigherFramerateWithinUs = 1000;

	// Slack (i - cooldown % i, 1 to i) is how late the seeing frame comes; the game's summed frame times run short, so an exact hit misses.
	// A frame may come up to the deviation early, so with less slack than that a miss costs another interval.
	uint64_t LockedCooldownDelayUs(uint64_t slackUs, uint64_t intervalUs, uint64_t maximumRenderIntervalDeviation) {
		return slackUs < maximumRenderIntervalDeviation ? slackUs + intervalUs : slackUs;
	}
}

uint64_t XivAlexander::RuntimeConfigRepository::CalculateLockFramerateIntervalUs(double fromFps, double toFps, uint64_t gcdUs, uint64_t renderIntervalDeviation) {
	fromFps = std::min(1000000., std::max(1., fromFps));
	toFps = std::min(1000000., std::max(1., toFps));
	const auto first = static_cast<uint64_t>(1000000. / toFps);
	const auto last = std::max(first, static_cast<uint64_t>(1000000. / fromFps));

	// In a block of equal frames = gcd / i, delay grows with i except a drop where slack reaches the deviation, so only the block's shortest
	// interval and its shortest with enough slack are candidates; usually a few dozen blocks (at most ~2 * sqrt(gcd)).
	const auto findInBlocks = [&](auto&& fn) -> std::optional<uint64_t> {
		for (auto lo = first; lo <= last;) {
			const auto frames = gcdUs / lo;
			const auto hi = frames ? std::min(last, gcdUs / frames) : last;
			const auto k = frames + 1;
			const auto delay = [&](uint64_t i) { return LockedCooldownDelayUs(k * i - gcdUs, i, renderIntervalDeviation); };
			if (const auto found = fn(lo, delay(lo)))
				return found;
			if (const auto enough = std::max(lo, (gcdUs + renderIntervalDeviation + k - 1) / k); enough <= hi) {
				if (const auto found = fn(enough, delay(enough)))
					return found;
			}
			lo = hi + 1;
		}
		return std::nullopt;
	};

	auto least = UINT64_MAX;
	findInBlocks([&least](uint64_t, uint64_t delay) {
		least = std::min(least, delay);
		return std::optional<uint64_t>();
	});
	return findInBlocks([least](uint64_t i, uint64_t delay) {
		return delay < least + PreferHigherFramerateWithinUs ? std::optional(i) : std::nullopt;
	}).value_or(first);
}

std::pair<uint64_t, uint64_t> XivAlexander::RuntimeConfigRepository::EstimateLockedCooldownUs(uint64_t cooldownUs, uint64_t intervalUs, uint64_t maximumRenderIntervalDeviation) {
	// Frames stay on their grid, so deviation doesn't accumulate: at worst the frame that should see the end misses it and the next one does.
	const auto shortest = cooldownUs + (intervalUs - cooldownUs % intervalUs);
	return {shortest, cooldownUs + LockedCooldownDelayUs(shortest - cooldownUs, intervalUs, maximumRenderIntervalDeviation)};
}

[[nodiscard]] const std::map<std::string, std::string>& XivAlexander::RuntimeConfigRepository::GetMusicDirectoryPurchaseWebsites(std::string name) const {
	static std::map<std::string, std::string> empty;
	const auto it = m_musicDirectoryPurchaseWebsites.find(name);
	if (it == m_musicDirectoryPurchaseWebsites.end())
		return empty;
	return it->second;
}
