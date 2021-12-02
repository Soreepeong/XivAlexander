#include "pch.h"
#include "App_Misc_VirtualSqPacks.h"

#include <XivAlexanderCommon/Sqex_EscapedString.h>
#include <XivAlexanderCommon/Sqex_Excel_Generator.h>
#include <XivAlexanderCommon/Sqex_Excel_Reader.h>
#include <XivAlexanderCommon/Sqex_FontCsv_Creator.h>
#include <XivAlexanderCommon/Sqex_Sound_Reader.h>
#include <XivAlexanderCommon/Sqex_Sound_Writer.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryProvider.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_ThirdParty_TexTools.h>
#include <XivAlexanderCommon/Utils_Win32_Process.h>
#include <XivAlexanderCommon/Utils_Win32_TaskDialogBuilder.h>
#include <XivAlexanderCommon/Utils_Win32_ThreadPool.h>

#include "App_ConfigRepository.h"
#include "App_Misc_ExcelTransformConfig.h"
#include "App_Misc_GameInstallationDetector.h"
#include "App_Misc_Logger.h"
#include "App_Window_ProgressPopupWindow.h"
#include "App_XivAlexApp.h"
#include "DllMain.h"
#include "resource.h"

struct App::Misc::VirtualSqPacks::Implementation {
	VirtualSqPacks& Sqpacks;
	const std::shared_ptr<Config> Config;
	const std::shared_ptr<Logger> Logger;
	const std::filesystem::path SqpackPath;

	static constexpr int PathTypeIndex = -1;
	static constexpr int PathTypeIndex2 = -2;
	static constexpr int PathTypeInvalid = -3;

	const Misc::GameInstallationDetector::GameReleaseInfo GameReleaseInfo;

	std::map<std::filesystem::path, Sqex::Sqpack::Creator::SqpackViews> SqpackViews;
	std::map<HANDLE, std::unique_ptr<OverlayedHandleData>> OverlayedHandles;

	uint64_t LastIoRequestTimestamp = 0;
	Utils::Win32::Event IoEvent = Utils::Win32::Event::Create();
	Utils::Win32::Event IoLockEvent = Utils::Win32::Event::Create(nullptr, TRUE, TRUE);

	std::vector<TtmpSet> TtmpSets;

	std::shared_ptr<const Sqex::RandomAccessStream> EmptyScd;

	Utils::CallOnDestruction::Multiple Cleanup;

	Implementation(VirtualSqPacks* sqpacks, std::filesystem::path sqpackPath)
		: Sqpacks(*sqpacks)
		, Config(Config::Acquire())
		, Logger(Logger::Acquire())
		, SqpackPath(std::move(sqpackPath))
		, GameReleaseInfo(Misc::GameInstallationDetector::GetGameReleaseInfo()) {

		const auto actCtx = Dll::ActivationContext().With();
		Window::ProgressPopupWindow progressWindow(Dll::FindGameMainWindow(false));
		progressWindow.Show();
		InitializeSqPacks(progressWindow);
		ReflectUsedEntries(true);

		Cleanup += Config->Runtime.MuteVoice_Battle.OnChangeListener([this](auto&) { ReflectUsedEntries(); });
		Cleanup += Config->Runtime.MuteVoice_Cm.OnChangeListener([this](auto&) { ReflectUsedEntries(); });
		Cleanup += Config->Runtime.MuteVoice_Emote.OnChangeListener([this](auto&) { ReflectUsedEntries(); });
		Cleanup += Config->Runtime.MuteVoice_Line.OnChangeListener([this](auto&) { ReflectUsedEntries(); });
	}

	~Implementation() {
		Cleanup.Clear();
	}

