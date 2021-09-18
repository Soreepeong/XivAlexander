#include "pch.h"
#include "App_Misc_ExcelTransformConfig.h"

void App::Misc::ExcelTransformConfig::to_json(nlohmann::json& j, const PluralColumns& o) {
	j = nlohmann::json::array({
		o.singularColumnIndex,
		o.pluralColumnIndex,
		o.capitalizedColumnIndex,
		o.languageSpecificColumnIndex,
	});
	for (auto& i : j) {
		if (i.get<uint32_t>() == PluralColumns::Index_NoColumn)
			i = nullptr;
	}
	while (!j.empty() && j.back().is_null())
		j.erase(j.size() - 1);
}

void App::Misc::ExcelTransformConfig::from_json(const nlohmann::json& j, PluralColumns& o) {
	if (!j.is_array())
		throw std::invalid_argument("PluralColumns must be an array");
	if (j.empty())
		return;

	o.singularColumnIndex = j.at(0).is_null() ? PluralColumns::Index_NoColumn : j.at(0).get<uint32_t>();
	if (j.size() >= 2)
		o.pluralColumnIndex = j.at(1).is_null() ? PluralColumns::Index_NoColumn : j.at(1).get<uint32_t>();
	if (j.size() >= 3)
		o.capitalizedColumnIndex = j.at(2).is_null() ? PluralColumns::Index_NoColumn : j.at(2).get<uint32_t>();
	if (j.size() >= 4)
		o.languageSpecificColumnIndex = j.at(3).is_null() ? PluralColumns::Index_NoColumn : j.at(3).get<uint32_t>();
}

void App::Misc::ExcelTransformConfig::to_json(nlohmann::json& j, const TargetGroup& o) {
	j = o.columnIndices;
}

void App::Misc::ExcelTransformConfig::from_json(const nlohmann::json& j, TargetGroup& o) {
	o.columnIndices = j.get<decltype(o.columnIndices)>();
}

void App::Misc::ExcelTransformConfig::to_json(nlohmann::json& j, const Rule& o) {
	j = nlohmann::json::object({
		{"targetGroups", o.targetGroups},
		{"stringPattern", o.stringPattern},
		{"replaceTo", o.replaceTo},
		{"skipIfAllSame", o.skipIfAllSame},
	});
}

void App::Misc::ExcelTransformConfig::from_json(const nlohmann::json& j, Rule& o) {
	o.targetGroups = j.at("targetGroups").get<decltype(o.targetGroups)>();
	o.stringPattern = j.value("stringPattern", decltype(o.stringPattern)());
	o.replaceTo = j.at("replaceTo").get<decltype(o.replaceTo)>();
	o.skipIfAllSame = j.value("skipIfAllSame", true);
}

void App::Misc::ExcelTransformConfig::to_json(nlohmann::json& j, const Config& o) {
	j = nlohmann::json::object({
		{"name", o.name},
		{"description", o.description},
		{"targetLanguage", o.targetLanguage},
		{"sourceLanguages", o.sourceLanguages},
		{"pluralMap", o.pluralMap},
		{"targetGroups", o.targetGroups},
		{"rules", o.rules},
	});
	if (o.sourceLanguages.empty())
		throw std::invalid_argument("sourceLanguages cannot be empty");
	if (o.sourceLanguages.size() > 7)
		throw std::invalid_argument("Only up to 7 sourceLanguages are supported");
	if (std::ranges::find(o.sourceLanguages, Sqex::Language::Unspecified) != o.sourceLanguages.end())
		throw std::invalid_argument("Unspecified language in sourceLanguages is not supported");
}

void App::Misc::ExcelTransformConfig::from_json(const nlohmann::json& j, Config& o) {
	o.name = j.at("name").get<decltype(o.name)>();
	o.description = j.value("description", decltype(o.description)());
	o.targetLanguage = j.at("targetLanguage").get<decltype(o.targetLanguage)>();
	o.sourceLanguages = j.at("sourceLanguages").get<decltype(o.sourceLanguages)>();
	if (const auto it = j.find("pluralMap"); it != j.end()) {
		o.pluralMap.clear();
		for (const auto& pair : it->items()) {
			if (pair.key().starts_with("#"))
				continue;

			o.pluralMap.emplace_back(pair.key(), PluralColumns{});
			from_json(pair.value(), o.pluralMap.back().second);
		}
	}
	o.targetGroups = j.at("targetGroups").get<decltype(o.targetGroups)>();
	o.rules = j.at("rules").get<decltype(o.rules)>();
}
