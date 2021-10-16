#include "pch.h"
#include "DllMain.h"

#include <XivAlexander/XivAlexander.h>
#include <XivAlexanderCommon/Sqex_CommandLine.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>

#include "App_ConfigRepository.h"
#include "App_Misc_CrashMessageBoxHandler.h"
#include "App_Misc_Hooks.h"
#include "resource.h"

static Utils::Win32::LoadedModule s_hModule;
static Utils::Win32::ActivationContext s_hActivationContext;
static std::string s_dllUnloadDisableReason;
static bool s_bLoadedAsDependency = false;
static bool s_bLoadedFromEntryPoint = false;
static std::unique_ptr<App::Misc::CrashMessageBoxHandler> s_crashMessageBoxHandler;

const Utils::Win32::LoadedModule& Dll::Module() {
	return s_hModule;
}

const Utils::Win32::ActivationContext& Dll::ActivationContext() {
	return s_hActivationContext;
}

const char* XivAlexDll::LoaderActionToString(LoaderAction val) {
	switch (val) {
		case LoaderAction::Interactive: return "interactive";
		case LoaderAction::Web: return "web";
		case LoaderAction::Ask: return "ask";
		case LoaderAction::Load: return "load";
		case LoaderAction::Unload: return "unload";
		case LoaderAction::Launcher: return "launcher";
		case LoaderAction::UpdateCheck: return "update-check";
		case LoaderAction::Install: return "install";
		case LoaderAction::Uninstall: return "uninstall";
		case LoaderAction::Internal_Update_DependencyDllMode: return "_internal_update_dependencydllmode";
		case LoaderAction::Internal_Update_Step2_ReplaceFiles: return "_internal_update_step2_replacefiles";
		case LoaderAction::Internal_Update_Step3_CleanupFiles: return "_internal_update_step3_cleanupfiles";
		case LoaderAction::Internal_Inject_HookEntryPoint: return "_internal_inject_hookentrypoint";
		case LoaderAction::Internal_Inject_LoadXivAlexanderImmediately: return "_internal_inject_loadxivalexanderimmediately";
		case LoaderAction::Internal_Inject_UnloadFromHandle: return "_internal_inject_unloadfromhandle";
	}
	return "<invalid>";
}

DWORD XivAlexDll::LaunchXivAlexLoaderWithTargetHandles(
	const std::vector<Utils::Win32::Process>& hSources,
	LoaderAction action,
	bool wait,
	const Utils::Win32::Process& waitForBeforeStarting,
	WhichLoader which,
	const std::filesystem::path& loaderPath) {

	const auto companionPath = loaderPath.empty() ? (
		lstrcmpiW(Utils::Win32::Process::Current().PathOf().filename().wstring().c_str(), XivAlexDll::GameExecutableNameW) == 0
		? App::Config::Acquire()->Init.ResolveXivAlexInstallationPath()
		: Dll::Module().PathOf().parent_path()
	) : loaderPath;
	const wchar_t* whichLoader;
	switch (which) {
		case Current:
			whichLoader = XivAlexDll::XivAlexLoaderNameW;
			break;
		case Opposite:
			whichLoader = XivAlexDll::XivAlexLoaderOppositeNameW;
			break;
		case Force32:
			whichLoader = XivAlexDll::XivAlexLoader32NameW;
			break;
		case Force64:
			whichLoader = XivAlexDll::XivAlexLoader64NameW;
			break;
		default:
			throw std::invalid_argument("Invalid which");
	}

	const auto companion = companionPath / whichLoader;

	if (!exists(companion))
		throw std::runtime_error(Utils::ToUtf8(std::format(FindStringResourceEx(Dll::Module(), IDS_ERROR_LOADER_NOT_FOUND) + 1, companion)));

	Utils::Win32::Process companionProcess;
	{
		Utils::Win32::ProcessBuilder creator;
		creator.WithPath(companion)
			.WithArgument(true, L"")
			.WithAppendArgument(L"--handle-instead-of-pid")
			.WithAppendArgument(L"--action")
			.WithAppendArgument(LoaderActionToString(action));

		if (waitForBeforeStarting)
			creator.WithAppendArgument("--wait-process")
				.WithAppendArgument("{}", creator.Inherit(waitForBeforeStarting).Value());
		for (const auto& h : hSources)
			creator.WithAppendArgument("{}", creator.Inherit(h).Value());

		companionProcess = creator.Run().first;
	}

	if (!wait)
		return 0;
	else {
		DWORD retCode = 0;
		companionProcess.Wait();
		if (!GetExitCodeProcess(companionProcess, &retCode))
			throw Utils::Win32::Error("GetExitCodeProcess");
		return retCode;
	}
}

