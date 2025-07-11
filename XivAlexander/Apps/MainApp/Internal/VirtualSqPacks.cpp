#include "pch.h"
#include "VirtualSqPacks.h"

#include <XivAlexanderCommon/Sqex/SeString.h>
#include <XivAlexanderCommon/Sqex/Est.h>
#include <XivAlexanderCommon/Sqex/Eqdp.h>
#include <XivAlexanderCommon/Sqex/EqpGmp.h>
#include <XivAlexanderCommon/Sqex/Excel/Generator.h>
#include <XivAlexanderCommon/Sqex/Excel/Reader.h>
#include <XivAlexanderCommon/Sqex/Imc.h>
#include <XivAlexanderCommon/Sqex/Sound/Reader.h>
#include <XivAlexanderCommon/Sqex/Sound/Writer.h>
#include <XivAlexanderCommon/Sqex/Sqpack/BinaryEntryProvider.h>
#include <XivAlexanderCommon/Sqex/Sqpack/Creator.h>
#include <XivAlexanderCommon/Sqex/Sqpack/EntryProvider.h>
#include <XivAlexanderCommon/Sqex/Sqpack/EntryRawStream.h>
#include <XivAlexanderCommon/Sqex/Sqpack/HotSwappableEntryProvider.h>
#include <XivAlexanderCommon/Sqex/Sqpack/ModelEntryProvider.h>
#include <XivAlexanderCommon/Sqex/Sqpack/RandomAccessStreamAsEntryProviderView.h>
#include <XivAlexanderCommon/Sqex/Sqpack/Reader.h>
#include <XivAlexanderCommon/Sqex/Sqpack/TextureEntryProvider.h>
#include <XivAlexanderCommon/Sqex/ThirdParty/TexTools.h>
#include <XivAlexanderCommon/Utils/Win32/Process.h>
#include <XivAlexanderCommon/Utils/Win32/TaskDialogBuilder.h>
#include <XivAlexanderCommon/Utils/Win32/ThreadPool.h>

#include "Apps/MainApp/Window/ProgressPopupWindow.h"
#include "Apps/MainApp/App.h"
#include "Config.h"
#include "Misc/GameInstallationDetector.h"
#include "Misc/Logger.h"
#include "resource.h"
#include "XivAlexander.h"

struct XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::Implementation {
	Apps::MainApp::App& App;
	VirtualSqPacks& Sqpacks;
	const std::shared_ptr<XivAlexander::Config> Config;
	const std::shared_ptr<XivAlexander::Misc::Logger> Logger;
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

	std::shared_ptr<NestedTtmp> Ttmps;

	std::shared_ptr<const Sqex::RandomAccessStream> EmptyScd;

	Utils::CallOnDestruction::Multiple Cleanup;

	Implementation(Apps::MainApp::App& app, VirtualSqPacks* sqpacks, std::filesystem::path sqpackPath)
		: App(app)
		, Sqpacks(*sqpacks)
		, Config(XivAlexander::Config::Acquire())
		, Logger(XivAlexander::Misc::Logger::Acquire())
		, SqpackPath(std::move(sqpackPath))
		, GameReleaseInfo(Misc::GameInstallationDetector::GetGameReleaseInfo()) {

		const auto actCtx = Dll::ActivationContext().With();
		{
			Apps::MainApp::Window::ProgressPopupWindow progressWindow(Dll::FindGameMainWindow(false));
			progressWindow.Show(std::chrono::milliseconds(5000));
			InitializeSqPacks(progressWindow);
			ReflectUsedEntries(true);
		}

		Cleanup += Config->Runtime.MuteVoice_Battle.OnChange([this]() { ReflectUsedEntries(); });
		Cleanup += Config->Runtime.MuteVoice_Cm.OnChange([this]() { ReflectUsedEntries(); });
		Cleanup += Config->Runtime.MuteVoice_Emote.OnChange([this]() { ReflectUsedEntries(); });
		Cleanup += Config->Runtime.MuteVoice_Line.OnChange([this]() { ReflectUsedEntries(); });
	}

	~Implementation() {
		Cleanup.Clear();
	}

	std::shared_ptr<Sqex::RandomAccessStream> GetOriginalEntry(const Sqex::Sqpack::EntryPathSpec& pathSpec) const {
		for (const auto& pack : SqpackViews | std::views::values) {
			auto it = pack.HashOnlyEntries.find(pathSpec);
			if (it == pack.HashOnlyEntries.end()) {
				it = pack.FullPathEntries.find(pathSpec);
				if (it == pack.FullPathEntries.end()) {
					continue;
				}
			}

			const auto provider = dynamic_cast<Sqex::Sqpack::HotSwappableEntryProvider*>(it->second->Provider.get());
			if (!provider)
				return std::make_shared<Sqex::Sqpack::EntryRawStream>(it->second->Provider);

			return std::make_shared<Sqex::Sqpack::EntryRawStream>(provider->GetBaseStream());
		}
		throw std::out_of_range("entry not found");
	}

	struct ReflectUsedEntriesTempData {
		std::map<Sqex::Sqpack::EntryPathSpec, std::tuple<Sqex::Sqpack::HotSwappableEntryProvider*, std::shared_ptr<Sqex::Sqpack::EntryProvider>, std::string>, Sqex::Sqpack::EntryPathSpec::AllHashComparator> Replacements;
		std::map<std::string, Sqex::Est::File> Est;
		Sqex::EqpGmp::ExpandedFile Eqp;
		Sqex::EqpGmp::ExpandedFile Gmp;
		std::map<std::string, Sqex::Imc::File> Imc;
		std::map<std::pair<Sqex::ThirdParty::TexTools::ItemMetadata::TargetItemType, uint32_t>, Sqex::Eqdp::ExpandedFile> Eqdp;
	};

