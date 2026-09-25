#include "pch.h"
#include "MainApp/Modding/PathRewriter.h"

#include "MainApp/Modding/VirtualSqPacks.h"
#include "Config.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	PathRewriter::PathRewriter(std::filesystem::path gamePath, const std::optional<VirtualSqPacks>& sqpacks)
		: m_config(Config::Acquire())
		, m_sqpacks(sqpacks)
		, m_speakers(std::move(gamePath)) {}

	PathRewriter::~PathRewriter() = default;

	std::string PathRewriter::DescribeSource(const std::string& path) const {
		return m_sqpacks ? m_sqpacks->DescribeEntrySource(xivres::path_spec(path)) : std::string();
	}

	std::string PathRewriter::Rewrite(const std::string& original, std::string& description) const {
		auto name = original;
		std::string replacedFrom;
		for (const auto& rule : m_config->Runtime.PathReplacements.Value()) {
			auto replaced = srell::regex_replace(name, rule.Regex(), rule.To);
			if (replaced == name)
				continue;

			if (replacedFrom.empty())
				replacedFrom = name;
			name = std::move(replaced);
			if (rule.Stop)
				break;
		}

		if (name.ends_with(".scd")) {
			if (auto spoken = m_speakers.ForceLanguageForSpeaker(name); !spoken.empty() && spoken != name) {
				if (replacedFrom.empty())
					replacedFrom = name;
				name = std::move(spoken);
			}
		}

		std::string ext, rest;
		if (const auto i1 = name.find_first_of('.'); i1 != std::string::npos) {
			ext = name.substr(i1);
			name.resize(i1);
			if (const auto i2 = ext.find_first_of('.', 1); i2 != std::string::npos) {
				rest = ext.substr(i2);
				ext.resize(i2);
			}
		}

		const auto nameLower = [&name] {
			auto val = xivres::util::unicode::convert<std::wstring>(name);
			CharLowerW(val.data());
			return xivres::util::unicode::convert<std::string>(val);
		}();

		const auto extLower = [&ext] {
			auto val = xivres::util::unicode::convert<std::wstring>(ext);
			CharLowerW(val.data());
			return xivres::util::unicode::convert<std::string>(val);
		}();

		auto overrideLanguage = xivres::game_language::Unspecified;
		if (extLower == ".scd") {
			if (nameLower.starts_with("cut/") || nameLower.starts_with("sound/voice/vo_line"))
				overrideLanguage = m_config->Runtime.VoiceResourceLanguageOverride;
		} else {
			overrideLanguage = m_config->Runtime.ResourceLanguageOverride;
		}

		if (overrideLanguage != xivres::game_language::Unspecified) {
			static constexpr xivres::game_language AllLanguages[]{
				xivres::game_language::Japanese, xivres::game_language::English,
				xivres::game_language::German, xivres::game_language::French,
				xivres::game_language::ChineseSimplified, xivres::game_language::ChineseTraditional,
				xivres::game_language::Korean, xivres::game_language::TraditionalChinese,
			};
			const auto targetLanguageCode = xivres::game_language_code(overrideLanguage);

			std::string newName;
			if (nameLower.starts_with("ui/uld/logo")) {
				// do nothing, as overriding this often freezes the game
			} else {
				for (const auto lang : AllLanguages) {
					const auto languageCode = xivres::game_language_code(lang);
					char t[16];
					sprintf_s(t, "_%s", languageCode);
					if (nameLower.ends_with(t)) {
						newName = name.substr(0, name.size() - strlen(languageCode)) + targetLanguageCode;
						break;
					}
					sprintf_s(t, "/%s/", languageCode);
					if (const auto pos = nameLower.find(t); pos != std::string::npos) {
						newName = std::format("{}/{}/{}", name.substr(0, pos), targetLanguageCode, name.substr(pos + strlen(t)));
						break;
					}
					sprintf_s(t, "_%s_", languageCode);
					if (const auto pos = nameLower.find(t); pos != std::string::npos) {
						newName = std::format("{}_{}_{}", name.substr(0, pos), targetLanguageCode, name.substr(pos + strlen(t)));
						break;
					}
				}
			}
			if (!newName.empty() && name != newName && m_sqpacks && m_sqpacks->EntryExists(std::format("{}{}", newName, ext))) {
				auto newStr = std::format("{}{}{}", newName, ext, rest);
				description = std::format("{} => {}", original, newStr);
				return newStr;
			}
		}

		if (!replacedFrom.empty()) {
			auto newStr = std::format("{}{}{}", name, ext, rest);
			if (newStr != original) {
				if (m_sqpacks && rest.empty()) {
					if (auto standIn = m_sqpacks->ReserveCrossSqpack(original, newStr); !standIn.empty()) {
						description = std::format("{} => {} (as {})", replacedFrom, newStr, standIn);
						return standIn;
					}
				}

				description = std::format("{} => {}", replacedFrom, newStr);
				return newStr;
			}
		}

		if (m_sqpacks) {
			auto resolved = std::format("{}{}{}", name, ext, rest);
			if (auto rsv = m_sqpacks->FindFutureReservationFor(resolved); !rsv.empty()) {
				description = std::format("{} => {}", replacedFrom.empty() ? original : replacedFrom, rsv);
				return rsv;
			}
		}

		return {};
	}
}
