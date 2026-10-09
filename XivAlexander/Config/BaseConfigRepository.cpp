#include "pch.h"
#include "Config.h"

#include "Misc/Logger.h"
#include "Utils/Win32/Process.h"
#include "resource.h"

XivAlexander::BaseConfigRepository::BaseConfigRepository(__in_opt const Config* pConfig, std::filesystem::path path, std::string parentKey)
	: ConfigNode(this)
	, m_pConfig(pConfig)
	, m_sConfigPath(std::move(path))
	, m_parentKey(std::move(parentKey))
	, m_logger(Misc::Logger::Acquire()) {}

XivAlexander::BaseConfigRepository::~BaseConfigRepository() = default;

void XivAlexander::ConfigNode::LoadItemsFrom(const nlohmann::json& data) {
	for (const auto& item : m_items)
		item->LoadFrom(data);
}

void XivAlexander::ConfigNode::SaveItemsTo(nlohmann::json& data) const {
	for (const auto& item : m_items)
		item->SaveTo(data);
}

XivAlexander::ConfigItemBase::ConfigItemBase(ConfigNode* pParent, const char* pszName)
	: Name(pszName)
	, m_pParent(pParent)
	, m_pBaseRepository(pParent->m_pRepository) {
	pParent->m_items.push_back(this);
}

void XivAlexander::ConfigItemBase::TriggerOnChange() {
	OnChange();
	m_pParent->OnItemChange();
}

XivAlexander::ConfigGroup::ConfigGroup(ConfigNode* pParent, const char* pszKey)
	: ConfigItemBase(pParent, pszKey)
	, ConfigNode(pParent->m_pRepository) {}

bool XivAlexander::ConfigGroup::LoadFrom(const nlohmann::json& data) {
	const auto it = data.find(Name);
	if (it == data.end() || !it->is_object())
		return false;

	const auto batch = Batch();
	LoadItemsFrom(*it);
	return false;
}

void XivAlexander::ConfigGroup::SaveTo(nlohmann::json& data) const {
	auto& target = data[Name];
	if (!target.is_object())
		target = nlohmann::json::object();
	SaveItemsTo(target);
}

void XivAlexander::ConfigGroup::OnItemChange() {
	if (m_batchDepth)
		m_changedInBatch = true;
	else
		TriggerOnChange();
}

xivres::util::on_dtor XivAlexander::ConfigGroup::Batch() {
	auto suppressSave = std::make_shared<xivres::util::on_dtor>(m_pBaseRepository->WithSuppressSave());
	m_batchDepth++;
	return {
		[this, suppressSave = std::move(suppressSave)] {
			if (!--m_batchDepth && std::exchange(m_changedInBatch, false))
				TriggerOnChange();
			suppressSave->clear();
		}
	};
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

	auto& currentConfig = m_parentKey.empty() ? totalConfig : totalConfig[FindParentKey(totalConfig)];
	Migrate(currentConfig);

	const auto suppressSave = WithSuppressSave();
	LoadItemsFrom(currentConfig);
}

void XivAlexander::BaseConfigRepository::MoveKey(nlohmann::json& config, const char* from, const ConfigItemBase& item) {
	if (!config.is_object())
		return;

	const auto it = config.find(from);
	if (it == config.end())
		return;

	auto value = std::move(*it);
	config.erase(it);

	std::vector<const char*> path{item.Name};
	for (auto group = item.m_pParent->AsItem(); group; group = group->m_pParent->AsItem())
		path.push_back(group->Name);

	auto target = &config;
	for (auto key = path.rbegin(); key != path.rend() - 1; ++key) {
		target = &(*target)[*key];
		if (target->is_null())
			*target = nlohmann::json::object();
		else if (!target->is_object())
			return;
	}
	if (!target->contains(item.Name))
		(*target)[item.Name] = std::move(value);
}

std::string XivAlexander::BaseConfigRepository::FindParentKey(const nlohmann::json& totalConfig) const {
	if (!totalConfig.is_object())
		return m_parentKey;

	const auto wanted = xivres::util::unicode::convert<std::wstring>(m_parentKey);
	for (const auto& [key, value] : totalConfig.items()) {
		const auto candidate = xivres::util::unicode::convert<std::wstring>(key);
		if (CompareStringOrdinal(candidate.c_str(), static_cast<int>(candidate.size()), wanted.c_str(), static_cast<int>(wanted.size()), TRUE) == CSTR_EQUAL)
			return key;
	}
	return m_parentKey;
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

	nlohmann::json& currentConfig = m_parentKey.empty() ? totalConfig : totalConfig[FindParentKey(totalConfig)];
	Migrate(currentConfig);
	SaveItemsTo(currentConfig);

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