	void ReflectUsedEntries(bool isCalledFromConstructor = false) {
		const auto pApp = XivAlexApp::GetCurrentApp();
		const auto mainThreadStallEvent = Utils::Win32::Event::Create();
		const auto mainThreadStalledEvent = Utils::Win32::Event::Create();
		const auto resumeMainThread = Utils::CallOnDestruction([&mainThreadStallEvent]() { mainThreadStallEvent.Set(); });

		// Step. Suspend game main thread, by sending run on UI thread message
		const auto staller = Utils::Win32::Thread(L"Staller", [isCalledFromConstructor, pApp, mainThreadStalledEvent, mainThreadStallEvent]() {
			if (!isCalledFromConstructor) {
				pApp->RunOnGameLoop([mainThreadStalledEvent, mainThreadStallEvent]() {
					mainThreadStalledEvent.Set();
					mainThreadStallEvent.Wait();
				});
			}
		});
		if (!isCalledFromConstructor)
			mainThreadStalledEvent.Wait();

		// Step. Wait until ReadFile stops
		if (!isCalledFromConstructor) {
			while (true) {
				const auto waitFor = static_cast<int64_t>(100LL + LastIoRequestTimestamp - GetTickCount64());
				if (waitFor < 0)
					break;
				IoEvent.Reset();
				if (WAIT_TIMEOUT == IoEvent.Wait(static_cast<DWORD>(waitFor)))
					break;
			}
			IoLockEvent.Reset();
			const auto resumeIo = Utils::CallOnDestruction([this]() { IoLockEvent.Set(); });
		}

		std::map<Sqex::Sqpack::EntryPathSpec, std::tuple<Sqex::Sqpack::HotSwappableEntryProvider*, std::shared_ptr<Sqex::Sqpack::EntryProvider>, std::string>, Sqex::Sqpack::EntryPathSpec::AllHashComparator> replacements;

		// Step. Find voices to enable or disable
		const auto voBattle = Sqex::Sqpack::SqexHash("sound/voice/vo_battle", SIZE_MAX);
		const auto voCm = Sqex::Sqpack::SqexHash("sound/voice/vo_cm", SIZE_MAX);
		const auto voEmote = Sqex::Sqpack::SqexHash("sound/voice/vo_emote", SIZE_MAX);
		const auto voLine = Sqex::Sqpack::SqexHash("sound/voice/vo_line", SIZE_MAX);
		for (const auto& entry : SqpackViews.at(SqpackPath / L"ffxiv/070000.win32.index").Entries) {
			const auto provider = dynamic_cast<Sqex::Sqpack::HotSwappableEntryProvider*>(entry->Provider.get());
			if (!provider)
				continue;

			const auto& pathSpec = provider->PathSpec();
			if (pathSpec.PathHash == voBattle || pathSpec.PathHash == voCm || pathSpec.PathHash == voEmote || pathSpec.PathHash == voLine)
				replacements.insert_or_assign(pathSpec, std::make_tuple(provider, std::shared_ptr<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(), std::string()));

			if (pathSpec.PathHash == voBattle && Config->Runtime.MuteVoice_Battle)
				std::get<1>(replacements.at(pathSpec)) = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(pathSpec, EmptyScd);
			if (pathSpec.PathHash == voCm && Config->Runtime.MuteVoice_Cm)
				std::get<1>(replacements.at(pathSpec)) = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(pathSpec, EmptyScd);
			if (pathSpec.PathHash == voEmote && Config->Runtime.MuteVoice_Emote)
				std::get<1>(replacements.at(pathSpec)) = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(pathSpec, EmptyScd);
			if (pathSpec.PathHash == voLine && Config->Runtime.MuteVoice_Line)
				std::get<1>(replacements.at(pathSpec)) = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(pathSpec, EmptyScd);
		}

		// Step. Find placeholders to adjust
		for (const auto& ttmp : TtmpSets) {
			for (const auto& entry : ttmp.List.SimpleModsList) {
				const auto v = SqpackPath / std::format(L"{}.win32.index", entry.ToExpacDatPath());
				const auto it = SqpackViews.find(v);
				if (it == SqpackViews.end()) {
					Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "Failed to find {} as a sqpack file", v.c_str());
					continue;
				}

				const auto pathSpec = Sqex::Sqpack::EntryPathSpec(entry.FullPath);
				auto entryIt = it->second.HashOnlyEntries.find(pathSpec);
				if (entryIt == it->second.HashOnlyEntries.end()) {
					entryIt = it->second.FullPathEntries.find(pathSpec);
					if (entryIt == it->second.FullPathEntries.end()) {
						continue;
					}
				}

				const auto provider = dynamic_cast<Sqex::Sqpack::HotSwappableEntryProvider*>(entryIt->second->Provider.get());
				if (!provider)
					continue;

				provider->UpdatePathSpec(entry.FullPath);
				replacements.insert_or_assign(entry.FullPath, std::make_tuple(provider, std::shared_ptr<Sqex::Sqpack::EntryProvider>(), std::string()));
			}

			for (const auto& modPackPage : ttmp.List.ModPackPages) {
				for (const auto& modGroup : modPackPage.ModGroups) {
					for (const auto& option : modGroup.OptionList) {
						for (const auto& entry : option.ModsJsons) {
							const auto v = SqpackPath / std::format(L"{}.win32.index", entry.ToExpacDatPath());
							const auto it = SqpackViews.find(v);
							if (it == SqpackViews.end()) {
								Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "Failed to find {} as a sqpack file", v.c_str());
								continue;
							}

							const auto pathSpec = Sqex::Sqpack::EntryPathSpec(entry.FullPath);
							auto entryIt = it->second.HashOnlyEntries.find(pathSpec);
							if (entryIt == it->second.HashOnlyEntries.end()) {
								entryIt = it->second.FullPathEntries.find(pathSpec);
								if (entryIt == it->second.FullPathEntries.end()) {
									continue;
								}
							}

							const auto provider = dynamic_cast<Sqex::Sqpack::HotSwappableEntryProvider*>(entryIt->second->Provider.get());
							if (!provider)
								continue;

							provider->UpdatePathSpec(entry.FullPath);
							replacements.insert_or_assign(entry.FullPath, std::make_tuple(provider, std::shared_ptr<Sqex::Sqpack::EntryProvider>(), std::string()));
						}
					}
				}
			}
		}

		// Step. Unregister TTMP files that no longer exist and delete associated files
		for (auto it = TtmpSets.begin(); it != TtmpSets.end();) {
			if (!exists(it->ListPath)) {
				it->DataFile.Clear();
				for (const auto& path : {
						it->ListPath,
						it->ListPath.parent_path() / "TTMPD.mpd",
						it->ListPath.parent_path() / "choices.json",
						it->ListPath.parent_path() / "disable",
						it->ListPath.parent_path(),
					}) {
					try {
						remove(path);
					} catch (...) {
						// pass
					}
				}
				it = TtmpSets.erase(it);
			} else if (!it->RenameTo.empty()) {
				try {
					create_directories(it->RenameTo);

					const auto renameToDirHandle = Utils::Win32::Handle::FromCreateFile(it->RenameTo, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0);

					const auto newListPath = (it->RenameTo / L"TTMPL.mpl").wstring();
					std::vector<char> renameInfoBuffer;
					renameInfoBuffer.resize(sizeof FILE_RENAME_INFO + newListPath.size());
					auto renameInfo = *reinterpret_cast<FILE_RENAME_INFO*>(&renameInfoBuffer[0]);
					renameInfo.ReplaceIfExists = true;
					renameInfo.RootDirectory = renameToDirHandle;
					renameInfo.FileNameLength = static_cast<DWORD>(newListPath.size());
					wcsncpy_s(renameInfo.FileName, renameInfo.FileNameLength, newListPath.data(), newListPath.size());
					SetFileInformationByHandle(it->DataFile, FileRenameInfo, &renameInfoBuffer[0], static_cast<DWORD>(renameInfoBuffer.size()));

					for (const auto& path : {
							"TTMPD.mpd",
							"choices.json",
							"disable",
						}) {
						const auto oldPath = it->ListPath.parent_path() / path;
						if (exists(oldPath))
							std::filesystem::rename(oldPath, it->RenameTo / path);
					}
					try {
						remove(it->ListPath.parent_path());
					} catch (...) {
						// pass
					}
					it->ListPath = newListPath;
					it->RenameTo.clear();
				} catch (const std::exception& e) {
					Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
						"Failed to move {} to {}: {}",
						it->ListPath.wstring(), it->RenameTo.wstring(), e.what());
				}
				++it;
			} else
				++it;
		}

		// Step. Set new replacements
		for (const auto& ttmp : TtmpSets) {
			if (!ttmp.Enabled || !ttmp.Allocated)
				continue;

			const auto& choices = ttmp.Choices;

			for (size_t i = 0; i < ttmp.List.SimpleModsList.size(); ++i) {
				const auto& entry = ttmp.List.SimpleModsList[i];

				const auto entryIt = replacements.find(entry.FullPath);
				if (entryIt == replacements.end())
					continue;

				std::get<1>(entryIt->second) = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(
					entry.FullPath,
					std::make_shared<Sqex::FileRandomAccessStream>(Utils::Win32::Handle{ttmp.DataFile, false}, entry.ModOffset, entry.ModSize)
				);
				std::get<2>(entryIt->second) = ttmp.List.Name;
			}

			for (size_t pageObjectIndex = 0; pageObjectIndex < ttmp.List.ModPackPages.size(); ++pageObjectIndex) {
				const auto& modGroups = ttmp.List.ModPackPages[pageObjectIndex].ModGroups;
				if (modGroups.empty())
					continue;
				const auto pageConf = choices.at(pageObjectIndex);

				for (size_t modGroupIndex = 0; modGroupIndex < modGroups.size(); ++modGroupIndex) {
					const auto& modGroup = modGroups[modGroupIndex];
					if (modGroups.empty())
						continue;

					std::set<size_t> indices;
					if (pageConf.at(modGroupIndex).is_array()) {
						const auto tmp = pageConf.at(modGroupIndex).get<std::vector<size_t>>();
						indices.insert(tmp.begin(), tmp.end());
					} else
						indices.insert(pageConf.at(modGroupIndex).get<size_t>());
					
					for (const auto optionIndex : indices) {
						const auto& option = modGroup.OptionList[optionIndex];

						for (const auto& entry : option.ModsJsons) {
							const auto entryIt = replacements.find(entry.FullPath);
							if (entryIt == replacements.end())
								continue;

							std::get<1>(entryIt->second) = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(
								entry.FullPath,
								std::make_shared<Sqex::FileRandomAccessStream>(Utils::Win32::Handle{ ttmp.DataFile, false }, entry.ModOffset, entry.ModSize)
								);
							std::get<2>(entryIt->second) = std::format("{} ({} > {})", ttmp.List.Name, modGroup.GroupName, option.Name);
						}
					}
				}
			}
		}

		// Step. Apply replacements
		for (const auto& pathSpec : replacements | std::views::keys) {
			auto& [place, newEntry, description] = replacements.at(pathSpec);
			if (!description.empty()) {
				if (newEntry)
					Logger->Format(LogCategory::VirtualSqPacks, "{}: {}", description, pathSpec);
				else
					Logger->Format(LogCategory::VirtualSqPacks, "Reset: {}", pathSpec);
			}
			place->SwapStream(std::move(newEntry));
		}

		if (!isCalledFromConstructor)
			Sqpacks.OnTtmpSetsChanged();
	}

	void InitializeSqPacks(Window::ProgressPopupWindow& progressWindow) {
		progressWindow.UpdateMessage(Utils::ToUtf8(Config->Runtime.GetStringRes(IDS_TITLE_DISCOVERINGFILES)));

		std::map<std::filesystem::path, std::unique_ptr<Sqex::Sqpack::Creator>> creators;
		for (const auto& expac : std::filesystem::directory_iterator(SqpackPath)) {
			if (!expac.is_directory())
				continue;

			for (const auto& sqpack : std::filesystem::directory_iterator(expac)) {
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					throw std::runtime_error("Cancelled");

				auto ext = sqpack.path().extension().wstring();
				CharLowerW(&ext[0]);
				if (ext != L".index")
					continue;

				creators.emplace(sqpack, std::make_unique<Sqex::Sqpack::Creator>(
					Utils::ToUtf8(expac.path().filename().wstring()),
					Utils::ToUtf8(sqpack.path().filename().replace_extension().replace_extension().wstring())
				));
			}
		}

		{
			std::vector<std::filesystem::path> ttmpls;
			std::vector<std::filesystem::path> dirs;
			dirs.emplace_back(SqpackPath / "TexToolsMods");
			dirs.emplace_back(Config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods");

			for (const auto& dir : Config->Runtime.AdditionalTexToolsModPackSearchDirectories.Value()) {
				if (!dir.empty())
					dirs.emplace_back(Config::TranslatePath(dir));
			}

			for (const auto& dir : dirs) {
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					throw std::runtime_error("Cancelled");
				if (dir.empty() || !is_directory(dir))
					continue;

				try {
					for (const auto& iter : std::filesystem::recursive_directory_iterator(dir)) {
						if (iter.path().filename() != "TTMPL.mpl")
							continue;
						ttmpls.emplace_back(iter);
					}
				} catch (const std::exception& e) {
					Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
						"Failed to list items in {}: {}",
						dir, e.what());
				}
			}

			std::ranges::sort(ttmpls);
			for (const auto& ttmpl : ttmpls) {
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					throw std::runtime_error("Cancelled");

				try {
					TtmpSets.emplace_back(TtmpSet{
						.Impl = this,
						.Allocated = true,
						.Enabled = !exists(ttmpl.parent_path() / "disable"),
						.ListPath = ttmpl,
						.List = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream{Utils::Win32::Handle::FromCreateFile(ttmpl, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0)}),
						.DataFile = Utils::Win32::Handle::FromCreateFile(ttmpl.parent_path() / "TTMPD.mpd", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0)
					});
				} catch (const std::exception& e) {
					Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
						"Failed to load TexTools ModPack from {}: {}", ttmpl.wstring(), e.what());
					continue;
				}
				if (const auto choicesPath = ttmpl.parent_path() / "choices.json"; exists(choicesPath)) {
					try {
						TtmpSets.back().Choices = Utils::ParseJsonFromFile(choicesPath);
						Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
							"Choices file loaded from {}", choicesPath.wstring());
					} catch (const std::exception& e) {
						Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
							"Failed to load choices from {}: {}", choicesPath.wstring(), e.what());
					}
				}
				TtmpSets.back().FixChoices();
			}
		}

		if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
			throw std::runtime_error("Cancelled");

		{
			std::mutex groupedLogPrintLock;
			const auto progressMax = creators.size() * (0
				+ 1 // original sqpack
				+ Config->Runtime.AdditionalSqpackRootDirectories.Value().size() // external sqpack
				+ TtmpSets.size() // TTMP
				+ 1 // replacement file entry
			);
			std::atomic_size_t progressValue = 0;
			std::atomic_size_t fileIndex = 0;

			Utils::Win32::TpEnvironment pool;
			const std::filesystem::path* pLastStartedIndexFile = &creators.begin()->first;
			const auto loaderThread = Utils::Win32::Thread(L"VirtualSqPack Constructor", [&]() {
				for (const auto& indexFile : creators | std::views::keys) {
					pool.SubmitWork([&, &creator = *creators.at(indexFile)]() {
						try {
							if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
								return;

							progressValue += 1;
							fileIndex += 1;
							pLastStartedIndexFile = &indexFile;
							if (const auto result = creator.AddEntriesFromSqPack(indexFile, true, true); result.AnyItem()) {
								const auto lock = std::lock_guard(groupedLogPrintLock);
								Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
									"[{}/{}] Source: added {}, replaced {}, ignored {}, error {}",
									creator.DatExpac, creator.DatName,
									result.Added.size(), result.Replaced.size(), result.SkippedExisting.size(), result.Error.size());
								for (const auto& error : result.Error) {
									Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
										"\t=> Error processing {}: {}", error.first, error.second);
								}
							}

							if (creator.DatExpac == "ffxiv" && creator.DatName == "070000") {
								const auto reader = Sqex::Sound::ScdReader(creator["sound/system/sample_system.scd"]);
								Sqex::Sound::ScdWriter writer;
								writer.SetTable1(reader.ReadTable1Entries());
								writer.SetTable4(reader.ReadTable4Entries());
								writer.SetTable2(reader.ReadTable2Entries());
								for (size_t i = 0; i < 256; ++i) {
									writer.SetSoundEntry(i, Sqex::Sound::ScdWriter::SoundEntry::EmptyEntry());
								}
								EmptyScd = std::make_shared<Sqex::MemoryRandomAccessStream>(
									Sqex::Sqpack::MemoryBinaryEntryProvider("dummy/dummy", std::make_shared<Sqex::MemoryRandomAccessStream>(writer.Export()))
									.ReadStreamIntoVector<uint8_t>(0));
							}

							if (creator.DatExpac != "ffxiv" || creator.DatName != "0a0000") {
								for (const auto& additionalSqpackRootDirectory : Config->Runtime.AdditionalSqpackRootDirectories.Value()) {
									if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
										return;

									progressValue += 1;

									const auto file = additionalSqpackRootDirectory / "sqpack" / indexFile.parent_path().filename() / indexFile.filename();
									if (!exists(file))
										continue;

									if (const auto result = creator.AddEntriesFromSqPack(file, false, false); result.AnyItem()) {
										const auto lock = std::lock_guard(groupedLogPrintLock);
										Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
											"[{}/{}] {}: added {}, replaced {}, ignored {}, error {}",
											creator.DatExpac, creator.DatName, file,
											result.Added.size(), result.Replaced.size(), result.SkippedExisting.size(), result.Error.size());
										for (const auto& error : result.Error) {
											Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
												"\t=> Error processing {}: {}", error.first, error.second);
										}
									}
								}
							} else
								progressValue += Config->Runtime.AdditionalSqpackRootDirectories.Value().size();

							for (const auto& ttmp : TtmpSets) {
								if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
									return;

								progressValue += 1;

								creator.ReserveSpacesFromTTMP(ttmp.List);
							}

							SetUpVirtualFileFromFileEntries(creator, indexFile);
							progressValue += 1;
						} catch (const std::exception& e) {
							pool.Cancel();
							Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
								"[{}/{}] Error: {}", creator.DatExpac, creator.DatName, e.what());
						}
					});
				}
				pool.WaitOutstanding();
			});
			
			do {
				progressWindow.UpdateMessage(Config->Runtime.FormatStringRes(IDS_TITLE_INDEXINGFILES, *pLastStartedIndexFile, static_cast<size_t>(fileIndex), creators.size()));
				progressWindow.UpdateProgress(progressValue, progressMax);
			} while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, { loaderThread }));
			pool.Cancel();
			loaderThread.Wait();
		}

		if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
			throw std::runtime_error("Cancelled");

		for (const auto& [indexFile, pCreator] : creators) {
			if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
				throw std::runtime_error("Cancelled");

			if (pCreator->DatExpac == "ffxiv" && pCreator->DatName == "000000")
				SetUpGeneratedFonts(progressWindow, *pCreator, indexFile);
			else if (pCreator->DatExpac == "ffxiv" && pCreator->DatName == "070000") {
				for (const auto& pathSpec : pCreator->AllPathSpec())
					pCreator->ReserveSwappableSpace(pathSpec, static_cast<uint32_t>(EmptyScd->StreamSize()));
			} else if (pCreator->DatExpac == "ffxiv" && pCreator->DatName == "0a0000")
				SetUpMergedExd(progressWindow, *pCreator, indexFile);
		}

		{
			const auto progressMax = creators.size();
			size_t progressValue = 0;
			const auto workerThread = Utils::Win32::Thread(L"VirtualSqPacks Element Finalizer", [&]() {
				Utils::Win32::TpEnvironment pool;
				std::mutex resLock;
				for (const auto& [indexFile, pCreator] : creators) {
					if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
						throw std::runtime_error("Cancelled");

					pool.SubmitWork([&]() {
						auto v = pCreator->AsViews(false);

						const auto lock = std::lock_guard(resLock);
						SqpackViews.emplace(indexFile, std::move(v));
						progressValue += 1;
					});
				}
				pool.WaitOutstanding();
			});
			while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, { workerThread })) {
				progressWindow.UpdateMessage(Utils::ToUtf8(Config->Runtime.GetStringRes(IDS_TITLE_FINALIZING)));
				progressWindow.UpdateProgress(progressValue, progressMax);
			}
		}
	}

	void SetUpMergedExd(Window::ProgressPopupWindow& progressWindow, Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexFile) {
		while (true) {
			try {
				const auto additionalGameRootDirectories{ Config->Runtime.AdditionalSqpackRootDirectories.Value() };
				const auto excelTransformConfigFiles{ Config->Runtime.ExcelTransformConfigFiles.Value() };
				if (additionalGameRootDirectories.empty() && excelTransformConfigFiles.empty())
					return;

				const auto fallbackLanguageList{ Config->Runtime.GetFallbackLanguageList() };
				const auto cachedDir = Config->Init.ResolveConfigStorageDirectoryPath() / "Cached" / GameReleaseInfo.CountryCode / creator.DatExpac / creator.DatName;

				std::map<std::string, int> exhTable;
				// maybe generate exl?

				for (const auto& pair : Sqex::Excel::ExlReader(*creator["exd/root.exl"]))
					exhTable.emplace(pair);

				std::string currentCacheKeys("VERSION:2\n");
				{
					const auto gameRoot = indexFile.parent_path().parent_path().parent_path();
					const auto versionFile = Utils::Win32::Handle::FromCreateFile(gameRoot / "ffxivgame.ver", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
					const auto versionContent = versionFile.Read<char>(0, static_cast<size_t>(versionFile.GetFileSize()));
					currentCacheKeys += std::format("SQPACK:{}:{}\n", canonical(gameRoot).wstring(), std::string(versionContent.begin(), versionContent.end()));
				}

				currentCacheKeys += "LANG";
				for (const auto& lang : fallbackLanguageList)
					currentCacheKeys += std::format(":{}", static_cast<int>(lang));
				currentCacheKeys += "\n";

				std::vector<std::unique_ptr<Sqex::Sqpack::Reader>> readers;
				for (const auto& additionalSqpackRootDirectory : additionalGameRootDirectories) {
					const auto file = additionalSqpackRootDirectory / "sqpack" / indexFile.parent_path().filename() / indexFile.filename();
					if (!exists(file))
						continue;

					const auto versionFile = Utils::Win32::Handle::FromCreateFile(additionalSqpackRootDirectory / "ffxivgame.ver", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
					const auto versionContent = versionFile.Read<char>(0, static_cast<size_t>(versionFile.GetFileSize()));
					currentCacheKeys += std::format("SQPACK:{}:{}\n", canonical(additionalSqpackRootDirectory).wstring(), std::string(versionContent.begin(), versionContent.end()));

					readers.emplace_back(std::make_unique<Sqex::Sqpack::Reader>(file));
				}

				for (const auto& configFile : excelTransformConfigFiles) {
					uint8_t hash[20]{};
					try {
						const auto file = Utils::Win32::Handle::FromCreateFile(configFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
						CryptoPP::SHA1 sha1;
						const auto content = file.Read<uint8_t>(0, static_cast<size_t>(file.GetFileSize()));
						sha1.Update(content.data(), content.size());
						sha1.Final(reinterpret_cast<byte*>(hash));
					} catch (...) {
					}

					CryptoPP::HexEncoder encoder;
					encoder.Put(hash, sizeof hash);
					encoder.MessageEnd();

					std::string buf(static_cast<size_t>(encoder.MaxRetrievable()), 0);
					encoder.Get(reinterpret_cast<byte*>(&buf[0]), buf.size());

					currentCacheKeys += std::format("CONF:{}:{}\n", configFile.wstring(), buf);
				}

				auto needRecreate = true;
				try {
					const auto file = Utils::Win32::Handle::FromCreateFile(cachedDir / "sources", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
					const auto content = file.Read<char>(0, static_cast<size_t>(file.GetFileSize()));
					needRecreate = !std::ranges::equal(content, currentCacheKeys);
				} catch (...) {
					// pass
				}

				if (needRecreate) {
					create_directories(cachedDir);

					std::vector<std::pair<srell::u8cregex, ExcelTransformConfig::PluralColumns>> pluralColumns;
					struct ReplacementRule {
						srell::u8cregex exhNamePattern;
						srell::u8cregex stringPattern;
						std::vector<Sqex::Language> sourceLanguage;
						std::string replaceTo;
						std::set<size_t> columnIndices;
						std::map<Sqex::Language, std::vector<std::string>> preprocessReplacements;
						std::vector<std::string> postprocessReplacements;
					};
					std::map<Sqex::Language, std::vector<ReplacementRule>> rowReplacementRules;
					std::map<Sqex::Language, std::set<ExcelTransformConfig::IgnoredCell>> ignoredCells;
					std::map<std::string, std::pair<srell::u8cregex, std::string>> columnReplacementTemplates;
					auto replacementFileParseFail = false;
					for (const auto& configFile : Config->Runtime.ExcelTransformConfigFiles.Value()) {
						if (configFile.empty())
							continue;

						try {
							ExcelTransformConfig::Config transformConfig;
							from_json(Utils::ParseJsonFromFile(Config->TranslatePath(configFile)), transformConfig);

							for (const auto& entry : transformConfig.pluralMap) {
								pluralColumns.emplace_back(srell::u8cregex(entry.first, srell::regex_constants::ECMAScript | srell::regex_constants::icase), entry.second);
							}
							for (const auto& entry : transformConfig.replacementTemplates) {
								columnReplacementTemplates.emplace(entry.first, std::make_pair(
									srell::u8cregex(entry.second.from, srell::regex_constants::ECMAScript | (entry.second.icase ? srell::regex_constants::icase : srell::regex_constants::syntax_option_type())),
									entry.second.to));
							}
							ignoredCells[transformConfig.targetLanguage].insert(transformConfig.ignoredCells.begin(), transformConfig.ignoredCells.end());
							for (const auto& rule : transformConfig.rules) {
								for (const auto& targetGroupName : rule.targetGroups) {
									for (const auto& target : transformConfig.targetGroups.at(targetGroupName).columnIndices) {
										rowReplacementRules[transformConfig.targetLanguage].emplace_back(ReplacementRule{
											srell::u8cregex(target.first, srell::regex_constants::ECMAScript | srell::regex_constants::icase),
											srell::u8cregex(rule.stringPattern, srell::regex_constants::ECMAScript | srell::regex_constants::icase),
											transformConfig.sourceLanguages,
											rule.replaceTo,
											{target.second.begin(), target.second.end()},
											rule.preprocessReplacements,
											rule.postprocessReplacements,
										});
									}
								}
							}
						} catch (const std::exception& e) {
							Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
								"Error occurred while parsing excel transformation configuration file {}: {}",
								configFile.wstring(), e.what());
							replacementFileParseFail = true;
						}
					}
					if (replacementFileParseFail) {
						Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
							"Skipping string table generation");
						return;
					}

					const auto actCtx = Dll::ActivationContext().With();
					Utils::Win32::TpEnvironment pool;
					progressWindow.UpdateMessage(Utils::ToUtf8(Config->Runtime.GetStringRes(IDS_TITLE_GENERATING_EXD_FILES)));

					static constexpr auto ProgressMaxPerTask = 1000;
					std::map<std::string, uint64_t> progressPerTask;
					for (const auto& exhName : exhTable | std::views::keys)
						progressPerTask.emplace(exhName, 0);
					progressWindow.UpdateProgress(0, 1ULL * exhTable.size() * ProgressMaxPerTask);
					{
						const auto ttmpl = Utils::Win32::Handle::FromCreateFile(cachedDir / "TTMPL.mpl.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
						const auto ttmpd = Utils::Win32::Handle::FromCreateFile(cachedDir / "TTMPD.mpd.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
						uint64_t ttmplPtr = 0, ttmpdPtr = 0;
						std::mutex writeMtx;

						std::string errorMessage;
						const auto compressThread = Utils::Win32::Thread(L"CompressThread", [&]() {
							for (const auto& exhName : exhTable | std::views::keys) {
								pool.SubmitWork([&]() {
									const char* lastStep = "Begin";
									try {
										if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
											return;

										lastStep = "Calculate maximum progress";
										size_t progressIndex = 0;
										uint64_t currentProgressMax = 0;
										uint64_t currentProgress = 0;
										auto& progressStoreTarget = progressPerTask.at(exhName);
										const auto publishProgress = [&]() {
											progressStoreTarget = (1ULL * progressIndex * ProgressMaxPerTask + (currentProgressMax ? currentProgress * ProgressMaxPerTask / currentProgressMax : 0)) / (readers.size() + 2ULL);
										};

										lastStep = "Load source EXH/D files";
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

											exCreator = std::make_unique<Sqex::Excel::Depth2ExhExdCreator>(exhName, *exhReaderSource.Columns, exhReaderSource.Header.SomeSortOfBufferSize);
											exCreator->FillMissingLanguageFrom = fallbackLanguageList;

											currentProgressMax = 1ULL * exhReaderSource.Languages.size() * exhReaderSource.Pages.size();
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
														throw std::runtime_error(std::format("Error occurred while processing {}: {}", exdPathSpec, e.what()));
													}
												}
											}
											currentProgress = 0;
											progressIndex++;
											publishProgress();
										}

										lastStep = "Load basic stuff";
										const auto sourceLanguages{ exCreator->Languages };
										const auto pluralColummIndices = [&]() {
											for (const auto& entry : pluralColumns) {
												if (srell::regex_search(exhName, entry.first))
													return entry.second;
											}
											return ExcelTransformConfig::PluralColumns();
										}();
										const auto exhRowReplacementRules = [&]() {
											std::map<Sqex::Language, std::vector<ReplacementRule>> res;
											for (const auto language : fallbackLanguageList)
												res.emplace(language, std::vector<ReplacementRule>{});

											for (auto& [language, rules] : rowReplacementRules) {
												auto& exhRules = res.at(language);
												for (auto& rule : rules)
													if (srell::regex_search(exhName, rule.exhNamePattern))
														exhRules.emplace_back(rule);
											}
											return res;
										}();

										lastStep = "Load external EXH/D files";
										for (const auto& reader : readers) {
											try {
												const auto exhReaderCurrent = Sqex::Excel::ExhReader(exhName, *(*reader)[exhPath]);

												currentProgressMax = 1ULL * exhReaderCurrent.Languages.size() * exhReaderCurrent.Pages.size();
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
																	const std::vector<Sqex::Excel::ExdColumn>* referenceRowPtr = nullptr;
																	for (const auto& l : exCreator->FillMissingLanguageFrom) {
																		if (auto it = rowSet.find(l);
																			it != rowSet.end()) {
																			referenceRowPtr = &it->second;
																			break;
																		}
																	}
																	if (!referenceRowPtr)
																		continue;
																	const auto& referenceRow = *referenceRowPtr;

																	// Exceptions for Chinese client are based on speculations.
																	if (((GameReleaseInfo.Region == Sqex::GameReleaseRegion::International || GameReleaseInfo.Region == Sqex::GameReleaseRegion::Korean) && language == Sqex::Language::ChineseSimplified) && exhName == "Fate") {
																		decltype(row) prevRow(std::move(row));
																		row = referenceRow;
																		for (size_t j = 0; j < 6; ++j)
																			row[0 + j].String = std::move(prevRow[30 + j].String);

																	} else if (GameReleaseInfo.Region == Sqex::GameReleaseRegion::Chinese && language != Sqex::Language::ChineseSimplified && exhName == "Fate") {
																		decltype(row) prevRow(std::move(row));
																		row = referenceRow;
																		for (size_t j = 0; j < 6; ++j)
																			row[30 + j].String = std::move(prevRow[0 + j].String);

																	} else {
																		if (row.size() < referenceRow.size()) {
																			row.reserve(referenceRow.size());
																			for (size_t j = row.size(); j < referenceRow.size(); ++j)
																				row.push_back(referenceRow[j]);
																		} else
																			row.resize(referenceRow.size());
																	}
																}
																exCreator->SetRow(i, language, std::move(row), false);
															}
														} catch (const std::out_of_range&) {
															// pass
														} catch (const std::exception& e) {
															Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
																"[{}] Skipping {} because of error: {}", exhName, exdPathSpec, e.what());
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

										lastStep = "Ensure that there are no missing rows from externally sourced exd files";
										for (auto& [id, rowSet] : exCreator->Data) {
											if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
												return;

											lastStep = "Find which language to use while filling current row if missing in other languages";
											const std::vector<Sqex::Excel::ExdColumn>* referenceRowPtr = nullptr;
											for (const auto& l : exCreator->FillMissingLanguageFrom) {
												if (auto it = rowSet.find(l);
													it != rowSet.end()) {
													referenceRowPtr = &it->second;
													break;
												}
											}
											if (!referenceRowPtr)
												continue;
											const auto& referenceRow = *referenceRowPtr;

											lastStep = "Fill missing rows for languages that aren't from source, and restore columns if unmodifiable";
											for (const auto& language : exCreator->Languages) {
												if (auto it = rowSet.find(language);
													it == rowSet.end())
													rowSet[language] = referenceRow;
												else {
													auto& row = it->second;
													for (size_t i = 0; i < row.size(); ++i) {
														if (referenceRow[i].Type != Sqex::Excel::Exh::String) {
															row[i] = referenceRow[i];
														}
													}
												}
											}

											lastStep = "Adjust language data per use config";
											std::map<Sqex::Language, std::vector<Sqex::Excel::ExdColumn>> pendingReplacements;
											for (const auto& language : exCreator->Languages) {
												const auto& rules = exhRowReplacementRules.at(language);
												if (rules.empty())
													continue;

												std::set<ExcelTransformConfig::IgnoredCell>* currentIgnoredCells = nullptr;
												if (const auto it = ignoredCells.find(language); it != ignoredCells.end())
													currentIgnoredCells = &it->second;

												auto row{rowSet.at(language)};

												for (size_t columnIndex = 0; columnIndex < row.size(); columnIndex++) {
													if (row[columnIndex].Type != Sqex::Excel::Exh::String)
														continue;

													if (currentIgnoredCells) {
														if (const auto it = currentIgnoredCells->find(ExcelTransformConfig::IgnoredCell{exhName, static_cast<int>(id), static_cast<int>(columnIndex)});
															it != currentIgnoredCells->end()) {
															if (const auto it2 = rowSet.find(it->forceLanguage);
																it2 != rowSet.end()) {
																Logger->Format(LogCategory::VirtualSqPacks, "Using \"{}\" in place of \"{}\" per rules, at {}({}, {})",
																	it2->second[columnIndex].String.Parsed(),
																	row[columnIndex].String.Parsed(),
																	exhName, id, columnIndex);
																row[columnIndex].String = it2->second[columnIndex].String;
																continue;
															}
														}
													}

													for (const auto& rule : rules) {
														if (!rule.columnIndices.contains(columnIndex))
															continue;

														if (!srell::regex_search(row[columnIndex].String.Escaped(), rule.stringPattern))
															continue;

														std::vector p = {std::format("{}:{}", exhName, id)};
														for (const auto ruleSourceLanguage : rule.sourceLanguage) {
															if (const auto it = rowSet.find(ruleSourceLanguage);
																it != rowSet.end()) {

																if (row[columnIndex].Type != Sqex::Excel::Exh::String)
																	throw std::invalid_argument(std::format("Column {} of sourceLanguage {} in {} is not a string column", columnIndex, static_cast<int>(ruleSourceLanguage), exhName));

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
																		if (pluralColummIndices.capitalizedColumnIndex != ExcelTransformConfig::PluralColumns::Index_NoColumn) {
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
																if (const auto rules = rule.preprocessReplacements.find(ruleSourceLanguage); rules != rule.preprocessReplacements.end()) {
																	Sqex::EscapedString escaped(it->second[readColumnIndex].String);
																	std::string replacing(escaped.Parsed());
																	for (const auto& ruleName : rules->second) {
																		const auto& [replaceFrom, replaceTo] = columnReplacementTemplates.at(ruleName);
																		replacing = srell::regex_replace(replacing, replaceFrom, replaceTo);
																	}
																	p.emplace_back(escaped.SetParsedCompatible(replacing).Escaped());
																} else
																	p.emplace_back(it->second[readColumnIndex].String.Escaped());
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
																	out = std::format(rule.replaceTo, p[0]);
																	break;
																case 2:
																	out = std::format(rule.replaceTo, p[0], p[1]);
																	break;
																case 3:
																	out = std::format(rule.replaceTo, p[0], p[1], p[2]);
																	break;
																case 4:
																	out = std::format(rule.replaceTo, p[0], p[1], p[2], p[3]);
																	break;
																case 5:
																	out = std::format(rule.replaceTo, p[0], p[1], p[2], p[3], p[4]);
																	break;
																case 6:
																	out = std::format(rule.replaceTo, p[0], p[1], p[2], p[3], p[4], p[5]);
																	break;
																case 7:
																	out = std::format(rule.replaceTo, p[0], p[1], p[2], p[3], p[4], p[5], p[6]);
																	break;
																default:
																	throw std::invalid_argument("Only up to 7 source languages are supported");
															}
														}

														Sqex::EscapedString escaped(out);
														if (!rule.postprocessReplacements.empty()) {
															std::string replacing(escaped.Parsed());
															for (const auto& ruleName : rule.postprocessReplacements) {
																const auto& [replaceFrom, replaceTo] = columnReplacementTemplates.at(ruleName);
																replacing = srell::regex_replace(replacing, replaceFrom, replaceTo);
															}
															escaped.SetParsedCompatible(replacing);
														}
														row[columnIndex].String = std::move(escaped);
														break;
													}
												}
												pendingReplacements.emplace(language, std::move(row));
											}
											for (auto& [language, row] : pendingReplacements)
												exCreator->SetRow(id, language, std::move(row));
										}

										{
											lastStep = "Compile";
											auto compiled = exCreator->Compile();

											lastStep = "Compress";
											currentProgressMax = compiled.size();
											for (auto& kv : compiled) {
												const auto& entryPathSpec = kv.first;
												auto& data = kv.second;
												currentProgress++;
												publishProgress();

#ifdef _DEBUG
												if (entryPathSpec.FullPath.extension() == L".exh") {
													auto data2 = creator[entryPathSpec]->ReadStreamIntoVector<char>(0);
													data2.resize(data2.size() - 2 * reinterpret_cast<Sqex::Excel::Exh::Header*>(&data2[0])->LanguageCount);
													reinterpret_cast<Sqex::Excel::Exh::Header*>(&data2[0])->LanguageCount = static_cast<uint16_t>(exCreator->Languages.size());
													for (const auto lang : exCreator->Languages) {
														data2.push_back(static_cast<char>(lang));
														data2.push_back(0);
													}
													const auto reader1 = Sqex::Excel::ExhReader(exhName, Sqex::MemoryRandomAccessStream(*reinterpret_cast<std::vector<uint8_t>*>(&data)));
													const auto reader2 = Sqex::Excel::ExhReader(exhName, Sqex::MemoryRandomAccessStream(*reinterpret_cast<std::vector<uint8_t>*>(&data2)));
													if (reader1.Header.FixedDataSize != reader2.Header.FixedDataSize)
														Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] FixedDataSize: new {}, was {}", exhName, reader1.Header.FixedDataSize.Value(), reader2.Header.FixedDataSize.Value());
													if (reader1.Header.ColumnCount != reader2.Header.ColumnCount)
														Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] ColumnCount: new {}, was {}", exhName, reader1.Header.ColumnCount.Value(), reader2.Header.ColumnCount.Value());
													if (reader1.Header.PageCount != reader2.Header.PageCount)
														Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] PageCount: new {}, was {}", exhName, reader1.Header.PageCount.Value(), reader2.Header.PageCount.Value());
													if (reader1.Header.SomeSortOfBufferSize != reader2.Header.SomeSortOfBufferSize)
														Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] SomeSortOfBufferSize: new {}, was {}", exhName, reader1.Header.SomeSortOfBufferSize.Value(), reader2.Header.SomeSortOfBufferSize.Value());
													if (reader1.Header.RowCountWithoutSkip != reader2.Header.RowCountWithoutSkip)
														Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] RowCountWithoutSkip: new {}, was {}", exhName, reader1.Header.RowCountWithoutSkip.Value(), reader2.Header.RowCountWithoutSkip.Value());
													if (reader1.Columns->size() != reader2.Columns->size())
														Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] Columns: new {}, was {}", exhName, reader1.Columns->size(), reader2.Columns->size());
													for (size_t i = 0, i_ = std::min(reader1.Columns->size(), reader2.Columns->size()); i < i_; ++i) {
														if ((*reader1.Columns)[i].Offset != (*reader2.Columns)[i].Offset)
															Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] Columns[{}].Offset: new {}, was {}", exhName, i, (*reader1.Columns)[i].Offset.Value(), (*reader2.Columns)[i].Offset.Value());
														if ((*reader1.Columns)[i].Type != (*reader2.Columns)[i].Type)
															Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] Columns[{}].Type: new {}, was {}", exhName, i, static_cast<int>((*reader1.Columns)[i].Type.Value()), static_cast<int>((*reader2.Columns)[i].Type.Value()));
													}
													if (reader1.Pages.size() != reader2.Pages.size())
														Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] Pages: new {}, was {}", exhName, reader1.Pages.size(), reader2.Pages.size());
													for (size_t i = 0, i_ = std::min(reader1.Pages.size(), reader2.Pages.size()); i < i_; ++i) {
														if (reader1.Pages[i].StartId != reader2.Pages[i].StartId)
															Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] Pages[{}].StartId: new {}, was {}", exhName, i, reader1.Pages[i].StartId.Value(), reader2.Pages[i].StartId.Value());
														if (reader1.Pages[i].RowCountWithSkip != reader2.Pages[i].RowCountWithSkip)
															Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] Pages[{}].RowCountWithSkip: new {}, was {}", exhName, i, reader1.Pages[i].RowCountWithSkip.Value(), reader2.Pages[i].RowCountWithSkip.Value());
													}
												}
#endif

												const auto targetPath = cachedDir / entryPathSpec.FullPath;

												const auto provider = Sqex::Sqpack::MemoryBinaryEntryProvider(entryPathSpec, std::make_shared<Sqex::MemoryRandomAccessStream>(std::move(*reinterpret_cast<std::vector<uint8_t>*>(&data))));
												const auto len = provider.StreamSize();
												const auto dv = provider.ReadStreamIntoVector<char>(0, static_cast<SSIZE_T>(len));

												if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
													return;

												lastStep = "Write to filesystem";
												const auto lock = std::lock_guard(writeMtx);
												const auto entryLine = std::format("{}\n", nlohmann::json::object({
													{"FullPath", Utils::StringReplaceAll<std::string>(Utils::ToUtf8(entryPathSpec.FullPath.wstring()), "\\", "/")},
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
										if (errorMessage.empty()) {
											const auto lock = std::lock_guard(writeMtx);
											if (errorMessage.empty()) {
												errorMessage = std::format("{}: {}: {}", exhName, lastStep, e.what());
												Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[{}] Error: {}/{}", exhName, lastStep, e.what());
												progressWindow.Cancel();
											}
										}
									}
								});
							}
							pool.WaitOutstanding();
						});
						while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, {compressThread})) {
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
						return;
					}

					try {
						std::filesystem::remove(cachedDir / "TTMPL.mpl");
					} catch (...) {
						// whatever
					}
					try {
						std::filesystem::remove(cachedDir / "TTMPD.mpd");
					} catch (...) {
						// whatever
					}
					std::filesystem::rename(cachedDir / "TTMPL.mpl.tmp", cachedDir / "TTMPL.mpl");
					std::filesystem::rename(cachedDir / "TTMPD.mpd.tmp", cachedDir / "TTMPD.mpd");
					Utils::Win32::Handle::FromCreateFile(cachedDir / "sources", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0)
						.Write(0, currentCacheKeys.data(), currentCacheKeys.size());
				}

				if (const auto result = creator.AddAllEntriesFromSimpleTTMP(cachedDir); result.AnyItem()) {
					Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
						"[ffxiv/0a0000] Generated string table: added {}, replaced {}, ignored {}, error {}",
						result.Added.size(), result.Replaced.size(), result.SkippedExisting.size(), result.Error.size());
					for (const auto& error : result.Error) {
						Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
							"\t=> Error processing {}: {}", error.first, error.second);
					}
				}
				return;

			} catch (const std::exception& e) {
				Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[ffxiv/0a0000] Error: {}", e.what());
				switch (Dll::MessageBoxF(progressWindow.Handle(), MB_ICONERROR | MB_ABORTRETRYIGNORE, IDS_ERROR_GENERATESTRINGTABLES, e.what())) {
					case IDRETRY:
						continue;
					case IDABORT:
						ExitProcess(-1);
					case IDIGNORE:
						return;
				}
			}
		}
	}

	void SetUpVirtualFileFromFileEntries(Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexPath) {
		std::vector<std::filesystem::path> rootDirs;
		rootDirs.emplace_back(indexPath.parent_path().parent_path());
		rootDirs.emplace_back(Config->Init.ResolveConfigStorageDirectoryPath() / "ReplacementFileEntries");

		for (const auto& dir : Config->Runtime.AdditionalGameResourceFileEntryRootDirectories.Value()) {
			if (!dir.empty())
				rootDirs.emplace_back(Config::TranslatePath(dir));
		}

		std::vector<std::pair<std::filesystem::path, std::filesystem::path>> dirs;
		for (const auto& dir : rootDirs) {
			dirs.emplace_back(dir / creator.DatExpac / creator.DatName, dir / creator.DatExpac / creator.DatName);
			dirs.emplace_back(dir / creator.DatExpac / std::format("{}.win32", creator.DatName), dir / creator.DatExpac / std::format("{}.win32", creator.DatName));
		}
		std::filesystem::path pathPrefix;
		if (const auto datType = indexPath.filename().wstring().substr(0, 2);
			lstrcmpiW(datType.c_str(), L"0c") == 0)
			pathPrefix = std::format("music/{}", creator.DatExpac);
		else if (datType == L"02")
			pathPrefix = std::format("bg/{}", creator.DatExpac);
		else if (datType == L"03")
			pathPrefix = std::format("cut/{}", creator.DatExpac);
		else if (datType == L"00" && creator.DatExpac == "ffxiv")
			pathPrefix = "common";
		else if (datType == L"01" && creator.DatExpac == "ffxiv")
			pathPrefix = "bgcommon";
		else if (datType == L"04" && creator.DatExpac == "ffxiv")
			pathPrefix = "chara";
		else if (datType == L"05" && creator.DatExpac == "ffxiv")
			pathPrefix = "shader";
		else if (datType == L"06" && creator.DatExpac == "ffxiv")
			pathPrefix = "ui";
		else if (datType == L"07" && creator.DatExpac == "ffxiv")
			pathPrefix = "sound";
		else if (datType == L"08" && creator.DatExpac == "ffxiv")
			pathPrefix = "vfx";
		else if (datType == L"0a" && creator.DatExpac == "ffxiv")
			pathPrefix = "exd";
		else if (datType == L"0b" && creator.DatExpac == "ffxiv")
			pathPrefix = "game_script";
		if (!pathPrefix.empty()) {
			for (const auto& dir : rootDirs)
				dirs.emplace_back(dir / pathPrefix, dir);
		}
		for (const auto& [dir, relativeTo] : dirs) {
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
				Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"[{}/{}] Failed to list items in {}: {}",
					creator.DatName, creator.DatExpac,
					dir, e.what());
				continue;
			}

			std::ranges::sort(files);
			for (const auto& file : files) {
				if (is_directory(file))
					continue;

				try {
					const auto result = creator.AddEntryFromFile(relative(file, relativeTo), file);
					if (const auto item = result.AnyItem())
						Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
							"[{}/{}] {} file {}: (nameHash={:08x}, pathHash={:08x}, fullPathHash={:08x})",
							creator.DatName, creator.DatExpac,
							result.Added.empty() ? "Replaced" : "Added",
							item->PathSpec().FullPath,
							item->PathSpec().NameHash,
							item->PathSpec().PathHash,
							item->PathSpec().FullPathHash);
					else
						for (const auto& error : result.Error | std::views::values)
							throw std::runtime_error(error);
				} catch (const std::exception& e) {
					Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
						"[{}/{}] Error processing {}: {}",
						creator.DatName, creator.DatExpac,
						file, e.what());
				}
			}
		}
	}

	void SetUpGeneratedFonts(Window::ProgressPopupWindow& progressWindow, Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexPath) {
		while (true) {
			const auto fontConfigPath{Config->Runtime.OverrideFontConfig.Value()};
			if (fontConfigPath.empty())
				return;
		
			try {
				const auto cachedDir = Config->Init.ResolveConfigStorageDirectoryPath() / "Cached" / GameReleaseInfo.CountryCode / creator.DatExpac / creator.DatName;

				std::string currentCacheKeys;
				{
					const auto gameRoot = indexPath.parent_path().parent_path().parent_path();
					const auto versionFile = Utils::Win32::Handle::FromCreateFile(gameRoot / "ffxivgame.ver", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
					const auto versionContent = versionFile.Read<char>(0, static_cast<size_t>(versionFile.GetFileSize()));
					currentCacheKeys += std::format("SQPACK:{}:{}\n", canonical(gameRoot).wstring(), std::string(versionContent.begin(), versionContent.end()));
				}

				if (const auto& configFile = Config->Runtime.OverrideFontConfig.Value(); !configFile.empty()) {
					uint8_t hash[20]{};
					try {
						const auto file = Utils::Win32::Handle::FromCreateFile(configFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
						CryptoPP::SHA1 sha1;
						const auto content = file.Read<uint8_t>(0, static_cast<size_t>(file.GetFileSize()));
						sha1.Update(content.data(), content.size());
						sha1.Final(reinterpret_cast<byte*>(hash));
					} catch (...) {
					}

					CryptoPP::HexEncoder encoder;
					encoder.Put(hash, sizeof hash);
					encoder.MessageEnd();

					std::string buf(static_cast<size_t>(encoder.MaxRetrievable()), 0);
					encoder.Get(reinterpret_cast<byte*>(&buf[0]), buf.size());

					currentCacheKeys += std::format("CONF:{}:{}\n", configFile, buf);
				}

				auto needRecreate = true;
				try {
					const auto file = Utils::Win32::Handle::FromCreateFile(cachedDir / "sources", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
					const auto content = file.Read<char>(0, static_cast<size_t>(file.GetFileSize()));
					needRecreate = !std::ranges::equal(content, currentCacheKeys);
				} catch (...) {
					// pass
				}

				if (needRecreate) {
					create_directories(cachedDir);

					progressWindow.UpdateMessage(Utils::ToUtf8(Config->Runtime.GetStringRes(IDS_TITLE_GENERATING_FONTS)));

					Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
						"=> Generating font per file: {}",
						fontConfigPath.wstring());

					auto cfg = Utils::ParseJsonFromFile(fontConfigPath).get<Sqex::FontCsv::CreateConfig::FontCreateConfig>();
					cfg.ValidateOrThrow();

					Sqex::FontCsv::FontSetsCreator fontCreator(cfg, Utils::Win32::Process::Current().PathOf().parent_path());
					for (const auto& additionalSqpackRootDirectory : Config->Runtime.AdditionalSqpackRootDirectories.Value()) {
						try {
							const auto info = Misc::GameInstallationDetector::GetGameReleaseInfo(Config::TranslatePath(additionalSqpackRootDirectory));
							fontCreator.ProvideGameDirectory(info.Region, info.RootPath);
						} catch (const std::exception& e) {
							Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
								"\t=> Skipping {} in additional game root directories while discovering game installations for font generation: {}",
								additionalSqpackRootDirectory.wstring(), e.what());
						}
					}
					for (const auto& info : Misc::GameInstallationDetector::FindInstallations())
						fontCreator.ProvideGameDirectory(info.Region, info.RootPath);
					SetupGeneratedFonts_VerifyRequirements(fontCreator, progressWindow);
					fontCreator.Start();

					while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, {fontCreator.GetWaitableObject()})) {
						const auto progress = fontCreator.GetProgress();
						progressWindow.UpdateProgress(progress.Progress, progress.Max);

						if (progress.Indeterminate)
							progressWindow.UpdateMessage(std::format("{} (+{})", Config->Runtime.GetStringRes(IDS_TITLE_GENERATING_FONTS), progress.Indeterminate));
						else
							progressWindow.UpdateMessage(Utils::ToUtf8(Config->Runtime.GetStringRes(IDS_TITLE_GENERATING_FONTS)));
					}
					if (progressWindow.GetCancelEvent().Wait(0) != WAIT_OBJECT_0) {
						progressWindow.UpdateMessage(Utils::ToUtf8(Config->Runtime.GetStringRes(IDS_TITLE_COMPRESSING)));
						Utils::Win32::TpEnvironment pool;
						const auto streams = fontCreator.GetResult().GetAllStreams();

						std::atomic_int64_t progress = 0;
						uint64_t maxProgress = 0;
						for (auto& stream : streams | std::views::values)
							maxProgress += stream->StreamSize() * 2;
						progressWindow.UpdateProgress(progress, maxProgress);

						const auto ttmpl = Utils::Win32::Handle::FromCreateFile(cachedDir / "TTMPL.mpl.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
						const auto ttmpd = Utils::Win32::Handle::FromCreateFile(cachedDir / "TTMPD.mpd.tmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
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
									auto extension = entryPathSpec.FullPath.extension().wstring();
									CharLowerW(&extension[0]);

									if (extension == L".tex")
										provider = std::make_shared<Sqex::Sqpack::MemoryTextureEntryProvider>(entryPathSpec, stream);
									else
										provider = std::make_shared<Sqex::Sqpack::MemoryBinaryEntryProvider>(entryPathSpec, stream);
									const auto len = provider->StreamSize();
									const auto dv = provider->ReadStreamIntoVector<char>(0, static_cast<SSIZE_T>(len));
									progress += stream->StreamSize();

									if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
										return;
									const auto lock = std::lock_guard(writeMtx);
									const auto entryLine = std::format("{}\n", nlohmann::json::object({
										{"FullPath", Utils::ToUtf8(entryPathSpec.FullPath.wstring())},
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

						while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, {compressThread})) {
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
						return;
					}

					try {
						std::filesystem::remove(cachedDir / "TTMPL.mpl");
					} catch (...) {
						// whatever
					}
					try {
						std::filesystem::remove(cachedDir / "TTMPD.mpd");
					} catch (...) {
						// whatever
					}
					std::filesystem::rename(cachedDir / "TTMPL.mpl.tmp", cachedDir / "TTMPL.mpl");
					std::filesystem::rename(cachedDir / "TTMPD.mpd.tmp", cachedDir / "TTMPD.mpd");
					Utils::Win32::Handle::FromCreateFile(cachedDir / "sources", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0)
						.Write(0, currentCacheKeys.data(), currentCacheKeys.size());
				}

				if (const auto result = creator.AddAllEntriesFromSimpleTTMP(cachedDir); result.AnyItem()) {
					Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
						"[ffxiv/000000] Generated font: added {}, replaced {}, ignored {}, error {}",
						result.Added.size(), result.Replaced.size(), result.SkippedExisting.size(), result.Error.size());
					for (const auto& error : result.Error) {
						Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
							"\t=> Error processing {}: {}", error.first, error.second);
					}
				}

				return;
			} catch (const Utils::Win32::CancelledError&) {
				Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks, "[ffxiv/000000] Font generation cancelled");
				return;

			} catch (const std::exception& e) {
				Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "[ffxiv/000000] Error: {}", e.what());
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					return;
				switch (Dll::MessageBoxF(progressWindow.Handle(), MB_ICONERROR | MB_ABORTRETRYIGNORE, IDS_ERROR_GENERATEFONT, e.what())) {
					case IDRETRY:
						continue;
					case IDABORT:
						ExitProcess(-1);
					case IDIGNORE:
						return;
				}
			}
		}
	}

	void SetupGeneratedFonts_VerifyRequirements(Sqex::FontCsv::FontSetsCreator& fontCreator, Window::ProgressPopupWindow& progressWindow) {
		fontCreator.VerifyRequirements(
			[this, &progressWindow](const Sqex::FontCsv::CreateConfig::GameIndexFile& gameIndexFile) -> std::filesystem::path {
				std::wstring prompt;
				if (gameIndexFile.fallbackPrompt.empty()) {
					prompt = Config->Runtime.GetStringRes(IDS_TITLE_SELECT_FFXIVEXECUTABLE);
					switch (gameIndexFile.autoDetectRegion) {
						case Sqex::GameReleaseRegion::International:
							prompt += std::format(L" ({})", Config->Runtime.GetStringRes(IDS_CLIENT_INTERNATIONAL));
							break;
						case Sqex::GameReleaseRegion::Chinese:
							prompt += std::format(L" ({})", Config->Runtime.GetStringRes(IDS_CLIENT_CHINESE));
							break;
						case Sqex::GameReleaseRegion::Korean:
							prompt += std::format(L" ({})", Config->Runtime.GetStringRes(IDS_CLIENT_KOREAN));
							break;
					}
				} else if (gameIndexFile.fallbackPrompt.size() == 1)
					prompt = Utils::FromUtf8(gameIndexFile.fallbackPrompt.front().second);
				else {
					auto found = false;
					for (const auto& [langId, localeName]: Config->Runtime.GetDisplayLanguagePriorities()) {
						for (const auto& [localeNameRegex, customPrompt] : gameIndexFile.fallbackPrompt) {
							if (srell::regex_search(localeName, srell::u8cregex(localeNameRegex, srell::u8cregex::icase))) {
								prompt = Utils::FromUtf8(customPrompt);
								found = true;
								break;
							}
						}
						if (found)
							break;
					}
					if (!found)
						prompt = Utils::FromUtf8(gameIndexFile.fallbackPrompt.front().second);
				}
						
				IFileOpenDialogPtr pDialog;
				DWORD dwFlags;
				static const COMDLG_FILTERSPEC fileTypes[] = {
					{FindStringResourceEx(Dll::Module(), IDS_FILTERSPEC_FFXIVEXECUTABLEFILES) + 1, L"ffxivboot.exe; ffxivboot64.exe; ffxiv_boot.exe; ffxiv_dx11.exe; ffxiv.exe"},
					{FindStringResourceEx(Dll::Module(), IDS_FILTERSPEC_EXECUTABLEFILES) + 1, L"*.exe"},
					{FindStringResourceEx(Dll::Module(), IDS_FILTERSPEC_ALLFILES) + 1, L"*"},
				};
				Utils::Win32::Error::ThrowIfFailed(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes));
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypeIndex(0));
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetDefaultExtension(L"exe"));
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetTitle(prompt.c_str()));
				Utils::Win32::Error::ThrowIfFailed(pDialog->GetOptions(&dwFlags));
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
				Utils::Win32::Error::ThrowIfFailed(pDialog->Show(progressWindow.Handle()), true);

				while (true) {
					std::filesystem::path fileName;
					{
						IShellItemPtr pResult;
						PWSTR pszFileName;
						Utils::Win32::Error::ThrowIfFailed(pDialog->GetResult(&pResult));
						Utils::Win32::Error::ThrowIfFailed(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
						if (!pszFileName)
							throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");
						fileName = pszFileName;
						CoTaskMemFree(pszFileName);
					}

					while (fileName != fileName.parent_path()) {
						fileName = fileName.parent_path();
						if (exists(fileName / "game" / "ffxivgame.ver"))
							return fileName;
					}
				}
			},
			[this, &progressWindow](const Sqex::FontCsv::CreateConfig::FontRequirement& requirement) -> bool {
				std::wstring instructions;
				if (requirement.installInstructions.empty())
					instructions = L"TODO"; // TODO
				else if (requirement.installInstructions.size() == 1)
					instructions = Utils::FromUtf8(requirement.installInstructions.front().second);
				else {
					auto found = false;
					for (const auto& [langId, localeName]: Config->Runtime.GetDisplayLanguagePriorities()) {
						for (const auto& [localeNameRegex, customPrompt] : requirement.installInstructions) {
							if (srell::regex_search(localeName, srell::u8cregex(localeNameRegex, srell::u8cregex::icase))) {
								instructions = Utils::FromUtf8(customPrompt);
								found = true;
								break;
							}
						}
						if (found)
							break;
					}
					if (!found)
						instructions = Utils::FromUtf8(requirement.installInstructions.front().second);
				}
						
				auto builder = Utils::Win32::TaskDialog::Builder();
				if (!requirement.homepage.empty()){
					builder.WithHyperlinkHandler(L"homepage", [homepage = Utils::FromUtf8(requirement.homepage)](auto& dialog) {
						try {
							Utils::Win32::ShellExecutePathOrThrow(homepage, dialog.GetHwnd());
						} catch (const std::exception& e) {
							Dll::MessageBoxF(dialog.GetHwnd(), MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
						}
						return Utils::Win32::TaskDialog::HyperlinkHandleResult::HandledKeepDialog;
					});
					builder.WithFooter(std::format(L"<a href=\"homepage\">{}</a>", requirement.homepage));
				}
				const auto res = builder
					.WithWindowTitle(Dll::GetGenericMessageBoxTitle())
					.WithParentWindow(progressWindow.Handle())
					.WithInstance(Dll::Module())
					.WithAllowDialogCancellation()
					.WithCanBeMinimized()
					.WithHyperlinkShellExecute()
					.WithMainIcon(IDI_TRAY_ICON)
					.WithMainInstruction(Config->Runtime.GetStringRes(IDS_GENERATEFONT_FONTINSTALLATIONREQUIRED))
					.WithContent(instructions)
					.WithButton({
						.Id = 1001,
						.Text = Config->Runtime.GetStringRes(IDS_GENERATEFONT_CHECKAGAIN),
					})
					.WithButtonDefault(1001)
					.WithCommonButton(TDCBF_CANCEL_BUTTON)
					.Build()
					.Show();
				if (res.Button == IDCANCEL)
					throw Utils::Win32::CancelledError();
				return true;
			}
		);
	}
};

