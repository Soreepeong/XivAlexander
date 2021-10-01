#include "pch.h"
#include "App_LoaderApp_Actions_Update.h"

#include <XivAlexanderCommon/Utils_Win32_Resource.h>
#include <XivAlexanderCommon/XivAlex.h>

#include "App_LoaderApp.h"
#include "App_LoaderApp_Actions_Interactive.h"
#include "App_LoaderApp_Actions_LoadUnload.h"
#include "DllMain.h"
#include "resource.h"

using namespace XivAlexDll;

#pragma optimize("", off)
template<typename...Args>
Utils::CallOnDestruction ShowLazyProgress(bool explicitCancel, const UINT nFormatResId, Args ...args) {
	const auto format = FindStringResourceEx(Dll::Module(), nFormatResId) + 1;
	auto cont = std::make_shared<bool>(true);
	auto t = Utils::Win32::Thread(L"UpdateStatus", [explicitCancel, msg = std::format(format, std::forward<Args>(args)...), cont]() {
		while (*cont) {
			if (Dll::MessageBoxF(nullptr, (explicitCancel ? MB_OKCANCEL : MB_OK) | MB_ICONINFORMATION, msg) == (explicitCancel ? IDCANCEL : IDOK)) {
				if (*cont)
					Utils::Win32::Process::Current().Terminate(0);
				else
					return;
			}
		}
	});
	return {
		[t = std::move(t), cont = std::move(cont)]() {
			*cont = false;
			const auto tid = t.GetId();
			while (t.Wait(100) == WAIT_TIMEOUT) {
				HWND hwnd = nullptr;
				while ((hwnd = FindWindowExW(nullptr, hwnd, nullptr, nullptr))) {
					const auto hwndThreadId = GetWindowThreadProcessId(hwnd, nullptr);
					if (tid == hwndThreadId) {
						SendMessageW(hwnd, WM_CLOSE, 0, 0);
					}
				}
			}
		}
	};
}
#pragma optimize("", on)

App::LoaderApp::Actions::Update::Update(const Arguments& args)
	: m_args(args) {
}

void App::LoaderApp::Actions::Update::CheckForUpdates(std::vector<Utils::Win32::Process> prevProcesses, bool offerAutomaticUpdate) {
	if (BOOL w = FALSE; IsWow64Process(GetCurrentProcess(), &w) && w) {
		Utils::Win32::RunProgram({
			.path = Utils::Win32::Process::Current().PathOf().parent_path() / XivAlex::XivAlexLoader64NameW,
			.args = Utils::FromUtf8(Utils::Win32::ReverseCommandLineToArgv({
				"-a", LoaderActionToString(LoaderAction::UpdateCheck),
			})),
			.wait = true,
		});
		return;
	}

	const auto updateZip = Utils::Win32::Process::Current().PathOf().parent_path() / "update.zip";
	if (exists(updateZip))
		PerformUpdateAndExitIfSuccessful(prevProcesses, "", updateZip);

	try {
		std::vector<int> remote, local;
		XivAlex::VersionInformation up;
		if (m_args.m_debugUpdate) {
			local = {0, 0, 0, 0};
			remote = {0, 0, 0, 1};
		} else {
			const auto checking = ShowLazyProgress(true, IDS_UPDATE_PROGRESS_CHECKING);
			const auto [selfFileVersion, selfProductVersion] = Utils::Win32::FormatModuleVersionString(GetModuleHandleW(nullptr));
			up = XivAlex::CheckUpdates();
			const auto remoteS = Utils::StringSplit<std::string>(up.Name.substr(1), ".");
			const auto localS = Utils::StringSplit<std::string>(selfProductVersion, ".");
			for (const auto& s : remoteS)
				remote.emplace_back(std::stoi(s));
			for (const auto& s : localS)
				local.emplace_back(std::stoi(s));
		}
		if (local.size() != 4 || remote.size() != 4)
			throw std::runtime_error(Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_UPDATE_FILE_TOO_BIG) + 1));
		if (local >= remote) {
			Dll::MessageBoxF(nullptr, MB_OK | MB_ICONINFORMATION,
				IDS_UPDATE_UNAVAILABLE,
				local[0], local[1], local[2], local[3], remote[0], remote[1], remote[2], remote[3], up.PublishDate);
			return;
		}
		if (offerAutomaticUpdate) {
			while (true) {
				switch (Dll::MessageBoxF(nullptr, MB_YESNOCANCEL,
					IDS_UPDATE_CONFIRM,
					remote[0], remote[1], remote[2], remote[3], up.PublishDate, local[0], local[1], local[2], local[3],
					Utils::Win32::MB_GetString(IDYES - 1),
					Utils::Win32::MB_GetString(IDNO - 1),
					Utils::Win32::MB_GetString(IDCANCEL - 1)
				)) {
					case IDYES:
						LoaderApp::ShellExecutePathOrThrow(FindStringResourceEx(Dll::Module(), IDS_URL_RELEASES) + 1);
						break;

					case IDNO:
						PerformUpdateAndExitIfSuccessful(std::move(prevProcesses), up.DownloadLink, updateZip);
						return;

					case IDCANCEL:
						return;
				}
			}
		} else {
			// TODO: create string resource: New update is available, cannot autoupdate because being called as dll, etc etc.
			LoaderApp::ShellExecutePathOrThrow(FindStringResourceEx(Dll::Module(), IDS_URL_RELEASES) + 1);
		}
	} catch (const std::exception& e) {
		Dll::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
	}
}

