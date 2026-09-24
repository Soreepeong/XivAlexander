#include "pch.h"
#include "MainApp/Modding/SqpackFileHooks.h"

#include <xivres/sqpack.generator.h>
#include <xivres/util.on_dtor.h>

#include "MainApp/Modding/SqpackRebuildLock.h"
#include "Config.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"
#include "Utils/AntiReentry.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	struct SqpackFileHooks::Implementation {
		struct OpenedFile {
			Utils::Win32::Event IdentifierHandle;
			std::filesystem::path Path;
			LARGE_INTEGER FilePointer{};
			std::shared_ptr<xivres::stream> Stream;

			std::shared_ptr<const xivres::sqpack::generator::data_view_stream> View;
			Utils::Win32::Handle Original;
		};

		SqpackRebuildLock& Gate;
		const Opener Open;
		const std::shared_ptr<Config> Config;
		const std::shared_ptr<Misc::Logger> Logger;

		std::mutex OpenedFilesMtx;
		std::map<HANDLE, std::shared_ptr<OpenedFile>> OpenedFiles;

		Utils::AntiReentry CreateFileWAntiReentry;

		Misc::Hooks::ImportedFunction<HANDLE, LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE> CreateFileW{"kernel32::CreateFileW", "kernel32.dll", "CreateFileW"};
		Misc::Hooks::ImportedFunction<BOOL, HANDLE> CloseHandle{"kernel32::CloseHandle", "kernel32.dll", "CloseHandle"};
		Misc::Hooks::ImportedFunction<BOOL, HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED> ReadFile{"kernel32::ReadFile", "kernel32.dll", "ReadFile"};
		Misc::Hooks::ImportedFunction<BOOL, HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD> SetFilePointerEx{"kernel32::SetFilePointerEx", "kernel32.dll", "SetFilePointerEx"};

		xivres::util::on_dtor::multi Cleanup;

		std::shared_ptr<OpenedFile> Find(HANDLE handle) {
			const auto lock = std::lock_guard(OpenedFilesMtx);
			const auto it = OpenedFiles.find(handle);
			return it == OpenedFiles.end() ? nullptr : it->second;
		}

		Implementation(SqpackRebuildLock& ioGate, Opener opener)
			: Gate(ioGate)
			, Open(std::move(opener))
			, Config(Config::Acquire())
			, Logger(Misc::Logger::Acquire()) {

			Cleanup += CreateFileW.SetHook([this](
				_In_ LPCWSTR lpFileName,
				_In_ DWORD dwDesiredAccess,
				_In_ DWORD dwShareMode,
				_In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
				_In_ DWORD dwCreationDisposition,
				_In_ DWORD dwFlagsAndAttributes,
				_In_opt_ HANDLE hTemplateFile
				) {
					if (const auto lock = Utils::AntiReentry::Lock(CreateFileWAntiReentry); lock &&
						!(dwDesiredAccess & GENERIC_WRITE) &&
						dwCreationDisposition == OPEN_EXISTING &&
						!hTemplateFile) {

						if (auto stream = Open(lpFileName)) {
							auto file = std::make_shared<OpenedFile>(Utils::Win32::Event::Create(), lpFileName, LARGE_INTEGER{}, std::move(stream));
							if (auto view = std::dynamic_pointer_cast<const xivres::sqpack::generator::data_view_stream>(file->Stream); view && view->original_size()) {
								try {
									file->Original = Utils::Win32::Handle::FromCreateFile(lpFileName, GENERIC_READ, dwShareMode | FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
									file->View = std::move(view);
								} catch (const Utils::Win32::Error& e) {
									Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"CreateFileW: {}, reading everything through the view: {}",
										file->Path.filename(), e.what());
								}
							}
							const auto key = static_cast<HANDLE>(file->IdentifierHandle);
							const auto lock2 = std::lock_guard(OpenedFilesMtx);
							OpenedFiles.insert_or_assign(key, std::move(file));
							return key;
						}
					}

					return CreateFileW.bridge(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
				});

			Cleanup += CloseHandle.SetHook([this](
				HANDLE handle
			) {
					{
						const auto lock = std::lock_guard(OpenedFilesMtx);
						if (OpenedFiles.erase(handle))
							return 0;
					}

					return CloseHandle.bridge(handle);
				});

			Cleanup += ReadFile.SetHook([this](
				_In_ HANDLE hFile,
				_Out_writes_bytes_to_opt_(nNumberOfBytesToRead, *lpNumberOfBytesRead) __out_data_source(FILE) LPVOID lpBuffer,
				_In_ DWORD nNumberOfBytesToRead,
				_Out_opt_ LPDWORD lpNumberOfBytesRead,
				_Inout_opt_ LPOVERLAPPED lpOverlapped
				) {
					if (const auto pvpath = Find(hFile)) {
						auto& vpath = *pvpath;
						try {
							Gate.OnSqpackRead();
							const auto fp = lpOverlapped ? ((static_cast<uint64_t>(lpOverlapped->OffsetHigh) << 32) | lpOverlapped->Offset) : vpath.FilePointer.QuadPart;

							std::streamsize read = -1;
							if (vpath.View && vpath.View->reads_original(fp, nNumberOfBytesToRead)) {
								OVERLAPPED ov{};
								ov.Offset = static_cast<DWORD>(fp);
								ov.OffsetHigh = static_cast<DWORD>(fp >> 32);
								if (DWORD originalRead{}; ReadFile.bridge(vpath.Original, lpBuffer, nNumberOfBytesToRead, &originalRead, &ov) && originalRead == nNumberOfBytesToRead)
									read = originalRead;
							}
							if (read < 0)
								read = vpath.Stream->read(fp, lpBuffer, nNumberOfBytesToRead);

							if (lpNumberOfBytesRead)
								*lpNumberOfBytesRead = static_cast<DWORD>(read);

							if (read != nNumberOfBytesToRead) {
								Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}, requested {} bytes, read {} bytes",
									vpath.Path.filename(), nNumberOfBytesToRead, read);
							} else {
								if (Config->Runtime.LogAllDataFileRead) {
									Logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, L"ReadFile: {}, requested {} bytes",
										vpath.Path.filename(), nNumberOfBytesToRead);
								}
							}

							if (lpOverlapped) {
								lpOverlapped->Internal = 0;
								lpOverlapped->InternalHigh = static_cast<DWORD>(read);
								if (const auto hEvent = lpOverlapped->hEvent)
									SetEvent(hEvent);
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
					if (const auto pvpath = Find(hFile)) {
						if (lpNewFilePointer)
							*lpNewFilePointer = {};

						auto& vpath = *pvpath;
						try {
							Gate.OnSqpackRead();
							const auto len = vpath.Stream->size();

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
			Cleanup.clear();
		}
	};

	SqpackFileHooks::SqpackFileHooks(SqpackRebuildLock& ioGate, Opener opener)
		: m_pImpl(std::make_unique<Implementation>(ioGate, std::move(opener))) {}

	SqpackFileHooks::~SqpackFileHooks() = default;
}