App::Misc::VirtualSqPacks::VirtualSqPacks(std::filesystem::path sqpackPath)
	: m_pImpl(std::make_unique<Implementation>(this, std::move(sqpackPath))) {
}

App::Misc::VirtualSqPacks::~VirtualSqPacks() = default;

HANDLE App::Misc::VirtualSqPacks::Open(const std::filesystem::path& path) {
	try {
		const auto fileToOpen = absolute(path);
		const auto recreatedFilePath = m_pImpl->SqpackPath / fileToOpen.parent_path().filename() / fileToOpen.filename();
		const auto indexFile = std::filesystem::path(recreatedFilePath).replace_extension(L".index");
		const auto index2File = std::filesystem::path(recreatedFilePath).replace_extension(L".index2");
		if (!exists(indexFile) || !exists(index2File))
			return nullptr;

		int pathType = Implementation::PathTypeInvalid;

		if (fileToOpen == indexFile) {
			pathType = Implementation::PathTypeIndex;
		} else if (fileToOpen == index2File) {
			pathType = Implementation::PathTypeIndex2;
		} else {
			for (auto i = 0; i < 8; ++i) {
				const auto datFile = std::filesystem::path(recreatedFilePath).replace_extension(std::format(L".dat{}", i));
				if (fileToOpen == datFile) {
					pathType = i;
					break;
				}
			}
		}

		if (pathType == Implementation::PathTypeInvalid)
			return nullptr;

		auto overlayedHandle = std::make_unique<OverlayedHandleData>(Utils::Win32::Event::Create(), fileToOpen, LARGE_INTEGER{}, nullptr);

		for (const auto& view : m_pImpl->SqpackViews) {
			if (equivalent(view.first, indexFile)) {
				switch (pathType) {
					case Implementation::PathTypeIndex:
						overlayedHandle->Stream = view.second.Index1;
						break;

					case Implementation::PathTypeIndex2:
						overlayedHandle->Stream = view.second.Index2;
						break;

					default:
						if (pathType < 0 || static_cast<size_t>(pathType) >= view.second.Data.size())
							throw std::runtime_error("invalid #");
						overlayedHandle->Stream = view.second.Data[pathType];
				}
				break;
			}
		}

		m_pImpl->Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
			"Taking control of {}/{} (parent: {}/{}, type: {})",
			fileToOpen.parent_path().filename(), fileToOpen.filename(),
			indexFile.parent_path().filename(), indexFile.filename(),
			pathType);

		if (!overlayedHandle->Stream)
			return nullptr;

		const auto key = static_cast<HANDLE>(overlayedHandle->IdentifierHandle);
		m_pImpl->OverlayedHandles.insert_or_assign(key, std::move(overlayedHandle));
		return key;
	} catch (const Utils::Win32::Error& e) {
		m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, L"CreateFileW: {}, Message: {}", path.wstring(), e.what());
	} catch (const std::exception& e) {
		m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "CreateFileW: {}, Message: {}", path.wstring(), e.what());
	}
	return nullptr;
}

