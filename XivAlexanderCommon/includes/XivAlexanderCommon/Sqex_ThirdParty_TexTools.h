#pragma once

#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

#include "Sqex.h"

namespace Sqex::ThirdParty::TexTools {

	struct ModPackEntry {
		std::string Name;
		std::string Author;
		std::string Version;
		std::string Url;
	};
	void to_json(nlohmann::json&, const ModPackEntry&);
	void from_json(const nlohmann::json&, ModPackEntry&);

	struct ModEntry {
		std::string Name;
		std::string Category;
		std::string FullPath;
		uint64_t ModOffset{};
		uint64_t ModSize{};
		std::string DatFile;
		bool IsDefault{};
		std::optional<ModPackEntry> ModPack;

		std::string ToExpacDatPath() const;
	};
	void to_json(nlohmann::json&, const ModEntry&);
	void from_json(const nlohmann::json&, ModEntry&);

	namespace ModPackPage {
		struct Option {
			std::string Name;
			std::string Description;
			std::string ImagePath;
			std::vector<ModEntry> ModsJsons;
			std::string GroupName;
			std::string SelectionType;
			bool IsChecked;
		};
		void to_json(nlohmann::json&, const Option&);
		void from_json(const nlohmann::json&, Option&);

		struct ModGroup {
			std::string GroupName;
			std::string SelectionType;
			std::vector<Option> OptionList;
		};
		void to_json(nlohmann::json&, const ModGroup&);
		void from_json(const nlohmann::json&, ModGroup&);

		struct Page {
			int PageIndex{};
			std::vector<ModGroup> ModGroups;
		};
		void to_json(nlohmann::json&, const Page&);
		void from_json(const nlohmann::json&, Page&);

	}

	struct TTMPL {
		std::string MinimumFrameworkVersion;
		std::string FormatVersion;
		std::string Name;
		std::string Author;
		std::string Version;
		std::string Description;
		std::string Url;
		std::vector<ModPackPage::Page> ModPackPages;
		std::vector<ModEntry> SimpleModsList;

		static TTMPL FromStream(const RandomAccessStream& stream);
	};
	void to_json(nlohmann::json&, const TTMPL&);
	void from_json(const nlohmann::json&, TTMPL&);
}