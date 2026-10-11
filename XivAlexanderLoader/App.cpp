#include "pch.h"

#include "Utils/Win32.h"
#include "Utils/Win32/Process.h"
#include "Utils/Win32/Resource.h"
#include "Utils/WinHttp.h"

#include "resource.h"
#include "XivAlexander.h"

// The Dll services that the loader app uses, as XivAlexanderLoader64.exe provides them.

namespace {
	Utils::Win32::LoadedModule s_hModule;
	Utils::Win32::ActivationContext s_hActivationContext;

	// XivAlexanderLoader64.dll of this folder, loaded on first use for what only it does.
	HMODULE LoaderDll() {
		static std::mutex s_mtx;
		static HMODULE s_hLoaderDll{};
		const auto lock = std::lock_guard(s_mtx);
		if (!s_hLoaderDll) {
			const auto path = Dll::ProgramDirectory() / Dll::XivAlexLoaderDll64NameW;
			s_hLoaderDll = LoadLibraryW(path.c_str());
			if (!s_hLoaderDll)
				throw Utils::Win32::Error(std::format("LoadLibraryW({})", path));
		}
		return s_hLoaderDll;
	}

	template<typename T>
	T LoaderDllFunction(const char* name) {
		if (const auto pfn = GetProcAddress(LoaderDll(), name))
			return reinterpret_cast<T>(pfn);
		throw Utils::Win32::Error(std::format("GetProcAddress({}, {})", xivres::util::unicode::convert<std::string>(Dll::XivAlexLoaderDll64NameW), name));
	}
}

const Utils::Win32::LoadedModule& Dll::Module() {
	return s_hModule;
}

const Utils::Win32::ActivationContext& Dll::ActivationContext() {
	return s_hActivationContext;
}

LPCWSTR Dll::GetStringResFromId(UINT resId) {
	return 1 + FindStringResourceEx(Module(), resId);
}

std::wstring Dll::GetOriginalCommandLine() {
	return GetCommandLineW();
}

std::filesystem::path Dll::ProgramDirectory() {
	return Module().PathOf().parent_path();
}

size_t Dll::EnableInjectOnCreateProcess(size_t flags) {
	return LoaderDllFunction<size_t(*)(size_t)>("EnableInjectOnCreateProcess")(flags);
}

Dll::InjectEntryPointParameters* Dll::PatchEntryPointForInjection(HANDLE hProcess) {
	wchar_t error[1024]{};
	const auto patch = LoaderDllFunction<void*(__stdcall*)(HANDLE, wchar_t*, DWORD)>("XA_PatchEntryPoint");
	if (const auto p = patch(hProcess, error, static_cast<DWORD>(std::size(error))))
		return static_cast<InjectEntryPointParameters*>(p);
	throw std::runtime_error(xivres::util::unicode::convert<std::string>(error));
}

[[nodiscard]] Dll::CheckPackageVersionResult Dll::CheckPackageVersion() {
	const auto dir = ProgramDirectory();
	std::vector<std::pair<std::string, std::string>> modules;
	try {
		modules = {
			Utils::Win32::FormatModuleVersionString(dir / XivAlexLoader64NameW),
			Utils::Win32::FormatModuleVersionString(dir / XivAlexLoaderDll32NameW),
			Utils::Win32::FormatModuleVersionString(dir / XivAlexLoaderDll64NameW),
			Utils::Win32::FormatModuleVersionString(dir / XivAlexDll64NameW),
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

Dll::VersionInformation Dll::CheckUpdates() {
	const auto response = Utils::Win32::WinHttp::Get("https://api.github.com/repos/Soreepeong/XivAlexander/releases/latest");
	const auto parsed = nlohmann::json::parse(response.Body);
	const auto& item = parsed.at("assets").at(0);

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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
	try {
		s_hModule.Attach(hInstance, Utils::Win32::LoadedModule::Null, false, "Instance attach failed <cannot happen>");
		s_hActivationContext = Utils::Win32::ActivationContext(ACTCTXW{
			.cbSize = static_cast<ULONG>(sizeof ACTCTXW),
			.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID,
			.lpResourceName = MAKEINTRESOURCE(IDR_RT_MANIFEST_LATE_ACTIVATION),
			.hModule = Dll::Module(),
		});
	} catch (const std::exception& e) {
		Utils::Win32::DebugPrint(L"wWinMain: {}", e.what());
		return -1;
	}
	return Dll::LoaderAppMain();
}