bool App::Misc::VirtualSqPacks::Close(HANDLE handle) {
	return m_pImpl->OverlayedHandles.erase(handle);
}

App::Misc::VirtualSqPacks::OverlayedHandleData* App::Misc::VirtualSqPacks::Get(HANDLE handle) {
	const auto vpit = m_pImpl->OverlayedHandles.find(handle);
	if (vpit != m_pImpl->OverlayedHandles.end())
		return vpit->second.get();
	return nullptr;
}

bool App::Misc::VirtualSqPacks::EntryExists(const Sqex::Sqpack::EntryPathSpec& pathSpec) const {
	return std::ranges::any_of(m_pImpl->SqpackViews | std::views::values, [&pathSpec](const auto& t) {
		return t.HashOnlyEntries.find(pathSpec) != t.HashOnlyEntries.end()
			|| (pathSpec.HasOriginal() && t.FullPathEntries.find(pathSpec) != t.FullPathEntries.end());
	});
}

std::shared_ptr<Sqex::RandomAccessStream> App::Misc::VirtualSqPacks::GetOriginalEntry(const Sqex::Sqpack::EntryPathSpec& pathSpec) const {
	for (const auto& pack : m_pImpl->SqpackViews | std::views::values) {
		auto it = pack.HashOnlyEntries.find(pathSpec);
		if (it == pack.HashOnlyEntries.end()) {
			it = pack.FullPathEntries.find(pathSpec);
			if (it == pack.FullPathEntries.end()) {
				continue;
			}
		}

		const auto provider = dynamic_cast<Sqex::Sqpack::HotSwappableEntryProvider*>(it->second->Provider.get());
		if (!provider)
			return std::make_shared<Sqex::Sqpack::EntryRawStream>(it->second->Provider.get());

		return std::make_shared<Sqex::Sqpack::EntryRawStream>(provider->GetBaseStream());
	}
	throw std::out_of_range("entry not found");
}

