#include "pch.h"
#include "Apps/MainApp/Internal/GameResourceOverrider.h"

#include <XivAlexanderCommon/Sqex/SeString.h>
#include <XivAlexanderCommon/Utils/Win32/Process.h>

#include "Apps/MainApp/Internal/VirtualSqPacks.h"
#include "Config.h"
#include "Misc/DebuggerDetectionDisabler.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"
#include "XivAlexander.h"

class AntiReentry {
	std::mutex m_lock;
	std::set<DWORD> m_tids;

public:
	class Lock {
		AntiReentry& p;
		bool re = false;

	public:
		explicit Lock(AntiReentry& p)
			: p(p) {
			const auto tid = GetCurrentThreadId();

			std::lock_guard lock(p.m_lock);
			re = p.m_tids.find(tid) != p.m_tids.end();
			if (!re)
				p.m_tids.insert(tid);
		}

		Lock(const Lock&) = delete;
		Lock& operator=(const Lock&) = delete;
		Lock(Lock&& r) = delete;
		Lock& operator=(Lock&&) = delete;

		~Lock() {
			if (!re) {
				std::lock_guard lock(p.m_lock);
				p.m_tids.erase(GetCurrentThreadId());
			}
		}

		operator bool() const {
			// True if new enter
			return !re;
		}
	};
};

struct XivAlexander::Apps::MainApp::Internal::GameResourceOverrider::Implementation {
	Apps::MainApp::App& App;
	const std::shared_ptr<XivAlexander::Config> Config;
	const std::shared_ptr<Misc::Logger> Logger;
	const std::shared_ptr<Misc::DebuggerDetectionDisabler> AntiDebugger;
	const std::filesystem::path SqpackPath;
	std::optional<Internal::VirtualSqPacks> Sqpacks;

	Utils::CallOnDestruction::Multiple Cleanup;

	Misc::Hooks::ImportedFunction<HANDLE, LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE> CreateFileW{"kernel32::CreateFileW", "kernel32.dll", "CreateFileW"};
	Misc::Hooks::ImportedFunction<BOOL, HANDLE> CloseHandle{"kernel32::CloseHandle", "kernel32.dll", "CloseHandle"};
	Misc::Hooks::ImportedFunction<BOOL, HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED> ReadFile{"kernel32::ReadFile", "kernel32.dll", "ReadFile"};
	Misc::Hooks::ImportedFunction<BOOL, HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD> SetFilePointerEx{"kernel32::SetFilePointerEx", "kernel32.dll", "SetFilePointerEx"};

	AntiReentry CreateFileWAntiReentry, ReadFileAntiReentry;
	
	Utils::Win32::Thread VirtualSqPackInitThread;
	Utils::ListenerManager<Implementation, void> OnVirtualSqPacksInitialized;

	Implementation(Apps::MainApp::App& app)
		: App(app)
		, Config(XivAlexander::Config::Acquire())
		, Logger(Misc::Logger::Acquire())
		, AntiDebugger(Misc::DebuggerDetectionDisabler::Acquire())
		, SqpackPath(Utils::Win32::Process::Current().PathOf().remove_filename() / L"sqpack") {

		if (!Dll::IsLoadedAsDependency() && !Dll::IsLoadedFromEntryPoint())
			return;

		if (!Config->Runtime.UseModding)
			return;

		VirtualSqPackInitThread = Utils::Win32::Thread(L"VirtualSqPackInitThread", [&]() {
			try {
				Sqpacks.emplace(App, Utils::Win32::Process::Current().PathOf().remove_filename() / L"sqpack");
			} catch (const std::exception& e) {
				Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"Failed to load VirtualSqPacks: {}", e.what());
				return;
			}

			OnVirtualSqPacksInitialized();

			});

