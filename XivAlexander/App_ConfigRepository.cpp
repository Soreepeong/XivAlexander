#include "pch.h"
#include "App_ConfigRepository.h"

#include <XivAlexanderCommon/Utils_Win32_Process.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>
#include <XivAlexanderCommon/XivAlex.h>

#include "App_Misc_Logger.h"
#include "DllMain.h"
#include "resource.h"

std::weak_ptr<App::Config> App::Config::s_instance;
const std::map<App::Config::Language, WORD> App::Config::LanguageIdMap{
	{Language::SystemDefault, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)},
	{Language::Japanese, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)},
	{Language::English, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)},
	{Language::Korean, MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)},
};
const std::map<App::Config::GameLanguage, WORD> App::Config::GameLanguageIdMap{
	{GameLanguage::Unspecified, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)},
	{GameLanguage::Japanese, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)},
	{GameLanguage::English, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)},
	{GameLanguage::German, MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)},
	{GameLanguage::French, MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH)},
	{GameLanguage::ChineseSimplified, MAKELANGID(LANG_CHINESE_SIMPLIFIED, SUBLANG_CHINESE_SIMPLIFIED)},
	{GameLanguage::ChineseTraditional, MAKELANGID(LANG_CHINESE_TRADITIONAL, SUBLANG_CHINESE_TRADITIONAL)},
	{GameLanguage::Korean, MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)},
};
const std::map<WORD, int> App::Config::LanguageIdNameResourceIdMap{
	{MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), IDS_LANGUAGE_NAME_UNSPECIFIED},
	{MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN), IDS_LANGUAGE_NAME_JAPANESE},
	{MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), IDS_LANGUAGE_NAME_ENGLISH},
	{MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), IDS_LANGUAGE_NAME_GERMAN},
	{MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), IDS_LANGUAGE_NAME_FRENCH},
	{MAKELANGID(LANG_CHINESE_SIMPLIFIED, SUBLANG_CHINESE_SIMPLIFIED), IDS_LANGUAGE_NAME_CHINESE_SIMPLIFIED},
	{MAKELANGID(LANG_CHINESE_TRADITIONAL, SUBLANG_CHINESE_TRADITIONAL), IDS_LANGUAGE_NAME_CHINESE_TRADITIONAL},
	{MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN), IDS_LANGUAGE_NAME_KOREAN},
};
const std::map<App::Config::GameRegion, int> App::Config::RegionResourceIdMap{
	{GameRegion::Unspecified, IDS_REGION_NAME_UNSPECIFIED},
	{GameRegion::Japan, IDS_REGION_NAME_JAPAN},
	{GameRegion::NorthAmerica, IDS_REGION_NAME_NORTH_AMERICA},
	{GameRegion::Europe, IDS_REGION_NAME_EUROPE},
	{GameRegion::China, IDS_REGION_NAME_CHINA},
	{GameRegion::Korea, IDS_REGION_NAME_KOREA},
};

App::Config::BaseRepository::BaseRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey)
	: m_pConfig(pConfig)
	, m_sConfigPath(std::move(path))
	, m_parentKey(std::move(parentKey))
	, m_logger(Misc::Logger::Acquire()) {
}

App::Config::ItemBase::ItemBase(BaseRepository* pRepository, const char* pszName)
	: m_pszName(pszName)
	, m_pBaseRepository(pRepository) {
	pRepository->m_allItems.push_back(this);
}

const char* App::Config::ItemBase::Name() const {
	return m_pszName;
}

