#include "pch.h"
#include "App_LoaderApp_Actions_LoadUnload.h"

#include <XivAlexanderCommon/Utils_Win32_InjectedModule.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>

#include "App_LoaderApp.h"
#include "DllMain.h"
#include "resource.h"

using namespace XivAlexDll;

void App::LoaderApp::Actions::LoadUnload::RunForPid(DWORD pid) {
	const auto process = OpenProcessForManipulation(pid);

	if (process.IsProcess64Bits() != Utils::Win32::Process::Current().IsProcess64Bits()) {
		Utils::Win32::RunProgram({
			.path = Utils::Win32::Process::Current().PathOf().parent_path() / (process.IsProcess64Bits() ? XivAlexDll::XivAlexLoader64NameW : XivAlexDll::XivAlexLoader32NameW),
			.args = std::format(L"{}-a {} {}",
				m_args.m_quiet ? L"-q " : L"",
				LoaderActionToString(m_args.m_action),
				process.GetId()),
			.wait = true
		});
		return;
	}

	const auto path = process.PathOf();
	auto loaderAction = m_args.m_action;
	if (loaderAction == LoaderAction::Ask || loaderAction == LoaderAction::Interactive) {
		if (IsXivAlexanderLoaded(process)) {
			switch (Dll::MessageBoxF(nullptr, MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1,
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
			switch (Dll::MessageBoxF(nullptr, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1,
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

bool App::LoaderApp::Actions::LoadUnload::IsXivAlexanderLoaded(const Utils::Win32::Process& process) {
	return process.AddressOf(m_dllPath, Utils::Win32::Process::ModuleNameCompareMode::FullPath, false);
}

void App::LoaderApp::Actions::LoadUnload::Load(DWORD pid) {
	return LoadOrUnload(OpenProcessForManipulation(pid), true);
}

void App::LoaderApp::Actions::LoadUnload::Unload(DWORD pid) {
	return LoadOrUnload(OpenProcessForManipulation(pid), false);
}

void App::LoaderApp::Actions::LoadUnload::LoadOrUnload(const Utils::Win32::Process& process, bool load) {
	if (process.IsProcess64Bits() != Utils::Win32::Process::Current().IsProcess64Bits()
		|| (!Utils::Win32::IsUserAnAdmin() && TestAdminRequirementForProcessManipulation({process.GetId()}))) {
		Utils::Win32::RunProgram({
			.path = Utils::Win32::Process::Current().PathOf().parent_path() / (process.IsProcess64Bits() ? XivAlexDll::XivAlexLoader64NameW : XivAlexDll::XivAlexLoader32NameW),
			.args = std::format(L"-a {} {}",
				LoaderActionToString(load ? LoaderAction::Load : LoaderAction::Unload),
				process.GetId()),
			.wait = true
		});
		return;
	}

	const auto injectedModule = Utils::Win32::InjectedModule(process, m_dllPath);
	auto unloadRequired = false;
	const auto cleanup = Utils::CallOnDestruction([&injectedModule, &unloadRequired]() {
		if (unloadRequired)
			injectedModule.Call("EnableXivAlexander", nullptr, "EnableXivAlexander(0)");
	});

	if (load) {
		unloadRequired = true;
		if (const auto loadResult = injectedModule.Call("EnableXivAlexander", reinterpret_cast<void*>(1), "EnableXivAlexander(1)"); loadResult == 0)
			unloadRequired = false;
	} else
		unloadRequired = true;
}

App::LoaderApp::Actions::LoadUnload::LoadUnload(const Arguments& args)
	: m_args(args)
	, m_dllPath(Utils::Win32::Process::Current().PathOf().parent_path() / XivAlexDll::XivAlexDllNameW) {
}

int App::LoaderApp::Actions::LoadUnload::Run() {
	const auto pids = m_args.GetTargetPidList();
	if (pids.empty()) {
		if (!m_args.m_quiet) {
			std::wstring errors;
			if (m_args.m_targetPids.empty() && m_args.m_targetSuffix.empty())
				errors = std::format(Utils::Win32::FindStringResourceEx(Dll::Module(), IDS_ERROR_NO_FFXIV_PROCESS) + 1, XivAlexDll::GameExecutableNameW);
			else
				errors = FindStringResourceEx(Dll::Module(), IDS_ERROR_NO_MATCHING_PROCESS) + 1;
			Dll::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, errors);
		}
		return -1;
	}

	std::string debugPrivilegeError;
	try {
		Utils::Win32::AddDebugPrivilege();
	} catch (const std::exception& err) {
		debugPrivilegeError = Utils::ToUtf8(std::format(FindStringResourceEx(Dll::Module(), IDS_ERROR_SEDEBUGPRIVILEGE) + 1, err.what()));
	}

	for (const auto pid : pids) {
		try {
			RunForPid(pid);
		} catch (const std::exception& e) {
			if (!m_args.m_quiet)
				Dll::MessageBoxF(nullptr, MB_OK | MB_ICONERROR,
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
	return 0;
}
