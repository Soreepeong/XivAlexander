#pragma once
#include "Config.h"
#include "LoaderApp/Arguments.h"

namespace XivAlexander::LoaderApp::Actions {
	class InstallUninstall {
		const Arguments& m_args;

	public:
		struct Paths {
			std::filesystem::path ConfigDir;  // %APPDATA%\XivAlexander of the invoking user
			std::filesystem::path ProgramDir;  // %LOCALAPPDATA%\XivAlexander of the invoking user
			std::filesystem::path SourceDir;  // where this loader is
			bool Quiet = false;  // passed on to an elevated child, which reports its own errors

			static Paths ForCurrentUser();
			static Paths FromArguments(const Arguments& args);
		};

		InstallUninstall(const Arguments& args);

		int Run();

		/// Installs, elevating a child for the game folder part when it is not writable; false if elevation was cancelled.
		static bool Install(const std::filesystem::path& gamePath, InstallMode installMode, const Paths& paths, bool allowElevation);

		/// Uninstalls, elevating a child for the game folder part when it is not writable; false if elevation was cancelled.
		static bool Uninstall(const std::filesystem::path& gamePath, const Paths& paths, bool allowElevation);

		/// The part that may need elevation: the game folder, and the game's ChainLoad section in the config.
		static void InstallGameFiles(const std::filesystem::path& gamePath, InstallMode installMode, const Paths& paths);
		static void UninstallGameFiles(const std::filesystem::path& gamePath, const Paths& paths);

		/// The part in the user's own profile, never elevated.
		static void InstallProgramFiles(const Paths& paths);

		[[nodiscard]] static bool IsDirectoryWritable(const std::filesystem::path& dir);

		/// The DLL name in the game folder for an install mode, such as d3d11.dll.
		[[nodiscard]] static const wchar_t* SlotNameOf(InstallMode installMode);

	private:
		static bool RunGameFilesPart(bool install, const std::filesystem::path& gamePath, InstallMode installMode, const Paths& paths, bool allowElevation);

		static void RevertChainLoadDlls(
			const std::filesystem::path& gamePath,
			const bool& success,
			xivres::util::on_dtor::multi& revert,
			RuntimeConfigRepository& config64);
		static void RemoveTemporaryFiles(const std::filesystem::path& gamePath);
	};
}
