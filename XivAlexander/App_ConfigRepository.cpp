#include "pch.h"
#include "App_ConfigRepository.h"

#include <XivAlexanderCommon/Sqex_Sound_MusicImporter.h>
#include <XivAlexanderCommon/Utils_Win32_Process.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>

#include "App_Misc_GameInstallationDetector.h"
#include "App_Misc_Logger.h"
#include "DllMain.h"
#include "resource.h"

std::weak_ptr<App::Config> App::Config::s_instance;
static const std::map<App::Language, WORD> LanguageIdMap{
	{App::Language::SystemDefault, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)},
	{App::Language::Japanese, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)},
	{App::Language::English, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)},
	{App::Language::Korean, MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)},
};
static const std::map<Sqex::Language, WORD> GameLanguageIdMap{
	{Sqex::Language::Unspecified, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)},
	{Sqex::Language::Japanese, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)},
	{Sqex::Language::English, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)},
	{Sqex::Language::German, MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)},
	{Sqex::Language::French, MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH)},
	{Sqex::Language::ChineseSimplified, MAKELANGID(LANG_CHINESE_SIMPLIFIED, SUBLANG_CHINESE_SIMPLIFIED)},
	{Sqex::Language::ChineseTraditional, MAKELANGID(LANG_CHINESE_TRADITIONAL, SUBLANG_CHINESE_TRADITIONAL)},
	{Sqex::Language::Korean, MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)},
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
static const std::map<Sqex::Region, int> RegionResourceIdMap{
	{Sqex::Region::Unspecified, IDS_REGION_NAME_UNSPECIFIED},
	{Sqex::Region::Japan, IDS_REGION_NAME_JAPAN},
	{Sqex::Region::NorthAmerica, IDS_REGION_NAME_NORTH_AMERICA},
	{Sqex::Region::Europe, IDS_REGION_NAME_EUROPE},
	{Sqex::Region::China, IDS_REGION_NAME_CHINA},
	{Sqex::Region::Korea, IDS_REGION_NAME_KOREA},
};

App::Config::BaseRepository::BaseRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey)
	: m_pConfig(pConfig)
	, m_sConfigPath(std::move(path))
	, m_parentKey(std::move(parentKey))
	, m_logger(Misc::Logger::Acquire()) {
}

App::Config::BaseRepository::~BaseRepository() = default;

App::Config::ItemBase::ItemBase(BaseRepository* pRepository, const char* pszName)
	: m_pszName(pszName)
	, m_pBaseRepository(pRepository) {
	pRepository->m_allItems.push_back(this);
}

void App::Config::BaseRepository::Reload(const std::filesystem::path& from, bool announceChange) {
	m_loaded = true;

	bool changed = false;
	nlohmann::json totalConfig;
	if (exists(from.empty() ? m_sConfigPath : from)) {
		try {
			totalConfig = Utils::ParseJsonFromFile(from.empty() ? m_sConfigPath : from);
			if (totalConfig.type() != nlohmann::detail::value_t::object)
				throw std::runtime_error("Root must be an object.");  // TODO: string resource
		} catch (const std::exception& e) {
			totalConfig = nlohmann::json::object();
			changed = true;
			m_logger->FormatDefaultLanguage<LogLevel::Warning>(LogCategory::General,
				IDS_ERROR_CONFIGURATION_LOAD,
				e.what());
		}
	} else {
		totalConfig = nlohmann::json::object();
		changed = true;
		m_logger->FormatDefaultLanguage(LogCategory::General, IDS_LOG_NEW_CONFIG, Utils::ToUtf8((from.empty() ? m_sConfigPath : from).wstring()));
	}

	const auto& currentConfig = m_parentKey.empty() ? totalConfig : totalConfig[m_parentKey];

	m_destructionCallbacks.clear();
	for (const auto& item : m_allItems) {
		changed |= item->LoadFrom(currentConfig, announceChange);
		m_destructionCallbacks.push_back(item->OnChangeListenerAlsoOnLoad([this](ItemBase&) { Save(); }));
	}

	if (changed)
		Save();
}

class App::Config::ConfigCreator : public Config {
public:
	ConfigCreator(std::filesystem::path initializationConfigPath)
		: Config(std::move(initializationConfigPath)) {
	}

	~ConfigCreator() override = default;
};

