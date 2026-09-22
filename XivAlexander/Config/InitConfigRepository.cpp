#include "pch.h"
#include "Config.h"

#include "Utils/Win32/Process.h"
#include "Misc/GameInstallationDetector.h"

std::filesystem::path XivAlexander::InitConfigRepository::ResolveConfigStorageDirectoryPath() {
	if (!Loaded())
		Reload();

	if (!FixedConfigurationFolderPath.Value().empty())
		return Utils::Win32::EnsureDirectory(Config::TranslatePath(FixedConfigurationFolderPath.Value()));
	else
		return Utils::Win32::EnsureDirectory(Utils::Win32::EnsureKnownFolderPath(FOLDERID_RoamingAppData) / L"XivAlexander");
}

std::filesystem::path XivAlexander::InitConfigRepository::ResolveXivAlexInstallationPath() {
	if (!Loaded())
		Reload();

	if (!XivAlexFolderPath.Value().empty())
		return Utils::Win32::EnsureDirectory(Config::TranslatePath(XivAlexFolderPath.Value()));
	else
		return Utils::Win32::EnsureDirectory(Utils::Win32::EnsureKnownFolderPath(FOLDERID_LocalAppData) / L"XivAlexander");
}

std::filesystem::path XivAlexander::InitConfigRepository::ResolveRuntimeConfigPath() {
	return ResolveConfigStorageDirectoryPath() / "config.runtime.json";
}

std::filesystem::path XivAlexander::InitConfigRepository::ResolveGameOpcodeConfigPath() {
	const auto gameReleaseInfo = Misc::GameInstallationDetector::GetGameReleaseInfo();
	return ResolveConfigStorageDirectoryPath() / std::format(L"game.{}.{}.json", gameReleaseInfo.CountryCode, gameReleaseInfo.PathSafeGameVersion);
}
