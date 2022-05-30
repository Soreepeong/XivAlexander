#pragma once

#include <nlohmann/json_fwd.hpp>
#include <XivAlexanderCommon/Sqex.h>

namespace XivAlexander::Misc::ExcelTransformConfig {

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

	struct ReplacementTemplate {
		std::string from;
		std::string to;
		bool icase = true;
	};

	void to_json(nlohmann::json& j, const ReplacementTemplate& o);
	void from_json(const nlohmann::json& j, ReplacementTemplate& o);

	struct IgnoredCell {
		std::string name;
		int id;
		int column;
		std::optional<Sqex::Language> forceLanguage;
		std::optional<std::string> forceString;

		bool operator<(const IgnoredCell& r) const {
			if (const auto eq = _stricmp(name.c_str(), r.name.c_str()); eq != 0)
				return eq < 0;
			if (id != r.id)
				return id < r.id;
			return column < r.column;
		}

		bool operator==(const IgnoredCell& r) const {
			return 0 == _stricmp(name.c_str(), r.name.c_str()) && id == r.id && column == r.column;
		}

		bool operator>(const IgnoredCell& r) const {
			return !operator<(r);
		}
	};

	void to_json(nlohmann::json& j, const IgnoredCell& o);
	void from_json(const nlohmann::json& j, IgnoredCell& o);

	struct Rule {
		std::vector<std::string> targetGroups;
		std::string stringPattern;
		std::string replaceTo;
		bool skipIfAllSame = true;
		std::map<Sqex::Language, std::vector<std::string>> preprocessReplacements;
		std::vector<std::string> postprocessReplacements;
	};

	void to_json(nlohmann::json& j, const Rule& o);
	void from_json(const nlohmann::json& j, Rule& o);

	struct Config {
		std::string name;
		std::string description;
		Sqex::Language targetLanguage{};
		std::vector<Sqex::Language> sourceLanguages;
		std::vector<std::pair<std::string, std::map<Sqex::Language, std::vector<size_t>>>> columnMap;
		std::vector<std::pair<std::string, PluralColumns>> pluralMap;
		std::map<std::string, TargetGroup> targetGroups;
		std::map<std::string, ReplacementTemplate> replacementTemplates;
		std::vector<IgnoredCell> ignoredCells;
		std::vector<Rule> rules;
	};

	void to_json(nlohmann::json& j, const Config& o);
	void from_json(const nlohmann::json& j, Config& o);
}
