#include "pch.h"
#include "LoadUnload.h"

#include "Utils/Win32/InjectedModule.h"
#include "Utils/Win32/Resource.h"

#include "LoaderApp/App.h"
#include "resource.h"
#include "XivAlexander.h"

using namespace Dll;

void XivAlexander::LoaderApp::Actions::LoadUnload::RunForPid(DWORD pid) {
	const auto process = OpenProcessForManipulation(pid);

	const auto path = process.PathOf();
	auto loaderAction = m_args.m_action;
	if (loaderAction == LoaderAction::Ask || loaderAction == LoaderAction::Interactive) {
		if (IsXivAlexanderLoaded(process)) {
			switch (MessageBoxF(nullptr, MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1,
				IDS_CONFIRM_INJECT_AGAIN,
				pid, path,
				Utils::Win32::MB_GetString(IDYES - 1),
				Utils::Win32::MB_GetString(IDNO - 1),
				Utils::Win32::MB_GetString(IDCANCEL - 1)
			)) {
				case IDYES:
					loaderAction = LoaderAction::Load;
					break;
				case IDNO:
					loaderAction = LoaderAction::Unload;
					break;
				case IDCANCEL:
					loaderAction = LoaderAction::Count_;
			}
		} else {
			switch (MessageBoxF(nullptr, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1,
				IDS_CONFIRM_INJECT,
				pid, path)) {
				case IDYES:
					loaderAction = LoaderAction::Load;
					break;
				case IDNO:
					loaderAction = LoaderAction::Count_;
			}
		}
	}

	switch (loaderAction) {
		case LoaderAction::Load:
		case LoaderAction::Unload:
			return LoadOrUnload(process, loaderAction == LoaderAction::Load);
	}
}

bool XivAlexander::LoaderApp::Actions::LoadUnload::IsXivAlexanderLoaded(const Utils::Win32::Process& process) {
	return process.AddressOf(m_featureDllPath, Utils::Win32::Process::ModuleNameCompareMode::FullPath, false);
}

void XivAlexander::LoaderApp::Actions::LoadUnload::Load(DWORD pid) {
	return LoadOrUnload(OpenProcessForManipulation(pid), true);
}

void XivAlexander::LoaderApp::Actions::LoadUnload::Unload(DWORD pid) {
	return LoadOrUnload(OpenProcessForManipulation(pid), false);
}

void XivAlexander::LoaderApp::Actions::LoadUnload::LoadOrUnload(const Utils::Win32::Process& process, bool load) {
	// Elevation is decided once by the caller; re-running a loader from here could chain without end.
	if (!process.IsProcess64Bits())
		throw std::runtime_error(xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_LOADER_ERROR_LOAD_32BIT) + 1));

	const auto injectedModule = Utils::Win32::InjectedModule(process, m_loaderDllPath);
	if (!load) {
		if (const auto result = injectedModule.Call("EnableXivAlexander", nullptr, "EnableXivAlexander(0)"); result != 0)
			throw std::runtime_error(xivres::util::unicode::convert<std::string>(std::vformat(FindStringResourceEx(Module(), IDS_LOADER_ERROR_UNLOAD) + 1, std::make_wformat_args(result))));
		return;
	}

	// A failed start leaves nothing running; this must not throw, as the process may have exited meanwhile.
	const auto disable = [&injectedModule] {
		try {
			injectedModule.Call("EnableXivAlexander", nullptr, "EnableXivAlexander(0)");
		} catch (...) {
			Utils::Win32::DebugPrint(L"EnableXivAlexander(0) failed");
		}
	};
	int loadResult;
	try {
		loadResult = injectedModule.Call("EnableXivAlexander", reinterpret_cast<void*>(1), "EnableXivAlexander(1)");
	} catch (...) {
		disable();
		throw;
	}
	if (loadResult != 0) {
		disable();
		throw std::runtime_error(xivres::util::unicode::convert<std::string>(std::vformat(FindStringResourceEx(Module(), IDS_ERROR_LOAD) + 1, std::make_wformat_args(loadResult))));
	}
}

XivAlexander::LoaderApp::Actions::LoadUnload::LoadUnload(const Arguments& args)
	: m_args(args)
	, m_loaderDllPath(ProgramDirectory() / XivAlexLoaderDll64NameW)
	, m_featureDllPath(ProgramDirectory() / XivAlexDll64NameW) {
}

int XivAlexander::LoaderApp::Actions::LoadUnload::Run() {
	const auto pids = m_args.GetTargetPidList();
	if (pids.empty()) {
		if (!m_args.m_quiet) {
			std::wstring errors;
			if (m_args.m_targetPids.empty() && m_args.m_targetSuffix.empty())
				errors = std::vformat(Utils::Win32::FindStringResourceEx(Module(), IDS_ERROR_NO_FFXIV_PROCESS) + 1, std::make_wformat_args(GameExecutableNameW));
			else
				errors = FindStringResourceEx(Module(), IDS_ERROR_NO_MATCHING_PROCESS) + 1;
			MessageBoxF(nullptr, MB_OK | MB_ICONERROR, errors);
		}
		return -1;
	}

	std::string debugPrivilegeError;
	try {
		Utils::Win32::AddDebugPrivilege();
	} catch (const std::exception& err) {
		const auto s = err.what();
		debugPrivilegeError = xivres::util::unicode::convert<std::string>(std::vformat(FindStringResourceEx(Module(), IDS_ERROR_SEDEBUGPRIVILEGE) + 1, std::make_wformat_args(s)));
	}

	auto result = 0;
	for (const auto pid : pids) {
		try {
			RunForPid(pid);
		} catch (const std::exception& e) {
			result = -1;
			if (m_args.m_quiet)
				ReportError(true, std::format(L"PID {}: {}", pid, xivres::util::unicode::convert<std::wstring>(e.what())));
			else
				MessageBoxF(nullptr, MB_OK | MB_ICONERROR,
					L"PID: {}\n"
					L"\n"
					L"SeDebugPrivilege: {}\n"
					L"\n"
					L"* {}",
					pid,
					debugPrivilegeError,
					e.what()
				);
		}
	}
	return result;
}