void App::Misc::VirtualSqPacks::MarkIoRequest() {
	m_pImpl->IoLockEvent.Wait();
	m_pImpl->LastIoRequestTimestamp = GetTickCount64();
	m_pImpl->IoEvent.Set();
}

void App::Misc::VirtualSqPacks::TtmpSet::FixChoices() {
	if (!Choices.is_array())
		Choices = nlohmann::json::array();
	for (size_t pageObjectIndex = 0; pageObjectIndex < List.ModPackPages.size(); ++pageObjectIndex) {
		const auto& modGroups = List.ModPackPages[pageObjectIndex].ModGroups;
		if (modGroups.empty())
			continue;

		while (Choices.size() <= pageObjectIndex)
			Choices.emplace_back(nlohmann::json::array());

		auto& pageChoices = Choices.at(pageObjectIndex);
		if (!pageChoices.is_array())
			pageChoices = nlohmann::json::array();

		for (size_t modGroupIndex = 0; modGroupIndex < modGroups.size(); ++modGroupIndex) {
			const auto& modGroup = modGroups[modGroupIndex];
			if (modGroups.empty())
				continue;
			
			while (pageChoices.size() <= modGroupIndex)
				pageChoices.emplace_back(modGroup.SelectionType == "Multi" ? nlohmann::json::array() : nlohmann::json::array({ 0 }));

			auto& modGroupChoice = pageChoices.at(modGroupIndex);
			if (!modGroupChoice.is_array())
				modGroupChoice = nlohmann::json::array({ modGroupChoice });

			for (auto& e : modGroupChoice) {
				if (!e.is_number_unsigned())
					e = 0;
				else if (e.get<size_t>() >= modGroup.OptionList.size())
					e = modGroup.OptionList.size() - 1;
			}
			modGroupChoice = modGroupChoice.get<std::set<size_t>>();
		}
	}
}

