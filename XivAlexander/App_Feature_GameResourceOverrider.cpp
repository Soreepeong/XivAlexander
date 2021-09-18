#include "pch.h"
#include "App_Feature_GameResourceOverrider.h"

#include <XivAlexanderCommon/Sqex_Excel_Generator.h>
#include <XivAlexanderCommon/Sqex_Excel_Reader.h>
#include <XivAlexanderCommon/Sqex_FontCsv_CreateConfig.h>
#include <XivAlexanderCommon/Sqex_FontCsv_Creator.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Utils_Win32_Process.h>
#include <XivAlexanderCommon/Utils_Win32_ThreadPool.h>
#include <XivAlexanderCommon/XivAlex.h>

#include "App_ConfigRepository.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_ExcelTransformConfig.h"
#include "App_Misc_Hooks.h"
#include "App_Misc_Logger.h"
#include "App_Window_ProgressPopupWindow.h"
#include "DllMain.h"

std::weak_ptr<App::Feature::GameResourceOverrider::Implementation> App::Feature::GameResourceOverrider::s_pImpl;

class ReEnterPreventer {
	std::mutex m_lock;
	std::set<DWORD> m_tids;

public:
	class Lock {
		ReEnterPreventer& p;
		bool re = false;

	public:
		explicit Lock(ReEnterPreventer& p)
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

	Misc::Hooks::ImportedFunction<HANDLE, LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE> CreateFileW{"kernel32::CreateFileW", "kernel32.dll", "CreateFileW"};
	Misc::Hooks::ImportedFunction<BOOL, HANDLE> CloseHandle{"kernel32::CloseHandle", "kernel32.dll", "CloseHandle"};
	Misc::Hooks::ImportedFunction<BOOL, HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED> ReadFile{"kernel32::ReadFile", "kernel32.dll", "ReadFile"};
	Misc::Hooks::ImportedFunction<BOOL, HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD> SetFilePointerEx{"kernel32::SetFilePointerEx", "kernel32.dll", "SetFilePointerEx"};

	Misc::Hooks::ImportedFunction<LPVOID, HANDLE, DWORD, SIZE_T> HeapAlloc{"kernel32.dll::HeapAlloc", "kernel32.dll", "HeapAlloc"};
	Misc::Hooks::ImportedFunction<BOOL, HANDLE, DWORD, LPVOID> HeapFree{"kernel32.dll::HeapFree", "kernel32.dll", "HeapFree"};

	ReEnterPreventer m_repCreateFileW, m_repReadFile;

	const std::filesystem::path m_baseSqpackDir = Utils::Win32::Process::Current().PathOf().remove_filename() / L"sqpack";

	static constexpr int PathTypeIndex = -1;
	static constexpr int PathTypeIndex2 = -2;
	static constexpr int PathTypeInvalid = -3;

	struct OverlayedHandleData {
		Utils::Win32::Event IdentifierHandle;
		std::filesystem::path Path;
		LARGE_INTEGER FilePointer;
		std::shared_ptr<Sqex::RandomAccessStream> Stream;

		void ChooseStreamFrom(const Sqex::Sqpack::Creator::SqpackViews& views, int pathType) {
			switch (pathType) {
				case PathTypeIndex:
					Stream = views.Index;
					break;

				case PathTypeIndex2:
					Stream = views.Index2;
					break;

				default:
					if (pathType < 0 || static_cast<size_t>(pathType) >= views.Data.size())
						throw std::runtime_error("invalid #");
					Stream = views.Data[pathType];
			}
		}
	};

	std::mutex m_virtualPathMapMutex;
	std::map<std::filesystem::path, Sqex::Sqpack::Creator::SqpackViews> m_sqpackViews;
	std::map<HANDLE, std::unique_ptr<OverlayedHandleData>> m_overlayedHandles;
	std::set<std::filesystem::path> m_ignoredIndexFiles;
	std::atomic<int> m_stk;

	std::mutex m_processHeapAllocationTrackerMutex;
	std::map<void*, size_t> m_processHeapAllocations;

