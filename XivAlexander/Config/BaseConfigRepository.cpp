#include "pch.h"
#include "Config.h"

#include "Misc/Logger.h"
#include "Utils/Win32/Process.h"
#include "resource.h"

XivAlexander::BaseConfigRepository::BaseConfigRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey)
	: m_pConfig(pConfig)
	, m_sConfigPath(std::move(path))
	, m_parentKey(std::move(parentKey))
	, m_logger(Misc::Logger::Acquire()) {}

XivAlexander::BaseConfigRepository::~BaseConfigRepository() = default;

XivAlexander::ConfigItemBase::ConfigItemBase(BaseConfigRepository* pRepository, const char* pszName)
	: Name(pszName)
	, m_pBaseRepository(pRepository) {
	pRepository->m_allItems.push_back(this);
}

void XivAlexander::ConfigItemBase::TriggerOnChange() {
	OnChange();
}

xivres::util::on_dtor XivAlexander::ConfigItemBase::AddAndCallOnChange(std::function<void()> cb, std::function<void()> onUnbind) {
	auto r = OnChange(cb, std::move(onUnbind));
	cb();
	return r;
}

void XivAlexander::BaseConfigRepository::Reload(const std::filesystem::path& from) {
	m_loaded = true;

	nlohmann::json totalConfig;
	if (exists(from.empty() ? m_sConfigPath : from)) {
		try {
			totalConfig = Utils::ParseJsonFromFile(from.empty() ? m_sConfigPath : from);
			if (totalConfig.type() != nlohmann::detail::value_t::object)
				throw std::runtime_error("Root must be an object.");  // TODO: string resource
		} catch (const std::exception& e) {
			totalConfig = nlohmann::json::object();
			m_logger->FormatDefaultLanguage<LogLevel::Warning>(LogCategory::General,
				IDS_ERROR_CONFIGURATION_LOAD,
				e.what());
		}
	} else {
		totalConfig = nlohmann::json::object();
		m_logger->FormatDefaultLanguage(LogCategory::General, IDS_LOG_NEW_CONFIG, xivres::util::unicode::convert<std::string>((from.empty() ? m_sConfigPath : from).wstring()));
	}

	const auto& currentConfig = m_parentKey.empty() ? totalConfig : totalConfig[m_parentKey];

	const auto suppressSave = WithSuppressSave();
	for (const auto& item : m_allItems)
		item->LoadFrom(currentConfig);
}

xivres::util::on_dtor XivAlexander::BaseConfigRepository::WithSuppressSave() {
	const auto _ = std::lock_guard(m_suppressSave.Mtx);
	m_suppressSave.SupressionCounter += 1;

	return {
		[this] {
			{
				const auto _ = std::lock_guard(m_suppressSave.Mtx);
				m_suppressSave.SupressionCounter -= 1;
				if (m_suppressSave.SupressionCounter || !m_suppressSave.PendingSave)
					return;
			}

			Save();
		}
	};
}

void XivAlexander::BaseConfigRepository::Save(const std::filesystem::path& to) {
	if (m_suppressSave.SupressionCounter) {
		m_suppressSave.PendingSave = true;
		return;
	}

	const auto& targetPath = to.empty() ? m_sConfigPath : to;
	if (targetPath.empty())
		return;

	nlohmann::json totalConfig;
	try {
		totalConfig = Utils::ParseJsonFromFile(targetPath);
		if (totalConfig.type() != nlohmann::detail::value_t::object)
			throw std::runtime_error("Root must be an object.");  // TODO: string resource
	} catch (const std::exception&) {
		totalConfig = nlohmann::json::object();
	}

	nlohmann::json& currentConfig = m_parentKey.empty() ? totalConfig : totalConfig[m_parentKey];
	for (const auto& item : m_allItems)
		item->SaveTo(currentConfig);

	try {
		Utils::SaveJsonToFile(targetPath, totalConfig);
	} catch (const std::exception& e) {
		m_logger->FormatDefaultLanguage<LogLevel::Error>(LogCategory::General, IDS_ERROR_CONFIGURATION_SAVE, e.what());
	}
}

template<>
bool XivAlexander::ConfigItem<uint16_t>::LoadFrom(const nlohmann::json& data) {
	if (const auto it = data.find(Name); it != data.end()) {
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

		*this = newValue;
	}
	return false;
}

template<>
void XivAlexander::ConfigItem<uint16_t>::SaveTo(nlohmann::json& data) const {
	data[Name] = std::format("0x{:04x}", m_value);
}