void App::Misc::VirtualSqPacks::TtmpSet::ApplyChanges(bool announce) {
	const auto disableFilePath = ListPath.parent_path() / "disable";
	const auto bDisabled = exists(disableFilePath);
	const auto choicesPath = ListPath.parent_path() / "choices.json";

	if (bDisabled && Enabled)
		remove(disableFilePath);
	else if (!bDisabled && !Enabled)
		Utils::Win32::Handle::FromCreateFile(disableFilePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0);

	Utils::SaveJsonToFile(choicesPath, Choices);

	if (announce)
		Impl->ReflectUsedEntries();
}

std::vector<App::Misc::VirtualSqPacks::TtmpSet>& App::Misc::VirtualSqPacks::TtmpSets() {
	return m_pImpl->TtmpSets;
}

void App::Misc::VirtualSqPacks::AddNewTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately) {
	auto pos = std::ranges::lower_bound(m_pImpl->TtmpSets, ttmpl, [](const auto& l, const auto& r) -> bool {
		return l.ListPath < r;
	});
	if (pos != m_pImpl->TtmpSets.end() && equivalent(pos->ListPath, ttmpl))
		return;
	try {
		pos = m_pImpl->TtmpSets.emplace(pos, TtmpSet{
			.Impl = m_pImpl.get(),
			.Allocated = true,
			.Enabled = !exists(ttmpl.parent_path() / "disable"),
			.ListPath = ttmpl,
			.List = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream{Utils::Win32::Handle::FromCreateFile(ttmpl, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0)}),
			.DataFile = Utils::Win32::Handle::FromCreateFile(ttmpl.parent_path() / "TTMPD.mpd", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0)
		});
	} catch (const std::exception& e) {
		m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
			"Failed to load TexTools ModPack from {}: {}", ttmpl.wstring(), e.what());
		return;
	}
	if (const auto choicesPath = ttmpl.parent_path() / "choices.json"; exists(choicesPath)) {
		try {
			pos->Choices = Utils::ParseJsonFromFile(choicesPath);
			m_pImpl->Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
				"Choices file loaded from {}", choicesPath.wstring());
		} catch (const std::exception& e) {
			m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to load choices from {}: {}", choicesPath.wstring(), e.what());
		}
	}
	pos->FixChoices();

	for (const auto& entry : pos->List.SimpleModsList) {
		const auto it = m_pImpl->SqpackViews.find(m_pImpl->SqpackPath / std::format(L"{}.win32.index", entry.ToExpacDatPath()));
		if (it == m_pImpl->SqpackViews.end())
			continue;

		const auto pathSpec = Sqex::Sqpack::EntryPathSpec(entry.FullPath);
		auto entryIt = it->second.HashOnlyEntries.find(pathSpec);
		if (entryIt == it->second.HashOnlyEntries.end()) {
			entryIt = it->second.FullPathEntries.find(pathSpec);
			if (entryIt == it->second.FullPathEntries.end()) {
				continue;
			}
		}

		const auto provider = dynamic_cast<Sqex::Sqpack::HotSwappableEntryProvider*>(entryIt->second->Provider.get());
		if (!provider)
			continue;

		pos->Allocated &= provider->StreamSize() >= entry.ModSize;
	}

	for (const auto& modPackPage : pos->List.ModPackPages) {
		for (const auto& modGroup : modPackPage.ModGroups) {
			for (const auto& option : modGroup.OptionList) {
				for (const auto& entry : option.ModsJsons) {
					const auto it = m_pImpl->SqpackViews.find(m_pImpl->SqpackPath / std::format(L"{}.win32.index", entry.ToExpacDatPath()));
					if (it == m_pImpl->SqpackViews.end())
						continue;

					const auto pathSpec = Sqex::Sqpack::EntryPathSpec(entry.FullPath);
					auto entryIt = it->second.HashOnlyEntries.find(pathSpec);
					if (entryIt == it->second.HashOnlyEntries.end()) {
						entryIt = it->second.FullPathEntries.find(pathSpec);
						if (entryIt == it->second.FullPathEntries.end()) {
							continue;
						}
					}

					const auto provider = dynamic_cast<Sqex::Sqpack::HotSwappableEntryProvider*>(entryIt->second->Provider.get());
					if (!provider)
						continue;

					pos->Allocated &= provider->StreamSize() >= entry.ModSize;
				}
			}
		}
	}

	if (reflectImmediately)
		m_pImpl->ReflectUsedEntries();
}

