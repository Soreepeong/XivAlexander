#include "pch.h"
#include "App_ConfigRepository.h"

std::weak_ptr<App::Config> App::Config::s_instance;

App::Config::BaseRepository::BaseRepository(Config* pConfig, std::filesystem::path path)
	: m_pConfig(pConfig)
	, m_sConfigPath(std::move(path))
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
	bool changed = false;
	nlohmann::json config;
	if (exists(m_sConfigPath)) {
		try {
			std::ifstream in(m_sConfigPath);
			in >> config;
		} catch (std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::General, "Failed to load configuration file: {}", e.what());
		}
	} else {
		changed = true;
		m_logger->Format(LogCategory::General, "Creating new config file: {}", Utils::ToUtf8(m_sConfigPath));
	}

	m_destructionCallbacks.clear();
	for (auto& item : m_allItems) {
		changed |= item->LoadFrom(config, announceChange);
		m_destructionCallbacks.push_back(item->OnChangeListener([this](ItemBase& item) { Save(); }));
	}

	if (changed)
		Save();
}

class App::Config::ConfigCreator : public Config {
public:
	ConfigCreator(std::wstring runtimeConfigPath, std::wstring gameInfoPath)
		: Config(std::move(runtimeConfigPath), std::move(gameInfoPath)) {
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
			const auto regionAndVersion = XivAlex::ResolveGameReleaseRegion();
			s_instance = r = std::make_shared<ConfigCreator>(
				dllDir / "config.runtime.json",
				dllDir / std::format(L"game.{}.{}.json",
					std::get<0>(regionAndVersion),
					std::get<1>(regionAndVersion))
			);
		}
	}
	return r;
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
		std::ofstream out(m_sConfigPath);
		out << config.dump(1, '\t');
	} catch (std::exception& e) {
		m_logger->Format<LogLevel::Error>(LogCategory::General, "JSON Config save error: {}", e.what());
	}
}

bool App::Config::Item<uint16_t>::LoadFrom(const nlohmann::json & data, bool announceChanged) {
	if (const auto it = data.find(Name()); it != data.end()) {
		uint16_t newValue;
		try {
			if (it->is_string())
				newValue = static_cast<uint16_t>(std::stoi(it->get<std::string>(), nullptr, 0));
			else if (it->is_number_integer())
				newValue = it->get<uint16_t>();
			else
				return false;
		} catch (std::exception& e) {
			m_pBaseRepository->m_logger->Format(LogCategory::General, "Config value parse error: {}", e.what());
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
