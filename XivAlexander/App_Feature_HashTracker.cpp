#include "pch.h"
#include "App_Feature_HashTracker.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_Hooks.h"

static constexpr bool DebugFlag_PassthroughFileApi = false;

std::weak_ptr<App::Feature::HashTracker::Implementation> App::Feature::HashTracker::s_pImpl;

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

class App::Feature::HashTracker::Implementation {
public:
	const std::shared_ptr<Config> m_config;
	const std::shared_ptr<Misc::Logger> m_logger;
	const std::shared_ptr<Misc::DebuggerDetectionDisabler> m_debugger;
	std::vector<std::unique_ptr<Misc::Hooks::PointerFunction<uint32_t, uint32_t, const char*, size_t>>> fns;
	std::set<std::string> m_alreadyLogged;
	Utils::CallOnDestruction::Multiple m_cleanup;

	Misc::Hooks::ImportedFunction<HANDLE, LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE> CreateFileW{ "kernel32::CreateFileW", "kernel32.dll", "CreateFileW" };
	Misc::Hooks::ImportedFunction<BOOL, HANDLE> CloseHandle{ "kernel32::CloseHandle", "kernel32.dll", "CloseHandle" };
	Misc::Hooks::ImportedFunction<BOOL, HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED> ReadFile{ "kernel32::ReadFile", "kernel32.dll", "ReadFile" };
	Misc::Hooks::ImportedFunction<BOOL, HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD> SetFilePointerEx{ "kernel32::SetFilePointerEx", "kernel32.dll", "SetFilePointerEx" };

	ReEnterPreventer m_repCreateFileW, m_repReadFile;

	const std::filesystem::path m_baseSqpackDir = Utils::Win32::Process::Current().PathOf().remove_filename() / L"sqpack";

	struct VirtualPath {
		static constexpr int PathTypeIndex = -1;
		static constexpr int PathTypeIndex2 = -2;
		static constexpr int PathTypeInvalid = -3;

		Utils::Win32::Event IdentifierHandle;
		std::filesystem::path IndexPath;
		std::shared_ptr<XivAlex::SqexDef::VirtualSqPack> VirtualSqPack;
		Utils::Win32::File PassthroughFile;
		int PathType;
		LARGE_INTEGER FilePointer;
	};

	std::mutex m_virtualPathMapMutex;
	std::map<HANDLE, std::unique_ptr<VirtualPath>> m_virtualPathMap;
	std::atomic<int> m_stk;

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

		m_cleanup += CreateFileW.SetHook([&](_In_ LPCWSTR lpFileName,
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
					if (// indexFile.filename().wstring() == L"000000.win32.index" &&
						exists(indexFile) &&
						exists(index2File) &&
						equivalent(fileToOpen, recreatedFilePath)) {
						int pathType = VirtualPath::PathTypeInvalid;

						if (equivalent(fileToOpen, indexFile)) {
							pathType = VirtualPath::PathTypeIndex;
						} else if (equivalent(fileToOpen, index2File)) {
							pathType = VirtualPath::PathTypeIndex2;
						} else {
							for (auto i = 0; i < 8; ++i) {
								const auto datFile = std::filesystem::path(recreatedFilePath).replace_extension(std::format(L".dat{}", i));
								if (equivalent(fileToOpen, datFile)) {
									pathType = i;
									break;
								}
							}
						}

						if (pathType != VirtualPath::PathTypeInvalid) {
							auto vpath = std::make_unique<VirtualPath>(
								Utils::Win32::Event::Create(),
								indexFile,
								nullptr,
								Utils::Win32::File{},
								pathType,
								LARGE_INTEGER{}
							);

							std::lock_guard lock(m_virtualPathMapMutex);
							for (const auto& [_, vp] : m_virtualPathMap) {
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

							m_logger->Format<LogLevel::Info>(LogCategory::HashTracker,
								"Taking control of {}/{} (parent: {}/{}, type: {})",
								recreatedFilePath.parent_path().filename(), recreatedFilePath.filename(),
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
									vpath->VirtualSqPack = std::make_shared<XivAlex::SqexDef::VirtualSqPack>();
									{
										const auto result = vpath->VirtualSqPack->AddEntriesFromSqPack(indexFile, true);
										m_logger->Format<LogLevel::Info>(LogCategory::HashTracker,
											"=> Processed SqPack {}: Added {}, replaced {}",
											indexFile, result.AddedCount, result.ReplacedCount);
									}

									for (const auto& replacementDirPath : {
										std::filesystem::path(recreatedFilePath).replace_extension(".replacements"),
										}) {
										if (!exists(replacementDirPath))
											continue;
										try {
											for (const auto& replacementItem : std::filesystem::recursive_directory_iterator(replacementDirPath)) {
												if (is_directory(replacementItem))
													continue;
												try {
													const auto relativePath = relative(replacementItem, replacementDirPath);
													const auto nameHash = XivAlex::SqexDef::SqexHash(relativePath.filename().string());
													const auto pathHash = XivAlex::SqexDef::SqexHash(relativePath.parent_path().string());
													const auto fullPathHash = XivAlex::SqexDef::SqexHash(relativePath.string());
													const auto result = vpath->VirtualSqPack->AddEntryFromFile(pathHash, nameHash, fullPathHash, replacementItem);
													m_logger->Format<LogLevel::Info>(LogCategory::HashTracker,
														"=> {} file {}: (nameHash={:08x}, pathHash={:08x}, fullPathHash={:08x})",
														result.AddedCount ? "Added" : "Replaced",
														replacementItem.path(), nameHash, pathHash, fullPathHash);
												} catch (const std::exception& e) {
													m_logger->Format<LogLevel::Warning>(LogCategory::HashTracker,
														"=> Failed to add file {}: {}",
														replacementItem.path(), e.what());
												}
											}
										} catch (const std::exception& e) {
											m_logger->Format<LogLevel::Warning>(LogCategory::HashTracker,
												"=> Failed to list items in {}: {}",
												replacementDirPath, e.what());
										}
									}

									vpath->VirtualSqPack->Freeze(false);
								}
							}

							const auto key = static_cast<HANDLE>(vpath->IdentifierHandle);
							m_virtualPathMap.insert_or_assign(key, std::move(vpath));
							return key;
						}
					}
				} catch (const Utils::Win32::Error& e) {
					Utils::Win32::MessageBoxF(nullptr, MB_OK, L"CreateFileW hook error", L"File: {}, Message: {}", lpFileName, e.what());
					SetLastError(e.Code());
				} catch (const std::exception& e) {
					Utils::Win32::MessageBoxF(nullptr, MB_OK, L"CreateFileW hook error", L"File: {}, Message: {}", lpFileName, e.what());
				}
			}