	void ReflectUsedEntries(bool isCalledFromConstructor = false) {
		const auto mainThreadStallEvent = Utils::Win32::Event::Create();
		const auto mainThreadStalledEvent = Utils::Win32::Event::Create();
		const auto resumeMainThread = Utils::CallOnDestruction([&mainThreadStallEvent]() { mainThreadStallEvent.Set(); });

		// Step. Suspend game main thread, by sending run on UI thread message
		const auto staller = Utils::Win32::Thread(L"Staller", [this, isCalledFromConstructor, mainThreadStalledEvent, mainThreadStallEvent]() {
			if (!isCalledFromConstructor) {
				App.RunOnGameLoop([mainThreadStalledEvent, mainThreadStallEvent]() {
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

		ReflectUsedEntriesTempData tempData{
			.Eqp{*GetOriginalEntry(Sqex::ThirdParty::TexTools::ItemMetadata::EqpPath)},
			.Gmp{*GetOriginalEntry(Sqex::ThirdParty::TexTools::ItemMetadata::GmpPath)},
		};

		// Step. Find voices to enable or disable
		const auto voBattle = Sqex::Sqpack::SqexHash("sound/voice/vo_battle", SIZE_MAX);
		const auto voCm = Sqex::Sqpack::SqexHash("sound/voice/vo_cm", SIZE_MAX);
		const auto voEmote = Sqex::Sqpack::SqexHash("sound/voice/vo_emote", SIZE_MAX);
		const auto voLine = Sqex::Sqpack::SqexHash("sound/voice/vo_line", SIZE_MAX);
		for (const auto& entry : SqpackViews.at(SqpackPath / L"ffxiv/070000.win32.index2").Entries) {
			const auto provider = dynamic_cast<Sqex::Sqpack::HotSwappableEntryProvider*>(entry->Provider.get());
			if (!provider)
				continue;

			const auto& pathSpec = provider->PathSpec();
			if (pathSpec.PathHash == voBattle || pathSpec.PathHash == voCm || pathSpec.PathHash == voEmote || pathSpec.PathHash == voLine)
				tempData.Replacements.insert_or_assign(pathSpec, std::make_tuple(provider, std::shared_ptr<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(), std::string()));

			if (pathSpec.PathHash == voBattle && Config->Runtime.MuteVoice_Battle)
				std::get<1>(tempData.Replacements.at(pathSpec)) = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(pathSpec, EmptyScd);
			if (pathSpec.PathHash == voCm && Config->Runtime.MuteVoice_Cm)
				std::get<1>(tempData.Replacements.at(pathSpec)) = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(pathSpec, EmptyScd);
			if (pathSpec.PathHash == voEmote && Config->Runtime.MuteVoice_Emote)
				std::get<1>(tempData.Replacements.at(pathSpec)) = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(pathSpec, EmptyScd);
			if (pathSpec.PathHash == voLine && Config->Runtime.MuteVoice_Line)
				std::get<1>(tempData.Replacements.at(pathSpec)) = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(pathSpec, EmptyScd);
		}

		Ttmps->Traverse(false, [&](NestedTtmp& nestedTtmp) {
			if (!nestedTtmp.Ttmp)
				return;
			TtmpSet& ttmp = *nestedTtmp.Ttmp;

			// Step. Find placeholders to adjust
			ttmp.ForEachEntryInterruptible(false, [&](const auto& entry) {
				const auto v = SqpackPath / std::format(L"{}.win32.index2", entry.ToExpacDatPath());
				const auto it = SqpackViews.find(v);
				if (it == SqpackViews.end()) {
					Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "Failed to find {} as a sqpack file", v.c_str());
					return Sqex::ThirdParty::TexTools::TTMPL::Continue;
				}

				if (!entry.IsMetadata()) {
					ReflectUsedEntries_FindPlaceholders(it->second, tempData, entry.FullPath);
				} else {
					const auto ttmpd = std::make_shared<Sqex::FileRandomAccessStream>(Utils::Win32::Handle{ ttmp.DataFile, false });
					const auto metadata = Sqex::ThirdParty::TexTools::ItemMetadata(entry.FullPath, Sqex::Sqpack::EntryRawStream(std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(entry.FullPath, ttmpd, entry.ModOffset, entry.ModSize)));
					ReflectUsedEntries_FindPlaceholders(it->second, tempData, metadata.TargetImcPath);
					ReflectUsedEntries_FindPlaceholders(it->second, tempData, Sqex::ThirdParty::TexTools::ItemMetadata::EqpPath);
					ReflectUsedEntries_FindPlaceholders(it->second, tempData, Sqex::ThirdParty::TexTools::ItemMetadata::GmpPath);
					if (const auto estPath = Sqex::ThirdParty::TexTools::ItemMetadata::EstPath(metadata.EstType))
						ReflectUsedEntries_FindPlaceholders(it->second, tempData, estPath);
					if (const auto eqdpedit = metadata.Get<Sqex::ThirdParty::TexTools::ItemMetadata::EqdpEntry>(Sqex::ThirdParty::TexTools::ItemMetadata::MetaDataType::Eqdp); !eqdpedit.empty()) {
						for (const auto& v : eqdpedit) {
							ReflectUsedEntries_FindPlaceholders(it->second, tempData, Sqex::ThirdParty::TexTools::ItemMetadata::EqdpPath(metadata.ItemType, v.RaceCode));
						}
					}
				}

				return Sqex::ThirdParty::TexTools::TTMPL::Continue;
				});

			// Step. Unregister TTMP files that no longer exist and delete associated files
			if (!exists(ttmp.ListPath)) {
				ttmp.TryCleanupUnusedFiles();
				return;
			}
			if (nestedTtmp.RenameTo) {
				try {
					create_directories(*nestedTtmp.RenameTo);

					const auto renameToDirHandle = Utils::Win32::Handle::FromCreateFile(*nestedTtmp.RenameTo, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0);

					const auto newListPath = (*nestedTtmp.RenameTo / L"TTMPL.mpl").wstring();
					std::vector<char> renameInfoBuffer;
					renameInfoBuffer.resize(sizeof(FILE_RENAME_INFO) + newListPath.size());
					auto renameInfo = *reinterpret_cast<FILE_RENAME_INFO*>(&renameInfoBuffer[0]);
					renameInfo.ReplaceIfExists = true;
					renameInfo.RootDirectory = renameToDirHandle;
					renameInfo.FileNameLength = static_cast<DWORD>(newListPath.size());
					wcsncpy_s(renameInfo.FileName, renameInfo.FileNameLength, newListPath.data(), newListPath.size());
					SetFileInformationByHandle(ttmp.DataFile, FileRenameInfo, &renameInfoBuffer[0], static_cast<DWORD>(renameInfoBuffer.size()));

					for (const auto& path : {
							"TTMPD.mpd",
							"choices.json",
							"disable",
							"compression",
						}) {
						const auto oldPath = ttmp.ListPath.parent_path() / path;
						if (exists(oldPath))
							std::filesystem::rename(oldPath, *nestedTtmp.RenameTo / path);
					}
					try {
						remove(ttmp.ListPath.parent_path());
					} catch (...) {
						// pass
					}
					ttmp.ListPath = newListPath;
					nestedTtmp.RenameTo.reset();
				} catch (const std::exception& e) {
					Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
						"Failed to move {} to {}: {}",
						ttmp.ListPath.wstring(), nestedTtmp.RenameTo->wstring(), e.what());
					nestedTtmp.RenameTo.reset();
				}
			}

			return;
			});

		Ttmps->RemoveEmptyChildren();

		// Step. Set new replacements
		Ttmps->Traverse(true, [&](NestedTtmp& nestedTtmp) {
			if (nestedTtmp.Ttmp && nestedTtmp.Ttmp->Allocated) {
				nestedTtmp.Ttmp->ForEachEntry(true, [&](const auto& entry) {
					ReflectUsedEntries_SetReplacementsFromTtmpEntry(tempData, *nestedTtmp.Ttmp, entry);
					});
			}
			});

		// Step. Replace metadata files
		for (const auto& [path, data] : tempData.Est)
			ReflectUsedEntries_SetFromBuffer(tempData, path, data.Data());
		ReflectUsedEntries_SetFromBuffer(tempData, Sqex::ThirdParty::TexTools::ItemMetadata::EqpPath, tempData.Eqp.DataBytes());
		ReflectUsedEntries_SetFromBuffer(tempData, Sqex::ThirdParty::TexTools::ItemMetadata::GmpPath, tempData.Gmp.DataBytes());
		for (const auto& [path, data] : tempData.Imc)
			ReflectUsedEntries_SetFromBuffer(tempData, path, data.Data());
		for (const auto& [eqdpKey, data] : tempData.Eqdp)
			ReflectUsedEntries_SetFromBuffer(tempData, Sqex::ThirdParty::TexTools::ItemMetadata::EqdpPath(eqdpKey.first, eqdpKey.second), data.Data());

		// Step. Apply replacements
		for (const auto& pathSpec : tempData.Replacements | std::views::keys) {
			auto& [place, newEntry, description] = tempData.Replacements.at(pathSpec);
			if (!description.empty()) {
				if (newEntry)
					Logger->Format(LogCategory::VirtualSqPacks, "{}: {}", description, pathSpec);
				else
					Logger->Format(LogCategory::VirtualSqPacks, "Reset: {}", pathSpec);
			}
			place->SwapStream(std::move(newEntry));
		}

		// Step. Flush caches if any
		for (const auto& view : SqpackViews) {
			for (const auto& dataView : view.second.Data) {
				dataView->Flush();
			}
		}

		if (!isCalledFromConstructor)
			Sqpacks.OnTtmpSetsChanged();
	}

	void ReflectUsedEntries_FindPlaceholders(
		Sqex::Sqpack::Creator::SqpackViews& view,
		ReflectUsedEntriesTempData& tempData,
		const Sqex::Sqpack::EntryPathSpec& pathSpec
	) {
		auto entryIt = view.HashOnlyEntries.find(pathSpec);
		if (entryIt == view.HashOnlyEntries.end()) {
			entryIt = view.FullPathEntries.find(pathSpec);
			if (entryIt == view.FullPathEntries.end())
				return;
		}

		const auto provider = dynamic_cast<Sqex::Sqpack::HotSwappableEntryProvider*>(entryIt->second->Provider.get());
		if (!provider)
			return;

		provider->UpdatePathSpec(pathSpec);
		tempData.Replacements.insert_or_assign(pathSpec, std::make_tuple(provider, std::shared_ptr<Sqex::Sqpack::EntryProvider>(), std::string()));
	}

	void ReflectUsedEntries_SetReplacementsFromTtmpEntry(
		ReflectUsedEntriesTempData& tempData,
		TtmpSet& ttmp,
		const Sqex::ThirdParty::TexTools::ModEntry& entry
	) {
		if (entry.IsMetadata()) {
			const auto ttmpd = std::make_shared<Sqex::FileRandomAccessStream>(Utils::Win32::Handle{ ttmp.DataFile, false });
			const auto metadata = Sqex::ThirdParty::TexTools::ItemMetadata(entry.FullPath, Sqex::Sqpack::EntryRawStream(std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(entry.FullPath, ttmpd, entry.ModOffset, entry.ModSize)));
			metadata.ApplyImcEdits([&]() -> Sqex::Imc::File& {
				const auto imcPath = metadata.TargetImcPath;
				if (const auto it = tempData.Imc.find(imcPath); it == tempData.Imc.end())
					return tempData.Imc[imcPath] = Sqex::Imc::File(*GetOriginalEntry(metadata.SourceImcPath));
				else
					return it->second;
				});
			metadata.ApplyEqdpEdits([&](auto type, auto race) -> Sqex::Eqdp::ExpandedFile& {
				const auto key = std::make_pair(type, race);
				if (const auto it = tempData.Eqdp.find(key); it == tempData.Eqdp.end())
					return tempData.Eqdp[key] = Sqex::Eqdp::ExpandedFile(*GetOriginalEntry(Sqex::ThirdParty::TexTools::ItemMetadata::EqdpPath(type, race)));
				else
					return it->second;
				});
			metadata.ApplyEqpEdits(tempData.Eqp);
			metadata.ApplyGmpEdits(tempData.Gmp);

			const auto estPath = Sqex::ThirdParty::TexTools::ItemMetadata::EstPath(metadata.EstType);
			if (estPath) {
				if (const auto it = tempData.Est.find(estPath); it == tempData.Est.end())
					metadata.ApplyEstEdits(tempData.Est[estPath] = Sqex::Est::File(*GetOriginalEntry(estPath)));
				else
					metadata.ApplyEstEdits(it->second);
			}
		} else {
			const auto entryIt = tempData.Replacements.find(entry.FullPath);
			if (entryIt == tempData.Replacements.end())
				return;

			std::get<1>(entryIt->second) = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(
				entry.FullPath,
				std::make_shared<Sqex::FileRandomAccessStream>(Utils::Win32::Handle{ ttmp.DataFile, false }, entry.ModOffset, entry.ModSize)
				);
			std::get<2>(entryIt->second) = ttmp.List.Name;
		}
	}

	void ReflectUsedEntries_SetFromBuffer(
		ReflectUsedEntriesTempData& tempData,
		const std::string& path,
		const std::vector<uint8_t>& data
	) {
		const auto pathSpec = Sqex::Sqpack::EntryPathSpec(path);

		const auto entryIt = tempData.Replacements.find(pathSpec);
		if (entryIt == tempData.Replacements.end())
			return;

		std::get<1>(entryIt->second) = std::make_shared<Sqex::Sqpack::OnTheFlyBinaryEntryProvider>(path, std::make_shared<Sqex::MemoryRandomAccessStream>(data));
		// std::get<1>(entryIt->second) = std::make_shared<Sqex::Sqpack::EmptyOrObfuscatedEntryProvider>(path, std::make_shared<Sqex::MemoryRandomAccessStream>(data));
		std::get<2>(entryIt->second) = "Metadata";
	}

	std::vector<std::filesystem::path> GetPossibleTtmpDirs() const {
		std::vector<std::filesystem::path> dirs;
		dirs.emplace_back(Config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods");
		dirs.emplace_back(SqpackPath / "TexToolsMods");

		for (const auto& dir : Config->Runtime.AdditionalTexToolsModPackSearchDirectories.Value())
			dirs.emplace_back(Config::TranslatePath(dir));

		for (auto it = dirs.begin(); it != dirs.end();) {
			if (!exists(*it) || !is_directory(*it) || it->empty())
				it = dirs.erase(it);
			else
				++it;
		}
		return dirs;
	}

	void SaveNestedTtmpConfig(NestedTtmp& ttmp, bool announce) {
		const auto disableFilePath = ttmp.Path / "disable";
		const auto choicesPath = ttmp.Path / "choices.json";

		if (ttmp.Enabled && exists(disableFilePath))
			remove(disableFilePath);
		else if (!ttmp.Enabled && !exists(disableFilePath))
			void(std::ofstream(disableFilePath));

		if (ttmp.Ttmp)
			Utils::SaveJsonToFile(choicesPath, ttmp.Ttmp->Choices);

		if (announce)
			ReflectUsedEntries();
	}

	void InitializeSqPacks(Apps::MainApp::Window::ProgressPopupWindow& progressWindow) {
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
				if (ext != L".index2")
					continue;

				creators.emplace(sqpack, std::make_unique<Sqex::Sqpack::Creator>(
					Utils::ToUtf8(expac.path().filename().wstring()),
					Utils::ToUtf8(sqpack.path().filename().replace_extension().replace_extension().wstring())
					));
			}
		}

		Ttmps = std::make_shared<NestedTtmp>(NestedTtmp{
			.Children = std::vector<std::shared_ptr<NestedTtmp>>{},
		});

		{
			const auto loaderThread = Utils::Win32::Thread(L"TTMP Scanner", [&]() {
				for (const auto& dir : GetPossibleTtmpDirs()) {
					if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
						throw std::runtime_error("Cancelled");
					RescanTtmpTree(dir, Ttmps, progressWindow);
				}
				});
			do {
				progressWindow.UpdateMessage(Config->Runtime.GetStringRes(IDS_TITLE_DISCOVERINGFILES));
			} while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, { loaderThread }));
		}

		if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
			throw std::runtime_error("Cancelled");

		{
			std::mutex groupedLogPrintLock;
			const auto progressMax = creators.size() * (0
				+ 1 // original sqpack
				+ Ttmps->Count() // TTMP
				+ 1 // replacement file entry
				);
			std::atomic_size_t progressValue = 0;
			std::atomic_size_t fileIndex = 0;

			Utils::Win32::TpEnvironment pool(L"InitializeSqPacks Initializer/Pool");
			const std::filesystem::path* pLastStartedIndexFile = &creators.begin()->first;
			const auto loaderThread = Utils::Win32::Thread(L"InitializeSqPacks Initializer", [&]() {
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
								try {
									const auto reader = Sqex::Sound::ScdReader(creator["sound/system/sample_system.scd"]);
									Sqex::Sound::ScdWriter writer;
									writer.SetTable1(reader.ReadTable1Entries());
									writer.SetTable2(reader.ReadTable2Entries());
									writer.SetTable4(reader.ReadTable4Entries());
									writer.SetTable5(reader.ReadTable5Entries());
									for (size_t i = 0; i < 256; ++i)
										writer.SetSoundEntry(i, Sqex::Sound::ScdWriter::SoundEntry::EmptyEntry(std::chrono::milliseconds(100)));

									EmptyScd = std::make_shared<Sqex::MemoryRandomAccessStream>(
										Sqex::Sqpack::MemoryBinaryEntryProvider("sound/empty256.scd", std::make_shared<Sqex::MemoryRandomAccessStream>(writer.Export()), Z_NO_COMPRESSION)
										.ReadStreamIntoVector<uint8_t>(0));
								} catch(std::out_of_range&) {
									// ignore
								}
							}

							if (creator.DatName == "040000") {
								creator.ReserveSwappableSpace(Sqex::ThirdParty::TexTools::ItemMetadata::EstPath(Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Body), 1048576);
								creator.ReserveSwappableSpace(Sqex::ThirdParty::TexTools::ItemMetadata::EstPath(Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Face), 1048576);
								creator.ReserveSwappableSpace(Sqex::ThirdParty::TexTools::ItemMetadata::EstPath(Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Hair), 1048576);
								creator.ReserveSwappableSpace(Sqex::ThirdParty::TexTools::ItemMetadata::EstPath(Sqex::ThirdParty::TexTools::ItemMetadata::TargetEstType::Head), 1048576);
								creator.ReserveSwappableSpace(Sqex::ThirdParty::TexTools::ItemMetadata::EqpPath, 1048576);
								creator.ReserveSwappableSpace(Sqex::ThirdParty::TexTools::ItemMetadata::GmpPath, 1048576);
							}

							if (Ttmps->TraverseInterruptible(false, [&](const auto& nestedTtmp) {
								if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
									return NestedTtmp::Break;

								progressValue += 1;

								if (!nestedTtmp.Ttmp)
									return NestedTtmp::Continue;
								const auto& ttmp = *nestedTtmp.Ttmp;

								creator.ReserveSpacesFromTTMP(ttmp.List, std::make_shared<Sqex::FileRandomAccessStream>(Utils::Win32::Handle(ttmp.DataFile, false)));
								return NestedTtmp::Continue;
								}) == NestedTtmp::Break) {
								return;
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

			if (pCreator->DatExpac == "ffxiv" && pCreator->DatName == "070000" && EmptyScd) {
				for (const auto& pathSpec : pCreator->AllPathSpec())
					pCreator->ReserveSwappableSpace(pathSpec, static_cast<uint32_t>(EmptyScd->StreamSize()));
			}
		}

		{
			const auto progressMax = creators.size();
			size_t progressValue = 0;
			const auto dataViewBuffer = std::make_shared<Sqex::Sqpack::Creator::SqpackViewEntryCache>();
			const auto workerThread = Utils::Win32::Thread(L"InitializeSqPacks Finalizer", [&]() {
				Utils::Win32::TpEnvironment pool(L"InitializeSqPacks Finalizer/Pool");
				std::mutex resLock;
				for (const auto& [indexFile, pCreator] : creators) {
					if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
						throw std::runtime_error("Cancelled");

					pool.SubmitWork([&]() {
						auto v = pCreator->AsViews(false, pCreator->DatName.starts_with("0c") ? nullptr : dataViewBuffer);

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

	void RescanTtmpTree(const std::filesystem::path& path, std::shared_ptr<NestedTtmp> parent, Apps::MainApp::Window::ProgressPopupWindow& progressWindow) {
		try {
			for (const auto& iter : std::filesystem::directory_iterator(path)) {
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					return;

				if (!iter.is_directory())
					continue;

				std::shared_ptr<NestedTtmp> current;
				for (auto& child : *parent->Children) {
					if (equivalent(child->Path, iter.path())) {
						current = child;
						break;
					}
				}
				if (const auto ttmplPath = iter.path() / "TTMPL.mpl"; exists(ttmplPath)) {
					if (!current)
						AddFromTtmpl(ttmplPath, parent, false, progressWindow);
				} else {
					if (!current)
						current = parent->Children->emplace_back(std::make_shared<NestedTtmp>(NestedTtmp{
							.Path = iter.path(),
							.Parent = parent,
							.Children = std::vector<std::shared_ptr<NestedTtmp>>{},
							}));
					RescanTtmpTree(iter.path(), current, progressWindow);
				}
			}
		} catch (const std::exception& e) {
			Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to list items in {}: {}",
				path.wstring(), e.what());
		}

		if (const auto orderingFile = path / "order.json"; exists(orderingFile)) {
			try {
				std::map<std::filesystem::path, uint64_t> orderMap;
				for (const auto& [path, index] : Utils::ParseJsonFromFile(orderingFile).items()) {
					orderMap.emplace(std::filesystem::path(Utils::FromUtf8(path)), index.get<uint64_t>());
				}
				Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
					"Ordering file loaded from {}", orderingFile.wstring());
				for (auto& child : *parent->Children) {
					if (const auto it = orderMap.find(child->Path.filename()); it != orderMap.end())
						child->Index = it->second;
				}
			} catch (const std::exception& e) {
				Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"Failed to load choices from {}: {}", orderingFile.wstring(), e.what());
			}
		}

		parent->Enabled = !exists(path / "disable");

		parent->RemoveEmptyChildren();
		parent->Sort();
	}

	std::shared_ptr<NestedTtmp> AddFromTtmpl(const std::filesystem::path& ttmplPath, const std::shared_ptr<NestedTtmp>& parent, bool checkAllocation, Apps::MainApp::Window::ProgressPopupWindow& progressWindow) {
		const auto ttmpDir = ttmplPath.parent_path();
		const auto ttmpdPath = ttmpDir / "TTMPD.mpd";
		if (ttmplPath.filename() != "TTMPL.mpl")
			return nullptr;

		std::shared_ptr<NestedTtmp> added;
		try {
			auto list = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream{ Utils::Win32::Handle::FromCreateFile(ttmplPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0) });
			auto dataFile = Utils::Win32::Handle::FromCreateFile(ttmpdPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
			const auto dataStream = std::make_shared<Sqex::FileRandomAccessStream>(Utils::Win32::Handle{ dataFile, false });

			added = parent->Children->emplace_back(std::make_shared<NestedTtmp>(NestedTtmp{
				.Path = ttmpDir,
				.Parent = parent,
				.Enabled = !exists(ttmpDir / "disable"),
				.Ttmp = TtmpSet{
					.ListPath = ttmplPath,
					.List = std::move(list),
					.DataFile = std::move(dataFile),
				},
				}));
		} catch (const std::exception& e) {
			Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to load TexTools ModPack from {}: {}", ttmplPath.wstring(), e.what());
			return nullptr;
		}
		if (checkAllocation)
			CheckTtmpAllocation(*added->Ttmp);
		else
			added->Ttmp->Allocated = true;
		if (const auto choicesPath = ttmpDir / "choices.json"; exists(choicesPath)) {
			try {
				added->Ttmp->Choices = Utils::ParseJsonFromFile(choicesPath);
				Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
					"Choices file loaded from {}", choicesPath.wstring());
			} catch (const std::exception& e) {
				Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"Failed to load choices from {}: {}", choicesPath.wstring(), e.what());
			}
		}
		added->Ttmp->FixChoices();
		return added;
	}

	std::shared_ptr<NestedTtmp> FindNestedTtmpContainer(const std::filesystem::path& ttmpl, bool create) {
		std::vector<std::filesystem::path> dirStack;
		for (const auto& dir : GetPossibleTtmpDirs()) {
			auto ttmpRoot{ ttmpl };
			while (ttmpRoot != ttmpRoot.parent_path()) {
				ttmpRoot = ttmpRoot.parent_path();
				if (equivalent(dir, ttmpRoot))
					break;
				dirStack.emplace_back(ttmpRoot);
			}
			if (ttmpRoot == ttmpRoot.parent_path()) {
				Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"{} is not in one of ttmp root folders.", ttmpl.wstring());
				return nullptr;
			}
		}

		std::shared_ptr<NestedTtmp> folder{Ttmps};
		while (!dirStack.empty()) {
			auto subfolder = folder->Find(dirStack.back());
			if (!subfolder) {
				if (!create)
					return nullptr;
				subfolder = folder->Children->emplace_back(std::make_shared<NestedTtmp>(NestedTtmp{
					.Path = dirStack.back(),
					.Parent = folder,
					.Children = std::vector<std::shared_ptr<NestedTtmp>>{},
					}));
				folder->Sort();
			}
			folder = subfolder;
			dirStack.pop_back();
		}

		return folder;
	}

	void CheckTtmpAllocation(TtmpSet& item) {
		item.Allocated = Sqex::ThirdParty::TexTools::TTMPL::Continue == item.ForEachEntryInterruptible(false, [&](const auto& entry) {
			const auto it = SqpackViews.find(SqpackPath / std::format(L"{}.win32.index2", entry.ToExpacDatPath()));
			if (it == SqpackViews.end())
				return Sqex::ThirdParty::TexTools::TTMPL::Break;

			const auto pathSpec = Sqex::Sqpack::EntryPathSpec(entry.FullPath);
			auto entryIt = it->second.HashOnlyEntries.find(pathSpec);
			if (entryIt == it->second.HashOnlyEntries.end()) {
				entryIt = it->second.FullPathEntries.find(pathSpec);
				if (entryIt == it->second.FullPathEntries.end()) {
					return Sqex::ThirdParty::TexTools::TTMPL::Break;
				}
			}

			const auto provider = dynamic_cast<Sqex::Sqpack::HotSwappableEntryProvider*>(entryIt->second->Provider.get());
			if (!provider)
				return Sqex::ThirdParty::TexTools::TTMPL::Continue;

			return provider->StreamSize() >= entry.ModSize ? Sqex::ThirdParty::TexTools::TTMPL::Continue : Sqex::ThirdParty::TexTools::TTMPL::Break;
			});
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
};

XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::VirtualSqPacks(Apps::MainApp::App& app, std::filesystem::path sqpackPath)
	: m_pImpl(std::make_unique<Implementation>(app, this, std::move(sqpackPath))) {
}

XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::~VirtualSqPacks() = default;

HANDLE XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::Open(const std::filesystem::path & path) {
	try {
		const auto fileToOpen = absolute(path);
		const auto recreatedFilePath = m_pImpl->SqpackPath / fileToOpen.parent_path().filename() / fileToOpen.filename();
		const auto indexFile = std::filesystem::path(recreatedFilePath).replace_extension(L".index");
		const auto index2File = std::filesystem::path(recreatedFilePath).replace_extension(L".index2");
		if (!exists(indexFile) && !exists(index2File))
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
			if ((exists(indexFile) && equivalent(view.first, indexFile))
				|| (exists(index2File) && equivalent(view.first, index2File))) {
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

bool XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::Close(HANDLE handle) {
	return m_pImpl->OverlayedHandles.erase(handle);
}

XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::OverlayedHandleData* XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::Get(HANDLE handle) {
	const auto vpit = m_pImpl->OverlayedHandles.find(handle);
	if (vpit != m_pImpl->OverlayedHandles.end())
		return vpit->second.get();
	return nullptr;
}

bool XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::EntryExists(const Sqex::Sqpack::EntryPathSpec & pathSpec) const {
	return std::ranges::any_of(m_pImpl->SqpackViews | std::views::values, [&pathSpec](const auto& t) {
		return t.HashOnlyEntries.find(pathSpec) != t.HashOnlyEntries.end()
			|| (pathSpec.HasOriginal() && t.FullPathEntries.find(pathSpec) != t.FullPathEntries.end());
		});
}

std::shared_ptr<Sqex::RandomAccessStream> XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::GetOriginalEntry(const Sqex::Sqpack::EntryPathSpec & pathSpec) const {
	return m_pImpl->GetOriginalEntry(pathSpec);
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::MarkIoRequest() {
	m_pImpl->IoLockEvent.Wait();
	m_pImpl->LastIoRequestTimestamp = GetTickCount64();
	m_pImpl->IoEvent.Set();
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::TtmpSet::FixChoices() {
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

XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::TtmpSet::TraverseCallbackResult XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::TtmpSet::ForEachEntryInterruptible(bool choiceOnly, std::function<TraverseCallbackResult(const Sqex::ThirdParty::TexTools::ModEntry&)> cb) const {
	if (!choiceOnly)
		return List.ForEachEntryInterruptible(cb);

	for (const auto& entry : List.SimpleModsList)
		if (TraverseCallbackResult::Break == cb(entry))
			return TraverseCallbackResult::Break;

	for (size_t pageObjectIndex = 0; pageObjectIndex < List.ModPackPages.size(); ++pageObjectIndex) {
		const auto& modGroups = List.ModPackPages[pageObjectIndex].ModGroups;
		if (modGroups.empty())
			continue;

		const auto& pageConf = Choices.at(pageObjectIndex);

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
				for (const auto& entry : modGroup.OptionList[optionIndex].ModsJsons)
					if (Sqex::ThirdParty::TexTools::TTMPL::Break == cb(entry))
						return Sqex::ThirdParty::TexTools::TTMPL::Break;
			}
		}
	}
	return Sqex::ThirdParty::TexTools::TTMPL::Continue;
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::TtmpSet::ForEachEntry(bool choiceOnly, std::function<void(const Sqex::ThirdParty::TexTools::ModEntry&)> cb) const {
	if (!choiceOnly)
		return List.ForEachEntry(cb);

	for (const auto& entry : List.SimpleModsList)
		cb(entry);

	for (size_t pageObjectIndex = 0; pageObjectIndex < List.ModPackPages.size(); ++pageObjectIndex) {
		const auto& modGroups = List.ModPackPages[pageObjectIndex].ModGroups;
		if (modGroups.empty())
			continue;

		const auto& pageConf = Choices.at(pageObjectIndex);

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
				for (const auto& entry : modGroup.OptionList[optionIndex].ModsJsons)
					cb(entry);
			}
		}
	}
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::TtmpSet::TryCleanupUnusedFiles() {
	DataFile.Clear();
	for (const auto& path : {
			ListPath.parent_path() / "TTMPD.mpd",
			ListPath.parent_path() / "choices.json",
			ListPath.parent_path() / "disable",
			ListPath.parent_path() / "compression",
			ListPath.parent_path(),
		}) {
		try {
			remove(path);
		} catch (...) {
			// pass
		}
	}
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::AddNewTtmp(const std::filesystem::path& ttmplPath, bool reflectImmediately, Apps::MainApp::Window::ProgressPopupWindow& progressWindow) {
	auto folder = m_pImpl->FindNestedTtmpContainer(ttmplPath, true);
	if (!folder || folder->Find(ttmplPath))  // already exists
		return;

	std::shared_ptr<NestedTtmp> added;
	try {
		added = m_pImpl->AddFromTtmpl(ttmplPath, folder, true, progressWindow);
		folder->Sort();
	} catch (const std::exception& e) {
		m_pImpl->Ttmps->RemoveEmptyChildren();
		m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
			"Failed to load TexTools ModPack from {}: {}", ttmplPath.wstring(), e.what());
		return;
	}

	m_pImpl->CheckTtmpAllocation(*added->Ttmp);

	if (reflectImmediately)
		m_pImpl->ReflectUsedEntries();
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::DeleteTtmp(const std::filesystem::path & ttmpl, bool reflectImmediately) {
	auto folder = m_pImpl->FindNestedTtmpContainer(ttmpl, false);
	if (!folder)
		return;
	auto ttmp = folder->Find(ttmpl);
	if (!ttmp || !ttmp->Ttmp)
		return;
	remove(ttmp->Ttmp->ListPath);
	m_pImpl->Ttmps->RemoveEmptyChildren();
	if (reflectImmediately)
		m_pImpl->ReflectUsedEntries();
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::RescanTtmp(Apps::MainApp::Window::ProgressPopupWindow& progressWindow) {
	for (const auto& dir : m_pImpl->GetPossibleTtmpDirs())
		m_pImpl->RescanTtmpTree(dir, m_pImpl->Ttmps, progressWindow);
	m_pImpl->ReflectUsedEntries();
}

std::shared_ptr<XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp> XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::GetTtmps() const {
	return m_pImpl->Ttmps;
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::ApplyTtmpChanges(NestedTtmp& nestedTtmp, bool announce) {
	return m_pImpl->SaveNestedTtmpConfig(nestedTtmp, announce);
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp::Traverse(bool traverseEnabledOnly, const std::function<void(NestedTtmp&)>& cb) {
	if (traverseEnabledOnly && !Enabled)
		return;
	cb(*this);
	if (Children)
		for (auto& t : *Children)
			t->Traverse(traverseEnabledOnly, cb);
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp::Traverse(bool traverseEnabledOnly, const std::function<void(const NestedTtmp&)>& cb) const {
	if (traverseEnabledOnly && !Enabled)
		return;
	cb(*this);
	if (Children)
		for (const auto& t : *Children)
			const_cast<const NestedTtmp*>(t.get())->Traverse(traverseEnabledOnly, cb);
}

XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp::TraverseCallbackResult XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp::TraverseInterruptible(bool traverseEnabledOnly, const std::function<TraverseCallbackResult(NestedTtmp&)>&cb) {
	if (traverseEnabledOnly && !Enabled)
		return Continue;
	if (const auto res = cb(*this); res != Continue)
		return res;
	if (Children) {
		for (auto it = Children->begin(); it != Children->end();) {
			switch ((*it)->TraverseInterruptible(traverseEnabledOnly, cb)) {
				case Break:
					return Break;
				case Delete:
					it = Children->erase(it);
					break;
				default:
					++it;
			}
		}
		return Continue;
	}
	return Continue;
}

XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp::TraverseCallbackResult XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp::TraverseInterruptible(bool traverseEnabledOnly, const std::function<TraverseCallbackResult(const NestedTtmp&)>&cb) const {
	if (traverseEnabledOnly && !Enabled)
		return Continue;
	if (const auto res = cb(*this); res != Continue)
		return res;
	if (Children) {
		for (const auto& t : *Children) {
			switch (const_cast<const NestedTtmp*>(t.get())->TraverseInterruptible(traverseEnabledOnly, cb)) {
				case Break:
					return Break;
				case Delete:
					throw std::runtime_error("Cannot delete from const");
			}
		}
		return Continue;
	}
	return Continue;
}

size_t XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp::Count() const {
	size_t i = Ttmp.has_value() ? 1 : 0;
	Traverse(false, [&i](const auto&) { ++i; });
	return i;
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp::Sort() {
	std::sort(Children->begin(), Children->end(), [](const auto& l, const auto& r) {
		if (l->Index == r->Index)
			return l->Path.wstring() < r->Path.wstring();
		return l->Index < r->Index;
		});
}

std::shared_ptr<XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp> XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp::Find(const std::filesystem::path & path) {
	if (IsGroup())
		for (auto& child : *Children)
			if (equivalent(child->Path, path))
				return child;
	return nullptr;
}

void XivAlexander::Apps::MainApp::Internal::VirtualSqPacks::NestedTtmp::RemoveEmptyChildren() {
	if (!Children)
		return;
	for (auto it = Children->begin(); it != Children->end();) {
		auto& t = **it;
		t.RemoveEmptyChildren();
		if (!t.Ttmp && (!t.Children || t.Children->empty())) {
			t.Parent = nullptr;
			it = Children->erase(it);
		} else
			++it;
	}
}