void App::Misc::VirtualSqPacks::DeleteTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately) {
	const auto pos = std::ranges::lower_bound(m_pImpl->TtmpSets, ttmpl, [](const auto& l, const auto& r) -> bool {
		return l.ListPath < r;
	});
	if (pos == m_pImpl->TtmpSets.end() || !equivalent(pos->ListPath, ttmpl))
		return;
	remove(pos->ListPath);
	if (reflectImmediately)
		m_pImpl->ReflectUsedEntries();
}

void App::Misc::VirtualSqPacks::RescanTtmp() {
	std::vector<std::filesystem::path> dirs = m_pImpl->Config->Runtime.AdditionalTexToolsModPackSearchDirectories.Value();
	dirs.emplace_back(m_pImpl->SqpackPath / "TexToolsMods");
	dirs.emplace_back(m_pImpl->Config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods");

	for (const auto& dir : dirs) {
		if (dir.empty() || !is_directory(dir))
			continue;

		try {
			for (const auto& iter : std::filesystem::recursive_directory_iterator(dir)) {
				const auto& ttmpl = iter.path();
				if (ttmpl.filename() != "TTMPL.mpl")
					continue;

				try {
					AddNewTtmp(ttmpl, false);
				} catch (const std::exception& e) {
					m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
						"Failed to load TexTools ModPack from {}: {}", ttmpl.wstring(), e.what());
				}
			}
		} catch (const std::exception& e) {
			m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to list items in {}: {}",
				dir, e.what());
		}
	}

	m_pImpl->ReflectUsedEntries();
}
