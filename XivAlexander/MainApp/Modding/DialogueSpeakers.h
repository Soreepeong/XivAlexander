#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <xivres/excel.h>
#include <xivres/installation.h>

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Misc {
	class Logger;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class DialogueSpeakers {
		const std::shared_ptr<Config> m_config;
		const std::shared_ptr<Misc::Logger> m_logger;
		const std::filesystem::path m_gamePath;

		mutable std::mutex m_mtx;
		mutable std::optional<xivres::installation> m_installation;
		mutable std::optional<xivres::excel::exl::reader> m_excelList;
		mutable std::optional<xivres::excel::reader> m_dialogueSheet;
		mutable std::string m_dialogueSheetName;

	public:
		explicit DialogueSpeakers(std::filesystem::path gamePath);
		~DialogueSpeakers();

		[[nodiscard]] std::string ForceLanguageForSpeaker(const std::string& path) const;

	private:
		[[nodiscard]] std::pair<std::string, std::string> ResolveDialogueSheet(const xivres::path_spec& pathSpec) const;
	};
}
