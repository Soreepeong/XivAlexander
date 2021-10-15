#pragma once

#include <XivAlexanderCommon/Sqex.h>

namespace App::Misc::GameInstallationDetector {
	struct GameReleaseInfo {
		std::string CountryCode;

		Sqex::GameReleaseRegion Region{};
		std::string GameVersion;
		std::string PathSafeGameVersion;

		std::filesystem::path RootPath;
		std::filesystem::path BootApp;
		bool BootAppRequiresAdmin{};
		bool BootAppDirectlyInjectable{};
		std::set<std::filesystem::path> RelatedApps;
		
		std::filesystem::path GamePath() const { return RootPath / "game"; }
		std::filesystem::path SqpackPath() const { return RootPath / "game" / "sqpack"; }
	};

	GameReleaseInfo GetGameReleaseInfo(std::filesystem::path deepestLookupPath = {});

	std::vector<GameReleaseInfo> FindInstallations();
	
}
