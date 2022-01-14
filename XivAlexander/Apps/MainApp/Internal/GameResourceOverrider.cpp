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

	std::vector<std::unique_ptr<Misc::Hooks::PointerFunction<size_t, uint32_t, const char*, size_t>>> FoundPathHashFunctions{};
	std::vector<std::string> LastLoggedPaths;
	std::mutex LastLoggedPathMtx;
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

		VirtualSqPackInitThread = Utils::Win32::Thread(L"VirtualSqPackInitThread", [&]() {
			if (!Dll::IsLoadedAsDependency() && !Dll::IsLoadedFromEntryPoint())
				return;

			if (!Config->Runtime.UseModding)
				return;

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


		for (auto ptr : Misc::Signatures::LookupForData([](const IMAGE_SECTION_HEADER& p) {
					return strncmp(reinterpret_cast<const char*>(p.Name), ".text", 5) == 0;
				},
				"\x40\x57\x48\x8d\x3d\x00\x00\x00\x00\x00\x8b\xd8\x4c\x8b\xd2\xf7\xd1\x00\x85\xc0\x74\x25\x41\xf6\xc2\x03\x74\x1f\x41\x0f\xb6\x12\x8b\xc1",
				"\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF",
				34,
				{}
			)) {
			FoundPathHashFunctions.emplace_back(std::make_unique<Misc::Hooks::PointerFunction<size_t, uint32_t, const char*, size_t>>(
				"FFXIV::GeneralHashCalcFn",
				reinterpret_cast<size_t(__stdcall*)(uint32_t, const char*, size_t)>(ptr)
			));
			Cleanup += FoundPathHashFunctions.back()->SetHook([this, ptr, self = FoundPathHashFunctions.back().get()](uint32_t initVal, const char* str, size_t len) {
				if (!str || !*str || len >= 512 || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, static_cast<int>(len), nullptr, 0))
					return self->bridge(initVal, str, len);

				auto name = std::string(str);
				std::string ext, rest;
				if (const auto i1 = name.find_first_of('.'); i1 != std::string::npos) {
					ext = name.substr(i1);
					name.resize(i1);
					if (const auto i2 = ext.find_first_of('.', 1); i2 != std::string::npos) {
						rest = ext.substr(i2);
						ext.resize(i2);
					}
				}

				const auto nameLower = [&name]() {
					auto val = Utils::FromUtf8(name);
					CharLowerW(&val[0]);
					return Utils::ToUtf8(val);
				}();

				const auto extLower = [&ext]() {
					auto val = Utils::FromUtf8(ext);
					CharLowerW(&val[0]);
					return Utils::ToUtf8(val);
				}();

				auto overrideLanguage = Sqex::Language::Unspecified;
				if (extLower == ".scd") {
					if (nameLower.starts_with("cut/") || nameLower.starts_with("sound/voice/vo_line"))
						overrideLanguage = Config->Runtime.VoiceResourceLanguageOverride;
				} else {
					overrideLanguage = Config->Runtime.ResourceLanguageOverride;
				}

				std::string description;
				if (overrideLanguage != Sqex::Language::Unspecified) {
					const char* languageCodes[] = {"ja", "en", "de", "fr", "chs", "cht", "ko"};
					const auto targetLanguageCode = languageCodes[static_cast<int>(overrideLanguage) - 1];

					std::string newName;
					if (nameLower.starts_with("ui/uld/logo")) {
						// do nothing, as overriding this often freezes the game
					} else {
						for (const auto languageCode : languageCodes) {
							char t[16];
							sprintf_s(t, "_%s", languageCode);
							if (nameLower.ends_with(t)) {
								newName = name.substr(0, name.size() - strlen(languageCode)) + targetLanguageCode;
								break;
							}
							sprintf_s(t, "/%s/", languageCode);
							if (const auto pos = nameLower.find(t); pos != std::string::npos) {
								newName = std::format("{}/{}/{}", name.substr(0, pos), targetLanguageCode, name.substr(pos + strlen(t)));
								break;
							}
							sprintf_s(t, "_%s_", languageCode);
							if (const auto pos = nameLower.find(t); pos != std::string::npos) {
								newName = std::format("{}_{}_{}", name.substr(0, pos), targetLanguageCode, name.substr(pos + strlen(t)));
								break;
							}
						}
					}
					if (!newName.empty() && name != newName && Sqpacks && Sqpacks->EntryExists(std::format("{}{}", newName, ext))) {
						const auto newStr = std::format("{}{}{}", newName, ext, rest);
						description = std::format("{} => {}", std::string_view(str, len), newStr);
						Utils::Win32::Process::Current().WriteMemory(const_cast<char*>(str), newStr.c_str(), newStr.size() + 1, true);
						len = newStr.size();
					}
				}
				const auto res = self->bridge(initVal, str, len);

				if (Config->Runtime.UseHashTrackerKeyLogging || !description.empty()) {
					auto current = std::string(str, len);

					const auto lock = std::lock_guard(LastLoggedPathMtx);
					if (const auto it = std::ranges::find(LastLoggedPaths, current); it != LastLoggedPaths.end()) {
						LastLoggedPaths.erase(it);
					} else {
						const auto pathSpec = Sqex::Sqpack::EntryPathSpec(current);
						Logger->Format(LogCategory::GameResourceOverrider,
							"{} (~{:08x}/~{:08x}, ~{:08x}) => ~{:08x} (f={:x}, iv={:x})",
							description.empty() ? current : description,
							pathSpec.PathHash, pathSpec.NameHash, pathSpec.FullPathHash,
							res, reinterpret_cast<size_t>(ptr), initVal);
					}
					LastLoggedPaths.push_back(std::move(current));
					while (LastLoggedPaths.size() > 16)
						LastLoggedPaths.erase(LastLoggedPaths.begin());
				}

				return res;
			});
		}
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