void App::Config::BaseRepository::Reload(bool announceChange) {
	m_loaded = true;

	bool changed = false;
	nlohmann::json totalConfig;
	if (exists(m_sConfigPath)) {
		try {
			std::ifstream in(m_sConfigPath);
			in >> totalConfig;
			if (totalConfig.type() != nlohmann::detail::value_t::object)
				throw std::runtime_error("Root must be an object.");  // TODO: string resource
		} catch (std::exception& e) {
			totalConfig = nlohmann::json::object();
			changed = true;
			m_logger->FormatDefaultLanguage<LogLevel::Warning>(LogCategory::General,
				IDS_ERROR_CONFIGURATION_LOAD,
				e.what());
		}
	} else {
		totalConfig = nlohmann::json::object();
		changed = true;
		m_logger->FormatDefaultLanguage(LogCategory::General, IDS_LOG_NEW_CONFIG, Utils::ToUtf8(m_sConfigPath));
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

WORD App::Config::Runtime::GetLangId() const {
	if (const auto i = LanguageIdMap.find(Language); i != LanguageIdMap.end())
		return i->second;

	return MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
}

LPCWSTR App::Config::Runtime::GetStringRes(UINT uId) const {
	return FindStringResourceEx(Dll::Module(), uId, GetLangId()) + 1;
}

std::wstring App::Config::Runtime::GetLanguageNameLocalized(GameLanguage gameLanguage) const {
	const auto langNameInUserLang = std::wstring(GetStringRes(LanguageIdNameResourceIdMap.at(GameLanguageIdMap.at(gameLanguage))));
	auto langNameInGameLang = std::wstring(FindStringResourceEx(Dll::Module(), LanguageIdNameResourceIdMap.at(GameLanguageIdMap.at(gameLanguage)), GameLanguageIdMap.at(gameLanguage)) + 1);
	if (langNameInUserLang == langNameInGameLang)
		return langNameInGameLang;
	else
		return std::format(L"{} ({})", langNameInUserLang, langNameInGameLang);
}

std::wstring App::Config::Runtime::GetRegionNameLocalized(GameRegion gameRegion) const {
	return GetStringRes(RegionResourceIdMap.at(gameRegion));
}

std::filesystem::path App::Config::InitializationConfig::ResolveConfigStorageDirectoryPath() {
	if (!Loaded())
		Reload();

	std::filesystem::path path;
	if (!static_cast<std::string>(FixedConfigurationFolderPath).empty()){
		path = TranslatePath(FixedConfigurationFolderPath);
	} else {
		PWSTR pszPath;
		const auto result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE | KF_FLAG_INIT, nullptr, &pszPath);
		if (result != S_OK)
			throw std::runtime_error(std::format("Failed to resolve %APPDATA%", _com_error(result).ErrorMessage()));
		path = std::filesystem::path(pszPath) / L"XivAlexander";
		CoTaskMemFree(pszPath);
	}
	if (!is_directory(path)) {
		if (const auto res = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
			res != ERROR_SUCCESS && res != ERROR_ALREADY_EXISTS)
			throw Utils::Win32::Error(res, "SHCreateDirectoryExW");
		if (!is_directory(path))
			throw std::runtime_error(std::format("Path \"{}\" is not a directory", path));
	}
	return path;
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

std::filesystem::path App::Config::TranslatePath(const std::string& s, bool dontTranslateEmpty) {
	if (dontTranslateEmpty && s.empty())
		return "";
	std::wstring buf;
	buf.resize(PATHCCH_MAX_CCH);
	buf.resize(ExpandEnvironmentStringsW(Utils::FromUtf8(s).c_str(), &buf[0], PATHCCH_MAX_CCH));
	if (!buf.empty())
		buf.resize(buf.size() - 1);

	auto path = std::filesystem::path(buf);
	if (path.is_relative())
		path = Dll::Module().PathOf().parent_path() / path;
	return path;
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
		std::ifstream in(m_sConfigPath);
		in >> totalConfig;
		if (totalConfig.type() != nlohmann::detail::value_t::object)
			throw std::runtime_error("Root must be an object.");  // TODO: string resource
	} catch (const std::exception&) {
		totalConfig = nlohmann::json::object();
	}
	
	nlohmann::json& currentConfig = m_parentKey.empty() ? totalConfig : totalConfig[m_parentKey];
	for (const auto& item : m_allItems)
		item->SaveTo(currentConfig);

	try {
		std::ofstream out(m_sConfigPath);
		out << totalConfig.dump(1, '\t');
	} catch (std::exception& e) {
		m_logger->FormatDefaultLanguage<LogLevel::Error>(LogCategory::General, IDS_ERROR_CONFIGURATION_SAVE, e.what());
	}
}

bool App::Config::Item<uint16_t>::LoadFrom(const nlohmann::json & data, bool announceChanged) {
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
		} catch (std::exception& e) {
			m_pBaseRepository->m_logger->FormatDefaultLanguage(LogCategory::General, IDS_ERROR_CONFIGURATION_PARSE_VALUE, strVal, e.what());
		}
		if (announceChanged)
			this->operator=(newValue);
		else
			Assign(newValue);
	}
	return false;
}

void App::Config::Item<uint16_t>::SaveTo(nlohmann::json & data) const {
	data[Name()] = std::format("0x{:04x}", m_value);
}

bool App::Config::Item<App::Config::Language>::LoadFrom(const nlohmann::json & data, bool announceChanged) {
	if (const auto it = data.find(Name()); it != data.end()) {
		auto newValueString = Utils::FromUtf8(it->get<std::string>());
		CharLowerW(&newValueString[0]);

		auto newValue = Language::SystemDefault;
		if (!newValueString.empty()) {
			if (newValueString.substr(0, std::min<size_t>(7, newValueString.size())) == L"english")
				newValue = Language::English;
			else if (newValueString.substr(0, std::min<size_t>(6, newValueString.size())) == L"korean")
				newValue = Language::Korean;
			else if (newValueString.substr(0, std::min<size_t>(8, newValueString.size())) == L"japanese")
				newValue = Language::Japanese;
		}

		if (announceChanged)
			this->operator=(newValue);
		else
			Assign(newValue);
	}
	return false;
}

