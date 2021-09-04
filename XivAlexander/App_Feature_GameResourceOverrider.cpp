#include "pch.h"
#include "App_Feature_GameResourceOverrider.h"

#include <XivAlexanderCommon/Sqex_Sqpack_Virtual.h>
#include <XivAlexanderCommon/Utils_Win32_Process.h>

#include "App_ConfigRepository.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_Hooks.h"
#include "App_Misc_Logger.h"

static constexpr bool DebugFlag_PassthroughFileApi = false;

std::weak_ptr<App::Feature::GameResourceOverrider::Implementation> App::Feature::GameResourceOverrider::s_pImpl;

class ReEnterPreventer {
	std::mutex m_lock;
	std::set<DWORD> m_tids;

public:
	class Lock {
		ReEnterPreventer& p;
		bool re = false;

	public:
		explicit Lock(ReEnterPreventer& p) : p(p) {
			const auto tid = GetCurrentThreadId();

			std::lock_guard lock(p.m_lock);
			re = p.m_tids.find(tid) != p.m_tids.end();
			if (!re)
				p.m_tids.insert(tid);
		}

		Lock(const Lock&) = delete;
		Lock& operator= (const Lock&) = delete;
		Lock(Lock&& r) = delete;
		Lock& operator= (Lock&&) = delete;

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

static std::map<void*, size_t>* s_TestVal;

__declspec(dllexport) std::map<void*, size_t>* GetHeapTracker() {
	return s_TestVal;
}

struct App::Feature::GameResourceOverrider::Implementation {
	const std::shared_ptr<Config> m_config;
	const std::shared_ptr<Misc::Logger> m_logger;
	const std::shared_ptr<Misc::DebuggerDetectionDisabler> m_debugger;
	std::vector<std::unique_ptr<Misc::Hooks::PointerFunction<uint32_t, uint32_t, const char*, size_t>>> fns{};
	std::set<std::string> m_alreadyLogged{};
	Utils::CallOnDestruction::Multiple m_cleanup;

	Misc::Hooks::ImportedFunction<HANDLE, LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE> CreateFileW{ "kernel32::CreateFileW", "kernel32.dll", "CreateFileW" };
	Misc::Hooks::ImportedFunction<BOOL, HANDLE> CloseHandle{ "kernel32::CloseHandle", "kernel32.dll", "CloseHandle" };
	Misc::Hooks::ImportedFunction<BOOL, HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED> ReadFile{ "kernel32::ReadFile", "kernel32.dll", "ReadFile" };
	Misc::Hooks::ImportedFunction<BOOL, HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD> SetFilePointerEx{ "kernel32::SetFilePointerEx", "kernel32.dll", "SetFilePointerEx" };

	Misc::Hooks::ImportedFunction<LPVOID, HANDLE, DWORD, SIZE_T> HeapAlloc{ "kernel32.dll::HeapAlloc", "kernel32.dll", "HeapAlloc" };
	Misc::Hooks::ImportedFunction<BOOL, HANDLE, DWORD, LPVOID> HeapFree{ "kernel32.dll::HeapFree", "kernel32.dll", "HeapFree" };

	ReEnterPreventer m_repCreateFileW, m_repReadFile;

	const std::filesystem::path m_baseSqpackDir = Utils::Win32::Process::Current().PathOf().remove_filename() / L"sqpack";

	struct VirtualPath {
		static constexpr int PathTypeIndex = -1;
		static constexpr int PathTypeIndex2 = -2;
		static constexpr int PathTypeInvalid = -3;

		Utils::Win32::Event IdentifierHandle;
		std::filesystem::path IndexPath;
		std::shared_ptr<Sqex::Sqpack::VirtualSqPack> VirtualSqPack;
		Utils::Win32::File PassthroughFile;
		int PathType;
		LARGE_INTEGER FilePointer;
	};

