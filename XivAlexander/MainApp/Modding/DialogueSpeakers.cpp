#include "pch.h"
#include "MainApp/Modding/DialogueSpeakers.h"

#include <xivres/util.unicode.h>

#include "Config.h"
#include "Misc/Logger.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	DialogueSpeakers::DialogueSpeakers(std::filesystem::path gamePath)
		: m_config(Config::Acquire())
		, m_logger(Misc::Logger::Acquire())
		, m_gamePath(std::move(gamePath)) {}

	DialogueSpeakers::~DialogueSpeakers() = default;

	std::pair<std::string, std::string> DialogueSpeakers::ResolveDialogueSheet(const xivres::path_spec& pathSpec) const {
		const auto parts = pathSpec.parts();
		if (parts.size() < 2)
			return {};

		const auto scdName = std::string(parts.back());
		const auto parentDirName = std::string(parts[parts.size() - 2]);
		if (!scdName.starts_with("vo_")
			|| !std::string_view(scdName).substr(3).starts_with(parentDirName)
			|| scdName.size() <= 3 + parentDirName.size()
			|| scdName.at(3 + parentDirName.size()) != '_')
			return {};

		if (parentDirName.starts_with("VOICEMAN_")) {
			if (scdName.size() < 24)
				return {};
			return {
				std::format("cut_scene/{}/voiceman_{}", scdName.substr(12, 3), scdName.substr(12, 5)),
				std::format("TEXT_{}_{}_", parentDirName, scdName.substr(18, 6)),
			};
		}

		if (scdName.size() < 19)
			return {};

		const auto parentDirNameLower = xivres::util::unicode::convert<std::string>(parentDirName, &xivres::util::unicode::lower);
		for (const auto& name : m_excelList->name_to_id_map() | std::views::keys) {
			if (!name.starts_with("quest/"))
				continue;
			if (xivres::util::unicode::convert<std::string>(name, &xivres::util::unicode::lower).find(parentDirNameLower) == std::string::npos)
				continue;

			return {name, std::format("TEXT_{}_{}_", name.substr(10), scdName.substr(13, 6))};
		}
		return {};
	}

	std::string DialogueSpeakers::ForceLanguageForSpeaker(const std::string& path) const {
		if (m_config->Runtime.ForcedCharacterLanguages.Value().empty() && !m_config->Runtime.LogDialogueCharacterNames)
			return {};

		const auto pathSpec = xivres::path_spec(path);
		if (pathSpec.category_id() != 0x03)  // cut
			return {};

		try {
			const auto lock = std::lock_guard(m_mtx);
			if (!m_installation) {
				m_installation.emplace(m_gamePath);
				m_excelList.emplace(*m_installation);
			}

			const auto [sheetName, keyPrefix] = ResolveDialogueSheet(pathSpec);
			if (sheetName.empty())
				return {};

			if (m_dialogueSheetName != sheetName) {
				m_dialogueSheet.emplace(m_installation->get_excel(sheetName));
				m_dialogueSheetName = sheetName;
			}

			const auto keyPrefixUpper = xivres::util::unicode::convert<std::string>(keyPrefix, &xivres::util::unicode::upper);
			for (size_t i = 0; i < m_dialogueSheet->get_exh_reader().get_pages().size(); i++) {
				for (const auto& row : m_dialogueSheet->get_exd_reader(i)) {
					const auto& key = row[0][0].String.escaped();
					if (!key.starts_with(keyPrefixUpper))
						continue;

					const auto speaker = xivres::util::unicode::convert<std::string>(key.substr(keyPrefixUpper.size()), &xivres::util::unicode::lower);
					if (m_config->Runtime.LogDialogueCharacterNames) {
						m_logger->Format(LogCategory::GameResourceOverrider,
							"Dialogue character name={} path={}", speaker, path);
					}

					const auto& forced = m_config->Runtime.ForcedCharacterLanguages.Value();
					auto it = forced.find(speaker);
					if (it == forced.end())
						it = forced.find("");  // anyone unnamed
					if (it == forced.end() || it->second.empty())
						return {};

					const auto scdName = std::string(pathSpec.parts().back());
					return std::format("{}/{}{}.scd",
						pathSpec.parent_path().text(), scdName.substr(0, scdName.rfind('_') + 1), it->second);
				}
			}
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
				"Failed to look up the speaker of {}: {}", path, e.what());
		}
		return {};
	}
}
