#include "pch.h"
#include "App_ConfigRepository.h"

#include <XivAlexanderCommon/Utils_Win32_Process.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>
#include <XivAlexanderCommon/XivAlex.h>

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

void App::Config::BaseRepository::Reload(bool announceChange) {
	m_loaded = true;

	bool changed = false;
	nlohmann::json totalConfig;
	if (exists(m_sConfigPath)) {
		try {
			totalConfig = Utils::ParseJsonFromFile(m_sConfigPath);
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
		m_logger->FormatDefaultLanguage(LogCategory::General, IDS_LOG_NEW_CONFIG, Utils::ToUtf8(m_sConfigPath.wstring()));
	}

	const auto& currentConfig = m_parentKey.empty() ? totalConfig : totalConfig[m_parentKey];

	m_destructionCallbacks.clear();
	for (const auto& item : m_allItems) {
		changed |= item->LoadFrom(currentConfig, announceChange);
		m_destructionCallbacks.push_back(item->OnChangeListener([this](ItemBase&) { Save(); }));
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

App::Config::Runtime::Runtime(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey)
	: BaseRepository(pConfig, std::move(path), std::move(parentKey)) {
	m_cleanup += Language.OnChangeListener([&](auto&) {
		Utils::Win32::Error::SetDefaultLanguageId(GetLangId());
	});
}

App::Config::Runtime::~Runtime() {
	m_cleanup.Clear();
}

void App::Config::Runtime::Reload(bool announceChange) {
	BaseRepository::Reload(announceChange);
	Utils::Win32::Error::SetDefaultLanguageId(GetLangId());
}

WORD App::Config::Runtime::GetLangId() const {
	if (const auto i = LanguageIdMap.find(Language); i != LanguageIdMap.end())
		return i->second;

	return MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
}

LPCWSTR App::Config::Runtime::GetStringRes(UINT uId) const {
	return FindStringResourceEx(Dll::Module(), uId, GetLangId()) + 1;
}

std::wstring App::Config::Runtime::GetLanguageNameLocalized(Sqex::Language gameLanguage) const {
	const auto langNameInUserLang = std::wstring(GetStringRes(LanguageIdNameResourceIdMap.at(GameLanguageIdMap.at(gameLanguage))));
	auto langNameInGameLang = std::wstring(FindStringResourceEx(Dll::Module(), LanguageIdNameResourceIdMap.at(GameLanguageIdMap.at(gameLanguage)), GameLanguageIdMap.at(gameLanguage)) + 1);
	if (langNameInUserLang == langNameInGameLang)
		return langNameInGameLang;
	else
		return std::format(L"{} ({})", langNameInUserLang, langNameInGameLang);
}

std::wstring App::Config::Runtime::GetRegionNameLocalized(Sqex::Region gameRegion) const {
	return GetStringRes(RegionResourceIdMap.at(gameRegion));
}

std::vector<Sqex::Language> App::Config::Runtime::GetFallbackLanguageList() const {
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

std::filesystem::path App::Config::InitializationConfig::ResolveConfigStorageDirectoryPath() {
	if (!Loaded())
		Reload();

	if (!FixedConfigurationFolderPath.Value().empty())
		return EnsureDirectory(TranslatePath(FixedConfigurationFolderPath.Value()));
	else
		return EnsureDirectory(Utils::Win32::EnsureKnownFolderPath(FOLDERID_RoamingAppData) / L"XivAlexander");
}

std::filesystem::path App::Config::InitializationConfig::ResolveXivAlexInstallationPath() {
	if (!Loaded())
		Reload();

	if (!XivAlexFolderPath.Value().empty())
		return EnsureDirectory(TranslatePath(XivAlexFolderPath.Value()));
	else
		return EnsureDirectory(Utils::Win32::EnsureKnownFolderPath(FOLDERID_LocalAppData) / L"XivAlexander");
}

std::filesystem::path App::Config::InitializationConfig::ResolveRuntimeConfigPath() {
	return ResolveConfigStorageDirectoryPath() / "config.runtime.json";
}

std::filesystem::path App::Config::InitializationConfig::ResolveGameOpcodeConfigPath() {
	const auto regionAndVersion = XivAlex::ResolveGameReleaseRegion();
	return ResolveConfigStorageDirectoryPath() / std::format(L"game.{}.{}.json",
		std::get<0>(regionAndVersion),
		std::get<1>(regionAndVersion));
}

std::filesystem::path App::Config::TranslatePath(const std::filesystem::path& path, const std::filesystem::path& relativeTo) {
	if (path.empty())
		return {};
	std::wstring buf;
	buf.resize(PATHCCH_MAX_CCH);
	buf.resize(ExpandEnvironmentStringsW(path.wstring().c_str(), &buf[0], PATHCCH_MAX_CCH));
	if (!buf.empty())
		buf.resize(buf.size() - 1);

	auto pathbuf = std::filesystem::path(buf);
	if (pathbuf.is_relative())
		pathbuf = (relativeTo.empty() ? Dll::Module().PathOf().parent_path() : relativeTo) / pathbuf;
	return pathbuf.lexically_normal();
}

std::filesystem::path App::Config::EnsureDirectory(const std::filesystem::path& path) {
	if (!is_directory(path)) {
		if (const auto res = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
			res != ERROR_SUCCESS && res != ERROR_ALREADY_EXISTS)
			throw Utils::Win32::Error(res, "SHCreateDirectoryExW");
		if (!is_directory(path))
			throw std::runtime_error(std::format("Path \"{}\" is not a directory", path));
	}
	return canonical(path);
}

App::Config::Config(std::filesystem::path initializationConfigPath)
	: Init(this, std::move(initializationConfigPath), "")
	, Runtime(this, Init.ResolveRuntimeConfigPath(), Utils::ToUtf8(Utils::Win32::Process::Current().PathOf().wstring()))
	, Game(this, Init.ResolveGameOpcodeConfigPath(), "") {
	Runtime.Reload();
	Game.Reload();
}

App::Config::~Config() = default;

void App::Config::SetQuitting() {
	m_bSuppressSave = true;
}

void App::Config::BaseRepository::Save() {
	if (m_pConfig && m_pConfig->m_bSuppressSave)
		return;

	nlohmann::json totalConfig;
	try {
		totalConfig = Utils::ParseJsonFromFile(m_sConfigPath);
		if (totalConfig.type() != nlohmann::detail::value_t::object)
			throw std::runtime_error("Root must be an object.");  // TODO: string resource
	} catch (const std::exception&) {
		totalConfig = nlohmann::json::object();
	}

	nlohmann::json& currentConfig = m_parentKey.empty() ? totalConfig : totalConfig[m_parentKey];
	for (const auto& item : m_allItems)
		item->SaveTo(currentConfig);

	try {
		Utils::SaveJsonToFile(m_sConfigPath, totalConfig);
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
		else
			Assign(newValue);
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
		else
			Assign(newValue);
	}
	return false;
}

template<typename T>
void App::Config::Item<T>::SaveTo(nlohmann::json& data) const {
	data[Name()] = m_value;
}
