#include "pch.h"
#include "ChoicesProfile.h"

bool XivAlexander::ChoicesProfile::operator==(const ChoicesProfile& r) const {
	return Name == r.Name && FileName == r.FileName && Active == r.Active;
}

void XivAlexander::to_json(nlohmann::json& j, const ChoicesProfile& v) {
	j = nlohmann::json::object({
		{"name", v.Name},
		{"filename", v.FileName},
		{"active", v.Active},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, ChoicesProfile& v) {
	v.FileName = j.at("filename").get<std::string>();
	v.Name = j.value("name", v.FileName);
	v.Active = j.value("active", true);
	if (v.Name.empty())
		v.Name = v.FileName;
}
