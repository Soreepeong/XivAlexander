#include "pch.h"
#include "App_ConfigRepository.h"

static
std::wstring GetConfigurationPath() {
	wchar_t configPath[MAX_PATH];
	GetModuleFileName(g_hInstance, configPath, MAX_PATH);
	wcsncat_s(configPath, L".json", _countof(configPath));
	return configPath;
}

static
std::string GetGamePath(){
	wchar_t path[MAX_PATH];
	GetModuleFileName(nullptr, path, MAX_PATH);
	return Utils::ToUtf8(path);
}

App::ConfigRepository::ConfigRepository()
	: m_sGamePath(GetGamePath())
	, m_sConfigPath(GetConfigurationPath()) {
}

App::ConfigItemBase::ConfigItemBase(ConfigRepository* pRepository, const char* pszName)
	: m_pszName(pszName) {
	pRepository->m_allItems.push_back(this);
}

const char* App::ConfigItemBase::Name() const {
	return m_pszName;
}

void App::ConfigRepository::Reload() {
	nlohmann::json config;
	try {
		std::ifstream in(m_sConfigPath);
		in >> config;
	} catch (std::exception& e) {
		App::Misc::Logger::GetLogger().Format("JSON Config load error: %s", e.what());
	}

	if (config.find(m_sGamePath) == config.end())
		config[m_sGamePath] = nlohmann::json::object();
	auto& specificConfig = config[m_sGamePath];

	bool changed = false;
	for (auto& item : m_allItems) {
		changed |= item->LoadFrom(specificConfig);
		m_destructionCallbacks.push_back(item->OnChangeListener([this](ConfigItemBase& item) {
			Save();
			}));
	}

	if (changed)
		Save();
}

void App::ConfigRepository::SetQuitting() {
	m_bSuppressSave = true;
}

void App::ConfigRepository::Save() {
	if (m_bSuppressSave)
		return;

	nlohmann::json config;
	try {
		std::ifstream in(m_sConfigPath);
		in >> config;
	} catch (std::exception& e) {
		App::Misc::Logger::GetLogger().Format("JSON Config load error: %s", e.what());
	}

	if (config.find(m_sGamePath) == config.end())
		config[m_sGamePath] = nlohmann::json::object();
	auto& specificConfig = config[m_sGamePath];

	for (auto& item : m_allItems) {
		item->SaveTo(specificConfig);
	}
	
	try {
		std::ofstream out(m_sConfigPath);
		out << config.dump(1, '\t');
	} catch (std::exception& e) {
		App::Misc::Logger::GetLogger().Format("JSON Config save error: %s", e.what());
	}
}

std::unique_ptr<App::ConfigRepository> App::ConfigRepository::s_pConfig;
App::ConfigRepository& App::ConfigRepository::Config() {
	if (!s_pConfig) {
		s_pConfig = std::make_unique<App::ConfigRepository>();
		s_pConfig->Reload();
	}
	return *s_pConfig;
}


bool App::ConfigItem<uint16_t>::LoadFrom(const nlohmann::json& data) {
	auto i = data.find(Name());
	if (i != data.end()) {
		try {
			if (i->is_string()) {
				return Assign(static_cast<uint16_t>(std::stoi(i->get<std::string>(), nullptr, 0)));
			} else if (i->is_number_integer())
				return Assign(i->get<uint16_t>());
		} catch (std::exception& e) {
			return false;
		}
	}
	return false;
}

void App::ConfigItem<uint16_t>::SaveTo(nlohmann::json& data) const {
	data[Name()] = Utils::FormatString("0x%04x", m_value);
}

template<typename T>
bool App::ConfigItem<T>::LoadFrom(const nlohmann::json& data) {
	auto i = data.find(Name());
	if (i != data.end()) {
		const auto value = i->get<T>();
		return Assign(value);
	}
	return false;
}

template<typename T>
void App::ConfigItem<T>::SaveTo(nlohmann::json& data) const {
	data[Name()] = m_value;
}