std::shared_ptr<App::Config> App::Config::Acquire() {
	auto r = s_instance.lock();
	if (!r) {
		static std::mutex mtx;
		std::lock_guard lock(mtx);

		r = s_instance.lock();
		if (!r) {
			const auto dllDir = Dll::Module().PathOf().parent_path();
			s_instance = r = std::make_shared<ConfigCreator>(dllDir / "config.xivalexinit.json");
		}
	}
	return r;
}

App::Config::RuntimeRepository::RuntimeRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey)
	: BaseRepository(pConfig, std::move(path), std::move(parentKey)) {
	m_cleanup += Language.OnChangeListenerAlsoOnLoad([&](auto&) {
		Utils::Win32::Error::SetDefaultLanguageId(GetLangId());
	});
	m_cleanup += MusicImportConfig.OnChangeListener([&](auto&) {
		std::set<std::string> newKeys;
		m_musicDirectoryPurchaseWebsites.clear();
		for (const auto& path : MusicImportConfig.Value()) {
			try {
				const auto importConfig = Utils::ParseJsonFromFile(TranslatePath(path)).get<Sqex::Sound::MusicImportConfig>();
				for (const auto& [dirName, dirInfo] : importConfig.searchDirectories) {
					m_musicDirectoryPurchaseWebsites[dirName].insert(dirInfo.purchaseLinks.begin(), dirInfo.purchaseLinks.end());
					if (!MusicImportConfig_Directories.Value().contains(dirName))
						newKeys.insert(dirName);
				}
			} catch (...) {
				// pass
			}
		}
		if (!newKeys.empty()) {
			auto newValue = MusicImportConfig_Directories.Value();
			for (const auto& newKey : newKeys)
				newValue[newKey].clear();
			MusicImportConfig_Directories = newValue;
		}
	});
}

App::Config::RuntimeRepository::~RuntimeRepository() {
	m_cleanup.Clear();
}

void App::Config::RuntimeRepository::Reload(const std::filesystem::path& from, bool announceChange) {
	BaseRepository::Reload(from, announceChange);
	Utils::Win32::Error::SetDefaultLanguageId(GetLangId());
}

WORD App::Config::RuntimeRepository::GetLangId() const {
	if (const auto i = LanguageIdMap.find(Language); i != LanguageIdMap.end())
		return i->second;

	return MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
}

LPCWSTR App::Config::RuntimeRepository::GetStringRes(UINT uId) const {
	return FindStringResourceEx(Dll::Module(), uId, GetLangId()) + 1;
}

std::wstring App::Config::RuntimeRepository::GetLanguageNameLocalized(Sqex::Language gameLanguage) const {
	const auto langNameInUserLang = std::wstring(GetStringRes(LanguageIdNameResourceIdMap.at(GameLanguageIdMap.at(gameLanguage))));
	auto langNameInGameLang = std::wstring(FindStringResourceEx(Dll::Module(), LanguageIdNameResourceIdMap.at(GameLanguageIdMap.at(gameLanguage)), GameLanguageIdMap.at(gameLanguage)) + 1);
	if (langNameInUserLang == langNameInGameLang)
		return langNameInGameLang;
	else
		return std::format(L"{} ({})", langNameInUserLang, langNameInGameLang);
}

std::wstring App::Config::RuntimeRepository::GetRegionNameLocalized(Sqex::Region gameRegion) const {
	return GetStringRes(RegionResourceIdMap.at(gameRegion));
}

std::vector<std::pair<WORD, std::string>> App::Config::RuntimeRepository::GetDisplayLanguagePriorities() const {
	std::vector<std::pair<WORD, std::string>> res;
	if (Language != Language::SystemDefault) {
		wchar_t buf[64];
		LCIDToLocaleName(LanguageIdMap.at(Language), &buf[0], 64, 0);
		res.emplace_back(LanguageIdMap.at(Language), Utils::ToUtf8(buf));
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
			res.emplace_back(LANGIDFROMLCID(LocaleNameToLCID(ptr, 0)), Utils::ToUtf8(ptr));
			ptr += len + 1;
		}
	} catch(...) {
		// pass
	}
	for (const auto& [language, languageId] : LanguageIdMap) {
		if (language == Language::SystemDefault || language == Language)
			continue;
		wchar_t buf[64];
		LCIDToLocaleName(languageId, &buf[0], 64, 0);
		res.emplace_back(languageId, Utils::ToUtf8(buf));
	}
	return res;
}

