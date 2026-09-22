#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace XivAlexander {
	struct ChoicesProfile {
		std::string Name;
		std::string FileName;
		bool Active = true;

		bool operator==(const ChoicesProfile& r) const;
		bool operator!=(const ChoicesProfile& r) const { return !operator==(r); }
	};

	void to_json(nlohmann::json&, const ChoicesProfile&);
	void from_json(const nlohmann::json&, ChoicesProfile&);
}
