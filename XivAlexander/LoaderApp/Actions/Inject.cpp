#include "pch.h"
#include "Inject.h"

#include "Utils/Win32/InjectedModule.h"

using namespace Dll;

XivAlexander::LoaderApp::Actions::Inject::Inject(const Arguments& args)
	: m_args(args)
	, m_loaderDllPath(ProgramDirectory() / XivAlexLoaderDll64NameW) {
}

int XivAlexander::LoaderApp::Actions::Inject::Run() {
	switch (m_args.m_action) {
		case LoaderAction::Internal_Inject_HookEntryPoint:
			HookEntryPoint();
			return 0;

		case LoaderAction::Internal_Inject_LoadXivAlexanderImmediately:
			InjectOrCleanup(true);
			return 0;

		case LoaderAction::Internal_Inject_UnloadFromHandle:
			InjectOrCleanup(false);
			return 0;
	}

	throw std::logic_error("invalid m_argument for Inject");
}

// Both bitnesses are handled here; nothing is handed to another loader process.
void XivAlexander::LoaderApp::Actions::Inject::HookEntryPoint() {
	for (auto& process : m_args.m_targetProcessHandles)
		PatchEntryPointForInjection(process);
}

void XivAlexander::LoaderApp::Actions::Inject::InjectOrCleanup(bool inject) {
	for (auto& process : m_args.m_targetProcessHandles) {
		try {
			if (process.IsProcess64Bits()) {
				const auto m = Utils::Win32::InjectedModule(process, m_loaderDllPath);
				if (inject)
					m.Call("EnableXivAlexander", reinterpret_cast<void*>(1), "EnableXivAlexander(1)");
				else
					m.Call("DisableAllApps", nullptr, "DisableAllApps(0)");
			} else if (!inject) {
				// Only 32-bit launchers have XivAlexanderLoader32.dll; it has nothing to enable.
				const auto dll32 = m_loaderDllPath.parent_path() / XivAlexLoaderDll32NameW;
				if (process.AddressOf(dll32, Utils::Win32::Process::ModuleNameCompareMode::FullPath, false)) {
					const auto m = Utils::Win32::InjectedModule(process, dll32);
					m.Call("DisableAllApps", nullptr, "DisableAllApps(0)");
				}
			}
		} catch (...) {
		}
	}
}