std::vector<Sqex::Language> App::Config::RuntimeRepository::GetFallbackLanguageList() const {
	std::vector<Sqex::Language> result;
	for (const auto lang : FallbackLanguagePriority.Value()) {
		if (std::ranges::find(result, lang) == result.end() && lang != Sqex::Language::ChineseTraditional && lang != Sqex::Language::Unspecified)
			result.push_back(lang);
	}
	for (const auto lang : {
			Sqex::Language::Japanese,
			Sqex::Language::English,
			Sqex::Language::German,
			Sqex::Language::French,
			Sqex::Language::ChineseSimplified,
			Sqex::Language::Korean,
		}) {
		if (std::ranges::find(result, lang) == result.end())
			result.push_back(lang);
	}
	return result;
}

[[nodiscard]] const std::map<std::string, std::string>& App::Config::RuntimeRepository::GetMusicDirectoryPurchaseWebsites(std::string name) const {
	static std::map<std::string, std::string> empty;
	const auto it = m_musicDirectoryPurchaseWebsites.find(name);
	if (it == m_musicDirectoryPurchaseWebsites.end())
		return empty;
	return it->second;
}

std::filesystem::path App::Config::InitRepository::ResolveConfigStorageDirectoryPath() {
	if (!Loaded())
		Reload({});

	if (!FixedConfigurationFolderPath.Value().empty())
		return Utils::Win32::EnsureDirectory(TranslatePath(FixedConfigurationFolderPath.Value()));
	else
		return Utils::Win32::EnsureDirectory(Utils::Win32::EnsureKnownFolderPath(FOLDERID_RoamingAppData) / L"XivAlexander");
}

std::filesystem::path App::Config::InitRepository::ResolveXivAlexInstallationPath() {
	if (!Loaded())
		Reload({});

	if (!XivAlexFolderPath.Value().empty())
		return Utils::Win32::EnsureDirectory(TranslatePath(XivAlexFolderPath.Value()));
	else
		return Utils::Win32::EnsureDirectory(Utils::Win32::EnsureKnownFolderPath(FOLDERID_LocalAppData) / L"XivAlexander");
}

std::filesystem::path App::Config::InitRepository::ResolveRuntimeConfigPath() {
	return ResolveConfigStorageDirectoryPath() / "config.runtime.json";
}

std::filesystem::path App::Config::InitRepository::ResolveGameOpcodeConfigPath() {
	const auto gameReleaseInfo = Misc::GameInstallationDetector::GetGameReleaseInfo();
	return ResolveConfigStorageDirectoryPath() / std::format(L"game.{}.{}.json", gameReleaseInfo.CountryCode, gameReleaseInfo.PathSafeGameVersion);
}

std::filesystem::path App::Config::TranslatePath(const std::filesystem::path& path, const std::filesystem::path& relativeTo) {
	return Utils::Win32::TranslatePath(path, relativeTo.empty() ? Dll::Module().PathOf().parent_path() : relativeTo);
}

App::Config::Config(std::filesystem::path initializationConfigPath)
	: Init(this, std::move(initializationConfigPath), "")
	, Runtime(this, Init.ResolveRuntimeConfigPath(), Utils::ToUtf8(Utils::Win32::Process::Current().PathOf().wstring()))
	, Game(this, Init.ResolveGameOpcodeConfigPath(), "") {
	Runtime.Reload({});
	Game.Reload({});
}

App::Config::~Config() = default;

void App::Config::SuppressSave(bool suppress) {
	m_bSuppressSave = suppress;
}

void App::Config::BaseRepository::Save(const std::filesystem::path& to) {
	if (to.empty() && (!m_pConfig || m_pConfig->m_bSuppressSave))
		return;

	nlohmann::json totalConfig;
	try {
		totalConfig = Utils::ParseJsonFromFile(to.empty() ? m_sConfigPath : to);
		if (totalConfig.type() != nlohmann::detail::value_t::object)
			throw std::runtime_error("Root must be an object.");  // TODO: string resource
	} catch (const std::exception&) {
		totalConfig = nlohmann::json::object();
	}

	nlohmann::json& currentConfig = m_parentKey.empty() ? totalConfig : totalConfig[m_parentKey];
	for (const auto& item : m_allItems)
		item->SaveTo(currentConfig);

	try {
		Utils::SaveJsonToFile(to.empty() ? m_sConfigPath : to, totalConfig);
	} catch (const std::exception& e) {
		m_logger->FormatDefaultLanguage<LogLevel::Error>(LogCategory::General, IDS_ERROR_CONFIGURATION_SAVE, e.what());
	}
}