	std::mutex m_virtualPathMapMutex;
	std::map<HANDLE, std::unique_ptr<VirtualPath>> m_virtualPathMap{};
	std::set<std::filesystem::path> m_ignoredIndexFiles{};
	std::atomic<int> m_stk;

	std::mutex m_processHeapAllocationTrackerMutex;
	std::map<void*, size_t> m_processHeapAllocations;

	class AtomicIntEnter {
		std::atomic<int>& v;
	public:
		AtomicIntEnter(std::atomic<int>& v) : v(v) {
			++v;
		}

		~AtomicIntEnter() {
			--v;
		}
	};

	Implementation()
		: m_config(Config::Acquire())
		, m_logger(Misc::Logger::Acquire())
		, m_debugger(Misc::DebuggerDetectionDisabler::Acquire()) {

		if (m_config->Runtime.UseResourceOverriding) {
			const auto hDefaultHeap = GetProcessHeap();

			m_cleanup += HeapAlloc.SetHook([this, hDefaultHeap](HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes) {
				const auto res = HeapAlloc.bridge(hHeap, dwFlags, dwBytes);
				if (res && hHeap == hDefaultHeap) {
					std::lock_guard lock(m_processHeapAllocationTrackerMutex);
					m_processHeapAllocations[res] = dwBytes;
				}
				return res;
			});

			m_cleanup += HeapFree.SetHook([this, hDefaultHeap](HANDLE hHeap, DWORD dwFlags, LPVOID lpMem) {
				const auto res = HeapFree.bridge(hHeap, dwFlags, lpMem);
				if (res && hHeap == hDefaultHeap) {
					std::lock_guard lock(m_processHeapAllocationTrackerMutex);
					m_processHeapAllocations.erase(hHeap);
				}
				return res;
			});

			m_cleanup += CreateFileW.SetHook([this](_In_ LPCWSTR lpFileName,
				_In_ DWORD dwDesiredAccess,
				_In_ DWORD dwShareMode,
				_In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
				_In_ DWORD dwCreationDisposition,
				_In_ DWORD dwFlagsAndAttributes,
				_In_opt_ HANDLE hTemplateFile) {

				AtomicIntEnter implUseLock(m_stk);

				if (const auto lock = ReEnterPreventer::Lock(m_repCreateFileW); lock &&
					!(dwDesiredAccess & GENERIC_WRITE) &&
					dwCreationDisposition == OPEN_EXISTING &&
					!hTemplateFile) {
					try {
						const auto fileToOpen = std::filesystem::absolute(lpFileName);
						const auto recreatedFilePath = m_baseSqpackDir / fileToOpen.parent_path().filename() / fileToOpen.filename();
						const auto indexFile = std::filesystem::path(recreatedFilePath).replace_extension(L".index");
						const auto index2File = std::filesystem::path(recreatedFilePath).replace_extension(L".index2");
						if (exists(indexFile) && exists(index2File) && m_ignoredIndexFiles.find(indexFile) == m_ignoredIndexFiles.end()) {
							int pathType = VirtualPath::PathTypeInvalid;

							if (fileToOpen == indexFile) {
								pathType = VirtualPath::PathTypeIndex;
							} else if (fileToOpen == index2File) {
								pathType = VirtualPath::PathTypeIndex2;
							} else {
								for (auto i = 0; i < 8; ++i) {
									const auto datFile = std::filesystem::path(recreatedFilePath).replace_extension(std::format(L".dat{}", i));
									if (fileToOpen == datFile) {
										pathType = i;
										break;
									}
								}
							}

							if (pathType != VirtualPath::PathTypeInvalid) {
								if (const auto res = SetUpVirtualFile(fileToOpen, indexFile, pathType))
									return res;
							}
						}
					} catch (const Utils::Win32::Error& e) {
						m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"CreateFileW: {}, Message: {}", lpFileName, e.what());
					} catch (const std::exception& e) {
						m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "CreateFileW: {}, Message: {}", lpFileName, e.what());
					}
				}

				return CreateFileW.bridge(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
			});

			m_cleanup += CloseHandle.SetHook([this](HANDLE handle) {

				AtomicIntEnter implUseLock(m_stk);

				std::unique_lock lock(m_virtualPathMapMutex);
				if (m_virtualPathMap.erase(handle))
					return 0;

				return CloseHandle.bridge(handle);
			});

			m_cleanup += ReadFile.SetHook([this](_In_ HANDLE hFile,
				_Out_writes_bytes_to_opt_(nNumberOfBytesToRead, *lpNumberOfBytesRead) __out_data_source(FILE) LPVOID lpBuffer,
				_In_ DWORD nNumberOfBytesToRead,
				_Out_opt_ LPDWORD lpNumberOfBytesRead,
				_Inout_opt_ LPOVERLAPPED lpOverlapped) {

				AtomicIntEnter implUseLock(m_stk);

				VirtualPath* pvpath = nullptr;
				{
					std::lock_guard lock(m_virtualPathMapMutex);
					const auto vpit = m_virtualPathMap.find(hFile);
					if (vpit != m_virtualPathMap.end())
						pvpath = vpit->second.get();
				}

				if (const auto lock = ReEnterPreventer::Lock(m_repReadFile); pvpath && lock) {
					auto& vpath = *pvpath;
					try {
						const auto fp = lpOverlapped ? ((static_cast<uint64_t>(lpOverlapped->OffsetHigh) << 32) | lpOverlapped->Offset) : vpath.FilePointer.QuadPart;
						size_t read;
						if (vpath.PassthroughFile)
							read = vpath.PassthroughFile.Read(fp, lpBuffer, nNumberOfBytesToRead, Utils::Win32::File::PartialIoMode::AllowPartial);
						else if (vpath.PathType == VirtualPath::PathTypeIndex)
							read = vpath.VirtualSqPack->ReadIndex1(fp, lpBuffer, nNumberOfBytesToRead);
						else if (vpath.PathType == VirtualPath::PathTypeIndex2)
							read = vpath.VirtualSqPack->ReadIndex2(fp, lpBuffer, nNumberOfBytesToRead);
						else
							read = vpath.VirtualSqPack->ReadData(vpath.PathType, fp, lpBuffer, nNumberOfBytesToRead);

						if (lpNumberOfBytesRead)
							*lpNumberOfBytesRead = static_cast<DWORD>(read);

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
							m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}({}), Message: {}",
								vpath.IndexPath.filename(), vpath.PathType, e.what());
						SetLastError(e.Code());
						return FALSE;

					} catch (const std::exception& e) {
						m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}({}), Message: {}",
							vpath.IndexPath.filename(), vpath.PathType, e.what());
						SetLastError(ERROR_READ_FAULT);
						return FALSE;
					}
				}
				return ReadFile.bridge(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
			});

