#include "pch.h"
#include "Update.h"

#include "Utils/Win32/Resource.h"

#include "Utils/WinHttp.h"
#include "LoaderApp/App.h"
#include "LoaderApp/Actions/Interactive.h"
#include "LoaderApp/Actions/LoadUnload.h"
#include "resource.h"
#include "XivAlexander.h"

using namespace Dll;

#pragma optimize("", off)
template<typename...Args>
xivres::util::on_dtor ShowLazyProgress(bool explicitCancel, const UINT nFormatResId, Args&&...args) {
	const auto format = FindStringResourceEx(Module(), nFormatResId) + 1;
	auto cont = std::make_shared<bool>(true);
	auto t = Utils::Win32::Thread(L"UpdateStatus", [explicitCancel, msg = std::vformat(format, std::make_wformat_args(std::forward<Args&>(args)...)), cont]() {
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

XivAlexander::LoaderApp::Actions::Update::Update(const Arguments& args)
	: m_args(args) {
}

int XivAlexander::LoaderApp::Actions::Update::CheckForUpdates(std::vector<Utils::Win32::Process> prevProcesses, bool offerAutomaticUpdate) {
	if (BOOL w = FALSE; IsWow64Process(GetCurrentProcess(), &w) && w) {
		return Utils::Win32::RunProgram({
			.path = Utils::Win32::Process::Current().PathOf().parent_path() / XivAlexLoader64NameW,
			.args = xivres::util::unicode::convert<std::wstring>(Utils::Win32::ReverseCommandLineToArgv({
				"-a", LoaderActionToString(offerAutomaticUpdate ? LoaderAction::UpdateCheck : LoaderAction::Internal_Update_DependencyDllMode),
			})),
			.wait = true,
		}).WaitAndGetExitCode();
	}

	const auto updateZip = Utils::Win32::Process::Current().PathOf().parent_path() / "update.zip";
	if (exists(updateZip) && offerAutomaticUpdate)
		return PerformUpdateAndExitIfSuccessful(prevProcesses, "", updateZip);

	try {
		std::vector<int> remote, local;
		VersionInformation up;
		if (m_args.m_debugUpdate) {
			local = {0, 0, 0, 0};
			remote = {0, 0, 0, 1};
		} else {
			const auto checking = ShowLazyProgress(true, IDS_UPDATE_PROGRESS_CHECKING);
			const auto [selfFileVersion, selfProductVersion] = Utils::Win32::FormatModuleVersionString(GetModuleHandleW(nullptr));
			up = CheckUpdates();
			const auto remoteS = xivres::util::split(up.Name.substr(1), std::string("."));
			const auto localS = xivres::util::split(selfProductVersion, std::string("."));
			for (const auto& s : remoteS)
				remote.emplace_back(std::stoi(s));
			for (const auto& s : localS)
				local.emplace_back(std::stoi(s));
		}
		if (local.size() != 4 || remote.size() != 4)
			throw std::runtime_error(xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_UPDATE_FILE_TOO_BIG) + 1));
		if (local >= remote) {
			MessageBoxF(nullptr, MB_OK | MB_ICONINFORMATION,
				IDS_UPDATE_UNAVAILABLE,
				local[0], local[1], local[2], local[3], remote[0], remote[1], remote[2], remote[3], up.PublishDate);
			return 0;
		}
		if (offerAutomaticUpdate) {
			while (true) {
				switch (MessageBoxF(nullptr, MB_YESNOCANCEL,
					IDS_UPDATE_CONFIRM,
					remote[0], remote[1], remote[2], remote[3], up.PublishDate, local[0], local[1], local[2], local[3],
					Utils::Win32::MB_GetString(IDYES - 1),
					Utils::Win32::MB_GetString(IDNO - 1),
					Utils::Win32::MB_GetString(IDCANCEL - 1)
				)) {
					case IDYES:
						Utils::Win32::ShellExecutePathOrThrow(FindStringResourceEx(Module(), IDS_URL_RELEASES) + 1);
						break;

					case IDNO:
						PerformUpdateAndExitIfSuccessful(std::move(prevProcesses), up.DownloadLink, updateZip);
						return 0;

					case IDCANCEL:
						return -1;
				}
			}
		} else {
			switch (MessageBoxF(nullptr, MB_YESNO,
				IDS_UPDATE_CONFIRM_NOAUTO,
				remote[0], remote[1], remote[2], remote[3], up.PublishDate, local[0], local[1], local[2], local[3]
			)) {
				case IDYES:
					Utils::Win32::ShellExecutePathOrThrow(FindStringResourceEx(Module(), IDS_URL_RELEASES) + 1);
					return 0;

				case IDNO:
					return -1;
			}
		}
	} catch (const std::exception& e) {
		MessageBoxF(nullptr, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
	}
	return -1;
}

int XivAlexander::LoaderApp::Actions::Update::Run() {
	switch (m_args.m_action) {
		case LoaderAction::UpdateCheck:
			return CheckForUpdates(m_args.m_targetProcessHandles, true);

		case LoaderAction::Internal_Update_DependencyDllMode:
			return CheckForUpdates(m_args.m_targetProcessHandles, false);

		case LoaderAction::Internal_Update_Step2_ReplaceFiles:
			return UpdateStep_ReplaceFiles();

		case LoaderAction::Internal_Update_Step3_CleanupFiles:
			return UpdateStep_CleanupFiles();
	}
	throw std::logic_error("invalid m_action for Update");
}

bool XivAlexander::LoaderApp::Actions::Update::RequiresElevationForUpdate(std::vector<DWORD> excludedPid) {
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

int XivAlexander::LoaderApp::Actions::Update::PerformUpdateAndExitIfSuccessful(std::vector<Utils::Win32::Process> gameProcesses, const std::string& url, const std::filesystem::path& updateZip) {
	std::vector<DWORD> prevProcessIds;
	std::ranges::transform(gameProcesses, std::back_inserter(prevProcessIds), [](const Utils::Win32::Process& k) { return k.GetId(); });
	const auto& currentProcess = Utils::Win32::Process::Current();
	const auto launcherDir = currentProcess.PathOf().parent_path();
	const auto tempExtractionDir = launcherDir / L"__UPDATE__";
	const auto loaderPath64 = launcherDir / XivAlexLoader64NameW;

	{
		const auto progress = ShowLazyProgress(true, IDS_UPDATE_PROGRESS_DOWNLOADING_FILES);

		try {
			for (int i = 0; i < 2; ++i) {
				try {
					if (m_args.m_debugUpdate) {
						const auto tempPath = launcherDir / L"updatesourcetest.zip";
						libzippp::ZipArchive arc(tempPath.string());
						arc.open(libzippp::ZipArchive::Write);
						arc.addFile(xivres::util::unicode::convert<std::string>(XivAlexDll64NameW), xivres::util::unicode::convert<std::string>((launcherDir / XivAlexDll64NameW).wstring()));
						arc.addFile(xivres::util::unicode::convert<std::string>(XivAlexLoader64NameW), xivres::util::unicode::convert<std::string>((launcherDir / XivAlexLoader64NameW).wstring()));
						arc.close();
						copy(tempPath, updateZip);
					} else {
						if (!exists(updateZip) || i == 1) {
							std::ofstream f(updateZip, std::ios::binary);
							Utils::Win32::WinHttp::Get(url, [&](const void* data, size_t size) {
								f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
							});
						}
					}

					remove_all(tempExtractionDir);

					const auto hFile = Utils::Win32::Handle::FromCreateFile(updateZip, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING);
					const auto fileSize = hFile.GetFileSize();
					if (fileSize > 128 * 1024 * 1024)
						throw Utils::Win32::Error(xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_UPDATE_FILE_TOO_BIG) + 1));
					const auto hFileMapping = Utils::Win32::FileMapping::Create(hFile);
					const auto fileMappingView = Utils::Win32::FileMapping::View::Create(hFileMapping);

					const auto pArc = libzippp::ZipArchive::fromBuffer(*fileMappingView, static_cast<uint32_t>(fileSize));
					const auto freeArc = xivres::util::on_dtor([pArc] {
						pArc->close();
						delete pArc;
					});
					for (const auto& entry : pArc->getEntries()) {
						const auto pData = static_cast<char*>(entry.readAsBinary());
						const auto freeData = xivres::util::on_dtor([pData]() { delete[] pData; });
						const auto targetPath = launcherDir / tempExtractionDir / entry.getName();
						create_directories(targetPath.parent_path());
						std::ofstream f(targetPath, std::ios::binary);
						f.write(pData, static_cast<std::streamsize>(entry.getSize()));
					}
					break;
				} catch (...) {
					if (url.empty())
						return -1;

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
		return Utils::Win32::RunProgram({
			.args = std::format(L"--action {}", LoaderActionToString(LoaderAction::UpdateCheck)),
			.elevateMode = Utils::Win32::RunProgramParams::Force,
		}).WaitAndGetExitCode();
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
			if (lstrcmpiW(processPath.c_str(), loaderPath64.c_str()) == 0) {
				while (true) {
					try {
						const auto hProcess = Utils::Win32::Process(PROCESS_TERMINATE | SYNCHRONIZE, false, pid);
						if (hProcess.Wait(0) != WAIT_TIMEOUT)
							break;
						hProcess.Terminate(0);
					} catch (const Utils::Win32::Error& e) {
						if (e.Code() == ERROR_INVALID_PARAMETER)  // this process already gone
							break;
						if (MessageBoxF(nullptr, MB_OKCANCEL, IDS_UPDATE_PROCESS_KILL_FAILURE, pid, e.what()) == IDCANCEL)
							return -1;
					}
				}

			} else if (lstrcmpiW(processPath.filename().c_str(), GameExecutable64NameW) == 0) {
				auto process = OpenProcessForManipulation(pid);
				gameProcesses.emplace_back(process);
				unloadTargets.emplace_back(std::move(process));
			} else {
				try {
					auto process = OpenProcessForManipulation(pid);
					const auto modulePath = launcherDir / XivAlexDll64NameW;
					if (process.AddressOf(modulePath, Utils::Win32::Process::ModuleNameCompareMode::FullPath, false))
						unloadTargets.emplace_back(std::move(process));
				} catch (...) {
				}
			}
		}

		LaunchXivAlexLoaderWithTargetHandles(unloadTargets, LoaderAction::Internal_Inject_UnloadFromHandle, true);
		LaunchXivAlexLoaderWithTargetHandles(gameProcesses, LoaderAction::Internal_Update_Step2_ReplaceFiles, false, currentProcess, tempExtractionDir);

		currentProcess.Terminate(0);
	}
	return 0;
}

int XivAlexander::LoaderApp::Actions::Update::UpdateStep_ReplaceFiles() {
	const auto checking = ShowLazyProgress(true, IDS_UPDATE_PROGRESS_UPDATING_FILES);
	const auto temporaryUpdatePath = Utils::Win32::Process::Current().PathOf().parent_path();
	const auto targetUpdatePath = temporaryUpdatePath.parent_path();
	if (temporaryUpdatePath.filename() != L"__UPDATE__")
		throw std::runtime_error("cannot update outside of update process");
	try {
		copy(
			temporaryUpdatePath,
			targetUpdatePath,
			std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing
		);
	} catch (const std::exception& e) {
		MessageBoxF(nullptr, MB_ICONWARNING, IDS_UPDATE_ERROR_ACCESS_DENIED, e.what());
		return -1;
	}
	LaunchXivAlexLoaderWithTargetHandles(m_args.m_targetProcessHandles, LoaderAction::Internal_Update_Step3_CleanupFiles, false, Utils::Win32::Process::Current(), targetUpdatePath);
	return 0;
}

int XivAlexander::LoaderApp::Actions::Update::UpdateStep_CleanupFiles() {
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
	return 0;
}