void App::Config::Item<App::Config::Language>::SaveTo(nlohmann::json & data) const {
	if (m_value == Language::SystemDefault)
		data[Name()] = "SystemDefault";
	else if (m_value == Language::English)
		data[Name()] = "English";
	else if (m_value == Language::Korean)
		data[Name()] = "Korean";
	else if (m_value == Language::Japanese)
		data[Name()] = "Japanese";
}

bool App::Config::Item<App::Config::HighLatencyMitigationMode>::LoadFrom(const nlohmann::json & data, bool announceChanged) {
	if (const auto it = data.find(Name()); it != data.end()) {
		auto newValueString = Utils::FromUtf8(it->get<std::string>());
		CharLowerW(&newValueString[0]);

		auto newValue = HighLatencyMitigationMode::SimulateNormalizedRttAndLatency;
		if (!newValueString.empty()) {
			if (newValueString.substr(0, std::min<size_t>(31, newValueString.size())) == L"subtractnormalizedrttandlatency")
				newValue = HighLatencyMitigationMode::SimulateNormalizedRttAndLatency;
			else if (newValueString.substr(0, std::min<size_t>(11, newValueString.size())) == L"simulatertt")
				newValue = HighLatencyMitigationMode::SimulateRtt;
			else if (newValueString.substr(0, std::min<size_t>(16, newValueString.size())) == L"subtractlatency")
				newValue = HighLatencyMitigationMode::SubtractLatency;
		}

		if (announceChanged)
			this->operator=(newValue);
		else
			Assign(newValue);
	}
	return false;
}

void App::Config::Item<App::Config::HighLatencyMitigationMode>::SaveTo(nlohmann::json & data) const {
	if (m_value == HighLatencyMitigationMode::SimulateNormalizedRttAndLatency)
		data[Name()] = "SimulateNormalizedRttAndLatency";
	else if (m_value == HighLatencyMitigationMode::SimulateRtt)
		data[Name()] = "SimulateRtt";
	else if (m_value == HighLatencyMitigationMode::SubtractLatency)
		data[Name()] = "SubtractLatency";
}

bool App::Config::Item<App::Config::GameLanguage>::LoadFrom(const nlohmann::json & data, bool announceChanged) {
	if (const auto it = data.find(Name()); it != data.end()) {
		auto newValueString = Utils::FromUtf8(it->get<std::string>());
		CharLowerW(&newValueString[0]);

		auto newValue = GameLanguage::Unspecified;
		if (!newValueString.empty()) {
			if (newValueString.substr(0, std::min<size_t>(8, newValueString.size())) == L"japanese")
				newValue = GameLanguage::Japanese;
			else if (newValueString.substr(0, std::min<size_t>(7, newValueString.size())) == L"english")
				newValue = GameLanguage::English;
			else if (newValueString.substr(0, std::min<size_t>(6, newValueString.size())) == L"german")
				newValue = GameLanguage::German;
			else if (newValueString.substr(0, std::min<size_t>(8, newValueString.size())) == L"deutsche")
				newValue = GameLanguage::German;
			else if (newValueString.substr(0, std::min<size_t>(6, newValueString.size())) == L"french")
				newValue = GameLanguage::French;
			else if (newValueString.substr(0, std::min<size_t>(17, newValueString.size())) == L"chinesesimplified")
				newValue = GameLanguage::ChineseSimplified;
			else if (newValueString.substr(0, std::min<size_t>(18, newValueString.size())) == L"chinesetraditional")
				newValue = GameLanguage::ChineseTraditional;
			else if (newValueString.substr(0, std::min<size_t>(6, newValueString.size())) == L"korean")
				newValue = GameLanguage::Korean;
		}

		if (announceChanged)
			this->operator=(newValue);
		else
			Assign(newValue);
	}
	return false;
}

void App::Config::Item<App::Config::GameLanguage>::SaveTo(nlohmann::json & data) const {
	if (m_value == GameLanguage::Unspecified)
		data[Name()] = "Unspecified";
	else if (m_value == GameLanguage::Japanese)
		data[Name()] = "Japanese";
	else if (m_value == GameLanguage::English)
		data[Name()] = "English";
	else if (m_value == GameLanguage::German)
		data[Name()] = "German";
	else if (m_value == GameLanguage::French)
		data[Name()] = "French";
	else if (m_value == GameLanguage::ChineseSimplified)
		data[Name()] = "ChineseSimplified";
	else if (m_value == GameLanguage::ChineseTraditional)
		data[Name()] = "ChineseTraditional";
	else if (m_value == GameLanguage::Korean)
		data[Name()] = "Korean";
}

template<typename T>
bool App::Config::Item<T>::LoadFrom(const nlohmann::json & data, bool announceChanged) {
	if (auto i = data.find(Name()); i != data.end()) {
		const auto newValue = i->get<T>();
		if (announceChanged)
			this->operator=(newValue);
		else
			Assign(newValue);
	}
	return false;
}

template<typename T>
void App::Config::Item<T>::SaveTo(nlohmann::json & data) const {
	data[Name()] = m_value;
}