bool App::Config::Item<uint16_t>::LoadFrom(const nlohmann::json& data, bool announceChanged) {
	if (const auto it = data.find(Name()); it != data.end()) {
		uint16_t newValue;
		std::string strVal;
		try {
			strVal = it->get<std::string>();
			if (it->is_string())
				newValue = static_cast<uint16_t>(std::stoi(it->get<std::string>(), nullptr, 0));
			else if (it->is_number_integer())
				newValue = it->get<uint16_t>();
			else
				return false;
		} catch (const std::exception& e) {
			m_pBaseRepository->m_logger->FormatDefaultLanguage(LogCategory::General, IDS_ERROR_CONFIGURATION_PARSE_VALUE, strVal, e.what());
		}
		if (announceChanged)
			this->operator=(newValue);
		else {
			Assign(newValue);
			AnnounceChanged(true);
		}
	}
	return false;
}

void App::Config::Item<uint16_t>::SaveTo(nlohmann::json& data) const {
	data[Name()] = std::format("0x{:04x}", m_value);
}

void App::to_json(nlohmann::json& j, const Language& value) {
	switch (value) {
		case Language::English:
			j = "English";
			break;

		case Language::Korean:
			j = "Korean";
			break;

		case Language::Japanese:
			j = "Japanese";
			break;

		case Language::SystemDefault:
		default:
			j = "SystemDefault";
	}
}

void App::from_json(const nlohmann::json& it, Language& value) {
	auto newValueString = Utils::FromUtf8(it.get<std::string>());
	CharLowerW(&newValueString[0]);

	value = Language::SystemDefault;
	if (newValueString.empty())
		return;

	if (newValueString.substr(0, std::min<size_t>(7, newValueString.size())) == L"english")
		value = Language::English;
	else if (newValueString.substr(0, std::min<size_t>(6, newValueString.size())) == L"korean")
		value = Language::Korean;
	else if (newValueString.substr(0, std::min<size_t>(8, newValueString.size())) == L"japanese")
		value = Language::Japanese;
}

void App::to_json(nlohmann::json& j, const HighLatencyMitigationMode& value) {
	switch (value) {
		case HighLatencyMitigationMode::SubtractLatency:
			j = "SubtractLatency";
			break;

		case HighLatencyMitigationMode::SimulateRtt:
			j = "SimulateRtt";
			break;

		case HighLatencyMitigationMode::SimulateNormalizedRttAndLatency:
		default:
			j = "SimulateNormalizedRttAndLatency";
	}
}

void App::from_json(const nlohmann::json& it, HighLatencyMitigationMode& value) {
	auto newValueString = Utils::FromUtf8(it.get<std::string>());
	CharLowerW(&newValueString[0]);

	value = HighLatencyMitigationMode::SimulateNormalizedRttAndLatency;
	if (newValueString.empty())
		return;

	if (newValueString.substr(0, std::min<size_t>(31, newValueString.size())) == L"subtractnormalizedrttandlatency")
		value = HighLatencyMitigationMode::SimulateNormalizedRttAndLatency;
	else if (newValueString.substr(0, std::min<size_t>(11, newValueString.size())) == L"simulatertt")
		value = HighLatencyMitigationMode::SimulateRtt;
	else if (newValueString.substr(0, std::min<size_t>(16, newValueString.size())) == L"subtractlatency")
		value = HighLatencyMitigationMode::SubtractLatency;
}

template<typename T>
bool App::Config::Item<T>::LoadFrom(const nlohmann::json& data, bool announceChanged) {
	if (const auto it = data.find(Name()); it != data.end()) {
		T newValue;
		try {
			newValue = it->template get<T>();
		} catch (...) {
			// do nothing for now
			// TODO: show how the value is invalid
#ifdef _DEBUG
			throw;
#endif
		}
		if (announceChanged)
			this->operator=(newValue);
		else {
			Assign(newValue);
			AnnounceChanged(true);
		}
	}
	return false;
}

template<typename T>
void App::Config::Item<T>::SaveTo(nlohmann::json& data) const {
	data[Name()] = m_value;
}