			return CreateFileW.bridge(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
		});

		m_cleanup += CloseHandle.SetHook([&](HANDLE handle) {

			AtomicIntEnter implUseLock(m_stk);

			std::unique_lock lock(m_virtualPathMapMutex);
			if (m_virtualPathMap.erase(handle))
				return 0;

			return CloseHandle.bridge(handle);
		});

		m_cleanup += ReadFile.SetHook([&](_In_ HANDLE hFile,
			_Out_writes_bytes_to_opt_(nNumberOfBytesToRead, *lpNumberOfBytesRead) __out_data_source(FILE) LPVOID lpBuffer,
			_In_ DWORD nNumberOfBytesToRead,
			_Out_opt_ LPDWORD lpNumberOfBytesRead,
			_Inout_opt_ LPOVERLAPPED lpOverlapped) {

			AtomicIntEnter implUseLock(m_stk);

			if (const auto lock = ReEnterPreventer::Lock(m_repReadFile); lock) {
				try {
					VirtualPath* pvpath = nullptr;
					{
						std::lock_guard lock(m_virtualPathMapMutex);
						const auto vpit = m_virtualPathMap.find(hFile);
						if (vpit != m_virtualPathMap.end())
							pvpath = vpit->second.get();
					}
					if (pvpath) {
						auto& vpath = *pvpath;

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
					}
				} catch (const Utils::Win32::Error& e) {
					if (e.Code() != ERROR_IO_PENDING)
						Utils::Win32::MessageBoxF(nullptr, MB_OK, L"ReadFile hook error", L"Message: {}", e.what());
					SetLastError(e.Code());
					return FALSE;
				} catch (const std::exception& e) {
					Utils::Win32::MessageBoxF(nullptr, MB_OK, L"ReadFile hook error", L"Message: {}", e.what());
					return FALSE;
				}
			}

			return ReadFile.bridge(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
		});

		m_cleanup += SetFilePointerEx.SetHook([&](_In_ HANDLE hFile,
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

			try {
				auto& vpath = *pvpath;
				uint64_t len;
				if (vpath.PassthroughFile)
					len = vpath.PassthroughFile.Length();
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

			} catch (const std::exception& e) {
				MessageBoxW(nullptr, Utils::FromUtf8(e.what()).c_str(), L"TEST", MB_OK);
			}

			return TRUE;

		});

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

				if (m_config->Runtime.HashTrackerLanguageOverride != Config::GameLanguage::Unspecified) {
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
							case Config::GameLanguage::Japanese:
								name += "_ja";
								break;
							case Config::GameLanguage::English:
								name += "_en";
								break;
							case Config::GameLanguage::German:
								name += "_de";
								break;
							case Config::GameLanguage::French:
								name += "_fr";
								break;
							case Config::GameLanguage::ChineseSimplified:
								name += "_chs";
								break;
							case Config::GameLanguage::ChineseTraditional:
								name += "_cht";
								break;
							case Config::GameLanguage::Korean:
								name += "_ko";
								break;
						}

						const auto newStr = std::format("{}{}{}", name, ext, rest);

						if (!m_config->Runtime.UseHashTrackerKeyLogging) {
							if (m_alreadyLogged.find(name) == m_alreadyLogged.end()) {
								m_alreadyLogged.emplace(name);
								m_logger->Format(LogCategory::HashTracker, "{:x}: {} => {}", reinterpret_cast<size_t>(ptr), str, newStr);
							}
						}
						Utils::Win32::Process::Current().WriteMemory(const_cast<char*>(str), newStr.c_str(), newStr.size() + 1, true);
					}
				}
				const auto res = self->bridge(initVal, str, len);

				if (m_config->Runtime.UseHashTrackerKeyLogging) {
					if (m_alreadyLogged.find(name) == m_alreadyLogged.end()) {
						m_alreadyLogged.emplace(name);
						m_logger->Format(LogCategory::HashTracker, "{:x}: {},{},{} => {:08x}", reinterpret_cast<size_t>(ptr), name, ext, rest, res);
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
};

App::Feature::HashTracker::HashTracker() {
	static std::mutex mtx;
	m_pImpl = s_pImpl.lock();
	if (!m_pImpl) {
		std::lock_guard lock(mtx);
		m_pImpl = s_pImpl.lock();
		if (!m_pImpl)
			s_pImpl = m_pImpl = std::make_unique<Implementation>();
	}
}

App::Feature::HashTracker::~HashTracker() {
	m_pImpl = nullptr;
}
