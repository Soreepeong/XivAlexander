#include "pch.h"
#include "App.h"

#include <XivAlexander/XivAlexander.h>
#include "Game/CommandLine.h"
#include "Utils/Win32/InjectedModule.h"
#include "Utils/Win32/Resource.h"

#include "LoaderApp/Actions/Inject.h"
#include "LoaderApp/Actions/InstallUninstall.h"
#include "LoaderApp/Actions/Interactive.h"
#include "LoaderApp/Actions/LoadUnload.h"
#include "LoaderApp/Actions/RunLauncher.h"
#include "LoaderApp/Actions/Update.h"
#include "LoaderApp/Arguments.h"
#include "resource.h"
#include "XivAlexander.h"

using namespace Dll;

Utils::Win32::Process XivAlexander::LoaderApp::OpenProcessForInformation(DWORD pid, bool errorOnAccessDenied) {
	try {
		return {PROCESS_QUERY_LIMITED_INFORMATION, false, pid};
	} catch (const Utils::Win32::Error& e) {
		if (e.Code() != ERROR_ACCESS_DENIED)
			throw;
	}

	// Some processes allow PROCESS_QUERY_INFORMATION while denying PROCESS_QUERY_LIMITED_INFORMATION.
	try {
		return {PROCESS_QUERY_INFORMATION, false, pid};
	} catch (const Utils::Win32::Error& e) {
		if (e.Code() != ERROR_ACCESS_DENIED)
			throw;
		if (errorOnAccessDenied)
			throw;
		return {};
	}
}

Utils::Win32::Process XivAlexander::LoaderApp::OpenProcessForManipulation(DWORD pid) {
	return {PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, pid};
}

bool XivAlexander::LoaderApp::TestAdminRequirementForProcessManipulation(const std::set<DWORD>& pids) {
	try {
		for (const auto pid : pids)
			OpenProcessForManipulation(pid);
	} catch (const Utils::Win32::Error& e) {
		if (e.Code() == ERROR_ACCESS_DENIED)
			return true;
	}
	return false;
}

namespace {
	// The game accepts an obfuscated block only within its key's time window, which a UAC prompt can outlast.
	std::wstring RefreshObfuscatedArguments(const std::wstring& args) {
		if (args.find(xivres::util::unicode::convert<std::wstring>(XivAlexander::Game::CommandLine::ObfuscationHead)) == std::wstring::npos)
			return args;
		try {
			auto obfuscated = false;
			const auto params = XivAlexander::Game::CommandLine::FromString(std::format(L"unused.exe {}", args), &obfuscated);
			if (obfuscated)
				return XivAlexander::Game::CommandLine::ToString(params, true);
		} catch (...) {
			// pass it on as it is
		}
		return args;
	}
}

bool XivAlexander::LoaderApp::RunProgramRetryAfterElevatingSelfAsNecessary(const std::filesystem::path& path, const std::wstring& args) {
	if (Utils::Win32::RunProgram({
		.path = path,
		.dir = path.parent_path().c_str(),
		.args = RefreshObfuscatedArguments(args),
		.elevateMode = Utils::Win32::RunProgramParams::CancelIfRequired,
	}))
		return true;

	// An elevated loader that still cannot start it must not start yet another one.
	if (Utils::Win32::IsUserAnAdmin())
		return false;

	return Utils::Win32::RunProgram({
		.dir = path.parent_path().c_str(),
		.args = xivres::util::unicode::convert<std::wstring>(Utils::Win32::ReverseCommandLineToArgv({
			"--disable-runas",
			"-a", LoaderActionToString(LoaderAction::Launcher),
			"-l", "select",
			path.string()
		})) + (args.empty() ? L"" : L" " + args),
		.elevateMode = Utils::Win32::RunProgramParams::Force,
	});
}

template<>
std::string argparse::details::repr(LoaderAction const& val) {
	return LoaderActionToString(val);
}