static std::wstring_view s_originalCommandLine;
static bool s_originalCommandLineIsObfuscated;

static void CheckObfuscatedArguments() {
	const auto process = Utils::Win32::Process::Current();
	auto filename = process.PathOf().filename().wstring();
	CharLowerW(&filename[0]);

	if (filename != XivAlexDll::GameExecutableNameW)
		return;  // not the game process

	try {
		std::vector<std::string> args;
		if (int nArgs; LPWSTR* szArgList = CommandLineToArgvW(s_originalCommandLine.data(), &nArgs)) {
			for (int i = 0; i < nArgs; i++)
				args.emplace_back(Utils::ToUtf8(szArgList[i]));
			LocalFree(szArgList);
		}
		if (args.size() == 2) {
			// Once this function is called, it means that this dll will stick to the process until it exits,
			// so it's safe to store stuff into static variables.

			static const auto pairs = Sqex::CommandLine::FromString(args[1], &s_originalCommandLineIsObfuscated);
			if (s_originalCommandLineIsObfuscated) {
				static auto newlyCreatedArgumentsW = std::format(L"\"{}\" {}", process.PathOf().wstring(), Sqex::CommandLine::ToString(pairs, false));
				static auto newlyCreatedArgumentsA = Utils::ToUtf8(newlyCreatedArgumentsW, CP_OEMCP);

				static App::Misc::Hooks::ImportedFunction<LPWSTR> GetCommandLineW("kernel32!GetCommandLineW", "kernel32.dll", "GetCommandLineW");
				static const auto h1 = GetCommandLineW.SetHook([]() -> LPWSTR {
					return &newlyCreatedArgumentsW[0];
				});

				static App::Misc::Hooks::ImportedFunction<LPSTR> GetCommandLineA("kernel32!GetCommandLineA", "kernel32.dll", "GetCommandLineA");
				static const auto h2 = GetCommandLineA.SetHook([]() -> LPSTR {
					return &newlyCreatedArgumentsA[0];
				});
			}
		}
	} catch (...) {
		// do nothing
	}
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {
	switch (fdwReason) {
		case DLL_PROCESS_ATTACH: {
			s_bLoadedAsDependency = !!lpReserved;  // non-null for static loads
			s_originalCommandLine = GetCommandLineW();

			try {
				s_hModule.Attach(hInstance, Utils::Win32::LoadedModule::Null, false, "Instance attach failed <cannot happen>");
				s_hActivationContext = Utils::Win32::ActivationContext(ACTCTXW{
					.cbSize = static_cast<ULONG>(sizeof ACTCTXW),
					.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID,
					.lpResourceName = MAKEINTRESOURCE(IDR_RT_MANIFEST_LATE_ACTIVATION),
					.hModule = Dll::Module(),
				});
				
				if (s_bLoadedAsDependency && lstrcmpiW(Utils::Win32::Process::Current().PathOf().filename().wstring().c_str(), XivAlexDll::GameExecutableNameW) == 0) {
					GetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", nullptr, 0);
					if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
						Dll::DisableUnloading(std::format("Loaded as DLL dependency in place of {}", Dll::Module().PathOf().filename()).c_str());
						const auto params = XivAlexDll::PatchEntryPointForInjection(GetCurrentProcess());
						params->SkipFree = true;
						params->LoadInstalledXivAlexDllOnly = true;
					}
				} else {
					MH_Initialize();
					CheckObfuscatedArguments();
					s_crashMessageBoxHandler = std::make_unique<App::Misc::CrashMessageBoxHandler>();
				}

			} catch (const std::exception& e) {
				Utils::Win32::DebugPrint(L"DllMain({:x}, DLL_PROCESS_ATTACH, {}) Error: {}",
					reinterpret_cast<size_t>(hInstance), reinterpret_cast<size_t>(lpReserved), e.what());
				return FALSE;
			}

			return TRUE;
		}

		case DLL_PROCESS_DETACH: {
			auto fail = false;

			s_crashMessageBoxHandler.reset();

			if (const auto res = MH_Uninitialize(); res != MH_OK && res != MH_ERROR_NOT_INITIALIZED) {
				fail = true;
				Utils::Win32::DebugPrint(L"MH_Uninitialize error: {}", MH_StatusToString(res));
			}
			if (fail)
				TerminateProcess(GetCurrentProcess(), -1);
			return TRUE;
		}
	}
	return TRUE;
}

