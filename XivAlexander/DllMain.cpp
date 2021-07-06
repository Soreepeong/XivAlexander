#include "pch.h"
#include "DllMain.h"
#include "resource.h"
#include "XivAlexander/XivAlexander.h"

static Utils::Win32::LoadedModule s_hModule;
static Utils::Win32::ActivationContext s_hActivationContext;

const Utils::Win32::LoadedModule& Dll::Module() {
	return s_hModule;
}

const Utils::Win32::ActivationContext& Dll::ActivationContext() {
	return s_hActivationContext;
}

const char* XivAlexDll::LoaderActionToString(LoaderAction val) {
	switch (val) {
	case LoaderAction::Auto: return "auto";
	case LoaderAction::Ask: return "ask";
	case LoaderAction::Load: return "load";
	case LoaderAction::Unload: return "unload";
	case LoaderAction::Launcher: return "launcher";
	case LoaderAction::UpdateCheck: return "update-check";
	case LoaderAction::Internal_Update_Step2_ReplaceFiles: return "_internal_update_step2_replacefiles";
	case LoaderAction::Internal_Update_Step3_CleanupFiles: return "_internal_update_step3_cleanupfiles";
	case LoaderAction::Internal_Inject_HookEntryPoint: return "_internal_inject_hookentrypoint";
	case LoaderAction::Internal_Inject_LoadXivAlexanderImmediately: return "_internal_inject_loadxivalexanderimmediately";
	case LoaderAction::Internal_Cleanup_Handle: return "_internal_cleanup_handle";
	}
	return "<invalid>";
}

XivAlexDll::LoaderAction XivAlexDll::ParseLoaderAction(std::string val) {
	auto valw = Utils::FromUtf8(val);
	CharLowerW(&valw[0]);
	val = Utils::ToUtf8(valw);
	for (size_t i = 0; i < static_cast<size_t>(LoaderAction::Count_); ++i) {
		const auto compare = std::string(LoaderActionToString(static_cast<LoaderAction>(i)));
		auto equal = true;
		for (size_t j = 0; equal && j < val.length() && j < compare.length(); ++j) {
			equal = val[j] == compare[j];
		}
		if (equal)
			return static_cast<LoaderAction>(i);
	}
	throw std::runtime_error("invalid LoaderAction");
}

XIVALEXANDER_DLLEXPORT DWORD XivAlexDll::LaunchXivAlexLoaderWithTargetHandles(
		const std::vector<Utils::Win32::Process>& hSources,
		LoaderAction action,
		bool wait,
		const std::filesystem::path& launcherPath,
		const Utils::Win32::Process& waitFor) {
	const auto companion = launcherPath.empty() ? Dll::Module().PathOf().parent_path() / XivAlex::XivAlexLoaderNameW : launcherPath;
	if (!exists(companion))
		throw std::runtime_error(std::format("loader not found: {}", companion));
	
	Utils::Win32::Process companionProcess;
	{
		Utils::Win32::Handle hStdinRead, hStdinWrite;
		if (auto r = INVALID_HANDLE_VALUE, w = INVALID_HANDLE_VALUE;
			!CreatePipe(&r, &w, nullptr, 0))
			throw Utils::Win32::Error("CreatePipe");
		else {
			hStdinRead.Attach(r, INVALID_HANDLE_VALUE, true, "CreatePipe(Read)");
			hStdinWrite.Attach(w, INVALID_HANDLE_VALUE, true, "CreatePipe(Write)");
		}

		Utils::Win32::Handle hInheritableStdinRead;
		if (auto h = INVALID_HANDLE_VALUE;
			!DuplicateHandle(GetCurrentProcess(), hStdinRead, GetCurrentProcess(), &h, 0, TRUE, DUPLICATE_SAME_ACCESS))
			throw Utils::Win32::Error("DuplicateHandle(hStdinRead)");
		else
			hInheritableStdinRead.Attach(h, INVALID_HANDLE_VALUE, true, "DuplicateHandle(hStdinRead.2)");

		{
			STARTUPINFOW si{};
			PROCESS_INFORMATION pi{};

			si.cb = sizeof si;
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdInput = hInheritableStdinRead;
			si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
			si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
			si.wShowWindow = SW_SHOW;

			auto args = std::format(L"\"{}\" --handle-instead-of-pid --action {}", companion, LoaderActionToString(action));
			std::vector<Utils::Win32::Process> duplicatedHandles;
			if (waitFor) {
				auto d = Utils::Win32::Process::DuplicateFrom<Utils::Win32::Process>(waitFor, true);
				args += std::format(L" --wait-process {}", d.Value());
				duplicatedHandles.emplace_back(std::move(d));
			}
			for (const auto& h : hSources) {
				auto d = Utils::Win32::Process::DuplicateFrom<Utils::Win32::Process>(h, true);
				args += std::format(L" {}", d.Value());
				duplicatedHandles.emplace_back(std::move(d));
			}
			
			if (!CreateProcessW(companion.c_str(), &args[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
				throw Utils::Win32::Error("CreateProcess");
			
			assert(pi.hThread);
			assert(pi.hProcess);
			
			CloseHandle(pi.hThread);
			companionProcess.Attach(pi.hProcess, true, "CreateProcess");
		}
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

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {	
	switch (fdwReason) {
		case DLL_PROCESS_ATTACH:
		{
			try {
				s_hModule.Attach(hInstance, Utils::Win32::LoadedModule::Null, false, "Instance attach failed <cannot happen>");
				s_hActivationContext = Utils::Win32::ActivationContext(ACTCTXW {
					.cbSize = sizeof ACTCTXW,
					.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID,
					.lpResourceName = MAKEINTRESOURCE(IDR_RT_MANIFEST_LATE_ACTIVATION),
					.hModule = Dll::Module(),
				});
				MH_Initialize();
			} catch (const std::exception& e) {
				Utils::Win32::DebugPrint(L"DllMain({:x}, DLL_PROCESS_ATTACH, {}) Error: {}",
					reinterpret_cast<size_t>(hInstance), reinterpret_cast<size_t>(lpReserved), e.what());
				return FALSE;
			}
			return TRUE;
		}

		case DLL_PROCESS_DETACH:
		{
			auto fail = false;
			if (const auto res = MH_Uninitialize(); res != MH_OK) {
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

extern "C" __declspec(dllexport) int __stdcall XivAlexDll::DisableAllApps(void*) {
	EnableXivAlexander(0);
	EnableInjectOnCreateProcess(0);
	return 0;
}

extern "C" __declspec(dllexport) int __stdcall XivAlexDll::CallFreeLibrary(void*) {
	FreeLibraryAndExitThread(Dll::Module(), 0);
}
