#include "pch.h"

#include "Utils/Win32.h"
#include "Utils/Win32/Process.h"
#include "Utils/Win32/Resource.h"

#include "resource.h"
#include "XivAlexander.h"
#ifndef XIVALEXANDER_LOADER
#include "Config.h"
#endif

// Everything here is also built into XivAlexanderLoader64.exe.

const char* Dll::LoaderActionToString(LoaderAction val) {
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

DWORD Dll::LaunchXivAlexLoaderWithTargetHandles(
	const std::vector<Utils::Win32::Process>& hSources,
	LoaderAction action,
	bool wait,
	const Utils::Win32::Process& waitForBeforeStarting,
	const std::filesystem::path& loaderPath) {

#ifndef XIVALEXANDER_LOADER
	const auto companionPath = loaderPath.empty() ? (
		lstrcmpiW(Utils::Win32::Process::Current().PathOf().filename().wstring().c_str(), GameExecutableNameW) == 0
		? XivAlexander::Config::Acquire()->Init.ResolveXivAlexInstallationPath()
		: Module().PathOf().parent_path()
	) : loaderPath;
#else
	const auto companionPath = loaderPath.empty() ? ProgramDirectory() : loaderPath;
#endif

	const auto companion = companionPath / XivAlexLoaderNameW;

	if (!exists(companion))
		throw std::runtime_error(xivres::util::unicode::convert<std::string>(std::vformat(FindStringResourceEx(Module(), IDS_ERROR_LOADER_NOT_FOUND) + 1, std::make_wformat_args(companion))));

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
	return MessageBoxF(hWnd, uType, xivres::util::unicode::convert<std::wstring>(text));
}

int Dll::MessageBoxF(HWND hWnd, UINT uType, const wchar_t* text) {
	return MessageBoxW(hWnd, text, GetGenericMessageBoxTitle(), uType);
}

int Dll::MessageBoxF(HWND hWnd, UINT uType, const char* text) {
	return MessageBoxF(hWnd, uType, xivres::util::unicode::convert<std::wstring>(text));
}

int Dll::MessageBoxF(HWND hWnd, UINT uType, UINT stringResId) {
	return MessageBoxF(hWnd, uType, GetStringResFromId(stringResId));
}

bool Dll::IsXivAlexanderDll(const std::filesystem::path& dllPath) {
	DWORD verHandle = 0;
	std::vector<BYTE> block;
	block.resize(GetFileVersionInfoSizeW(dllPath.c_str(), &verHandle));
	if (block.empty()) {
		const auto err = GetLastError();
		if (err == ERROR_RESOURCE_TYPE_NOT_FOUND)
			return false;
		throw Utils::Win32::Error(err, "GetFileVersionInfoSizeW");
	}
	if (!GetFileVersionInfoW(dllPath.c_str(), 0, static_cast<DWORD>(block.size()), block.data()))
		throw Utils::Win32::Error("GetFileVersionInfoW");
	struct LANGANDCODEPAGE {
		WORD wLanguage;
		WORD wCodePage;
	} * lpTranslate;
	UINT cbTranslate;
	if (!VerQueryValueW(block.data(),
		TEXT("\\VarFileInfo\\Translation"),
		reinterpret_cast<LPVOID*>(&lpTranslate),
		&cbTranslate))
		return false;

	for (size_t i = 0; i < (cbTranslate / sizeof(struct LANGANDCODEPAGE)); i++) {
		wchar_t* buf = nullptr;
		UINT size = 0;
		if (!VerQueryValueW(block.data(),
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
		// The loader DLL, or XivAlexander64.dll as copied into game folders before there were loader DLLs.
		if (currName == L"XivAlexander Loader DLL" || currName == L"XivAlexander Main DLL")
			return true;
	}
	return false;
}
