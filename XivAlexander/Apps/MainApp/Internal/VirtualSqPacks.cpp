#include "pch.h"
#include "VirtualSqPacks.h"

#include <XivAlexanderCommon/Sqex/SeString.h>
#include <XivAlexanderCommon/Sqex/Est.h>
#include <XivAlexanderCommon/Sqex/Eqdp.h>
#include <XivAlexanderCommon/Sqex/EqpGmp.h>
#include <XivAlexanderCommon/Sqex/Excel/Generator.h>
#include <XivAlexanderCommon/Sqex/Excel/Reader.h>
#include <XivAlexanderCommon/Sqex/FontCsv/Creator.h>
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
#include "Misc/ExcelTransformConfig.h"
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
		Apps::MainApp::Window::ProgressPopupWindow progressWindow(Dll::FindGameMainWindow(false));
		progressWindow.Show();
		InitializeSqPacks(progressWindow);
		ReflectUsedEntries(true);

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
		for (const auto& entry : SqpackViews.at(SqpackPath / L"ffxiv/070000.win32.index").Entries) {
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
				const auto v = SqpackPath / std::format(L"{}.win32.index", entry.ToExpacDatPath());
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
					renameInfoBuffer.resize(sizeof FILE_RENAME_INFO + newListPath.size());
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
				if (ext != L".index")
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
				+ Config->Runtime.AdditionalSqpackRootDirectories.Value().size() // external sqpack
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
								const auto reader = Sqex::Sound::ScdReader(creator["sound/system/sample_system.scd"]);
								Sqex::Sound::ScdWriter writer;
								writer.SetTable1(reader.ReadTable1Entries());
								writer.SetTable4(reader.ReadTable4Entries());
								writer.SetTable2(reader.ReadTable2Entries());
								for (size_t i = 0; i < 256; ++i) {
									writer.SetSoundEntry(i, Sqex::Sound::ScdWriter::SoundEntry::EmptyEntry());
								}
								EmptyScd = std::make_shared<Sqex::MemoryRandomAccessStream>(
									Sqex::Sqpack::MemoryBinaryEntryProvider("dummy/dummy", std::make_shared<Sqex::MemoryRandomAccessStream>(writer.Export()), Config->Runtime.CompressModdedFiles ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION)
									.ReadStreamIntoVector<uint8_t>(0));
								//EmptyScd = std::make_shared<Sqex::MemoryRandomAccessStream>(
								//	Sqex::Sqpack::EmptyOrObfuscatedEntryProvider("dummy/dummy", std::make_shared<Sqex::MemoryRandomAccessStream>(writer.Export()))
								//	.ReadStreamIntoVector<uint8_t>(0));
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
								}) == NestedTtmp::Break)
								return;

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

			const auto extractedStateFile = ttmpDir / "compression";
			bool currentlyCompressed = false, compressAgain = false;
			if (!exists(extractedStateFile)) {
				compressAgain = true;
			} else {
				std::string s;
				std::ifstream(extractedStateFile) >> s;
				currentlyCompressed = s == "true";

				compressAgain = currentlyCompressed != Config->Runtime.CompressModdedFiles;
			}

			if (compressAgain) {
				currentlyCompressed = Config->Runtime.CompressModdedFiles.Value();

				uint64_t progressMax = 0, progressCurrent = 0;
				list.ForEachEntry([&](const auto& entry) { progressMax += entry.ModSize; });

				const auto workerThread = Utils::Win32::Thread(L"CompressTtmpEntry", [&]() {
					const auto tempTtmplPath = ttmpDir / "TTMPL.mpl.tmp";
					const auto tempTtmpdPath = ttmpDir / "TTMPD.mpd.tmp";

					auto out = Utils::Win32::Handle::FromCreateFile(tempTtmpdPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS);
					uint64_t outPtr = 0;

					std::mutex writeMtx;
					Utils::Win32::TpEnvironment pool(L"CompressTtmpEntry/pool");
					list.ForEachEntry([&](Sqex::ThirdParty::TexTools::ModEntry& entry) {
						pool.SubmitWork([&]() {
							if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
								return;

							Logger->Format(LogCategory::VirtualSqPacks, "Rewriting {}:{}", ttmplPath.wstring(), entry.FullPath);

							const auto pathSpec = Sqex::Sqpack::EntryPathSpec(entry.FullPath);
							std::shared_ptr<Sqex::Sqpack::EntryProvider> stream = std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(pathSpec, dataStream, entry.ModOffset, entry.ModSize);
							auto rawStream = std::make_shared<Sqex::Sqpack::EntryRawStream>(stream);

							switch (rawStream->EntryType()) {
								case Sqex::Sqpack::SqData::FileEntryType::EmptyOrObfuscated:
									if (const auto header = stream->ReadStream<Sqex::Sqpack::SqData::FileEntryHeader>(0);
										header.DecompressedSize != header.BlockCountOrVersion || header.DecompressedSize == 0) {
										break;
									}
									[[fallthrough]];

								case Sqex::Sqpack::SqData::FileEntryType::Binary:
									//if (currentlyCompressed)
										stream = std::make_shared<Sqex::Sqpack::MemoryBinaryEntryProvider>(pathSpec, std::move(rawStream), currentlyCompressed ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION);
									//else
									//	stream = std::make_shared<Sqex::Sqpack::EmptyOrObfuscatedEntryProvider>(pathSpec, rawStream);
									break;

								case Sqex::Sqpack::SqData::FileEntryType::Model:
									stream = std::make_shared<Sqex::Sqpack::MemoryModelEntryProvider>(pathSpec, std::move(rawStream), currentlyCompressed ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION);
									// stream = std::make_shared<Sqex::Sqpack::EmptyOrObfuscatedEntryProvider>(pathSpec, rawStream);
									break;

								case Sqex::Sqpack::SqData::FileEntryType::Texture:
									stream = std::make_shared<Sqex::Sqpack::MemoryTextureEntryProvider>(pathSpec, std::move(rawStream), currentlyCompressed ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION);
									// stream = std::make_shared<Sqex::Sqpack::EmptyOrObfuscatedEntryProvider>(pathSpec, rawStream);
									break;
							}

							if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
								return;

							const auto size = stream->StreamSize();
							progressMax = progressMax + size - entry.ModSize;
							entry.ModSize = size;

							{
								const auto _ = std::lock_guard(writeMtx);
								entry.ModOffset = outPtr;
								outPtr += size;
							}

							std::vector<char> buf(65536);
							Sqex::Align<uint64_t>(size, buf.size()).IterateChunkedBreakable([&](uint64_t, uint64_t offset, uint64_t size) {
								if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
									return false;

								stream->ReadStream(offset, &buf[0], size);
								out.Write(entry.ModOffset + offset, &buf[0], static_cast<size_t>(size));
								progressCurrent += size;

								return true;
								});
							});
						});

					pool.WaitOutstanding();

					nlohmann::json j;
					to_json(j, list);
					Utils::SaveJsonToFile(tempTtmplPath, j);
					out = {};
					dataFile = {};

					const auto oldTtmplPath = ttmpDir / "TTMPL.mpl.old";
					const auto oldTtmpdPath = ttmpDir / "TTMPD.mpd.old";
					if (exists(oldTtmplPath))
						remove(oldTtmplPath);
					if (exists(oldTtmpdPath))
						remove(oldTtmpdPath);
					rename(ttmplPath, oldTtmplPath);
					try {
						rename(ttmpdPath, oldTtmpdPath);
						try {
							rename(tempTtmplPath, ttmplPath);
							rename(tempTtmpdPath, ttmpdPath);
							remove(oldTtmplPath);
							remove(oldTtmpdPath);
						} catch (...) {
							if (exists(ttmpdPath))
								remove(ttmpdPath);
							rename(oldTtmpdPath, ttmpdPath);
							throw;
						}
					} catch (...) {
						if (exists(ttmplPath))
							remove(ttmplPath);
						rename(oldTtmplPath, ttmplPath);
						throw;
					}
					});

				do {
					progressWindow.UpdateMessage(Config->Runtime.GetStringRes(IDS_TITLE_DISCOVERINGFILES));
					progressWindow.UpdateProgress(progressCurrent, progressMax);
					progressWindow.Show();
				} while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, { workerThread }));

				std::ofstream(extractedStateFile) << (currentlyCompressed ? "true" : "false");
				dataFile = Utils::Win32::Handle::FromCreateFile(ttmpdPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
			}

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
			const auto it = SqpackViews.find(SqpackPath / std::format(L"{}.win32.index", entry.ToExpacDatPath()));
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

	void SetUpMergedExd(Apps::MainApp::Window::ProgressPopupWindow& progressWindow, Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexFile) {
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

				std::string currentCacheKeys("VERSION:4\n");
				currentCacheKeys += Config->Runtime.CompressModdedFiles ? "compress:true\n" : "compress:false\n";
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

					std::vector<std::pair<srell::u8cregex, std::map<Sqex::Language, std::vector<size_t>>>> columnMaps;
					std::vector<std::pair<srell::u8cregex, Misc::ExcelTransformConfig::PluralColumns>> pluralColumns;
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
					std::map<Sqex::Language, std::set<Misc::ExcelTransformConfig::IgnoredCell>> ignoredCells;
					std::map<std::string, std::pair<srell::u8cregex, std::string>> columnReplacementTemplates;
					auto replacementFileParseFail = false;
					for (const auto& configFile : Config->Runtime.ExcelTransformConfigFiles.Value()) {
						if (configFile.empty())
							continue;

						try {
							Misc::ExcelTransformConfig::Config transformConfig;
							from_json(Utils::ParseJsonFromFile(Config->TranslatePath(configFile)), transformConfig);

							for (const auto& entry : transformConfig.columnMap) {
								columnMaps.emplace_back(srell::u8cregex(entry.first, srell::regex_constants::ECMAScript | srell::regex_constants::icase), entry.second);
							}
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
					Utils::Win32::TpEnvironment pool(L"SetUpMergedExd");
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

											exCreator = std::make_unique<Sqex::Excel::Depth2ExhExdCreator>(exhName, *exhReaderSource.Columns, exhReaderSource.Header.Flags.Value());
											exCreator->FillMissingLanguageFrom = fallbackLanguageList;
											exCreator->AddLanguage(Sqex::Language::Japanese);
											exCreator->AddLanguage(Sqex::Language::English);
											exCreator->AddLanguage(Sqex::Language::German);
											exCreator->AddLanguage(Sqex::Language::French);
											exCreator->AddLanguage(Sqex::Language::ChineseSimplified);
											exCreator->AddLanguage(Sqex::Language::Korean);

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
											for (const auto& [pattern, data] : pluralColumns) {
												if (srell::regex_search(exhName, pattern))
													return data;
											}
											return Misc::ExcelTransformConfig::PluralColumns();
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
										const auto columnMap = [&]() -> std::map<Sqex::Language, std::vector<size_t>> {
											std::map<Sqex::Language, std::vector<size_t>> res;
											for (const auto& [pattern, data] : columnMaps) {
												if (!srell::regex_search(exhName, pattern))
													continue;
												for (const auto& [language, data2] : data)
													res[language] = data2;
											}
											return res;
										}();
										const auto translateColumnIndex = [&](Sqex::Language fromLanguage, Sqex::Language toLanguage, size_t fromIndex) -> size_t {
											const auto fromIter = columnMap.find(fromLanguage);
											const auto toIter = columnMap.find(toLanguage);
											if (fromIter == columnMap.end() || toIter == columnMap.end())
												return fromIndex;

											const auto it = std::lower_bound(fromIter->second.begin(), fromIter->second.end(), fromIndex);
											if (it == fromIter->second.end() || *it != fromIndex)
												return fromIndex;
											return toIter->second.at(it - fromIter->second.begin());
										};

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
															Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
																"[{}] Adding {}", exhName, exdPathSpec);
															const auto exdReader = Sqex::Excel::ExdReader(exhReaderCurrent, (*reader)[exdPathSpec]);
															exCreator->AddLanguage(language);
															for (const auto i : exdReader.GetIds()) {
																auto row = exdReader.ReadDepth2(i);
																const auto& rowSet = exCreator->Data.at(i);
																auto referenceRowLanguage = Sqex::Language::Unspecified;
																const std::vector<Sqex::Excel::ExdColumn>* referenceRowPtr = nullptr;
																for (const auto& l : exCreator->FillMissingLanguageFrom) {
																	if (auto it = rowSet.find(l);
																		it != rowSet.end()) {
																		referenceRowLanguage = l;
																		referenceRowPtr = &it->second;
																		break;
																	}
																}
																if (!referenceRowPtr)
																	continue;
																const auto& referenceRow = *referenceRowPtr;

																auto prevRow{ std::move(row) };
																row = referenceRow;
																for (size_t j = 0; j < row.size(); ++j) {
																	if (row[j].Type != Sqex::Excel::Exh::ColumnDataType::String)
																		continue;
																	const auto otherColIndex = translateColumnIndex(referenceRowLanguage, language, j);
																	if (otherColIndex >= prevRow.size()) {
																		if (otherColIndex != j)
																			Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
																				"[{}] Skipping column: Column {} of language {} is was requested but there are {} columns",
																				exhName, j, otherColIndex, static_cast<int>(language), prevRow.size());
																		continue;
																	}
																	if (prevRow[otherColIndex].Type != Sqex::Excel::Exh::ColumnDataType::String) {
																		Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
																			"[{}] Skipping column: Column {} of language {} is string but column {} of language {} is not a string",
																			exhName, j, static_cast<int>(referenceRowLanguage), otherColIndex, static_cast<int>(language));
																		continue;
																	}
																	if (prevRow[otherColIndex].String.Empty())
																		continue;
																	if (prevRow[otherColIndex].String.Escaped().starts_with("_rsv_"))
																		continue;
																	row[j].String = std::move(prevRow[otherColIndex].String);
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
											auto referenceRowLanguage = Sqex::Language::Unspecified;
											for (const auto& l : exCreator->FillMissingLanguageFrom) {
												if (auto it = rowSet.find(l);
													it != rowSet.end()) {
													referenceRowPtr = &it->second;
													referenceRowLanguage = l;
													break;
												}
											}
											if (!referenceRowPtr)
												continue;
											const auto& referenceRow = *referenceRowPtr;

											lastStep = "Fill missing rows for languages that aren't from source, and restore columns if unmodifiable";
											std::set<size_t> referenceRowUsedColumnIndices;
											for (const auto& language : exCreator->Languages) {
												if (auto it = rowSet.find(language);
													it == rowSet.end())
													rowSet[language] = referenceRow;
												else {
													auto& row = it->second;
													for (size_t i = 0; i < row.size(); ++i) {
														if (referenceRow[i].Type != Sqex::Excel::Exh::String) {
															row[i] = referenceRow[i];
															referenceRowUsedColumnIndices.insert(i);
														} else {
															if (row[i].String.Empty()) {
																// apply only if made of incompatible languages

																int sourceLanguageType = 0, referenceLanguageType = 0;
																switch (language) {
																	case Sqex::Language::Japanese:
																	case Sqex::Language::English:
																	case Sqex::Language::German:
																	case Sqex::Language::French:
																		sourceLanguageType = 1;
																		break;
																	case Sqex::Language::ChineseSimplified:
																		sourceLanguageType = 2;
																		break;
																	case Sqex::Language::Korean:
																		sourceLanguageType = 3;
																}

																switch (referenceRowLanguage) {
																	case Sqex::Language::Japanese:
																	case Sqex::Language::English:
																	case Sqex::Language::German:
																	case Sqex::Language::French:
																		referenceLanguageType = 1;
																		break;
																	case Sqex::Language::ChineseSimplified:
																		referenceLanguageType = 2;
																		break;
																	case Sqex::Language::Korean:
																		referenceLanguageType = 3;
																}

																if (sourceLanguageType != referenceLanguageType) {
																	row[i] = referenceRow[i];
																	referenceRowUsedColumnIndices.insert(i);
																}
															}
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

												std::set<Misc::ExcelTransformConfig::IgnoredCell>* currentIgnoredCells = nullptr;
												if (const auto it = ignoredCells.find(language); it != ignoredCells.end())
													currentIgnoredCells = &it->second;

												auto row{ rowSet.at(language) };

												for (size_t columnIndex = 0; columnIndex < row.size(); columnIndex++) {
													if (row[columnIndex].Type != Sqex::Excel::Exh::String)
														continue;

													if (referenceRowUsedColumnIndices.find(columnIndex) != referenceRowUsedColumnIndices.end())
														continue;

													if (currentIgnoredCells) {
														if (const auto it = currentIgnoredCells->find(Misc::ExcelTransformConfig::IgnoredCell{ exhName, static_cast<int>(id), static_cast<int>(columnIndex) });
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

														std::vector p = { std::format("{}:{}", exhName, id) };
														for (const auto ruleSourceLanguage : rule.sourceLanguage) {
															if (const auto it = rowSet.find(ruleSourceLanguage);
																it != rowSet.end()) {

																if (row[columnIndex].Type != Sqex::Excel::Exh::String)
																	throw std::invalid_argument(std::format("Column {} of sourceLanguage {} in {} is not a string column", columnIndex, static_cast<int>(ruleSourceLanguage), exhName));

																auto readColumnIndex = columnIndex;
																bool normalizeToCapital = false;
																switch (ruleSourceLanguage) {
																	case Sqex::Language::English:
																	case Sqex::Language::German:
																	case Sqex::Language::French:
																		switch (language) {
																			case Sqex::Language::Japanese:
																			case Sqex::Language::ChineseSimplified:
																			case Sqex::Language::ChineseTraditional:
																			case Sqex::Language::Korean:
																				normalizeToCapital = true;
																		}
																		break;

																	case Sqex::Language::Japanese:
																	case Sqex::Language::ChineseSimplified:
																	case Sqex::Language::ChineseTraditional:
																	case Sqex::Language::Korean: {
																		normalizeToCapital = true;
																		break;
																	}
																}
																if (normalizeToCapital) {
																	if (pluralColummIndices.capitalizedColumnIndex != Misc::ExcelTransformConfig::PluralColumns::Index_NoColumn) {
																		if (readColumnIndex == pluralColummIndices.pluralColumnIndex
																			|| readColumnIndex == pluralColummIndices.singularColumnIndex)
																			readColumnIndex = pluralColummIndices.capitalizedColumnIndex;
																	} else {
																		if (readColumnIndex == pluralColummIndices.pluralColumnIndex)
																			readColumnIndex = pluralColummIndices.singularColumnIndex;
																	}
																}
																if (const auto rules = rule.preprocessReplacements.find(ruleSourceLanguage); rules != rule.preprocessReplacements.end()) {
																	Sqex::SeString escaped(it->second[readColumnIndex].String);
																	escaped.NewlineAsCarriageReturn(true);
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
														while (p.size() < 16)
															p.emplace_back();

														auto allSame = true;
														size_t nonEmptySize = 0;
														size_t lastNonEmptyIndex = 1;
														for (size_t i = 1; i < p.size(); ++i) {
															if (!p[i].empty()) {
																if (p[i] != p[1])
																	allSame = false;
																nonEmptySize++;
																lastNonEmptyIndex = i;
															}
														}
														std::string out;
														if (allSame)
															out = p[1];
														else if (nonEmptySize <= 1)
															out = p[lastNonEmptyIndex];
														else
															out = std::format(rule.replaceTo, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);

														Sqex::SeString escaped(out);
														escaped.NewlineAsCarriageReturn(true);
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

												std::unique_ptr<Sqex::Sqpack::EntryProvider> provider;
												//if (Config->Runtime.CompressModdedFiles)
													provider = std::make_unique<Sqex::Sqpack::MemoryBinaryEntryProvider>(entryPathSpec, std::make_shared<Sqex::MemoryRandomAccessStream>(std::move(*reinterpret_cast<std::vector<uint8_t>*>(&data))), Config->Runtime.CompressModdedFiles ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION);
												//else
												//	provider = std::make_unique<Sqex::Sqpack::EmptyOrObfuscatedEntryProvider>(entryPathSpec, std::make_shared<Sqex::MemoryRandomAccessStream>(std::move(*reinterpret_cast<std::vector<uint8_t>*>(&data))));
												const auto len = provider->StreamSize();
												const auto dv = provider->ReadStreamIntoVector<char>(0, static_cast<SSIZE_T>(len));

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
						while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, { compressThread })) {
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

	void SetUpGeneratedFonts(Apps::MainApp::Window::ProgressPopupWindow& progressWindow, Sqex::Sqpack::Creator& creator, const std::filesystem::path& indexPath) {
		while (true) {
			const auto fontConfigPath{ Config->Runtime.OverrideFontConfig.Value() };
			if (fontConfigPath.empty())
				return;

			try {
				const auto cachedDir = Config->Init.ResolveConfigStorageDirectoryPath() / "Cached" / GameReleaseInfo.CountryCode / "Font" / fontConfigPath.filename().replace_extension("");

				std::string currentCacheKeys("VERSION:2\n");
				{
					const auto gameRoot = indexPath.parent_path().parent_path().parent_path();
					const auto versionFile = Utils::Win32::Handle::FromCreateFile(gameRoot / "ffxivgame.ver", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
					const auto versionContent = versionFile.Read<char>(0, static_cast<size_t>(versionFile.GetFileSize()));
					currentCacheKeys += std::format("SQPACK:{}:{}:{}\n",
						canonical(gameRoot).wstring(),
						Config->Runtime.CompressModdedFiles ? "compress" : "nocompress",
						std::string(versionContent.begin(), versionContent.end()));
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

					while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, { fontCreator.GetWaitableObject() })) {
						const auto progress = fontCreator.GetProgress();
						progressWindow.UpdateProgress(progress.Progress, progress.Max);

						if (progress.Indeterminate)
							progressWindow.UpdateMessage(std::format("{} (+{})", Config->Runtime.GetStringRes(IDS_TITLE_GENERATING_FONTS), progress.Indeterminate));
						else
							progressWindow.UpdateMessage(Utils::ToUtf8(Config->Runtime.GetStringRes(IDS_TITLE_GENERATING_FONTS)));
					}
					if (progressWindow.GetCancelEvent().Wait(0) != WAIT_OBJECT_0) {
						progressWindow.UpdateMessage(Utils::ToUtf8(Config->Runtime.GetStringRes(IDS_TITLE_COMPRESSING)));
						Utils::Win32::TpEnvironment pool(L"SetUpGeneratedFonts");
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

									std::unique_ptr<Sqex::Sqpack::EntryProvider> provider;
									auto extension = entryPathSpec.FullPath.extension().wstring();
									CharLowerW(&extension[0]);

									if (extension == L".tex") {
										provider = std::make_unique<Sqex::Sqpack::MemoryTextureEntryProvider>(entryPathSpec, stream, Config->Runtime.CompressModdedFiles ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION);
										// provider = std::make_unique<Sqex::Sqpack::EmptyOrObfuscatedEntryProvider>(entryPathSpec, static_cast<uint32_t>(stream->StreamSize()), stream);
									} else {
										//if (Config->Runtime.CompressModdedFiles)
											provider = std::make_unique<Sqex::Sqpack::MemoryBinaryEntryProvider>(entryPathSpec, stream, Config->Runtime.CompressModdedFiles ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION);
										//else
										//	provider = std::make_unique<Sqex::Sqpack::EmptyOrObfuscatedEntryProvider>(entryPathSpec, stream);
									}

									const auto len = provider->StreamSize();
									const auto dv = provider->ReadStreamIntoVector<char>(0, static_cast<SSIZE_T>(len));
									progress += stream->StreamSize();

									if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
										return;
									const auto lock = std::lock_guard(writeMtx);
									const auto entryLine = std::format("{}\n", nlohmann::json::object({
										{"FullPath", Utils::StringReplaceAll<std::string>(Utils::ToUtf8(entryPathSpec.FullPath.wstring()), "\\", "/")},
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

						while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, { compressThread })) {
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

	void SetupGeneratedFonts_VerifyRequirements(Sqex::FontCsv::FontSetsCreator& fontCreator, Apps::MainApp::Window::ProgressPopupWindow& progressWindow) {
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
					for (const auto& [langId, localeName] : Config->Runtime.GetDisplayLanguagePriorities()) {
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
					for (const auto& [langId, localeName] : Config->Runtime.GetDisplayLanguagePriorities()) {
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
				if (!requirement.homepage.empty()) {
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
