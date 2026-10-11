#pragma once

#include <stop_token>

#include <xivres/common.h>

namespace XivAlexander::Misc::GameInstallationDetector {
	struct GameReleaseInfo {
		std::string CountryCode;

		xivres::game_release_publisher Region{};
		std::string GameVersion;
		std::string PathSafeGameVersion;

		std::filesystem::path RootPath;
		std::filesystem::path BootApp;

		std::filesystem::path GamePath() const { return RootPath / "game"; }
		std::filesystem::path SqpackPath() const { return RootPath / "game" / "sqpack"; }
	};

	GameReleaseInfo GetGameReleaseInfo(std::filesystem::path deepestLookupPath = {});

	std::vector<GameReleaseInfo> FindInstallations();

	// Bit flags, for "found via".
	enum class DiscoverySource : uint32_t {
		RegistrySquareEnix = 1 << 0,
		Steam = 1 << 1,
		RegistryActoz = 1 << 2,
		RegistryShengqu = 1 << 3,
		RegistryUserjoy = 1 << 4,
		MsiComponent = 1 << 5,
		KnownFolder = 1 << 6,
		XivLauncher = 1 << 7,
		ShortcutDesktop = 1 << 8,
		ShortcutStartMenu = 1 << 9,
		RunningProcess = 1 << 10,
		XivAlexanderConfig = 1 << 11,
		UserAdded = 1 << 12,
		CommandLine = 1 << 13,
	};

	struct InstallationCandidate {
		std::filesystem::path Path;
		DiscoverySource Source{};
		std::wstring Detail;  // the key, file or PID it came from
	};

	struct CollectOptions {
		std::vector<std::filesystem::path> UserAdded;
		std::vector<std::filesystem::path> CommandLine;
		std::filesystem::path XivAlexanderConfigPath;  // config.runtime.json
		bool Shortcuts = true;
	};

	/// Cheap sources only; nothing here reads signatures. Shortcut scanning needs COM initialized on the calling thread.
	std::vector<InstallationCandidate> CollectCandidates(const CollectOptions& options, std::stop_token stop = {});

	/// Walks up from path to the folder that has game\ffxivgame.ver and game\ffxiv_dx11.exe.
	std::optional<std::filesystem::path> FindRootStructurally(std::filesystem::path path, int maxLevels = 6);

	/// Roots in dir or up to depth levels below it, for a picked parent folder such as "SquareEnix".
	std::vector<std::filesystem::path> FindRootsBelow(const std::filesystem::path& dir, int depth = 2);

	/// Identity of a folder that survives junctions, subst drives, 8.3 names and case; nullopt if it cannot be read.
	std::optional<FILE_ID_INFO> GetDirectoryId(const std::filesystem::path& dir);

	enum class ManagedDllKind {
		XivAlexander,
		ReShade,
		GShade,
		SpecialK,
		Dxvk,
		Enb,
		Unknown,
	};

	struct ManagedDll {
		std::wstring Name;  // d3d11.dll, dxgi.dll, dinput8.dll or d3d9.dll
		ManagedDllKind Kind = ManagedDllKind::Unknown;
		bool Is32Bit = false;
		bool IsOldStyleXivAlexander = false;  // XivAlexander64.dll itself, as installed before XivAlexanderLoader64.dll
		std::string Version;
		std::wstring Product;
	};

	struct InstallState {
		std::vector<ManagedDll> Dlls;
		std::vector<std::filesystem::path> ChainDlls;  // in xivalex.chaindll.*
		bool HasLeftoverTempFiles = false;

		/// The 64-bit XivAlexander DLL the game will load, if any.
		[[nodiscard]] const ManagedDll* Installed() const;
		/// Any XivAlexander DLL, including a stale 32-bit one; what Uninstall can remove.
		[[nodiscard]] bool HasAnyXivAlexander() const;
	};

	InstallState ProbeInstallState(const std::filesystem::path& gamePath);

	struct FoundInstallation {
		std::filesystem::path RootPath;
		GameReleaseInfo Info;  // Region is Unspecified when the signer could not be read
		uint32_t Sources = 0;
		std::vector<std::pair<DiscoverySource, std::wstring>> Details;
		std::optional<FILE_ID_INFO> GameDirId;

		[[nodiscard]] bool IsSameAs(const FoundInstallation& r) const;
	};

	/// Reads version and region of a root; never throws for an unknown region, only for a missing game.
	FoundInstallation ResolveInstallation(const std::filesystem::path& root);

	/// Collects, deduplicates by game folder identity, then resolves each root and reports it as soon as it is known.
	void FindInstallations(const CollectOptions& options, const std::function<void(FoundInstallation)>& onFound, std::stop_token stop = {});

	/// File names of game and launcher programs, lowercase.
	const std::vector<std::wstring>& GameProgramNames();
}