			m_cleanup += SetFilePointerEx.SetHook([this](_In_ HANDLE hFile,
				_In_ LARGE_INTEGER liDistanceToMove,
				_Out_opt_ PLARGE_INTEGER lpNewFilePointer,
				_In_ DWORD dwMoveMethod) {

				AtomicIntEnter implUseLock(m_stk);

				VirtualPath* pvpath = nullptr;
				{
					std::lock_guard lock(m_virtualPathMapMutex);
					const auto vpit = m_virtualPathMap.find(hFile);
					if (vpit != m_virtualPathMap.end())
						pvpath = vpit->second.get();
				}
				if (!pvpath)
					return SetFilePointerEx.bridge(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);

				auto& vpath = *pvpath;
				try {
					uint64_t len;
					if (vpath.PassthroughFile)
						len = vpath.PassthroughFile.GetLength();
					else if (vpath.PathType == VirtualPath::PathTypeIndex)
						len = vpath.VirtualSqPack->SizeIndex1();
					else if (vpath.PathType == VirtualPath::PathTypeIndex2)
						len = vpath.VirtualSqPack->SizeIndex2();
					else
						len = vpath.VirtualSqPack->SizeData(vpath.PathType);

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
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"SetFilePointerEx: {}({}), Message: {}",
						vpath.IndexPath.filename(), vpath.PathType, e.what());
					SetLastError(e.Code());
					return FALSE;

				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}({}), Message: {}",
						vpath.IndexPath.filename(), vpath.PathType, e.what());
					SetLastError(ERROR_READ_FAULT);
					return FALSE;
				}

				return TRUE;

			});
		}

		for (auto ptr : Misc::Signatures::LookupForData([](const IMAGE_SECTION_HEADER& p) {
			return strncmp(reinterpret_cast<const char*>(p.Name), ".text", 5) == 0;
		},
			"\x40\x57\x48\x8d\x3d\x00\x00\x00\x00\x00\x8b\xd8\x4c\x8b\xd2\xf7\xd1\x00\x85\xc0\x74\x25\x41\xf6\xc2\x03\x74\x1f\x41\x0f\xb6\x12\x8b\xc1",
			"\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF",
			34,
		{}
			)) {
			fns.emplace_back(std::make_unique<Misc::Hooks::PointerFunction<uint32_t, uint32_t, const char*, size_t>>(
				"FFXIV::GeneralHashCalcFn",
				static_cast<uint32_t(__stdcall*)(uint32_t, const char*, size_t)>(ptr)
				));
			m_cleanup += fns.back()->SetHook([this, ptr, self = fns.back().get()](uint32_t initVal, const char* str, size_t len) {
				if (!str || !*str || !m_config->Runtime.UseHashTracker)
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

				if (m_config->Runtime.HashTrackerLanguageOverride != Sqex::Language::Unspecified) {
					auto appendLanguageCode = false;
					if (name.ends_with("_ja")
						|| name.ends_with("_en")
						|| name.ends_with("_de")
						|| name.ends_with("_fr")
						|| name.ends_with("_ko")) {
						name = name.substr(0, name.size() - 3);
						appendLanguageCode = true;
					} else if (name.ends_with("_chs")
						|| name.ends_with("_cht")) {
						name = name.substr(0, name.size() - 4);
						appendLanguageCode = true;
					}
					if (appendLanguageCode) {
						switch (m_config->Runtime.HashTrackerLanguageOverride) {
							case Sqex::Language::Japanese:
								name += "_ja";
								break;
							case Sqex::Language::English:
								name += "_en";
								break;
							case Sqex::Language::German:
								name += "_de";
								break;
							case Sqex::Language::French:
								name += "_fr";
								break;
							case Sqex::Language::ChineseSimplified:
								name += "_chs";
								break;
							case Sqex::Language::ChineseTraditional:
								name += "_cht";
								break;
							case Sqex::Language::Korean:
								name += "_ko";
								break;
						}

						const auto newStr = std::format("{}{}{}", name, ext, rest);

						if (!m_config->Runtime.UseHashTrackerKeyLogging) {
							if (m_alreadyLogged.find(name) == m_alreadyLogged.end()) {
								m_alreadyLogged.emplace(name);
								m_logger->Format(LogCategory::GameResourceOverrider, "{:x}: {} => {}", reinterpret_cast<size_t>(ptr), str, newStr);
							}
						}
						Utils::Win32::Process::Current().WriteMemory(const_cast<char*>(str), newStr.c_str(), newStr.size() + 1, true);
					}
				}
				const auto res = self->bridge(initVal, str, len);

				if (m_config->Runtime.UseHashTrackerKeyLogging) {
					if (m_alreadyLogged.find(name) == m_alreadyLogged.end()) {
						m_alreadyLogged.emplace(name);
						m_logger->Format(LogCategory::GameResourceOverrider, "{:x}: {},{},{} => {:08x}", reinterpret_cast<size_t>(ptr), name, ext, rest, res);
					}
				}

				return res;
			});
		}
	}

	~Implementation() {
		m_cleanup.Clear();

		while (m_stk) {
			Sleep(1);
		}
		Sleep(1);
	}

	HANDLE SetUpVirtualFile(const std::filesystem::path& fileToOpen, const std::filesystem::path& indexFile, int pathType) {
		auto vpath = std::make_unique<VirtualPath>(
			Utils::Win32::Event::Create(),
			indexFile,
			nullptr,
			Utils::Win32::File{},
			pathType,
			LARGE_INTEGER{}
		);

		std::lock_guard lock(m_virtualPathMapMutex);
		for (const auto& vp : m_virtualPathMap | std::views::values) {
			if constexpr (DebugFlag_PassthroughFileApi) {
				if (equivalent(vp->PassthroughFile.ResolveName(false, true), fileToOpen))
					continue;
			} else {
				if (!equivalent(vp->IndexPath, indexFile))
					continue;
			}
			vpath->VirtualSqPack = vp->VirtualSqPack;
			vpath->PassthroughFile = vp->PassthroughFile;
			break;
		}

		m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
			"Taking control of {}/{} (parent: {}/{}, type: {})",
			fileToOpen.parent_path().filename(), fileToOpen.filename(),
			indexFile.parent_path().filename(), indexFile.filename(),
			pathType);

		if constexpr (DebugFlag_PassthroughFileApi) {
			if (!vpath->PassthroughFile) {
				vpath->PassthroughFile = Utils::Win32::File::Create(
					fileToOpen, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0
				);
			}
		} else {
			if (!vpath->VirtualSqPack) {
				vpath->VirtualSqPack = std::make_shared<Sqex::Sqpack::VirtualSqPack>(
					indexFile.parent_path().filename().string(),
					indexFile.filename().replace_extension().replace_extension().string()
					);
				{
					const auto result = vpath->VirtualSqPack->AddEntriesFromSqPack(indexFile, true, true);
					m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
						"=> Processed SqPack {}/{}: Added {}, replaced {}",
						vpath->VirtualSqPack->DatExpac, vpath->VirtualSqPack->DatName,
						result.Added.size(), result.Replaced.size());
				}

				auto additionalEntriesFound = false;
				additionalEntriesFound |= SetUpVirtualFileFromTexToolsModPacks(*vpath);
				additionalEntriesFound |= SetUpVirtualFileFromFileEntries(*vpath);

				// Nothing to override, 
				if (!additionalEntriesFound) {
					m_ignoredIndexFiles.insert(indexFile);
					m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
						"=> Found no resources to override, releasing control.");
					return nullptr;
				}

				vpath->VirtualSqPack->Freeze(false);
			}
		}

		const auto key = static_cast<HANDLE>(vpath->IdentifierHandle);
		m_virtualPathMap.insert_or_assign(key, std::move(vpath));
		SetLastError(0);
		return key;
	}

	bool SetUpVirtualFileFromTexToolsModPacks(const VirtualPath& vpath) {
		auto additionalEntriesFound = false;
		std::vector<std::filesystem::path> dirs;

		if (m_config->Runtime.UseDefaultTexToolsModPackSearchDirectory) {
			dirs.emplace_back(vpath.IndexPath.parent_path().parent_path().parent_path() / "TexToolsMods");
			dirs.emplace_back(m_config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods");
		}

		for (const auto& dir : m_config->Runtime.AdditionalTexToolsModPackSearchDirectories.Value())
			dirs.emplace_back(Config::TranslatePath(dir, false));

		for (const auto& dir : dirs) {
			if (!is_directory(dir))
				continue;

			std::vector<std::filesystem::path> files;
			try {
				for (const auto& iter : std::filesystem::recursive_directory_iterator(dir)) {
					if (iter.path().filename() != "TTMPL.mpl") continue;
					files.emplace_back(iter);
				}
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
					"=> Failed to list items in {}: {}",
					dir, e.what());
				continue;
			}

			std::sort(files.begin(), files.end());
			for (const auto& file : files) {
				m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, "Processing {}", file);

				if (exists(file.parent_path() / "disable")) {
					m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, "=> Disabled because \"disable\" file exists");
					continue;
				}
				try {
					const auto logCacher = vpath.VirtualSqPack->Log([&](const auto& s) {
						m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, "=> {}", s);
					});
					const auto result = vpath.VirtualSqPack->AddEntriesFromTTMP(file.parent_path());
					if (!result.Added.empty() || !result.Replaced.empty())
						additionalEntriesFound = true;
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "=> Error: {}", e.what());
				}
			}
		}
		return additionalEntriesFound;
	}

	bool SetUpVirtualFileFromFileEntries(const VirtualPath& vpath) {
		auto additionalEntriesFound = false;
		std::vector<std::filesystem::path> dirs;

		if (m_config->Runtime.UseDefaultGameResourceFileEntryRootDirectory) {
			dirs.emplace_back(vpath.IndexPath.parent_path().parent_path());
			dirs.emplace_back(m_config->Init.ResolveConfigStorageDirectoryPath() / "ReplacementFileEntries");
		}

		for (const auto& dir : m_config->Runtime.AdditionalGameResourceFileEntryRootDirectories.Value())
			dirs.emplace_back(Config::TranslatePath(dir, false));

		for (size_t i = 0, i_ = dirs.size(); i < i_; ++i) {
			dirs.emplace_back(dirs[i] / std::format("{}.win32", vpath.VirtualSqPack->DatExpac) / vpath.VirtualSqPack->DatName);
			dirs[i] = dirs[i] / vpath.VirtualSqPack->DatExpac / vpath.VirtualSqPack->DatName;
		}

		for (auto dir : dirs) {
			if (!is_directory(dir))
				continue;

			std::vector<std::filesystem::path> files;

			try {
				for (const auto& iter : std::filesystem::recursive_directory_iterator(dir)) {
					if (is_directory(iter)) continue;
					files.emplace_back(iter);
				}
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
					"=> Failed to list items in {}: {}",
					dir, e.what());
				continue;
			}

			std::sort(files.begin(), files.end());
			for (const auto& file : files) {
				if (is_directory(file))
					continue;

				try {
					const auto result = vpath.VirtualSqPack->AddEntryFromFile(relative(file, dir), file);
					const auto item = result.AnyItem();
					if (!item)
						throw std::runtime_error("Unexpected error");
					m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
						"=> {} file {}: (nameHash={:08x}, pathHash={:08x}, fullPathHash={:08x})",
						result.Added.empty() ? "Replaced" : "Added",
						item->PathSpec().Original,
						item->PathSpec().NameHash,
						item->PathSpec().PathHash,
						item->PathSpec().FullPathHash);
					additionalEntriesFound = true;
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
						"=> Failed to add file {}: {}",
						file, e.what());
				}
			}
		}
		return additionalEntriesFound;
	}
};

std::shared_ptr<App::Feature::GameResourceOverrider::Implementation> App::Feature::GameResourceOverrider::AcquireImplementation() {
	static std::mutex mtx;
	auto m_pImpl = s_pImpl.lock();
	if (!m_pImpl) {
		std::lock_guard lock(mtx);
		m_pImpl = s_pImpl.lock();
		if (!m_pImpl)
			s_pImpl = m_pImpl = std::make_unique<Implementation>();
	}
	return m_pImpl;
}

App::Feature::GameResourceOverrider::GameResourceOverrider()
	: m_pImpl(AcquireImplementation()) {
}

App::Feature::GameResourceOverrider::~GameResourceOverrider() = default;