size_t __stdcall XivAlexDll::DisableAllApps(void*) {
	if (s_bLoadedAsDependency)
		return -1;

	EnableXivAlexander(0);
	EnableInjectOnCreateProcess(0);
	return 0;
}

void __stdcall XivAlexDll::CallFreeLibrary(void*) {
	FreeLibraryAndExitThread(Dll::Module(), 0);
}

[[nodiscard]] XivAlexDll::CheckPackageVersionResult XivAlexDll::CheckPackageVersion() {
	const auto dir = Utils::Win32::Process::Current().PathOf().parent_path();
	std::vector<std::pair<std::string, std::string>> modules;
	try {
		modules = {
			Utils::Win32::FormatModuleVersionString(dir / XivAlexDll::XivAlexLoader32NameW),
			Utils::Win32::FormatModuleVersionString(dir / XivAlexDll::XivAlexLoader64NameW),
			Utils::Win32::FormatModuleVersionString(dir / XivAlexDll::XivAlexDll32NameW),
			Utils::Win32::FormatModuleVersionString(dir / XivAlexDll::XivAlexDll64NameW),
		};
	} catch (const Utils::Win32::Error& e) {
		if (e.Code() == ERROR_FILE_NOT_FOUND)
			return CheckPackageVersionResult::MissingFiles;
		throw;
	}
	for (size_t i = 1; i < modules.size(); ++i) {
		if (modules[0].first != modules[i].first || modules[0].second != modules[i].second)
			return CheckPackageVersionResult::VersionMismatch;
	}
	return CheckPackageVersionResult::OK;
}

size_t Dll::DisableUnloading(const char* pszReason) {
	if (s_bLoadedAsDependency)
		return -1;

	s_dllUnloadDisableReason = pszReason ? pszReason : "(reason not specified)";
	Module().SetPinned();
	return 0;
}

const char* Dll::GetUnloadDisabledReason() {
	return s_dllUnloadDisableReason.empty() ? nullptr : s_dllUnloadDisableReason.c_str();
}

bool Dll::IsLoadedAsDependency() {
	return s_bLoadedAsDependency;
}

const wchar_t* Dll::GetGenericMessageBoxTitle() {
	static std::wstring buf;
	if (buf.empty()) {
		buf = std::format(L"{} {}",
			GetStringResFromId(IDS_APP_NAME),
			Utils::Win32::FormatModuleVersionString(Module().PathOf()).second);
	}
	return buf.data();
}

int Dll::MessageBoxF(HWND hWnd, UINT uType, const std::wstring& text) {
	return MessageBoxF(hWnd, uType, text.c_str());
}

int Dll::MessageBoxF(HWND hWnd, UINT uType, const std::string& text) {
	return MessageBoxF(hWnd, uType, Utils::FromUtf8(text));
}

int Dll::MessageBoxF(HWND hWnd, UINT uType, const wchar_t* text) {
	return MessageBoxW(hWnd, text, GetGenericMessageBoxTitle(), uType);
}

int Dll::MessageBoxF(HWND hWnd, UINT uType, const char* text) {
	return MessageBoxF(hWnd, uType, Utils::FromUtf8(text));
}

LPCWSTR Dll::GetStringResFromId(UINT resId) {
	try {
		const auto conf = App::Config::Acquire();
		return conf->Runtime.GetStringRes(resId);
	} catch (...) {
		return 1 + FindStringResourceEx(Module(), resId);
	}
}

int Dll::MessageBoxF(HWND hWnd, UINT uType, UINT stringResId) {
	return MessageBoxF(hWnd, uType, GetStringResFromId(stringResId));
}