template<>
std::string argparse::details::repr(XivAlexander::LoaderApp::LauncherType const& val) {
	switch (val) {
		case XivAlexander::LoaderApp::LauncherType::Auto: return "auto";
		case XivAlexander::LoaderApp::LauncherType::Select: return "select";
		case XivAlexander::LoaderApp::LauncherType::SquareEnix: return "squareenix";
		case XivAlexander::LoaderApp::LauncherType::ActozSoft: return "actozsoft";
		case XivAlexander::LoaderApp::LauncherType::ShengquGames: return "shengqugames";
		case XivAlexander::LoaderApp::LauncherType::UserjoyGames: return "userjoygames";
	}
	return std::format("({})", static_cast<int>(val));
}

template<>
std::string argparse::details::repr(XivAlexander::LoaderApp::InstallMode const& val) {
	switch (val) {
		case XivAlexander::LoaderApp::InstallMode::D3D: return "d3d";
		case XivAlexander::LoaderApp::InstallMode::DInput8: return "dinput8";
		case XivAlexander::LoaderApp::InstallMode::Dxgi: return "dxgi";
	}
	return std::format("({})", static_cast<int>(val));
}

template<>
std::string argparse::details::repr(xivres::game_release_publisher const& val) {
	switch (val) {
		case xivres::game_release_publisher::SquareEnix: return xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_CLIENT_INTERNATIONAL) + 1);
		case xivres::game_release_publisher::ActozSoft: return xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_CLIENT_ACTOZSOFT) + 1);
		case xivres::game_release_publisher::ShengquGames: return xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_CLIENT_SHENGQUGAMES) + 1);
		case xivres::game_release_publisher::UserjoyGames: return xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_CLIENT_USERJOYGAMES) + 1);
	}
	return std::format("({})", static_cast<int>(val));
}


