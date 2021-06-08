#include "pch.h"
#include "App_ConfigRepository.h"

std::unique_ptr<App::Config> App::Config::s_pInstance;

App::Config::BaseRepository::BaseRepository(Config* pConfig, Utils::WinPath path)
	: m_pConfig(pConfig)
	, m_sConfigPath(std::move(path)) {
}

App::Config::ItemBase::ItemBase(BaseRepository* pRepository, const char* pszName)
	: m_pszName(pszName) {
	pRepository->m_allItems.push_back(this);
}

const char* App::Config::ItemBase::Name() const {
	return m_pszName;
}

void App::Config::BaseRepository::Reload(bool announceChange) {
	bool changed = false;
	nlohmann::json config;
	if (m_sConfigPath.Exists()) {
		try {
			std::ifstream in(m_sConfigPath.wstr());
			in >> config;
		} catch (std::exception& e) {
			Misc::Logger::GetLogger().Format<LogLevel::Warning>(LogCategory::General, "Failed to load configuration file: %s", e.what());
		}
	} else {
		changed = true;
		Misc::Logger::GetLogger().Format(LogCategory::General, "Creating new config file: %s", m_sConfigPath.wbuf());
	}

	m_destructionCallbacks.clear();
	for (auto& item : m_allItems) {
		changed |= item->LoadFrom(config, announceChange);
		m_destructionCallbacks.push_back(item->OnChangeListener([this](ItemBase& item) { Save(); }));
	}

	if (changed)
		Save();
}

App::Config& App::Config::Instance() {
	if (!s_pInstance) {
		const auto dllDir = Utils::WinPath(g_hInstance).RemoveComponentInplace();
		const auto regionAndVersion = Utils::ResolveGameReleaseRegion();
		
		s_pInstance = std::make_unique<Config>(
			Utils::WinPath(dllDir, "config.runtime.json"),
			Utils::WinPath(dllDir, Utils::FormatString(L"game.%s.%s.json",
				std::get<0>(regionAndVersion).c_str(),
				std::get<1>(regionAndVersion).c_str())
			));
	}
	return *s_pInstance;
}

void App::Config::DestroyInstance() {
	s_pInstance = nullptr;
}


App::Config::Config(std::wstring runtimeConfigPath, std::wstring gameInfoPath)
	: Runtime(this, std::move(runtimeConfigPath))
	, Game(this, std::move(gameInfoPath)) {
	Runtime.Reload();
	Game.Reload();
}

App::Config::~Config() = default;

void App::Config::SetQuitting() {
	m_bSuppressSave = true;
}

void App::Config::BaseRepository::Save() {
	if (m_pConfig->m_bSuppressSave)
		return;

	nlohmann::json config;
	for (auto& item : m_allItems)
		item->SaveTo(config);

	try {
		std::ofstream out(m_sConfigPath.wstr());
		out << config.dump(1, '\t');
	} catch (std::exception& e) {
		Misc::Logger::GetLogger().Format<LogLevel::Error>(LogCategory::General, "JSON Config save error: %s", e.what());
	}
}

bool App::Config::Item<uint16_t>::LoadFrom(const nlohmann::json & data, bool announceChanged) {
	if (auto i = data.find(Name()); i != data.end()) {
		uint16_t newValue;
		try {
			if (i->is_string())
				newValue = static_cast<uint16_t>(std::stoi(i->get<std::string>(), nullptr, 0));
			else if (i->is_number_integer())
				newValue = i->get<uint16_t>();
			else
				return false;
		} catch (std::exception& e) {
			Misc::Logger::GetLogger().Format(LogCategory::General, "Config value parse error: %s", e.what());
		}
		if (announceChanged)
			this->operator=(newValue);
		else
			Assign(newValue);
	}
	return false;
}

void App::Config::Item<uint16_t>::SaveTo(nlohmann::json & data) const {
	data[Name()] = Utils::FormatString("0x%04x", m_value);
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