int App::LoaderApp::Actions::Update::Run() {
	switch (m_args.m_action) {
		case LoaderAction::UpdateCheck:
			CheckForUpdates(m_args.m_targetProcessHandles, true);
			return 0;

		case LoaderAction::Internal_Update_DependencyDllMode:
			CheckForUpdates(m_args.m_targetProcessHandles, false);
			return 0;

		case LoaderAction::Internal_Update_Step2_ReplaceFiles:
			UpdateStep_ReplaceFiles();
			return 0;

		case LoaderAction::Internal_Update_Step3_CleanupFiles:
			UpdateStep_CleanupFiles();
			return 0;
	}
	throw std::logic_error("invalid m_action for Update");
}

bool App::LoaderApp::Actions::Update::RequiresElevationForUpdate(std::vector<DWORD> excludedPid) {
	// check if other ffxiv processes else than excludedPid exists, and if they're inaccessible.
	for (const auto pid : m_args.GetTargetPidList()) {
		try {
			if (std::ranges::find(excludedPid, pid) == excludedPid.end())
				OpenProcessForManipulation(pid);
		} catch (...) {
			return true;
		}
	}
	return false;
}

void App::LoaderApp::Actions::Update::PerformUpdateAndExitIfSuccessful(std::vector<Utils::Win32::Process> gameProcesses, const std::string& url, const std::filesystem::path& updateZip) {
	std::vector<DWORD> prevProcessIds;
	std::ranges::transform(gameProcesses, std::back_inserter(prevProcessIds), [](const Utils::Win32::Process& k) { return k.GetId(); });
	const auto& currentProcess = Utils::Win32::Process::Current();
	const auto launcherDir = currentProcess.PathOf().parent_path();
	const auto tempExtractionDir = launcherDir / L"__UPDATE__";
	const auto loaderPath32 = launcherDir / XivAlex::XivAlexLoader32NameW;
	const auto loaderPath64 = launcherDir / XivAlex::XivAlexLoader64NameW;

	{
		const auto progress = ShowLazyProgress(true, IDS_UPDATE_PROGRESS_DOWNLOADING_FILES);

		try {
			for (int i = 0; i < 2; ++i) {
				try {
					if (m_args.m_debugUpdate) {
						const auto tempPath = launcherDir / L"updatesourcetest.zip";
						libzippp::ZipArchive arc(tempPath.string());
						arc.open(libzippp::ZipArchive::Write);
						arc.addFile(Utils::ToUtf8(XivAlex::XivAlexDll32NameW), Utils::ToUtf8((launcherDir / XivAlex::XivAlexDll32NameW).wstring()));
						arc.addFile(Utils::ToUtf8(XivAlex::XivAlexDll64NameW), Utils::ToUtf8((launcherDir / XivAlex::XivAlexDll64NameW).wstring()));
						arc.addFile(Utils::ToUtf8(XivAlex::XivAlexLoader32NameW), Utils::ToUtf8((launcherDir / XivAlex::XivAlexLoader32NameW).wstring()));
						arc.addFile(Utils::ToUtf8(XivAlex::XivAlexLoader64NameW), Utils::ToUtf8((launcherDir / XivAlex::XivAlexLoader64NameW).wstring()));
						arc.close();
						copy(tempPath, updateZip);
					} else {
						if (!exists(updateZip) || i == 1) {
							std::ofstream f(updateZip, std::ios::binary);
							curlpp::Easy req;
							req.setOpt(curlpp::options::Url(url));
							req.setOpt(curlpp::options::UserAgent("Mozilla/5.0"));
							req.setOpt(curlpp::options::FollowLocation(true));
							f << req;
						}
					}

					remove_all(tempExtractionDir);

					const auto hFile = Utils::Win32::Handle(CreateFileW(updateZip.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr),
						INVALID_HANDLE_VALUE, "CreateFileW({}, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)", updateZip);
					LARGE_INTEGER fileSize;
					if (!GetFileSizeEx(hFile, &fileSize))
						throw Utils::Win32::Error("GetFileSizeEx");
					if (fileSize.QuadPart > 128 * 1024 * 1024)
						throw Utils::Win32::Error(Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_UPDATE_FILE_TOO_BIG) + 1));
					const auto hMapFile = Utils::Win32::Handle(CreateFileMappingW(hFile, nullptr, PAGE_READONLY, fileSize.HighPart, fileSize.LowPart, nullptr), nullptr, "CreateFileMappingW");
					const auto pMapped = Utils::Win32::Closeable<void*, UnmapViewOfFile>(MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, fileSize.LowPart));

					const auto pArc = libzippp::ZipArchive::fromBuffer(pMapped, fileSize.LowPart);
					const auto freeArc = Utils::CallOnDestruction([pArc]() {
						pArc->close();
						delete pArc;
					});
					for (const auto& entry : pArc->getEntries()) {
						const auto pData = static_cast<char*>(entry.readAsBinary());
						const auto freeData = Utils::CallOnDestruction([pData]() { delete[] pData; });
						const auto targetPath = launcherDir / tempExtractionDir / entry.getName();
						create_directories(targetPath.parent_path());
						std::ofstream f(targetPath, std::ios::binary);
						f.write(pData, static_cast<std::streamsize>(entry.getSize()));
					}
					break;
				} catch (...) {
					if (url.empty())
						return;

					if (i == 1)
						throw;
				}
			}
		} catch (const std::exception&) {
			remove_all(tempExtractionDir);
			remove(updateZip);
			throw;
		}
	}
	// TODO: ask user to quit all FFXIV related processes, except the game itself.

	if (RequiresElevationForUpdate(prevProcessIds)) {
		Utils::Win32::RunProgram({
			.args = std::format(L"--action {}", LoaderActionToString(LoaderAction::UpdateCheck)),
			.elevateMode = Utils::Win32::RunProgramParams::Force,
		});
		return;
	}

	{
		const auto progress = ShowLazyProgress(true, IDS_UPDATE_PROGRESS_PREPARING_FILES);

		std::vector unloadTargets = gameProcesses;
		for (const auto pid : Utils::Win32::GetProcessList()) {
			if (pid == GetCurrentProcessId() || std::ranges::find(prevProcessIds, pid) != prevProcessIds.end())
				continue;

			std::filesystem::path processPath;

			try {
				processPath = Utils::Win32::Process(PROCESS_QUERY_LIMITED_INFORMATION, false, pid).PathOf();
			} catch (const std::exception&) {
				try {
					processPath = Utils::Win32::Process(PROCESS_QUERY_INFORMATION, false, pid).PathOf();
				} catch (const std::exception&) {
					// ¯\_(ツ)_/¯
					// unlikely that we can't access information of process that has anything to do with xiv,
					// unless it's antivirus, in which case we can't do anything, which will result in failure anyway.
					continue;
				}
			}
			if (lstrcmpiW(processPath.c_str(), loaderPath32.c_str()) == 0 || lstrcmpiW(processPath.c_str(), loaderPath64.c_str()) == 0) {
				while (true) {
					try {
						const auto hProcess = Utils::Win32::Process(PROCESS_TERMINATE | SYNCHRONIZE, false, pid);
						if (hProcess.Wait(0) != WAIT_TIMEOUT)
							break;
						hProcess.Terminate(0);
					} catch (const Utils::Win32::Error& e) {
						if (e.Code() == ERROR_INVALID_PARAMETER)  // this process already gone
							break;
						if (Dll::MessageBoxF(nullptr, MB_OKCANCEL, IDS_UPDATE_PROCESS_KILL_FAILURE, pid, e.what()) == IDCANCEL)
							return;
					}
				}

			} else if (lstrcmpiW(processPath.filename().c_str(), XivAlex::GameExecutable64NameW) == 0 || lstrcmpiW(processPath.filename().c_str(), XivAlex::GameExecutable32NameW) == 0) {
				auto process = OpenProcessForManipulation(pid);
				gameProcesses.emplace_back(process);
				unloadTargets.emplace_back(std::move(process));
			} else {
				try {
					auto process = OpenProcessForManipulation(pid);
					const auto is64 = process.IsProcess64Bits();
					const auto modulePath = launcherDir / (is64 ? XivAlex::XivAlexDll64NameW : XivAlex::XivAlexDll32NameW);
					if (process.AddressOf(modulePath, Utils::Win32::Process::ModuleNameCompareMode::FullPath, false))
						unloadTargets.emplace_back(std::move(process));
				} catch (...) {
				}
			}
		}

		LaunchXivAlexLoaderWithTargetHandles(unloadTargets, LoaderAction::Internal_Inject_UnloadFromHandle, true);
		LaunchXivAlexLoaderWithTargetHandles(gameProcesses, LoaderAction::Internal_Update_Step2_ReplaceFiles, false, currentProcess, Current, tempExtractionDir);

		currentProcess.Terminate(0);
	}
}

