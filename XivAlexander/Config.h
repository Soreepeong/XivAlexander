#pragma once

#include "Config/BaseConfigRepository.h"

#include "Config/GameConfigRepository.h"
#include "Config/InitConfigRepository.h"
#include "Config/PatchCodeRepository.h"
#include "Config/RuntimeConfigRepository.h"

namespace XivAlexander {
	class Config {
	public:
		class ConfigCreator;

		static std::filesystem::path TranslatePath(const std::filesystem::path& path, const std::filesystem::path& relativeTo = {});

		// Where a configured directory starts: in the configuration folder, or in the game's sqpack folder. Neither can
		// be in a path, so that they can't be mistaken for one.
		static constexpr std::wstring_view ConfigDirectoryToken = L"<config>";
		static constexpr std::wstring_view SqpackDirectoryToken = L"<sqpack>";

	protected:
		static std::weak_ptr<Config> s_instance;

		Config(std::filesystem::path initializationConfigPath);

	public:
		InitConfigRepository Init;
		RuntimeConfigRepository Runtime;
		GameConfigRepository Game;
		PatchCodeRepository PatchCode;

		virtual ~Config();

		void Reload();

		/// Turns a configured directory into a path, as TranslatePath does, after replacing the token it starts with;
		/// sqpackPath is the game's sqpack folder, or the running game's if empty.
		std::filesystem::path TranslateDirectoryPath(const std::filesystem::path& path, const std::filesystem::path& sqpackPath = {});

		static std::shared_ptr<Config> Acquire();
	};
}
