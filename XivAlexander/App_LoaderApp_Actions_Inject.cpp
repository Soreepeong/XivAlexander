#include "pch.h"
#include "App_LoaderApp_Actions_Inject.h"

#include <XivAlexanderCommon/Utils_Win32_InjectedModule.h>
#include <XivAlexanderCommon/XivAlex.h>

using namespace XivAlexDll;

App::LoaderApp::Actions::Inject::Inject(const Arguments& args)
	: m_args(args)
	, m_dllPath(Utils::Win32::Process::Current().PathOf().parent_path() / XivAlex::XivAlexDllNameW) {
}

int App::LoaderApp::Actions::Inject::Run() {
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

void App::LoaderApp::Actions::Inject::HookEntryPoint() {
	std::vector<Utils::Win32::Process> mine, defer;
	for (auto& process : m_args.m_targetProcessHandles) {
		if (process.IsProcess64Bits() == (INT64_MAX == INTPTR_MAX))
			mine.emplace_back(process);
		else
			defer.emplace_back(process);
	}
	for (auto& process : mine)
		PatchEntryPointForInjection(process);

	if (!defer.empty())
		LaunchXivAlexLoaderWithTargetHandles(defer, m_args.m_action, true, {}, Opposite);
}

void App::LoaderApp::Actions::Inject::InjectOrCleanup(bool inject) {
	std::vector<Utils::Win32::Process> mine, defer;
	for (auto& process : m_args.m_targetProcessHandles) {
		if (process.IsProcess64Bits() == (INT64_MAX == INTPTR_MAX))
			mine.emplace_back(process);
		else
			defer.emplace_back(process);
	}
	for (auto& process : mine) {
		try {
			const auto m = Utils::Win32::InjectedModule(std::move(process), m_dllPath);
			if (inject)
				m.Call("EnableXivAlexander", reinterpret_cast<void*>(1), "EnableXivAlexander(1)");
			else
				m.Call("DisableAllApps", nullptr, "DisableAllApps(0)");
		} catch (...) {
			// pass
		}
	}

	if (!defer.empty())
		LaunchXivAlexLoaderWithTargetHandles(defer, m_args.m_action, true, {}, Opposite);
}
