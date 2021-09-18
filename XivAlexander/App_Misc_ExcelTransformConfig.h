#pragma once

#include <nlohmann/json_fwd.hpp>

#include "XivAlexanderCommon/Sqex.h"

namespace App::Misc::ExcelTransformConfig {

	struct PluralColumns {
		static constexpr uint32_t Index_NoColumn = UINT32_MAX;

		uint32_t singularColumnIndex = Index_NoColumn;
		uint32_t pluralColumnIndex = Index_NoColumn;
		uint32_t capitalizedColumnIndex = Index_NoColumn;
		uint32_t languageSpecificColumnIndex = Index_NoColumn;
	};

	void to_json(nlohmann::json& j, const PluralColumns& o);
	void from_json(const nlohmann::json& j, PluralColumns& o);

	struct TargetGroup {
		std::map<std::string, std::vector<size_t>> columnIndices;
	};

	void to_json(nlohmann::json& j, const TargetGroup& o);
	void from_json(const nlohmann::json& j, TargetGroup& o);

	struct Rule {
		std::vector<std::string> targetGroups;
		std::string stringPattern;
		std::string replaceTo;
		bool skipIfAllSame = true;
	};

	void to_json(nlohmann::json& j, const Rule& o);
	void from_json(const nlohmann::json& j, Rule& o);

	struct Config {
		std::string name;
		std::string description;
		Sqex::Language targetLanguage;
		std::vector<Sqex::Language> sourceLanguages;
		std::vector<std::pair<std::string, PluralColumns>> pluralMap;
		std::map<std::string, TargetGroup> targetGroups;
		std::vector<Rule> rules;
	};

	void to_json(nlohmann::json& j, const Config& o);
	void from_json(const nlohmann::json& j, Config& o);
}
