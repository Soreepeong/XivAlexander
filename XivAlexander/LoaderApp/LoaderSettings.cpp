#include "pch.h"
#include "LoaderSettings.h"

namespace {
	constexpr auto FileName = L"loader.json";

	nlohmann::json ReadObject(const std::filesystem::path& path) {
		try {
			if (std::error_code ec; !is_regular_file(path, ec))
				return nlohmann::json::object();
			auto json = Utils::ParseJsonFromFile(path);
			if (json.is_object())
				return json;
		} catch (...) {
			// unreadable; start over
		}
		return nlohmann::json::object();
	}

	template<typename T>
	std::optional<T> FindInRuntimeConfig(const std::filesystem::path& configDir, const char* name) {
		// Sections are keyed by game; the first one that has the value decides.
		const auto json = ReadObject(configDir / L"config.runtime.json");
		for (const auto& [_, section] : json.items()) {
			if (!section.is_object())
				continue;
			try {
				if (const auto ui = section.find("Ui"); ui != section.end() && ui->is_object()) {
					if (const auto it = ui->find(name); it != ui->end() && it->is_string())
						return it->get<T>();
				}
				if (const auto it = section.find(name); it != section.end() && it->is_string())
					return it->get<T>();
			} catch (...) {
				// not a valid value
			}
		}
		return std::nullopt;
	}
}

XivAlexander::LoaderApp::LoaderSettings XivAlexander::LoaderApp::LoaderSettings::Load(const std::filesystem::path& configDir) {
	LoaderSettings settings;
	const auto json = ReadObject(configDir / FileName);
	try {
		if (const auto it = json.find("Language"); it != json.end() && it->is_string())
			settings.UiLanguage = it->get<Language>();
		if (const auto it = json.find("ThemeMode"); it != json.end() && it->is_string())
			settings.UiThemeMode = it->get<ThemeMode>();
		if (const auto it = json.find("Advanced"); it != json.end() && it->is_boolean())
			settings.Advanced = it->get<bool>();
		if (const auto it = json.find("AddedPaths"); it != json.end() && it->is_array()) {
			for (const auto& item : *it) {
				if (item.is_string())
					settings.AddedPaths.emplace_back(item.get<std::filesystem::path>());
			}
		}
	} catch (...) {
		// keep what was read
	}
	return settings;
}

void XivAlexander::LoaderApp::LoaderSettings::Save(const std::filesystem::path& configDir) const {
	const auto path = configDir / FileName;
	auto json = ReadObject(path);
	if (UiLanguage)
		json["Language"] = *UiLanguage;
	else
		json.erase("Language");
	if (UiThemeMode)
		json["ThemeMode"] = *UiThemeMode;
	else
		json.erase("ThemeMode");
	json["Advanced"] = Advanced;
	json["AddedPaths"] = AddedPaths;
	create_directories(configDir);
	Utils::SaveJsonToFile(path, json);
}

XivAlexander::Language XivAlexander::LoaderApp::LoaderSettings::EffectiveLanguage(const std::filesystem::path& configDir) const {
	if (UiLanguage)
		return *UiLanguage;
	return FindInRuntimeConfig<Language>(configDir, "Language").value_or(Language::SystemDefault);
}

XivAlexander::ThemeMode XivAlexander::LoaderApp::LoaderSettings::EffectiveThemeMode(const std::filesystem::path& configDir) const {
	if (UiThemeMode)
		return *UiThemeMode;
	return FindInRuntimeConfig<ThemeMode>(configDir, "ThemeMode").value_or(ThemeMode::System);
}

WORD XivAlexander::LoaderApp::LangIdOf(Language language) {
	switch (language) {
		case Language::English: return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
		case Language::Korean: return MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN);
		case Language::Japanese: return MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN);
		default: return MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
	}
}
