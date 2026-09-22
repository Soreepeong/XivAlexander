#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "MainApp/Modding/DialogueSpeakers.h"

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class VirtualSqPacks;

	class PathRewriter {
		const std::shared_ptr<Config> m_config;
		const std::optional<VirtualSqPacks>& m_sqpacks;
		DialogueSpeakers m_speakers;

	public:
		PathRewriter(std::filesystem::path gamePath, const std::optional<VirtualSqPacks>& sqpacks);
		~PathRewriter();

		[[nodiscard]] std::string Rewrite(const std::string& original, std::string& description) const;
	};
}
