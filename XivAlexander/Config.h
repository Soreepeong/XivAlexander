#pragma once

#include "Config/BaseConfigRepository.h"

#include "Config/GameConfigRepository.h"
#include "Config/InitConfigRepository.h"
#include "Config/RuntimeConfigRepository.h"

namespace XivAlexander {
	class Config {
	public:
		class ConfigCreator;

		static std::filesystem::path TranslatePath(const std::filesystem::path& path, const std::filesystem::path& relativeTo = {});

	protected:
		static std::weak_ptr<Config> s_instance;

		Config(std::filesystem::path initializationConfigPath);

	public:
		InitConfigRepository Init;
		RuntimeConfigRepository Runtime;
		GameConfigRepository Game;

		virtual ~Config();

		void Reload();

		static std::shared_ptr<Config> Acquire();
	};
}
