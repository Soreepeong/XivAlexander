#include "pch.h"
#include "App_LoaderApp_Arguments.h"

#include <XivAlexanderCommon/Utils_Win32_Resource.h>
#include <XivAlexanderCommon/XivAlex.h>

#include "App_LoaderApp.h"
#include "DllMain.h"
#include "resource.h"

using namespace XivAlexDll;

App::LoaderApp::Arguments::Arguments()
	: argp("XivAlexanderLoader") {

	argp.add_argument("-a", "--action")
		.help(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(Dll::Module(), IDS_HELP_ACTION) + 1))
		.required()
		.nargs(1)
		.default_value(LoaderAction::Interactive)
		.action([](const std::string& val_) {
			if (val_.empty())
				return LoaderAction::Interactive;
			auto valw = Utils::FromUtf8(val_);
			CharLowerW(&valw[0]);
			auto val = Utils::ToUtf8(valw);
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
		});
	argp.add_argument("-l", "--launcher")
		.help(Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_HELP_LAUNCHER) + 1))
		.required()
		.nargs(1)
		.default_value(LauncherType::Auto)
		.action([](const std::string& val_) {
			auto val = Utils::FromUtf8(val_);
			CharLowerW(&val[0]);
			for (size_t i = 0; i < static_cast<size_t>(LauncherType::Count_); ++i) {
				auto compare = Utils::FromUtf8(argparse::details::repr(static_cast<LauncherType>(i)));
				CharLowerW(&compare[0]);

				auto equal = true;
				for (size_t j = 0; equal && j < val.length() && j < compare.length(); ++j)
					equal = val[j] == compare[j];
				if (equal)
					return static_cast<LauncherType>(i);
			}
			if (val[0] == L'e' || val[0] == L'd' || val[0] == L'g' || val[0] == L'f' || val[0] == L'j')
				return LauncherType::International;

			throw std::runtime_error(Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_ERROR_INVALID_LAUNCHER_TYPE) + 1));
		});
	argp.add_argument("-q", "--quiet")
		.help(Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_HELP_QUIET) + 1))
		.default_value(false)
		.implicit_value(true);
	argp.add_argument("-d", "--disable-runas")
		.help(Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_HELP_DISABLE_RUNAS) + 1))
		.default_value(false)
		.implicit_value(true);

	// internal use
	argp.add_argument("--handle-instead-of-pid")
		.help(Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_HELP_INTERNAL_USE_ONLY) + 1))
		.required()
		.default_value(false)
		.implicit_value(true);
	argp.add_argument("--wait-process")
		.help(Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_HELP_INTERNAL_USE_ONLY) + 1))
		.required()
		.nargs(1)
		.default_value(Utils::Win32::Process())
		.action([](const std::string& val) {
			return Utils::Win32::Process(reinterpret_cast<HANDLE>(std::stoull(val, nullptr, 0)), true);
		});

	argp.add_argument("targets")
		.help(Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_HELP_TARGETS) + 1))
		.remaining();
}

void App::LoaderApp::Arguments::Parse() {
	std::vector<std::string> args;

	if (int nArgs; LPWSTR* szArgList = CommandLineToArgvW(Dll::GetOriginalCommandLine().data(), &nArgs)) {
		for (int i = 0; i < nArgs; i++)
			args.emplace_back(Utils::ToUtf8(szArgList[i]));
		LocalFree(szArgList);
	}

	// prevent argparse from taking over --help
	if (std::find(args.begin() + 1, args.end(), std::string("-h")) != args.end()
		|| std::find(args.begin() + 1, args.end(), std::string("-help")) != args.end()) {
		m_help = true;
		return;
	}

	argp.parse_args(args);

	m_action = argp.get<XivAlexDll::LoaderAction>("-a");
	m_launcherType = argp.get<LauncherType>("-l");
	m_quiet = argp.get<bool>("-q");
	m_disableAutoRunAs = argp.get<bool>("-d");
	if (const auto waitHandle = argp.get<Utils::Win32::Process>("--wait-process"))
		waitHandle.Wait();

	if (argp.present("targets")) {
		const auto targets = argp.get<std::vector<std::string>>("targets");
		if (argp.get<bool>("--handle-instead-of-pid")) {
			for (const auto& target : targets) {
				m_targetProcessHandles.emplace_back(reinterpret_cast<HANDLE>(std::stoull(target, nullptr, 0)), true);
			}

		} else {
			if (!targets.empty()) {
				m_runProgram = Utils::FromUtf8(targets[0]);
				m_runProgramArgs = Utils::FromUtf8(Utils::Win32::ReverseCommandLineToArgv(std::span(targets.begin() + 1, targets.end())));
			}

			for (const auto& target : targets) {
				size_t idx = 0;
				DWORD pid = 0;

				try {
					pid = std::stoi(target, &idx);
				} catch (const std::invalid_argument&) {
					// empty
				} catch (const std::out_of_range&) {
					// empty
				}
				if (idx != target.length()) {
					auto buf = Utils::FromUtf8(target);
					CharLowerW(&buf[0]);
					m_targetSuffix.emplace(std::move(buf));
				} else {
					m_targetPids.insert(pid);
				}
			}
		}
	}

	if (m_targetPids.empty() && m_targetSuffix.empty()) {
		m_targetSuffix.emplace(XivAlex::GameExecutable32NameW);
		m_targetSuffix.emplace(XivAlex::GameExecutable64NameW);
	}
}

std::wstring App::LoaderApp::Arguments::GetHelpMessage() const {
	return std::format(FindStringResourceEx(Dll::Module(), IDS_APP_DESCRIPTION) + 1, argp.help().str());
}

std::set<DWORD> App::LoaderApp::Arguments::GetTargetPidList() const {
	std::set<DWORD> pids;
	if (!m_targetPids.empty()) {
		auto list = Utils::Win32::GetProcessList();
		std::ranges::sort(list);
		std::ranges::set_intersection(list, m_targetPids, std::inserter(pids, pids.end()));
		pids.insert(m_targetPids.begin(), m_targetPids.end());
	}
	if (!m_targetSuffix.empty()) {
		std::ranges::copy_if(Utils::Win32::GetProcessList(), std::inserter(pids, pids.end()), [this](DWORD pid) {
			try {
				const auto hProcess = OpenProcessForInformation(pid);
				auto pathbuf = hProcess.PathOf().wstring();
				CharLowerW(&pathbuf[0]);
				for (const auto& suffix : m_targetSuffix) {
					if (pathbuf.ends_with(suffix))
						return true;
				}
			} catch (const std::exception& e) {
				OutputDebugStringW(std::format(L"Error for PID {}: {}\n", pid, e.what()).c_str());
			}
			return false;
		});
	}
	return pids;
}