std::wstring_view Dll::GetOriginalCommandLine() {
	return s_originalCommandLine;
}

bool Dll::IsOriginalCommandLineObfuscated() {
	return s_originalCommandLineIsObfuscated;
}

void Dll::SetLoadedFromEntryPoint() {
	s_bLoadedFromEntryPoint = true;
}

bool Dll::IsLoadedFromEntryPoint() {
	return s_bLoadedFromEntryPoint;
}

HWND Dll::FindGameMainWindow(bool throwOnError) {
	HWND hwnd = nullptr;
	while ((hwnd = FindWindowExW(nullptr, hwnd, L"FFXIVGAME", nullptr))) {
		DWORD pid;
		GetWindowThreadProcessId(hwnd, &pid);

		if (pid == GetCurrentProcessId())
			break;
	}
	if (hwnd == nullptr && throwOnError)
		throw std::runtime_error(Utils::ToUtf8(FindStringResourceEx(Module(), IDS_ERROR_GAME_WINDOW_NOT_FOUND) + 1));
	return hwnd;
}


XivAlexDll::VersionInformation XivAlexDll::CheckUpdates() {
	std::ostringstream os;

	curlpp::Easy req;
	req.setOpt(curlpp::options::Url("https://api.github.com/repos/Soreepeong/XivAlexander/releases/latest"));
	req.setOpt(curlpp::options::UserAgent("Mozilla/5.0"));
	os << req;
	const auto parsed = nlohmann::json::parse(os.str());
	const auto assets = parsed.at("assets");
	if (assets.empty())
		throw std::runtime_error("Could not detect updates. Please try again at a later time.");
	const auto item = assets[0];

	std::istringstream in(parsed.at("published_at").get<std::string>());
	std::chrono::sys_seconds tp;
	from_stream(in, "%FT%TZ", tp);
	if (in.fail())
		throw std::format_error(std::format("Failed to parse datetime string \"{}\"", in.str()));

	return {
		.Name = parsed.at("name").get<std::string>(),
		.Body = parsed.at("body").get<std::string>(),
		.PublishDate = std::chrono::zoned_time(std::chrono::current_zone(), tp),
		.DownloadLink = item.at("browser_download_url").get<std::string>(),
		.DownloadSize = item.at("size").get<size_t>(),
	};
}

bool XivAlexDll::IsXivAlexanderDll(const std::filesystem::path& dllPath) {
	DWORD verHandle = 0;
	std::vector<BYTE> block;
	block.resize(GetFileVersionInfoSizeW(dllPath.c_str(), &verHandle));
	if (block.empty()) {
		const auto err = GetLastError();
		if (err == ERROR_RESOURCE_TYPE_NOT_FOUND)
			return false;
		throw Utils::Win32::Error(err, "GetFileVersionInfoSizeW");
	}
	if (!GetFileVersionInfoW(dllPath.c_str(), 0, static_cast<DWORD>(block.size()), &block[0]))
		throw Utils::Win32::Error("GetFileVersionInfoW");
	struct LANGANDCODEPAGE {
		WORD wLanguage;
		WORD wCodePage;
	} * lpTranslate;
	UINT cbTranslate;
	if (!VerQueryValueW(&block[0],
		TEXT("\\VarFileInfo\\Translation"),
		reinterpret_cast<LPVOID*>(&lpTranslate),
		&cbTranslate))
		return false;

	for (size_t i = 0; i < (cbTranslate / sizeof(struct LANGANDCODEPAGE)); i++) {
		wchar_t* buf = nullptr;
		UINT size = 0;
		if (!VerQueryValueW(&block[0],
			std::format(L"\\StringFileInfo\\{:04x}{:04x}\\FileDescription",
				lpTranslate[i].wLanguage,
				lpTranslate[i].wCodePage).c_str(),
			reinterpret_cast<LPVOID*>(&buf),
			&size))
			continue;
		auto currName = std::wstring_view(buf, size);
		while (!currName.empty() && currName.back() == L'\0')
			currName = currName.substr(0, currName.size() - 1);
		if (currName.empty())
			continue;
		if (currName == L"XivAlexander Main DLL")
			return true;
	}
	return false;
}
