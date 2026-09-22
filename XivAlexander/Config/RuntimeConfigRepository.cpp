#include "pch.h"
#include "Config.h"

#include "Utils/Win32/Process.h"
#include "Utils/Win32/Resource.h"
#include "resource.h"
#include "XivAlexander.h"

#include "Misc/GameInstallationDetector.h"

static const std::map<XivAlexander::Language, WORD> LanguageIdMap{
	{XivAlexander::Language::SystemDefault, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)},
	{XivAlexander::Language::Japanese, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)},
	{XivAlexander::Language::English, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)},
	{XivAlexander::Language::Korean, MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)},
};

static const std::map<xivres::game_language, WORD> GameLanguageIdMap{
	{xivres::game_language::Unspecified, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)},
	{xivres::game_language::Japanese, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)},
	{xivres::game_language::English, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)},
	{xivres::game_language::German, MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)},
	{xivres::game_language::French, MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH)},
	{xivres::game_language::ChineseSimplified, MAKELANGID(LANG_CHINESE_SIMPLIFIED, SUBLANG_CHINESE_SIMPLIFIED)},
	{xivres::game_language::ChineseTraditional, MAKELANGID(LANG_CHINESE_TRADITIONAL, SUBLANG_CHINESE_TRADITIONAL)},
	{xivres::game_language::Korean, MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)},
};

static const std::map<WORD, int> LanguageIdNameResourceIdMap{
	{MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), IDS_LANGUAGE_NAME_UNSPECIFIED},
	{MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN), IDS_LANGUAGE_NAME_JAPANESE},
	{MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), IDS_LANGUAGE_NAME_ENGLISH},
	{MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), IDS_LANGUAGE_NAME_GERMAN},
	{MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), IDS_LANGUAGE_NAME_FRENCH},
	{MAKELANGID(LANG_CHINESE_SIMPLIFIED, SUBLANG_CHINESE_SIMPLIFIED), IDS_LANGUAGE_NAME_CHINESE_SIMPLIFIED},
	{MAKELANGID(LANG_CHINESE_TRADITIONAL, SUBLANG_CHINESE_TRADITIONAL), IDS_LANGUAGE_NAME_CHINESE_TRADITIONAL},
	{MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN), IDS_LANGUAGE_NAME_KOREAN},
};

static const std::map<xivres::game_publisher, int> RegionResourceIdMap{
	{xivres::game_publisher::Unspecified, IDS_REGION_NAME_UNSPECIFIED},
	{xivres::game_publisher::SquareEnixJapan, IDS_REGION_NAME_JAPAN},
	{xivres::game_publisher::SquareEnixAmerica, IDS_REGION_NAME_NORTH_AMERICA},
	{xivres::game_publisher::SquareEnixEurope, IDS_REGION_NAME_EUROPE},
	{xivres::game_publisher::ShandaGames, IDS_REGION_NAME_CHINA},
	{xivres::game_publisher::ActozSoft, IDS_REGION_NAME_KOREA},
};


XivAlexander::RuntimeConfigRepository::RuntimeConfigRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey)
	: BaseConfigRepository(pConfig, std::move(path), std::move(parentKey)) {
	m_cleanup += Language.AddAndCallOnChange([&] {
		Utils::Win32::Error::SetDefaultLanguageId(GetLangId());
	});

	m_cleanup += SynchronizeProcessing.AddAndCallOnChange([&] { UseMainThreadTimingHandler = SynchronizeProcessing || LockFramerateAutomatic || LockFramerateInterval; });
	m_cleanup += LockFramerateAutomatic.AddAndCallOnChange([&] { UseMainThreadTimingHandler = SynchronizeProcessing || LockFramerateAutomatic || LockFramerateInterval; });
	m_cleanup += LockFramerateInterval.AddAndCallOnChange([&] { UseMainThreadTimingHandler = SynchronizeProcessing || LockFramerateAutomatic || LockFramerateInterval; });
}

XivAlexander::RuntimeConfigRepository::~RuntimeConfigRepository() {
	m_cleanup.clear();
}