	class AtomicIntEnter {
		std::atomic<int>& v;
	public:
		AtomicIntEnter(std::atomic<int>& v)
			: v(v) {
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

			m_cleanup += CreateFileW.SetHook([this](
				_In_ LPCWSTR lpFileName,
				_In_ DWORD dwDesiredAccess,
				_In_ DWORD dwShareMode,
				_In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
				_In_ DWORD dwCreationDisposition,
				_In_ DWORD dwFlagsAndAttributes,
				_In_opt_ HANDLE hTemplateFile
			) {
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
								int pathType = PathTypeInvalid;

								if (fileToOpen == indexFile) {
									pathType = PathTypeIndex;
								} else if (fileToOpen == index2File) {
									pathType = PathTypeIndex2;
								} else {
									for (auto i = 0; i < 8; ++i) {
										const auto datFile = std::filesystem::path(recreatedFilePath).replace_extension(std::format(L".dat{}", i));
										if (fileToOpen == datFile) {
											pathType = i;
											break;
										}
									}
								}

								if (pathType != PathTypeInvalid) {
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

			m_cleanup += CloseHandle.SetHook([this](
				HANDLE handle
			) {
					AtomicIntEnter implUseLock(m_stk);

					std::unique_lock lock(m_virtualPathMapMutex);
					if (m_overlayedHandles.erase(handle))
						return 0;

					return CloseHandle.bridge(handle);
				});

			m_cleanup += ReadFile.SetHook([this](
				_In_ HANDLE hFile,
				_Out_writes_bytes_to_opt_(nNumberOfBytesToRead, *lpNumberOfBytesRead) __out_data_source(FILE) LPVOID lpBuffer,
				_In_ DWORD nNumberOfBytesToRead,
				_Out_opt_ LPDWORD lpNumberOfBytesRead,
				_Inout_opt_ LPOVERLAPPED lpOverlapped
			) {
					AtomicIntEnter implUseLock(m_stk);

					OverlayedHandleData* pvpath = nullptr;
					{
						std::lock_guard lock(m_virtualPathMapMutex);
						const auto vpit = m_overlayedHandles.find(hFile);
						if (vpit != m_overlayedHandles.end())
							pvpath = vpit->second.get();
					}

					if (const auto lock = ReEnterPreventer::Lock(m_repReadFile); pvpath && lock) {
						auto& vpath = *pvpath;
						try {
							const auto fp = lpOverlapped ? ((static_cast<uint64_t>(lpOverlapped->OffsetHigh) << 32) | lpOverlapped->Offset) : vpath.FilePointer.QuadPart;
							const auto read = vpath.Stream->ReadStreamPartial(fp, lpBuffer, nNumberOfBytesToRead);

							if (lpNumberOfBytesRead)
								*lpNumberOfBytesRead = static_cast<DWORD>(read);

							if (read != nNumberOfBytesToRead) {
								m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}, requested {} bytes, read {} bytes; state: {}",
									vpath.Path.filename(), nNumberOfBytesToRead, read, vpath.Stream->DescribeState());
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
								m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}, Message: {}",
									vpath.Path.filename(), e.what());
							SetLastError(e.Code());
							return FALSE;

						} catch (const std::exception& e) {
							m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}, Message: {}",
								vpath.Path.filename(), e.what());
							SetLastError(ERROR_READ_FAULT);
							return FALSE;
						}
					}
					return ReadFile.bridge(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
				});

			m_cleanup += SetFilePointerEx.SetHook([this](
				_In_ HANDLE hFile,
				_In_ LARGE_INTEGER liDistanceToMove,
				_Out_opt_ PLARGE_INTEGER lpNewFilePointer,
				_In_ DWORD dwMoveMethod) {
					AtomicIntEnter implUseLock(m_stk);

					OverlayedHandleData* pvpath = nullptr;
					{
						std::lock_guard lock(m_virtualPathMapMutex);
						const auto vpit = m_overlayedHandles.find(hFile);
						if (vpit != m_overlayedHandles.end())
							pvpath = vpit->second.get();
					}
					if (!pvpath)
						return SetFilePointerEx.bridge(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);

					auto& vpath = *pvpath;
					try {
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
						m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"SetFilePointerEx: {}, Message: {}",
							vpath.Path.filename(), e.what());
						SetLastError(e.Code());
						return FALSE;

					} catch (const std::exception& e) {
						m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, L"ReadFile: {}, Message: {}",
							vpath.Path.filename(), e.what());
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
				reinterpret_cast<uint32_t(__stdcall*)(uint32_t, const char*, size_t)>(ptr)
			));
			m_cleanup += fns.back()->SetHook([this, ptr, self = fns.back().get()](uint32_t initVal, const char* str, size_t len) {
				if (!str || !*str)
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
					const char* languageCodes[] = {"ja", "en", "de", "fr", "chs", "cht", "ko"};
					const auto targetLanguageCode = languageCodes[static_cast<int>(m_config->Runtime.HashTrackerLanguageOverride.Value()) - 1];

					std::string nameLower = name;
					std::ranges::transform(nameLower, nameLower.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
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
					if (!newName.empty()) {
						const auto verifyTarget = Sqex::Sqpack::EntryPathSpec(std::format("{}{}", newName, ext));
						auto found = false;
						for (const auto& t : m_sqpackViews | std::views::values) {
							found = t.EntryOffsets.find(verifyTarget) != t.EntryOffsets.end();
							if (found)
								break;
						}

						if (found) {
							name = newName;
							const auto newStr = std::format("{}{}{}", name, ext, rest);
							if (!m_config->Runtime.UseHashTrackerKeyLogging) {
								if (m_alreadyLogged.find(name) == m_alreadyLogged.end()) {
									m_alreadyLogged.emplace(name);
									m_logger->Format(LogCategory::GameResourceOverrider, "{:x}: {} => {}", reinterpret_cast<size_t>(ptr), str, newStr);
								}
							}
							Utils::Win32::Process::Current().WriteMemory(const_cast<char*>(str), newStr.c_str(), newStr.size() + 1, true);
							len = newStr.size();
						}
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
		auto overlayedHandle = std::make_unique<OverlayedHandleData>(Utils::Win32::Event::Create(), fileToOpen, LARGE_INTEGER{}, nullptr);

		std::lock_guard lock(m_virtualPathMapMutex);
		for (const auto& view : m_sqpackViews) {
			if (equivalent(view.first, indexFile)) {
				overlayedHandle->ChooseStreamFrom(view.second, pathType);
				break;
			}
		}

		m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
			"Taking control of {}/{} (parent: {}/{}, type: {})",
			fileToOpen.parent_path().filename(), fileToOpen.filename(),
			indexFile.parent_path().filename(), indexFile.filename(),
			pathType);

		if (!overlayedHandle->Stream) {
			if (pathType != PathTypeIndex && pathType != PathTypeIndex2) {
				m_ignoredIndexFiles.insert(indexFile);
				m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
					"=> Ignoring, because the game is accessing dat file without accessing either index or index2 file.");
				return nullptr;
			}

			auto creator = Sqex::Sqpack::Creator(
				indexFile.parent_path().filename().string(),
				indexFile.filename().replace_extension().replace_extension().string()
			);
			if (const auto result = creator.AddEntriesFromSqPack(indexFile, true, true);
				!result.Added.empty() || !result.Replaced.empty()) {
				m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
					"=> Processed SqPack {}/{}: Added {}, replaced {}, ignored {}, error {}",
					creator.DatExpac, creator.DatName,
					result.Added.size(), result.Replaced.size(), result.SkippedExisting.size(), result.Error.size());
				for (const auto& error : result.Error) {
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
						"\t=> Error processing {}: {}", error.first, error.second);
				}
			}

			auto additionalEntriesFound = false;
			additionalEntriesFound |= SetUpVirtualFileFromExternalSqpacks(creator, indexFile);
			additionalEntriesFound |= SetUpVirtualFileFromTexToolsModPacks(creator, indexFile);
			additionalEntriesFound |= SetUpVirtualFileFromFileEntries(creator, indexFile);
			additionalEntriesFound |= SetUpVirtualFileFromFontConfig(creator, indexFile);

			// Nothing to override, 
			if (!additionalEntriesFound) {
				m_ignoredIndexFiles.insert(indexFile);
				m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
					"=> Found no resources to override, releasing control.");
				return nullptr;
			}