void App::LoaderApp::Actions::Update::UpdateStep_ReplaceFiles() {
	const auto checking = ShowLazyProgress(true, IDS_UPDATE_PROGRESS_UPDATING_FILES);
	const auto temporaryUpdatePath = Utils::Win32::Process::Current().PathOf().parent_path();
	const auto targetUpdatePath = temporaryUpdatePath.parent_path();
	if (temporaryUpdatePath.filename() != L"__UPDATE__")
		throw std::runtime_error("cannot update outside of update process");
	copy(
		temporaryUpdatePath,
		targetUpdatePath,
		std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing
	);
	LaunchXivAlexLoaderWithTargetHandles(m_args.m_targetProcessHandles, LoaderAction::Internal_Update_Step3_CleanupFiles, false, Utils::Win32::Process::Current(), Current, targetUpdatePath);
}

void App::LoaderApp::Actions::Update::UpdateStep_CleanupFiles() {
	{
		const auto checking = ShowLazyProgress(true, IDS_UPDATE_CLEANUP_UPDATE);
		remove_all(Utils::Win32::Process::Current().PathOf().parent_path() / L"__UPDATE__");
		remove(Utils::Win32::Process::Current().PathOf().parent_path() / L"update.zip");
	}

	if (m_args.m_targetProcessHandles.empty()) {
		Interactive(m_args).Run();

	} else {
		const auto checking = ShowLazyProgress(true, IDS_UPDATE_RELOADING_XIVALEXANDER);
		auto pids = m_args.GetTargetPidList();
		for (const auto& process : m_args.m_targetProcessHandles)
			pids.erase(process.GetId());

		if (!m_args.m_targetProcessHandles.empty())
			LaunchXivAlexLoaderWithTargetHandles(m_args.m_targetProcessHandles, LoaderAction::Internal_Inject_LoadXivAlexanderImmediately, true);

		for (const auto& pid : pids) {
			try {
				LoadUnload(m_args).Load(pid);
			} catch (...) {
				// pass
			}
		}

		{
			const auto checking = ShowLazyProgress(false, IDS_UPDATE_COMPLETE);
			Sleep(3000);
		}
	}
}