void XivAlexander::RuntimeConfigRepository::Reload(const std::filesystem::path& from) {
	BaseConfigRepository::Reload(from);
	Utils::Win32::Error::SetDefaultLanguageId(GetLangId());
}

WORD XivAlexander::RuntimeConfigRepository::GetLangId() const {
	if (const auto i = LanguageIdMap.find(Language); i != LanguageIdMap.end())
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
	for (const auto lang : FallbackLanguagePriority.Value()) {
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

std::wstring XivAlexander::RuntimeConfigRepository::GetRegionNameLocalized(xivres::game_publisher gameRegion) const {
	return GetStringRes(RegionResourceIdMap.at(gameRegion));
}

std::vector<std::pair<WORD, std::string>> XivAlexander::RuntimeConfigRepository::GetDisplayLanguagePriorities() const {
	std::vector<std::pair<WORD, std::string>> res;
	if (Language != Language::SystemDefault) {
		wchar_t buf[64];
		LCIDToLocaleName(LanguageIdMap.at(Language), &buf[0], 64, 0);
		res.emplace_back(LanguageIdMap.at(Language), xivres::util::unicode::convert<std::string>(buf));
	}
	try {
		ULONG num = 0, bufSize = 0;
		if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, nullptr, &bufSize))
			throw Utils::Win32::Error("GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, nullptr, &bufSize)");
		std::wstring buf(bufSize, L'\0');
		if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, &buf[0], &bufSize))
			throw Utils::Win32::Error("GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, &buf[0], &bufSize)");
		buf.resize(bufSize);
		auto ptr = &buf[0];
		while (*ptr) {
			const auto len = wcslen(ptr);
			res.emplace_back(LANGIDFROMLCID(LocaleNameToLCID(ptr, 0)), xivres::util::unicode::convert<std::string>(ptr));
			ptr += len + 1;
		}
	} catch (...) {
		// pass
	}
	for (const auto& [language, languageId] : LanguageIdMap) {
		if (language == Language::SystemDefault || language == Language)
			continue;
		wchar_t buf[64];
		LCIDToLocaleName(languageId, &buf[0], 64, 0);
		res.emplace_back(languageId, xivres::util::unicode::convert<std::string>(buf));
	}
	return res;
}

uint64_t XivAlexander::RuntimeConfigRepository::CalculateLockFramerateIntervalUs(double fromFps, double toFps, uint64_t gcdUs, uint64_t renderIntervalDeviation) {
	static double prevFromFps{}, prevToFps{};
	static uint64_t prevGcdUs{}, prevRenderIntervalDeviation{};
	static uint64_t prevResult{};
	if (prevFromFps == fromFps && prevToFps == toFps && prevGcdUs == gcdUs && prevRenderIntervalDeviation == renderIntervalDeviation) {
		return prevResult;
	}

	fromFps = std::min(1000000., std::max(1., fromFps));
	toFps = std::min(1000000., std::max(1., toFps));
	auto minInterval = static_cast<uint64_t>(1000000. / toFps);
	for (auto i = minInterval + 1, i_ = static_cast<uint64_t>(1000000. / fromFps); i <= i_; ++i) {
		if (i - gcdUs % i < minInterval - gcdUs % minInterval && i - gcdUs % i >= renderIntervalDeviation) {
			minInterval = i;
		}
	}
	prevFromFps = fromFps;
	prevToFps = toFps;
	prevGcdUs = gcdUs;
	prevRenderIntervalDeviation = renderIntervalDeviation;
	prevResult = minInterval;
	return minInterval;
}

[[nodiscard]] const std::map<std::string, std::string>& XivAlexander::RuntimeConfigRepository::GetMusicDirectoryPurchaseWebsites(std::string name) const {
	static std::map<std::string, std::string> empty;
	const auto it = m_musicDirectoryPurchaseWebsites.find(name);
	if (it == m_musicDirectoryPurchaseWebsites.end())
		return empty;
	return it->second;
}