			auto res = creator.AsViews(false);
			overlayedHandle->ChooseStreamFrom(res, pathType);
			m_sqpackViews.emplace(indexFile, std::move(res));
		}

		const auto key = static_cast<HANDLE>(overlayedHandle->IdentifierHandle);
		m_overlayedHandles.insert_or_assign(key, std::move(overlayedHandle));
		SetLastError(0);
		return key;
	}

	bool SetUpVirtualFileFromExternalSqpacks(Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexFile) {
		auto changed = false;

		const auto [region, _] = XivAlex::ResolveGameReleaseRegion();

		if (creator.DatName == "0a0000") {
			const auto cachedDir = m_config->Init.ResolveConfigStorageDirectoryPath() / "Cached" / region / creator.DatExpac / creator.DatName;
			if (!(exists(cachedDir / "TTMPD.mpd") && exists(cachedDir / "TTMPL.mpl"))) {

				std::map<std::string, int> exhTable;
				// maybe generate exl?

				for (const auto& pair : Sqex::Excel::ExlReader(*creator["exd/root.exl"]))
					exhTable.emplace(pair);

				std::vector<std::unique_ptr<Sqex::Sqpack::Reader>> readers;

				for (const auto& additionalSqpackRootDirectory : m_config->Runtime.AdditionalSqpackRootDirectories.Value()) {
					const auto file = additionalSqpackRootDirectory / "sqpack" / indexFile.parent_path().filename() / indexFile.filename();
					if (!exists(file))
						continue;

					readers.emplace_back(std::make_unique<Sqex::Sqpack::Reader>(file));
				}
				if (readers.empty())
					return false;

				create_directories(cachedDir);

				std::vector<std::pair<std::regex, Misc::ExcelTransformConfig::PluralColumns>> pluralColumns;
				std::map<Sqex::Language, std::vector<std::tuple<std::regex, std::regex, std::vector<Sqex::Language>, std::string, std::vector<size_t>>>> replacements;
				auto replacementFileParseFail = false;
				for (auto configFile : m_config->Runtime.ExcelTransformConfigFiles.Value()) {
					configFile = m_config->TranslatePath(configFile);
					if (configFile.empty())
						continue;

					try {
						std::ifstream in(configFile);
						nlohmann::json j;
						in >> j;

						Misc::ExcelTransformConfig::Config transformConfig;
						from_json(j, transformConfig);

						for (const auto& entry : transformConfig.pluralMap) {
							pluralColumns.emplace_back(std::regex(entry.first, std::regex_constants::ECMAScript | std::regex_constants::icase), entry.second);
						}
						for (const auto& rule : transformConfig.rules) {
							for (const auto& targetGroupName : rule.targetGroups) {
								for (const auto& target : transformConfig.targetGroups.at(targetGroupName).columnIndices) {
									replacements[transformConfig.targetLanguage].emplace_back(
										std::regex(target.first, std::regex_constants::ECMAScript | std::regex_constants::icase),
										std::regex(rule.stringPattern, std::regex_constants::ECMAScript | std::regex_constants::icase),
										transformConfig.sourceLanguages,
										rule.replaceTo,
										target.second
									);
								}
							}
						}
					} catch (const std::exception& e) {
						m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
							"=> Error occurred while parsing excel transformation configuration file {}: {}",
							configFile.wstring(), e.what());
						replacementFileParseFail = true;
					}
				}
				if (replacementFileParseFail) {
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
						"=> Skipping merged 0a0000 file generation");
					return false;
				}

				const auto actCtx = Dll::ActivationContext().With();
				Window::ProgressPopupWindow progressWindow(Dll::FindGameMainWindow(false));
				Utils::Win32::TpEnvironment pool;
				progressWindow.UpdateMessage("Generating merged exd files...");

				static constexpr auto ProgressMaxPerTask = 1000;
				std::map<std::string, uint64_t> progressPerTask;
				for (const auto& exhName : exhTable | std::views::keys)
					progressPerTask.emplace(exhName, 0);
				progressWindow.UpdateProgress(0, exhTable.size() * ProgressMaxPerTask);
				progressWindow.Show();
				{
					const auto ttmpl = Utils::Win32::File::Create(cachedDir / "TTMPL.mpl.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
					const auto ttmpd = Utils::Win32::File::Create(cachedDir / "TTMPD.mpd.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
					uint64_t ttmplPtr = 0, ttmpdPtr = 0;
					std::mutex writeMtx;

					const auto compressThread = Utils::Win32::Thread(L"CompressThread", [&]() {
						for (const auto& exhName : exhTable | std::views::keys) {
							pool.SubmitWork([&]() {
								try {
									if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
										return;

									// Step. Calculate maximum progress
									size_t progressIndex = 0;
									uint64_t currentProgressMax = 0;
									uint64_t currentProgress = 0;
									auto& progressStoreTarget = progressPerTask.at(exhName);
									const auto publishProgress = [&]() {
										progressStoreTarget = (progressIndex * ProgressMaxPerTask + (currentProgressMax ? currentProgress * ProgressMaxPerTask / currentProgressMax : 0)) / (readers.size() + 2);
									};

									// Step. Load source EXH/D files
									const auto exhPath = Sqex::Sqpack::EntryPathSpec(std::format("exd/{}.exh", exhName));
									std::unique_ptr<Sqex::Excel::Depth2ExhExdCreator> exCreator;
									{
										const auto exhReaderSource = Sqex::Excel::ExhReader(exhName, *creator[exhPath]);
										if (exhReaderSource.Header.Depth != Sqex::Excel::Exh::Depth::Level2) {
											progressStoreTarget = ProgressMaxPerTask;
											return;
										}

										if (std::ranges::find(exhReaderSource.Languages, Sqex::Language::Unspecified) != exhReaderSource.Languages.end()) {
											progressStoreTarget = ProgressMaxPerTask;
											return;
										}

										m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
											"=> Merging {}", exhName);

										exCreator = std::make_unique<Sqex::Excel::Depth2ExhExdCreator>(exhName, *exhReaderSource.Columns, exhReaderSource.Header.SomeSortOfBufferSize);
										exCreator->FillMissingLanguageFrom = Sqex::Language::English;  // TODO: make it into option

										currentProgressMax = exhReaderSource.Languages.size() * exhReaderSource.Pages.size();
										for (const auto language : exhReaderSource.Languages) {
											for (const auto& page : exhReaderSource.Pages) {
												if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
													return;
												currentProgress++;
												publishProgress();

												const auto exdPathSpec = exhReaderSource.GetDataPathSpec(page, language);
												try {
													const auto exdReader = Sqex::Excel::ExdReader(exhReaderSource, creator[exdPathSpec]);
													exCreator->AddLanguage(language);
													for (const auto i : exdReader.GetIds())
														exCreator->SetRow(i, language, exdReader.ReadDepth2(i));
												} catch (const std::out_of_range&) {
													// pass
												} catch (const std::exception& e) {
													throw std::runtime_error(std::format("Error while processing {}: {}", exdPathSpec, e.what()));
												}
											}
										}
										currentProgress = 0;
										progressIndex++;
										publishProgress();
									}

									auto sourceLanguages = exCreator->Languages;
									std::vector languagePriorities{  // TODO
										Sqex::Language::English,
										Sqex::Language::Japanese,
										Sqex::Language::German,
										Sqex::Language::French,
										Sqex::Language::ChineseSimplified,
										Sqex::Language::ChineseTraditional,
										Sqex::Language::Korean,
									};

									// Step. Load external EXH/D files
									for (const auto& reader : readers) {
										try {
											const auto exhReaderCurrent = Sqex::Excel::ExhReader(exhName, *(*reader)[exhPath]);

											currentProgressMax = exhReaderCurrent.Languages.size() * exhReaderCurrent.Pages.size();
											for (const auto language : exhReaderCurrent.Languages) {
												for (const auto& page : exhReaderCurrent.Pages) {
													if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
														return;
													currentProgress++;
													publishProgress();

													const auto exdPathSpec = exhReaderCurrent.GetDataPathSpec(page, language);
													try {
														const auto exdReader = Sqex::Excel::ExdReader(exhReaderCurrent, (*reader)[exdPathSpec]);
														exCreator->AddLanguage(language);
														for (const auto i : exdReader.GetIds()) {
															auto row = exdReader.ReadDepth2(i);
															if (row.size() != exCreator->Columns.size()) {
																const auto& rowSet = exCreator->Data.at(i);
																const std::vector<Sqex::Excel::ExdColumn>* referenceRow = nullptr;
																for (const auto& l : languagePriorities) {
																	if (auto it = rowSet.find(l);
																		it != rowSet.end()) {
																		referenceRow = &it->second;
																		break;
																	}
																}
																if (!referenceRow)
																	continue;

																// Exceptions for Chinese client are based on speculations.
																if (((region == L"JP" || region == L"KR") && language == Sqex::Language::ChineseSimplified) && exhName == "Fate") {
																	auto replacements = std::vector{row[30].String, row[31].String, row[32].String, row[33].String, row[34].String, row[35].String};
																	row = *referenceRow;
																	for (size_t j = 0; j < replacements.size(); ++j)
																		row[j].String = std::move(replacements[j]);

																} else if (region == L"CN" && language != Sqex::Language::ChineseSimplified && exhName == "Fate") {
																	auto replacements = std::vector{row[0].String, row[1].String, row[2].String, row[3].String, row[4].String, row[5].String};
																	row = *referenceRow;
																	for (size_t j = 0; j < replacements.size(); ++j)
																		row[30 + j].String = std::move(replacements[j]);

																} else {
																	for (size_t j = row.size(); j < referenceRow->size(); ++j)
																		row.push_back((*referenceRow)[j]);
																	row.resize(referenceRow->size());
																}
															}
															exCreator->SetRow(i, language, std::move(row), false);
														}
													} catch (const std::out_of_range&) {
														// pass
													} catch (const std::exception& e) {
														m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
															"=> Skipping {} because of error: {}", exdPathSpec, e.what());
													}
												}
											}
										} catch (const std::out_of_range&) {
											// pass
										}
										currentProgress = 0;
										progressIndex++;
										publishProgress();
									}

									// Step. Ensure that there are no missing rows from externally sourced exd files
									std::vector<uint32_t> idsToRemove;
									for (auto& kv : exCreator->Data) {
										const auto id = kv.first;
										auto& rowSet = kv.second;
										static_assert(std::is_reference_v<decltype(rowSet)>);

										if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
											return;

										// Step. Find which language to use while filling current row if missing in other languages
										const std::vector<Sqex::Excel::ExdColumn>* referenceRow = nullptr;
										for (const auto& l : languagePriorities) {
											if (auto it = rowSet.find(l);
												it != rowSet.end()) {
												referenceRow = &it->second;
												break;
											}
										}
										if (!referenceRow) {
											idsToRemove.push_back(id);
											continue;
										}

										// Step. Figure out which columns to not modify and which are modifiable
										std::set<size_t> columnsToModify, columnsToNeverModify;
										try {
											std::vector<Sqex::Excel::ExdColumn>* cols[4] = {
												&rowSet.at(Sqex::Language::Japanese),
												&rowSet.at(Sqex::Language::English),
												&rowSet.at(Sqex::Language::German),
												&rowSet.at(Sqex::Language::French),
											};
											for (size_t i = 0; i < cols[0]->size(); ++i) {
												std::string compareTarget;
												for (size_t j = 0; j < _countof(cols); ++j) {
													if ((*cols[j])[i].Type != Sqex::Excel::Exh::String) {
														columnsToNeverModify.insert(i);
														break;
													} else if (j != 0 && (*cols[0])[i].String != (*cols[j])[i].String) {
														columnsToModify.insert(i);
														break;
													}
												}
											}
										} catch (const std::out_of_range&) {
											// pass
										}
										try {
											std::vector<Sqex::Excel::ExdColumn>& cols = rowSet.at(Sqex::Language::Korean);
											for (size_t i = 0; i < cols.size(); ++i) {
												if (cols[i].Type != Sqex::Excel::Exh::String) {
													columnsToNeverModify.insert(i);

												} else {
													// If it includes a valid UTF-8 byte sequence that is longer than a byte, set as a candidate.
													const auto& s = cols[i].String;
													for (size_t j = 0; j < s.size(); ++j) {
														char32_t charCode;

														if ((s[j] & 0x80) == 0) {
															// pass; single byte
															charCode = s[j];

														} else if ((s[j] & 0xE0) == 0xC0) {
															// 2 bytes
															if (j + 2 > s.size())
																break; // not enough bytes
															if ((s[j + 1] & 0xC0) != 0x80)
																continue;
															charCode = static_cast<char32_t>(
																((s[j + 0] & 0x1F) << 6) |
																((s[j + 1] & 0x3F) << 0)
															);
															j += 1;

														} else if ((s[j] & 0xF0) == 0xE0) {
															// 3 bytes
															if (j + 3 > s.size())
																break; // not enough bytes
															if ((s[j + 1] & 0xC0) != 0x80
																|| (s[j + 2] & 0xC0) != 0x80)
																continue;
															charCode = static_cast<char32_t>(
																((s[j + 0] & 0x0F) << 12) |
																((s[j + 1] & 0x3F) << 6) |
																((s[j + 2] & 0x3F) << 0)
															);
															j += 2;

														} else if ((s[j] & 0xF8) == 0xF0) {
															// 4 bytes
															if (j + 4 > s.size())
																break; // not enough bytes
															if ((s[j + 1] & 0xC0) != 0x80
																|| (s[j + 2] & 0xC0) != 0x80
																|| (s[j + 3] & 0xC0) != 0x80)
																continue;
															charCode = static_cast<char32_t>(
																((s[j + 0] & 0x07) << 18) |
																((s[j + 1] & 0x3F) << 12) |
																((s[j + 2] & 0x3F) << 6) |
																((s[j + 3] & 0x3F) << 0)
															);
															j += 3;

														} else {
															// invalid
															continue;
														}

														if (charCode >= 128) {
															columnsToModify.insert(i);
															break;
														}
													}
												}
											}
										} catch (const std::out_of_range&) {
											// pass
										}

										// Step. Fill missing rows for languages that aren't from source, and restore columns if unmodifiable
										for (const auto& language : exCreator->Languages) {
											if (auto it = rowSet.find(language);
												it == rowSet.end())
												rowSet[language] = *referenceRow;
											else {
												auto& row = it->second;
												for (size_t i = 0; i < row.size(); ++i) {
													if (columnsToNeverModify.find(i) != columnsToNeverModify.end()) {
														row[i] = (*referenceRow)[i];
													}
												}
											}
										}

										// Step. Adjust language data per use config
										std::map<Sqex::Language, std::vector<Sqex::Excel::ExdColumn>> pendingReplacements;
										for (const auto& language : exCreator->Languages) {
											const auto rules = replacements.find(language);
											if (rules == replacements.end())
												continue;

											auto row = rowSet.at(language);

											for (const auto columnIndex : columnsToModify) {
												for (const auto& rule : rules->second) {
													const auto& exhNamePattern = std::get<0>(rule);
													const auto& columnDataPattern = std::get<1>(rule);
													const auto& ruleSourceLanguages = std::get<2>(rule);
													const auto& replacementFormat = std::get<3>(rule);
													const auto& columnIndices = std::get<4>(rule);

													if (!columnIndices.empty() && std::ranges::find(columnIndices, columnIndex) == columnIndices.end())
														continue;

													if (!std::regex_search(exhName, exhNamePattern))
														continue;

													if (!std::regex_search(row[columnIndex].String, columnDataPattern))
														continue;

													std::vector p = {std::format("{}", id)};
													for (const auto ruleSourceLanguage : ruleSourceLanguages) {
														if (const auto it = rowSet.find(ruleSourceLanguage);
															it != rowSet.end()) {

															if (row[columnIndex].Type != Sqex::Excel::Exh::String)
																throw std::invalid_argument(std::format("Column {} of sourceLanguage {} in {} is not a string column", columnIndex, static_cast<int>(ruleSourceLanguage), exhName));

															Misc::ExcelTransformConfig::PluralColumns pluralColummIndices;
															for (const auto& entry : pluralColumns) {
																if (std::regex_search(exhName, entry.first)) {
																	pluralColummIndices = entry.second;
																	break;
																}
															}

															auto readColumnIndex = columnIndex;
															switch (ruleSourceLanguage) {
																case Sqex::Language::English:
																	// add stuff if stuff happens(tm)
																	break;

																case Sqex::Language::German:
																case Sqex::Language::French:
																	// add stuff if stuff happens(tm)
																	break;

																case Sqex::Language::Japanese:
																case Sqex::Language::ChineseSimplified:
																case Sqex::Language::ChineseTraditional:
																case Sqex::Language::Korean: {
																	if (pluralColummIndices.capitalizedColumnIndex != Misc::ExcelTransformConfig::PluralColumns::Index_NoColumn) {
																		if (readColumnIndex == pluralColummIndices.pluralColumnIndex
																			|| readColumnIndex == pluralColummIndices.singularColumnIndex)
																			readColumnIndex = pluralColummIndices.capitalizedColumnIndex;
																	} else {
																		if (readColumnIndex == pluralColummIndices.pluralColumnIndex)
																			readColumnIndex = pluralColummIndices.singularColumnIndex;
																	}
																	break;
																}
															}
															p.emplace_back(it->second[readColumnIndex].String);
														} else
															p.emplace_back();
													}

													auto allSame = true;
													size_t nonEmptySize = 0;
													size_t lastNonEmptyIndex = 1;
													for (size_t i = 1; i < p.size(); ++i) {
														if (p[i] != p[0])
															allSame = false;
														if (!p[i].empty()) {
															nonEmptySize++;
															lastNonEmptyIndex = i;
														}
													}
													std::string out;
													if (allSame)
														out = p[1];
													else if (nonEmptySize <= 1)
														out = p[lastNonEmptyIndex];
													else {
														switch (p.size()) {
															case 1:
																out = std::format(replacementFormat, p[0]);
																break;
															case 2:
																out = std::format(replacementFormat, p[0], p[1]);
																break;
															case 3:
																out = std::format(replacementFormat, p[0], p[1], p[2]);
																break;
															case 4:
																out = std::format(replacementFormat, p[0], p[1], p[2], p[3]);
																break;
															case 5:
																out = std::format(replacementFormat, p[0], p[1], p[2], p[3], p[4]);
																break;
															case 6:
																out = std::format(replacementFormat, p[0], p[1], p[2], p[3], p[4], p[5]);
																break;
															case 7:
																out = std::format(replacementFormat, p[0], p[1], p[2], p[3], p[4], p[5], p[6]);
																break;
															default:
																throw std::invalid_argument("Only up to 7 source languages are supported");
														}
													}
													row[columnIndex].String = Utils::StringTrim(out);
													break;
												}
											}
											pendingReplacements.emplace(language, std::move(row));
										}
										for (auto& pair : pendingReplacements)
											exCreator->SetRow(id, pair.first, pair.second);
									}

									{
										auto compiled = exCreator->Compile();
										m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
											"=> Saving {}", exhName);

										currentProgressMax = compiled.size();
										for (auto& kv : compiled) {
											const auto& entryPathSpec = kv.first;
											auto& data = kv.second;
											currentProgress++;
											publishProgress();

											const auto targetPath = cachedDir / entryPathSpec.Original;

											const auto provider = Sqex::Sqpack::MemoryBinaryEntryProvider(entryPathSpec, std::make_shared<Sqex::MemoryRandomAccessStream>(std::move(*reinterpret_cast<std::vector<uint8_t>*>(&data))));
											const auto len = provider.StreamSize();
											const auto dv = provider.ReadStreamIntoVector<char>(0, len);

											if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
												return;
											const auto lock = std::lock_guard(writeMtx);
											const auto entryLine = std::format("{}\n", nlohmann::json::object({
												{"FullPath", Utils::ToUtf8(entryPathSpec.Original.wstring())},
												{"ModOffset", ttmpdPtr},
												{"ModSize", len},
												{"DatFile", "0a0000"},
											}).dump());
											ttmplPtr += ttmpl.Write(ttmplPtr, std::span(entryLine));
											ttmpdPtr += ttmpd.Write(ttmpdPtr, std::span(dv));
										}
										currentProgress = 0;
										progressIndex++;
										publishProgress();
									}
								} catch (const std::exception& e) {
									m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "=> Error: {}", e.what());
									progressWindow.Cancel();
								}
							});
						}
						pool.WaitOutstanding();
					});
					while (true) {
						if (WAIT_TIMEOUT != progressWindow.DoModalLoop(100, {compressThread}))
							break;

						uint64_t p = 0;
						for (const auto& val : progressPerTask | std::views::values)
							p += val;
						progressWindow.UpdateProgress(p, progressPerTask.size() * ProgressMaxPerTask);
					}
					pool.Cancel();
					compressThread.Wait();
				}

				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0) {
					try {
						std::filesystem::remove(cachedDir / "TTMPL.mpl.tmp");
					} catch (...) {
						// whatever
					}
					try {
						std::filesystem::remove(cachedDir / "TTMPD.mpd.tmp");
					} catch (...) {
						// whatever
					}
					return false;
				}

				std::filesystem::rename(cachedDir / "TTMPL.mpl.tmp", cachedDir / "TTMPL.mpl");
				std::filesystem::rename(cachedDir / "TTMPD.mpd.tmp", cachedDir / "TTMPD.mpd");
			}

			try {
				const auto logCacher = creator.Log([&](const auto& s) {
					m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, "=> {}", s);
				});
				const auto result = creator.AddEntriesFromTTMP(cachedDir);
				if (!result.Added.empty() || !result.Replaced.empty())
					changed = true;
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "=> Error: {}", e.what());
			}
		} else {
			for (const auto& additionalSqpackRootDirectory : m_config->Runtime.AdditionalSqpackRootDirectories.Value()) {
				const auto file = additionalSqpackRootDirectory / "sqpack" / indexFile.parent_path().filename() / indexFile.filename();
				if (!exists(file))
					continue;

				const auto batchAddResult = creator.AddEntriesFromSqPack(file, false, false);

				if (!batchAddResult.Added.empty()) {
					changed = true;
					m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
						"=> Processed external SqPack {}: Added {}",
						file, batchAddResult.Added.size());
					for (const auto& error : batchAddResult.Error) {
						m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
							"\t=> Error processing {}: {}", error.first, error.second);
					}
				}
			}
		}

		return changed;
	}

	bool SetUpVirtualFileFromTexToolsModPacks(Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexPath) {
		auto additionalEntriesFound = false;
		std::vector<std::filesystem::path> dirs;

		if (m_config->Runtime.UseDefaultTexToolsModPackSearchDirectory) {
			dirs.emplace_back(indexPath.parent_path().parent_path().parent_path() / "TexToolsMods");
			dirs.emplace_back(m_config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods");
		}

		for (const auto& dir : m_config->Runtime.AdditionalTexToolsModPackSearchDirectories.Value()) {
			if (!dir.empty())
				dirs.emplace_back(Config::TranslatePath(dir));
		}

		for (const auto& dir : dirs) {
			if (dir.empty() || !is_directory(dir))
				continue;

			std::vector<std::filesystem::path> files;
			try {
				for (const auto& iter : std::filesystem::recursive_directory_iterator(dir)) {
					if (iter.path().filename() != "TTMPL.mpl")
						continue;
					files.emplace_back(iter);
				}
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
					"=> Failed to list items in {}: {}",
					dir, e.what());
				continue;
			}

			std::ranges::sort(files);
			for (const auto& file : files) {
				m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, "Processing {}", file);

				if (exists(file.parent_path() / "disable")) {
					m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, "=> Disabled because \"disable\" file exists");
					continue;
				}
				try {
					const auto logCacher = creator.Log([&](const auto& s) {
						m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, "=> {}", s);
					});
					const auto result = creator.AddEntriesFromTTMP(file.parent_path());
					if (!result.Added.empty() || !result.Replaced.empty())
						additionalEntriesFound = true;
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "=> Error: {}", e.what());
				}
			}
		}
		return additionalEntriesFound;
	}

	bool SetUpVirtualFileFromFileEntries(Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexPath) {
		auto additionalEntriesFound = false;
		std::vector<std::filesystem::path> dirs;

		if (m_config->Runtime.UseDefaultGameResourceFileEntryRootDirectory) {
			dirs.emplace_back(indexPath.parent_path().parent_path());
			dirs.emplace_back(m_config->Init.ResolveConfigStorageDirectoryPath() / "ReplacementFileEntries");
		}

		for (const auto& dir : m_config->Runtime.AdditionalGameResourceFileEntryRootDirectories.Value()) {
			if (!dir.empty())
				dirs.emplace_back(Config::TranslatePath(dir));
		}

		for (size_t i = 0, i_ = dirs.size(); i < i_; ++i) {
			dirs.emplace_back(dirs[i] / std::format("{}.win32", creator.DatExpac) / creator.DatName);
			dirs[i] = dirs[i] / creator.DatExpac / creator.DatName;
		}

		for (const auto& dir : dirs) {
			if (!is_directory(dir))
				continue;

			std::vector<std::filesystem::path> files;

			try {
				for (const auto& iter : std::filesystem::recursive_directory_iterator(dir)) {
					if (is_directory(iter))
						continue;
					files.emplace_back(iter);
				}
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider,
					"=> Failed to list items in {}: {}",
					dir, e.what());
				continue;
			}

			std::ranges::sort(files);
			for (const auto& file : files) {
				if (is_directory(file))
					continue;

				try {
					const auto result = creator.AddEntryFromFile(relative(file, dir), file);
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

	bool SetUpVirtualFileFromFontConfig(Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexPath) {
		if (indexPath.filename() != L"000000.win32.index")
			return false;

		if (const auto fontConfigPathStr = m_config->Runtime.OverrideFontConfig.Value(); !fontConfigPathStr.empty()) {
			const auto fontConfigPath = Config::TranslatePath(fontConfigPathStr);
			try {
				if (!exists(fontConfigPath))
					throw std::runtime_error(std::format("=> Font config file was not found: ", fontConfigPathStr));

				const auto [region, _] = XivAlex::ResolveGameReleaseRegion();

				const auto cachedDir = m_config->Init.ResolveConfigStorageDirectoryPath() / "Cached" / region / creator.DatExpac / creator.DatName;
				if (!(exists(cachedDir / "TTMPD.mpd") && exists(cachedDir / "TTMPL.mpl"))) {
					create_directories(cachedDir);

					const auto actCtx = Dll::ActivationContext().With();
					Window::ProgressPopupWindow progressWindow(Dll::FindGameMainWindow(false));
					progressWindow.UpdateMessage("Generating fonts...");
					progressWindow.Show();

					m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider,
						"=> Generating font per file: {}",
						fontConfigPathStr);

					std::ifstream fin(fontConfigPath);
					nlohmann::json j;
					fin >> j;
					auto cfg = j.get<Sqex::FontCsv::CreateConfig::FontCreateConfig>();

					Sqex::FontCsv::FontSetsCreator fontCreator(cfg, Utils::Win32::Process::Current().PathOf().parent_path());
					while (true) {
						if (WAIT_TIMEOUT != progressWindow.DoModalLoop(100, {fontCreator.GetWaitableObject()}))
							break;

						const auto progress = fontCreator.GetProgress();
						progressWindow.UpdateProgress(progress.Progress, progress.Max);
						if (progress.Indeterminate)
							progressWindow.UpdateMessage(std::format("Generating fonts... ({} task(s) yet to be started)", progress.Indeterminate));
						else
							progressWindow.UpdateMessage("Generating fonts...");
					}
					if (progressWindow.GetCancelEvent().Wait(0) != WAIT_OBJECT_0) {
						progressWindow.UpdateMessage("Compressing data...");
						Utils::Win32::TpEnvironment pool;
						const auto streams = fontCreator.GetResult().GetAllStreams();

						std::atomic_int64_t progress = 0;
						uint64_t maxProgress = 0;
						for (auto& stream : streams | std::views::values)
							maxProgress += stream->StreamSize() * 2;
						progressWindow.UpdateProgress(progress, maxProgress);

						const auto ttmpl = Utils::Win32::File::Create(cachedDir / "TTMPL.mpl.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
						const auto ttmpd = Utils::Win32::File::Create(cachedDir / "TTMPD.mpd.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
						uint64_t ttmplPtr = 0, ttmpdPtr = 0;
						std::mutex writeMtx;

						const auto compressThread = Utils::Win32::Thread(L"CompressThread", [&]() {
							for (const auto& kv : streams) {
								const auto& entryPathSpec = kv.first;
								const auto& stream = kv.second;

								pool.SubmitWork([&]() {
									if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
										return;

									std::shared_ptr<Sqex::Sqpack::EntryProvider> provider;
									auto extension = entryPathSpec.Original.extension().wstring();
									CharLowerW(&extension[0]);

									if (extension == L".tex")
										provider = std::make_shared<Sqex::Sqpack::MemoryTextureEntryProvider>(entryPathSpec, stream);
									else
										provider = std::make_shared<Sqex::Sqpack::MemoryBinaryEntryProvider>(entryPathSpec, stream);
									const auto len = provider->StreamSize();
									const auto dv = provider->ReadStreamIntoVector<char>(0, len);
									progress += stream->StreamSize();

									if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
										return;
									const auto lock = std::lock_guard(writeMtx);
									const auto entryLine = std::format("{}\n", nlohmann::json::object({
										{"FullPath", Utils::ToUtf8(entryPathSpec.Original.wstring())},
										{"ModOffset", ttmpdPtr},
										{"ModSize", len},
										{"DatFile", "000000"},
									}).dump());
									ttmplPtr += ttmpl.Write(ttmplPtr, std::span(entryLine));
									ttmpdPtr += ttmpd.Write(ttmpdPtr, std::span(dv));
									progress += stream->StreamSize();
								});
							}
							pool.WaitOutstanding();
						});

						while (true) {
							if (WAIT_TIMEOUT != progressWindow.DoModalLoop(100, {compressThread}))
								break;

							progressWindow.UpdateProgress(progress, maxProgress);
						}
						pool.Cancel();
						compressThread.Wait();
					}

					if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0) {
						try {
							std::filesystem::remove(cachedDir / "TTMPL.mpl.tmp");
						} catch (...) {
							// whatever
						}
						try {
							std::filesystem::remove(cachedDir / "TTMPD.mpd.tmp");
						} catch (...) {
							// whatever
						}
						return false;
					}

					std::filesystem::rename(cachedDir / "TTMPL.mpl.tmp", cachedDir / "TTMPL.mpl");
					std::filesystem::rename(cachedDir / "TTMPD.mpd.tmp", cachedDir / "TTMPD.mpd");
				}

				try {
					const auto logCacher = creator.Log([&](const auto& s) {
						m_logger->Format<LogLevel::Info>(LogCategory::GameResourceOverrider, "=> {}", s);
					});
					const auto result = creator.AddEntriesFromTTMP(cachedDir);
					if (!result.Added.empty() || !result.Replaced.empty())
						return true;
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "=> Error: {}", e.what());
				}
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, e.what());
			}
		}

		return false;
	}
};

std::shared_ptr<App::Feature::GameResourceOverrider::Implementation> App::Feature::GameResourceOverrider::AcquireImplementation() {
	auto m_pImpl = s_pImpl.lock();
	if (!m_pImpl) {
		static std::mutex mtx;
		const auto lock = std::lock_guard(mtx);
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

bool App::Feature::GameResourceOverrider::CanUnload() const {
	return m_pImpl->m_overlayedHandles.empty();
}
