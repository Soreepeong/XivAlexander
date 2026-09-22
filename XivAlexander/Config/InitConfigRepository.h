#pragma once

#include "BaseConfigRepository.h"

namespace XivAlexander {
	class InitConfigRepository : public BaseConfigRepository {
		friend class Config;
		using BaseConfigRepository::BaseConfigRepository;

	public:
		// Default value if empty: %APPDATA%/XivAlexander
		ConfigItem<std::filesystem::path> FixedConfigurationFolderPath{this, "FixedConfigurationFolderPath", std::filesystem::path()};
		// Default value if empty: %LOCALAPPDATA%/XivAlexander
		ConfigItem<std::filesystem::path> XivAlexFolderPath{
			this,
#ifdef _DEBUG
			"XivAlexFolderPath_DEBUG"
#else
			"XivAlexFolderPath"
#endif
			, std::filesystem::path()
		};

		std::filesystem::path ResolveConfigStorageDirectoryPath();
		std::filesystem::path ResolveXivAlexInstallationPath();
		std::filesystem::path ResolveRuntimeConfigPath();
		std::filesystem::path ResolveGameOpcodeConfigPath();
	};
}
