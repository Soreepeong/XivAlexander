#include "pch.h"
#include "Sqex_ThirdParty_TexTools.h"
#include "Sqex.h"

using namespace std::string_literals;

template<typename T>
T JsonValueOrDefault(const nlohmann::json& json, const char* key, T defaultValue, T nullDefaultValue) {
	if (const auto it = json.find(key); it != json.end()) {
		if (it->is_null())
			return nullDefaultValue;
		return it->get<T>();
	}
	return defaultValue;
}

void Sqex::ThirdParty::TexTools::from_json(const nlohmann::json& j, ModPackEntry& p) {
	if (j.is_null())
		return;
	if (!j.is_object())
		throw CorruptDataException("ModPackEntry must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Author = JsonValueOrDefault(j, "Author", ""s, ""s);
	p.Version = JsonValueOrDefault(j, "Version", ""s, ""s);
	p.Url = JsonValueOrDefault(j, "Url", ""s, ""s);
}

void Sqex::ThirdParty::TexTools::from_json(const nlohmann::json& j, ModEntry& p) {
	if (!j.is_object())
		throw CorruptDataException("ModEntry must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Category = JsonValueOrDefault(j, "Category", ""s, ""s);
	p.FullPath = JsonValueOrDefault(j, "FullPath", ""s, ""s);
	p.ModOffset = JsonValueOrDefault(j, "ModOffset", 0ULL, 0ULL);
	p.ModSize = JsonValueOrDefault(j, "ModSize", 0ULL, 0ULL);
	p.DatFile = JsonValueOrDefault(j, "DatFile", ""s, ""s);
	p.IsDefault = JsonValueOrDefault(j, "IsDefault", false, false);
	if (const auto it = j.find("ModPackEntry"); it != j.end() && !it->is_null())
		p.ModPack = it->get<ModPackEntry>();
}

void Sqex::ThirdParty::TexTools::ModPackPage::from_json(const nlohmann::json& j, Option& p) {
	if (!j.is_object())
		throw CorruptDataException("Option must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Description = JsonValueOrDefault(j, "Description ", ""s, ""s);
	p.ImagePath = JsonValueOrDefault(j, "ImagePath", ""s, ""s);
	if (const auto it = j.find("ModsJsons"); it != j.end() && !it->is_null())
		p.ModsJsons = it->get<decltype(p.ModsJsons)>();
	p.GroupName = JsonValueOrDefault(j, "GroupName", ""s, ""s);
	p.SelectionType = JsonValueOrDefault(j, "SelectionType", ""s, ""s);
	p.IsChecked = JsonValueOrDefault(j, "IsChecked", false, false);
}

void Sqex::ThirdParty::TexTools::ModPackPage::from_json(const nlohmann::json& j, ModGroup& p) {
	if (!j.is_object())
		throw CorruptDataException("Option must be an object");

	p.GroupName = JsonValueOrDefault(j, "GroupName", ""s, ""s);
	p.SelectionType = JsonValueOrDefault(j, "SelectionType", ""s, ""s);
	if (const auto it = j.find("OptionList"); it != j.end() && !it->is_null())
		p.OptionList = it->get<decltype(p.OptionList)>();
}

void Sqex::ThirdParty::TexTools::ModPackPage::from_json(const nlohmann::json& j, Page& p) {
	if (!j.is_object())
		throw CorruptDataException("Option must be an object");

	p.PageIndex = JsonValueOrDefault(j, "PageIndex", 0, 0);
	if (const auto it = j.find("ModGroups"); it != j.end() && !it->is_null())
		p.ModGroups = it->get<decltype(p.ModGroups)>();
}

Sqex::ThirdParty::TexTools::TTMPL Sqex::ThirdParty::TexTools::TTMPL::FromStream(const RandomAccessStream& stream) {
	const auto size = stream.StreamSize();
	if (size > 16 * 1024 * 1024)
		throw CorruptDataException("File too big (>16MB).");

	std::string buf(static_cast<size_t>(size), '\0');
	stream.ReadStream(0, &buf[0], buf.size());

	std::istringstream in(buf);
	TTMPL res;
	while (!in.eof()) {
		nlohmann::json j;
		try {
			in >> j;
		} catch (...) {
			if (in.eof())
				break;
		}
		if (j.find("ModOffset") != j.end()) {
			res.SimpleModsList.emplace_back(j.get<ModEntry>());
		} else {
			return j.get<TTMPL>();
		}
	}
	return res;
}

void Sqex::ThirdParty::TexTools::from_json(const nlohmann::json& j, TTMPL& p) {
	if (!j.is_object())
		throw CorruptDataException("TTMPL must be an object");

	p.MinimumFrameworkVersion = JsonValueOrDefault(j, "MinimumFrameworkVersion", ""s, ""s);
	p.FormatVersion = JsonValueOrDefault(j, "FormatVersion", ""s, ""s);
	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Author = JsonValueOrDefault(j, "Author", ""s, ""s);
	p.Version = JsonValueOrDefault(j, "Version", ""s, ""s);
	p.Description = JsonValueOrDefault(j, "Description", ""s, ""s);
	p.Url = JsonValueOrDefault(j, "Url", ""s, ""s);
	if (const auto it = j.find("ModPackPages"); it != j.end() && !it->is_null())
		p.ModPackPages = it->get<decltype(p.ModPackPages)>();
	if (const auto it = j.find("SimpleModsList"); it != j.end() && !it->is_null())
		p.SimpleModsList = it->get<decltype(p.SimpleModsList)>();
}