void XivAlexander::LoaderApp::ReportError(bool quiet, const std::wstring& message) {
	if (!quiet) {
		MessageBoxF(nullptr, MB_ICONWARNING, IDS_ERROR_UNEXPECTED, message);
		return;
	}
	OutputDebugStringW(std::format(L"XivAlexanderLoader: {}\n", message).c_str());
	if (const auto hStderr = GetStdHandle(STD_ERROR_HANDLE); hStderr && hStderr != INVALID_HANDLE_VALUE) {
		const auto utf8 = xivres::util::unicode::convert<std::string>(message) + "\n";
		DWORD written;
		WriteFile(hStderr, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
	}
}

class XivAlexander::LoaderApp::LoaderApp {
	const Arguments& m_args;

	std::string m_errorClaimingSeDebugPrivilege;

public:
	LoaderApp(const Arguments& args)
		: m_args(args) {
		if (m_args.m_help) {
			MessageBoxF(nullptr, MB_OK, m_args.GetHelpMessage().c_str());
			ExitProcess(0);
		}

		if (const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); FAILED(hr))
			throw Utils::Win32::Error(_com_error(hr));

		if (NeedsProcessAccess()) {
			try {
				Utils::Win32::AddDebugPrivilege();
			} catch (const std::exception& err) {
				const auto s = err.what();
				m_errorClaimingSeDebugPrivilege = xivres::util::unicode::convert<std::string>(std::vformat(FindStringResourceEx(Module(), IDS_ERROR_SEDEBUGPRIVILEGE) + 1, std::make_wformat_args(s)));
			}
		}
	}

	// Actions that open other processes; only those use or report SeDebugPrivilege.
	[[nodiscard]] bool NeedsProcessAccess() const {
		switch (m_args.m_action) {
			case LoaderAction::Ask:
			case LoaderAction::Load:
			case LoaderAction::Unload:
			case LoaderAction::Launcher:
			case LoaderAction::Internal_Inject_HookEntryPoint:
			case LoaderAction::Internal_Inject_LoadXivAlexanderImmediately:
			case LoaderAction::Internal_Inject_UnloadFromHandle:
			case LoaderAction::UpdateCheck:
			case LoaderAction::Internal_Update_DependencyDllMode:
			case LoaderAction::Internal_Update_Step2_ReplaceFiles:
			case LoaderAction::Internal_Update_Step3_CleanupFiles:
				return true;
			default:
				return false;
		}
	}

	int Run() {
		try {
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", nullptr);

			VerifyPackageVersionOrThrow();

#ifdef _DEBUG
			Dll::MessageBoxF(nullptr, MB_OK, L"Action: {}", argparse::details::repr(m_args.m_action));
#endif
			switch (m_args.m_action) {
				case LoaderAction::Web:
					Utils::Win32::ShellExecutePathOrThrow(FindStringResourceEx(Module(), IDS_URL_MAIN) + 1);
					return 0;

				case LoaderAction::Interactive:
					// The dialog stays unelevated and elevates only the operations that need it.
					return Actions::Interactive(m_args).Run();

				case LoaderAction::Ask:
				case LoaderAction::Load:
				case LoaderAction::Unload:
					if (!m_args.m_disableAutoRunAs && !Utils::Win32::IsUserAnAdmin() && TestAdminRequirementForProcessManipulation(m_args.GetTargetPidList()))
						RunElevatedSelfIfPossible();
					return Actions::LoadUnload(m_args).Run();

				case LoaderAction::Launcher:
					return Actions::RunLauncher(m_args).Run();

				case LoaderAction::Install:
				case LoaderAction::Uninstall:
					return Actions::InstallUninstall(m_args).Run();

				case LoaderAction::Internal_Inject_HookEntryPoint:
				case LoaderAction::Internal_Inject_LoadXivAlexanderImmediately:
				case LoaderAction::Internal_Inject_UnloadFromHandle:
					return Actions::Inject(m_args).Run();

				case LoaderAction::UpdateCheck:
				case LoaderAction::Internal_Update_DependencyDllMode:
				case LoaderAction::Internal_Update_Step2_ReplaceFiles:
				case LoaderAction::Internal_Update_Step3_CleanupFiles:
					return Actions::Update(m_args).Run();
			}

			throw std::logic_error("invalid m_action value");
		} catch (const std::exception& e) {
			auto message = xivres::util::unicode::convert<std::wstring>(e.what());
			if (NeedsProcessAccess() && !m_errorClaimingSeDebugPrivilege.empty())
				message += std::format(L"\nSeDebugPrivilege: {}", xivres::util::unicode::convert<std::wstring>(m_errorClaimingSeDebugPrivilege));
			ReportError(m_args.m_quiet, message);
			return -1;
		}
	}

private:
	void VerifyPackageVersionOrThrow() {
		try {
			switch (CheckPackageVersion()) {
				case CheckPackageVersionResult::OK:
					break;

				case CheckPackageVersionResult::MissingFiles:
					throw std::runtime_error(xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_ERROR_MISSING_FILES) + 1));

				case CheckPackageVersionResult::VersionMismatch:
					throw std::runtime_error(xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_ERROR_INCONSISTENT_FILES) + 1));
			}
		} catch (const std::exception& e) {
			if (MessageBoxF(nullptr, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON1, IDS_ERROR_COMPONENTS, e.what()) == IDYES)
				Utils::Win32::ShellExecutePathOrThrow(FindStringResourceEx(Module(), IDS_URL_RELEASES) + 1);
			ExitProcess(-1);
		}
	}

	void RunElevatedSelfIfPossible() {
		try {
			ExitProcess(Utils::Win32::RunProgram({
				.args = std::format(L"--disable-runas {}", Utils::Win32::SplitCommandLineIntoNameAndArgs().second),
				.elevateMode = Utils::Win32::RunProgramParams::Force,
				}).WaitAndGetExitCode());
		} catch (const std::exception&) {
		}
	}
};

int Dll::LoaderAppMain() {
	const auto activationContextCleanup = ActivationContext().With();

	XivAlexander::LoaderApp::Arguments parameters;
	try {
		parameters.Parse();
	} catch (const std::exception& err) {
		MessageBoxF(nullptr, MB_ICONWARNING, IDS_ERROR_COMMAND_LINE, err.what(), parameters.GetHelpMessage());
		return -1;
	}
	try {
		return XivAlexander::LoaderApp::LoaderApp(parameters).Run();
	} catch (const std::exception& err) {
		XivAlexander::LoaderApp::ReportError(parameters.m_quiet, xivres::util::unicode::convert<std::wstring>(err.what()));
		return -1;
	}
}