		Cleanup += CreateFileW.SetHook([this](
			_In_ LPCWSTR lpFileName,
			_In_ DWORD dwDesiredAccess,
			_In_ DWORD dwShareMode,
			_In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
			_In_ DWORD dwCreationDisposition,
			_In_ DWORD dwFlagsAndAttributes,
			_In_opt_ HANDLE hTemplateFile
			) {
				if (const auto lock = AntiReentry::Lock(CreateFileWAntiReentry); lock &&
					!(dwDesiredAccess & GENERIC_WRITE) &&
					dwCreationDisposition == OPEN_EXISTING &&
					!hTemplateFile) {

					VirtualSqPackInitThread.Wait();
					if (const auto res = Sqpacks ? Sqpacks->Open(lpFileName) : nullptr)
						return res;
				}

				return CreateFileW.bridge(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
			});

		Cleanup += CloseHandle.SetHook([this](
			HANDLE handle
		) {
				if (Sqpacks && Sqpacks->Close(handle))
					return 0;

				return CloseHandle.bridge(handle);
			});

		Cleanup += ReadFile.SetHook([this](
			_In_ HANDLE hFile,
			_Out_writes_bytes_to_opt_(nNumberOfBytesToRead, *lpNumberOfBytesRead) __out_data_source(FILE) LPVOID lpBuffer,
			_In_ DWORD nNumberOfBytesToRead,
			_Out_opt_ LPDWORD lpNumberOfBytesRead,
			_Inout_opt_ LPOVERLAPPED lpOverlapped
			) {
				if (const auto pvpath = Sqpacks ? Sqpacks->Get(hFile) : nullptr) {
					auto& vpath = *pvpath;
					try {
						Sqpacks->MarkIoRequest();
						const auto fp = lpOverlapped ? ((static_cast<uint64_t>(lpOverlapped->OffsetHigh) << 32) | lpOverlapped->Offset) : vpath.FilePointer.QuadPart;
						const auto read = vpath.Stream->ReadStreamPartial(fp, lpBuffer, nNumberOfBytesToRead);

						if (lpNumberOfBytesRead)
							*lpNumberOfBytesRead = static_cast<DWORD>(read);

						if (read != nNumberOfBytesToRead) {
							Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}, requested {} bytes, read {} bytes; state: {}",
								vpath.Path.filename(), nNumberOfBytesToRead, read, vpath.Stream->DescribeState());
						} else {
							if (Config->Runtime.LogAllDataFileRead) {
								Logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, L"ReadFile: {}, requested {} bytes; state: {}",
									vpath.Path.filename(), nNumberOfBytesToRead, vpath.Stream->DescribeState());
							}
						}

						if (lpOverlapped) {
							if (lpOverlapped->hEvent)
								SetEvent(lpOverlapped->hEvent);
							lpOverlapped->Internal = 0;
							lpOverlapped->InternalHigh = static_cast<DWORD>(read);
						} else
							vpath.FilePointer.QuadPart = fp + read;

						return TRUE;

					} catch (const Utils::Win32::Error& e) {
						if (e.Code() != ERROR_IO_PENDING)
							Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}, Message: {}",
								vpath.Path.filename(), e.what());
						SetLastError(e.Code());
						return FALSE;

					} catch (const std::exception& e) {
						Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}, Message: {}",
							vpath.Path.filename(), e.what());
						SetLastError(ERROR_READ_FAULT);
						return FALSE;
					}
				}
				return ReadFile.bridge(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
			});

		Cleanup += SetFilePointerEx.SetHook([this](
			_In_ HANDLE hFile,
			_In_ LARGE_INTEGER liDistanceToMove,
			_Out_opt_ PLARGE_INTEGER lpNewFilePointer,
			_In_ DWORD dwMoveMethod) {
				if (const auto pvpath = Sqpacks ? Sqpacks->Get(hFile) : nullptr) {
					if (lpNewFilePointer)
						*lpNewFilePointer = {};

					auto& vpath = *pvpath;
					try {
						Sqpacks->MarkIoRequest();
						const auto len = vpath.Stream->StreamSize();

						if (dwMoveMethod == FILE_BEGIN)
							vpath.FilePointer.QuadPart = liDistanceToMove.QuadPart;
						else if (dwMoveMethod == FILE_CURRENT)
							vpath.FilePointer.QuadPart += liDistanceToMove.QuadPart;
						else if (dwMoveMethod == FILE_END)
							vpath.FilePointer.QuadPart = len - liDistanceToMove.QuadPart;
						else {
							SetLastError(ERROR_INVALID_PARAMETER);
							return FALSE;
						}

						if (vpath.FilePointer.QuadPart > static_cast<int64_t>(len))
							vpath.FilePointer.QuadPart = static_cast<int64_t>(len);

						if (lpNewFilePointer)
							*lpNewFilePointer = vpath.FilePointer;

					} catch (const Utils::Win32::Error& e) {
						Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"SetFilePointerEx: {}, Message: {}",
							vpath.Path.filename(), e.what());
						SetLastError(e.Code());
						return FALSE;

					} catch (const std::exception& e) {
						Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}, Message: {}",
							vpath.Path.filename(), e.what());
						SetLastError(ERROR_READ_FAULT);
						return FALSE;
					}

					return TRUE;
				}
				return SetFilePointerEx.bridge(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
			});
	}

	~Implementation() {
		Cleanup.Clear();
	}
};

XivAlexander::Apps::MainApp::Internal::GameResourceOverrider::GameResourceOverrider(Apps::MainApp::App& app)
	: m_pImpl(std::make_unique<Implementation>(app)) {
}

XivAlexander::Apps::MainApp::Internal::GameResourceOverrider::~GameResourceOverrider() = default;

std::optional<XivAlexander::Apps::MainApp::Internal::VirtualSqPacks>& XivAlexander::Apps::MainApp::Internal::GameResourceOverrider::GetVirtualSqPacks() {
	return m_pImpl->Sqpacks;
}

Utils::CallOnDestruction XivAlexander::Apps::MainApp::Internal::GameResourceOverrider::OnVirtualSqPacksInitialized(std::function<void()> f) {
	return m_pImpl->OnVirtualSqPacksInitialized(std::move(f));
}
