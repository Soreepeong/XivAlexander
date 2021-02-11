#include "pch.h"
#include "App_ConfigRepository.h"

App::ConfigRepository::ConfigRepository()
	: m_sGamePath(GetGamePath())
	, m_sConfigPath(GetConfigPath()) {
}

App::ConfigItemBase::ConfigItemBase(ConfigRepository* pRepository, const char* pszName)
	: m_pszName(pszName) {
	pRepository->m_allItems.push_back(this);
}

const char* App::ConfigItemBase::Name() const {
	return m_pszName;
}

void App::ConfigRepository::Reload(bool announceChange) {
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

	m_destructionCallbacks.clear();

	bool changed = false;
	for (auto& item : m_allItems) {
		changed |= item->LoadFrom(specificConfig, announceChange);
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

std::string App::ConfigRepository::GetGamePath() {
	wchar_t path[MAX_PATH];
	GetModuleFileName(nullptr, path, MAX_PATH);
	return Utils::ToUtf8(path);
}

std::wstring App::ConfigRepository::GetConfigPath() {
	wchar_t configPath[MAX_PATH];
	GetModuleFileName(g_hInstance, configPath, MAX_PATH);
	wcsncat_s(configPath, L".json", _countof(configPath));
	return configPath;
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


bool App::ConfigItem<uint16_t>::LoadFrom(const nlohmann::json& data, bool announceChanged) {
	auto i = data.find(Name());
	if (i != data.end()) {
		uint16_t newValue;
		try {
			if (i->is_string())
				newValue = static_cast<uint16_t>(std::stoi(i->get<std::string>(), nullptr, 0));
			else if (i->is_number_integer())
				newValue = i->get<uint16_t>();
			else
				return false;
		} catch (std::exception& e) {
			App::Misc::Logger::GetLogger().Format("Config value parse error: %s", e.what());
		}
		if (announceChanged)
			this->operator=(newValue);
		else
			Assign(newValue);
	}
	return false;
}

void App::ConfigItem<uint16_t>::SaveTo(nlohmann::json& data) const {
	data[Name()] = Utils::FormatString("0x%04x", m_value);
}

template<typename T>
bool App::ConfigItem<T>::LoadFrom(const nlohmann::json& data, bool announceChanged) {
	auto i = data.find(Name());
	if (i != data.end()) {
		const auto newValue = i->get<T>();
		if (announceChanged)
			this->operator=(newValue);
		else
			Assign(newValue);
	}
	return false;
}

template<typename T>
void App::ConfigItem<T>::SaveTo(nlohmann::json& data) const {
	data[Name()] = m_value;
}
