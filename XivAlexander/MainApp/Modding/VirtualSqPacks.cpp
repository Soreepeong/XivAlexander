#include "pch.h"
#include "MainApp/Modding/VirtualSqPacks.h"

#include "MainApp/Modding/FutureReservations.h"
#include "MainApp/Modding/GamePause.h"
#include "MainApp/Modding/SqpackRebuildLock.h"
#include "MainApp/Modding/PackQueue.h"
#include "MainApp/Modding/PackSources.h"
#include "MainApp/Modding/TtmpLibrary.h"

#include <xivres/ex_skeleton_table.h>
#include <xivres/equipment_deformer_parameter.h>
#include <xivres/equipment_and_gimmick_parameter.h>
#include <xivres/image_change_data.h>
#include <xivres/packed_stream.standard.h>
#include <xivres/sqpack.generator.h>
#include <xivres/packed_stream.h>
#include <xivres/unpacked_stream.h>
#include <xivres/textools.h>

#include "MainApp/Windows/ProgressPopupWindow.h"
#include "MainApp/App.h"
#include "Config.h"
#include "Misc/Logger.h"
#include "resource.h"
#include "XivAlexander.h"

struct XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::Implementation {
	App& App;
	VirtualSqPacks& Sqpacks;
	const std::shared_ptr<Config> Config;
	const std::shared_ptr<Misc::Logger> Logger;
	const std::filesystem::path SqpackPath;

	static constexpr int PathTypeIndex = -1;
	static constexpr int PathTypeIndex2 = -2;
	static constexpr int PathTypeInvalid = -3;

	static constexpr size_t BuilderCount = 4;

	SqpackRebuildLock& RebuildLock;
	PackQueue Queue;
	PackSources Sources;
	TtmpLibrary Library;
	TtmpLibrary::Reservations PackReservations;
	FutureReservations Reservations;

	std::shared_ptr<xivres::sqpack::generator::sqpack_view_entry_cache> DataViewBuffer;
	std::shared_ptr<const xivres::stream> EmptyScd;

	xivres::util::on_dtor::multi Cleanup;

	Implementation(MainApp::App& app, VirtualSqPacks* sqpacks, std::filesystem::path sqpackPath, SqpackRebuildLock& ioGate)
		: App(app)
		, Sqpacks(*sqpacks)
		, Config(Config::Acquire())
		, Logger(Misc::Logger::Acquire())
		, SqpackPath(std::move(sqpackPath))
		, RebuildLock(ioGate)
		, Queue(SqpackPath)
		, Library(SqpackPath) {
		const auto actCtx = Dll::ActivationContext().With();
		{
			Window::ProgressPopupWindow progressWindow(Dll::FindGameMainWindow(false));
			progressWindow.Show(std::chrono::milliseconds(5000));
			progressWindow.UpdateMessage(xivres::util::unicode::convert<std::string>(Config->Runtime.GetStringRes(IDS_TITLE_DISCOVERINGFILES)));

			Queue.Discover(progressWindow);
			Library.Scan(progressWindow);

			// Read every modpack's data here, before the builders start, as they all want the same reservations.
			PackReservations = Library.CollectReservations(progressWindow);
		}

		DataViewBuffer = std::make_shared<xivres::sqpack::generator::sqpack_view_entry_cache>();
		Queue.Start(BuilderCount, [this](Pack& pack) { BuildPack(pack); });

		Cleanup += Config->Runtime.MuteVoice_Battle.OnChange([this] { ReflectUsedEntries(); });
		Cleanup += Config->Runtime.MuteVoice_Cm.OnChange([this] { ReflectUsedEntries(); });
		Cleanup += Config->Runtime.MuteVoice_Emote.OnChange([this] { ReflectUsedEntries(); });
		Cleanup += Config->Runtime.MuteVoice_Line.OnChange([this] { ReflectUsedEntries(); });
		Cleanup += Config->Runtime.TtmpChoicesFiles.OnChange([this] {
			{
				const auto lock = Queue.Lock();
				Library.ReloadChoices();
			}
			ReflectUsedEntries();
		});
	}

	~Implementation() {
		Queue.Stop();
		Cleanup.clear();
	}

	/// Builds \p pack from its sources, then applies the current mods to it.
	void BuildPack(Pack& pack) {
		auto& creator = *pack.Creator;
		if (auto emptyScd = Sources.Populate(creator, pack.IndexPath, PackReservations, Reservations))
			EmptyScd = std::move(emptyScd);

		pack.Views.emplace(creator.export_to_views(false, DataViewBuffer, creator.DatName.starts_with("0c")));
		pack.Creator.reset();

		ApplyToPacks({&pack}, false);
	}

	/// \returns Where \p pathSpec is in the game's own files. Waits for its pack to be built, except while mods are
	///          being applied, when only packs already built are looked in.
	std::shared_ptr<xivres::stream> GetOriginalEntry(const xivres::path_spec& pathSpec) const {
		const auto pack = Queue.Find(pathSpec);
		const auto views = pack ? Queue.EnsureBuilt(*pack) : nullptr;
		if (!views)
			throw std::out_of_range("entry not found");

		auto it = views->HashOnlyEntries.find(pathSpec);
		if (it == views->HashOnlyEntries.end()) {
			it = views->FullPathEntries.find(pathSpec);
			if (it == views->FullPathEntries.end())
				throw std::out_of_range("entry not found");
		}

		return std::make_shared<xivres::unpacked_stream>(it->second.base_stream());
	}

	struct ReflectUsedEntriesTempData {
		std::map<xivres::path_spec, std::tuple<xivres::sqpack::generator::entry_info*, std::shared_ptr<const xivres::packed_stream>, std::string>, xivres::path_spec::AllHashComparator> Replacements;
		std::map<std::string, xivres::ex_skeleton_table_file> Est;
		std::optional<xivres::equipment_and_gimmick_parameter_file> Eqp;
		std::optional<xivres::equipment_and_gimmick_parameter_file> Gmp;
		std::map<std::string, xivres::image_change_data::file> Imc;
		std::map<std::pair<xivres::textools::metafile::item_types, uint32_t>, xivres::equipment_deformer_parameter_file> Eqdp;
		FutureReservations::Streams Unplaced;
	};

	void ReflectUsedEntries() {
		const auto pause = GamePause(App);

		// Step. Wait until ReadFile stops, and keep it stopped until done
		const auto resumeIo = RebuildLock.Hold();

		const auto lock = PackQueue::ApplyLock(Queue);
		ApplyToPacks(Queue.Built(), true);

		// Step. Flush caches if any
		if (DataViewBuffer)
			DataViewBuffer->flush();

		Sqpacks.OnTtmpSetsChanged();
	}

	void ApplyToPacks(const std::set<const Pack*>& packs, bool reconcileTtmpFiles) {
		const auto lock = PackQueue::ApplyLock(Queue);
		ReflectUsedEntriesTempData tempData;

		const auto packOf = [this, &packs](const xivres::path_spec& pathSpec) -> const Pack* {
			const auto pack = Queue.Find(pathSpec);
			return pack && packs.contains(pack) ? pack : nullptr;
		};

		// Step. Find voices to enable or disable
		if (const auto soundPack = Queue.Find(SqpackPath / L"ffxiv/070000.win32.index2"); soundPack && packs.contains(soundPack) && soundPack->Views) {
			const auto voBattle = xivres::path_spec::hash("sound/voice/vo_battle");
			const auto voCm = xivres::path_spec::hash("sound/voice/vo_cm");
			const auto voEmote = xivres::path_spec::hash("sound/voice/vo_emote");
			const auto voLine = xivres::path_spec::hash("sound/voice/vo_line");
			for (const auto& entry : soundPack->Views->Entries) {
				const auto provider = entry;
				const auto& pathSpec = provider->path_spec();
				if (pathSpec.path_hash() == voBattle || pathSpec.path_hash() == voCm || pathSpec.path_hash() == voEmote || pathSpec.path_hash() == voLine)
					tempData.Replacements.insert_or_assign(pathSpec, std::make_tuple(provider, std::shared_ptr<xivres::stream_as_packed_stream>(), std::string()));

				if (pathSpec.path_hash() == voBattle && Config->Runtime.MuteVoice_Battle)
					std::get<1>(tempData.Replacements.at(pathSpec)) = std::make_shared<xivres::stream_as_packed_stream>(pathSpec, EmptyScd);
				if (pathSpec.path_hash() == voCm && Config->Runtime.MuteVoice_Cm)
					std::get<1>(tempData.Replacements.at(pathSpec)) = std::make_shared<xivres::stream_as_packed_stream>(pathSpec, EmptyScd);
				if (pathSpec.path_hash() == voEmote && Config->Runtime.MuteVoice_Emote)
					std::get<1>(tempData.Replacements.at(pathSpec)) = std::make_shared<xivres::stream_as_packed_stream>(pathSpec, EmptyScd);
				if (pathSpec.path_hash() == voLine && Config->Runtime.MuteVoice_Line)
					std::get<1>(tempData.Replacements.at(pathSpec)) = std::make_shared<xivres::stream_as_packed_stream>(pathSpec, EmptyScd);
			}
		}

		Library.Root()->Traverse(false, [&](NestedTtmp& nestedTtmp) {
			if (!nestedTtmp.Ttmp)
				return;
			TtmpSet& ttmp = *nestedTtmp.Ttmp;

			// Step. Find placeholders to adjust
			ttmp.ForEachEntryInterruptible(false, [&](const auto& entry) {
				const auto entryPathSpec = xivres::path_spec(entry.FullPath);
				const auto pack = packOf(entryPathSpec);
				if (!pack) {
					if (!Queue.Find(entryPathSpec))
						Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "Failed to find the sqpack file {} belongs to", entry.FullPath);
					return true;
				}
				auto& view = const_cast<xivres::sqpack::generator::sqpack_views&>(*pack->Views);

				if (!entry.is_textools_metadata()) {
					ReflectUsedEntries_FindPlaceholders(view, tempData, entry.FullPath);
				} else {
					const auto& ttmpd = ttmp.DataStream;
					const auto metadata = xivres::textools::metafile(entry.FullPath, xivres::unpacked_stream(std::make_shared<xivres::stream_as_packed_stream>(entry.FullPath, std::shared_ptr<const xivres::stream>(ttmpd->substream(entry.ModOffset, entry.ModSize)))));
					ReflectUsedEntries_FindPlaceholders(view, tempData, metadata.TargetImcPath);
					ReflectUsedEntries_FindPlaceholders(view, tempData, xivres::textools::metafile::EqpPath);
					ReflectUsedEntries_FindPlaceholders(view, tempData, xivres::textools::metafile::GmpPath);
					if (const auto estPath = xivres::textools::metafile::ex_skeleton_table_path(metadata.EstType))
						ReflectUsedEntries_FindPlaceholders(view, tempData, estPath);
					if (const auto eqdpedit = metadata.get_span<xivres::textools::metafile::equipment_deformer_parameter_entry>(xivres::textools::metafile::meta_types::Eqdp); !eqdpedit.empty()) {
						for (const auto& v : eqdpedit) {
							ReflectUsedEntries_FindPlaceholders(view, tempData, xivres::textools::metafile::equipment_deformer_parameter_path(metadata.ItemType, v.RaceCode));
						}
					}
				}

				return true;
			});
		});

		// Step. Let go of modpacks no longer there, once what they replaced is known to be put back
		if (reconcileTtmpFiles)
			Library.ReconcileFiles();

		// Step. Set new replacements
		Library.Root()->Traverse(true, [&](NestedTtmp& nestedTtmp) {
			if (nestedTtmp.Ttmp && nestedTtmp.Ttmp->Allocated) {
				nestedTtmp.Ttmp->ForEachEntry(true, [&](const auto& entry) {
					if (packOf(xivres::path_spec(entry.FullPath)))
						ReflectUsedEntries_SetReplacementsFromTtmpEntry(tempData, *nestedTtmp.Ttmp, entry);
				});
			}
		});

		// Step. Replace metadata files
		for (const auto& [path, data] : tempData.Est)
			ReflectUsedEntries_SetFromBuffer(tempData, path, data.data());
		if (tempData.Eqp)
			ReflectUsedEntries_SetFromBuffer(tempData, xivres::textools::metafile::EqpPath, tempData.Eqp->data_bytes());
		if (tempData.Gmp)
			ReflectUsedEntries_SetFromBuffer(tempData, xivres::textools::metafile::GmpPath, tempData.Gmp->data_bytes());
		for (const auto& [path, data] : tempData.Imc)
			ReflectUsedEntries_SetFromBuffer(tempData, path, data.data());
		for (const auto& [eqdpKey, data] : tempData.Eqdp)
			ReflectUsedEntries_SetFromBuffer(tempData, xivres::textools::metafile::equipment_deformer_parameter_path(eqdpKey.first, eqdpKey.second), data.data());

		// Step. Apply replacements
		for (const auto& pathSpec : tempData.Replacements | std::views::keys) {
			auto& [place, newEntry, description] = tempData.Replacements.at(pathSpec);
			if (!description.empty()) {
				if (newEntry)
					Logger->Format(LogCategory::VirtualSqPacks, "{}: {}", description, pathSpec);
				else
					Logger->Format(LogCategory::VirtualSqPacks, "Reset: {}", pathSpec);
			}
			// Too large for what the view reserved, so it has to be answered from elsewhere.
			if (newEntry && newEntry->size() > place->entry_size()) {
				tempData.Unplaced.insert_or_assign(pathSpec, newEntry);
				place->swap_stream(nullptr);
				continue;
			}

			place->swap_stream(std::move(newEntry));
		}

		std::set<std::string> packKeys;
		for (const auto pack : packs)
			packKeys.emplace(pack->Key);
		Reservations.SetUnplaced(packKeys, std::move(tempData.Unplaced));
	}

	void ReflectUsedEntries_FindPlaceholders(
		xivres::sqpack::generator::sqpack_views& view,
		ReflectUsedEntriesTempData& tempData,
		const xivres::path_spec& pathSpec
	) {
		auto entryIt = view.HashOnlyEntries.find(pathSpec);
		if (entryIt == view.HashOnlyEntries.end()) {
			entryIt = view.FullPathEntries.find(pathSpec);
			if (entryIt == view.FullPathEntries.end())
				return;
		}

		const auto provider = &entryIt->second;
		provider->update_path_spec(pathSpec);
		tempData.Replacements.insert_or_assign(pathSpec, std::make_tuple(provider, std::shared_ptr<xivres::packed_stream>(), std::string()));
	}

	void ReflectUsedEntries_SetReplacementsFromTtmpEntry(
		ReflectUsedEntriesTempData& tempData,
		TtmpSet& ttmp,
		const xivres::textools::mods_json& entry
	) {
		if (entry.is_textools_metadata()) {
			const auto& ttmpd = ttmp.DataStream;
			const auto metadata = xivres::textools::metafile(entry.FullPath, xivres::unpacked_stream(std::make_shared<xivres::stream_as_packed_stream>(entry.FullPath, std::shared_ptr<const xivres::stream>(ttmpd->substream(entry.ModOffset, entry.ModSize)))));
			metadata.apply_image_change_data_edits([&]() -> xivres::image_change_data::file& {
				const auto imcPath = metadata.TargetImcPath;
				if (const auto it = tempData.Imc.find(imcPath); it == tempData.Imc.end())
					return tempData.Imc[imcPath] = xivres::image_change_data::file(*GetOriginalEntry(metadata.SourceImcPath));
				else
					return it->second;
			});
			metadata.apply_equipment_deformer_parameter_edits([&](auto type, auto race) -> xivres::equipment_deformer_parameter_file& {
				const auto key = std::make_pair(type, race);
				if (const auto it = tempData.Eqdp.find(key); it == tempData.Eqdp.end()) {
					auto& eqdp = tempData.Eqdp[key] = xivres::equipment_deformer_parameter_file(*GetOriginalEntry(xivres::textools::metafile::equipment_deformer_parameter_path(type, race)));
					eqdp.expand_or_collapse(true);
					return eqdp;
				} else
					return it->second;
			});
			if (metadata.has_equipment_parameter_edits()) {
				if (!tempData.Eqp)
					tempData.Eqp = xivres::equipment_and_gimmick_parameter_file(*GetOriginalEntry(xivres::textools::metafile::EqpPath)).expand_or_collapse(true);
				metadata.apply_equipment_parameter_edits(*tempData.Eqp);
			}
			if (metadata.has_gimmick_parameter_edits()) {
				if (!tempData.Gmp)
					tempData.Gmp = xivres::equipment_and_gimmick_parameter_file(*GetOriginalEntry(xivres::textools::metafile::GmpPath)).expand_or_collapse(true);
				metadata.apply_gimmick_parameter_edits(*tempData.Gmp);
			}

			const auto estPath = xivres::textools::metafile::ex_skeleton_table_path(metadata.EstType);
			if (estPath) {
				if (const auto it = tempData.Est.find(estPath); it == tempData.Est.end())
					metadata.apply_ex_skeleton_table_edits(tempData.Est[estPath] = xivres::ex_skeleton_table_file(*GetOriginalEntry(estPath)));
				else
					metadata.apply_ex_skeleton_table_edits(it->second);
			}
		} else {
			auto packed = std::make_shared<xivres::stream_as_packed_stream>(
				entry.FullPath,
				std::shared_ptr<const xivres::stream>(ttmp.DataStream->substream(entry.ModOffset, entry.ModSize))
			);

			// The index never had this path, so there is no entry to sit over and only a stand-in can
			// answer for it. Adding files this way is what a modpack does with its own shader or ui.
			const auto entryIt = tempData.Replacements.find(entry.FullPath);
			if (entryIt == tempData.Replacements.end()) {
				tempData.Unplaced.insert_or_assign(entry.FullPath, std::move(packed));
				return;
			}

			std::get<1>(entryIt->second) = std::move(packed);
			std::get<2>(entryIt->second) = ttmp.List.Name;
		}
	}

	void ReflectUsedEntries_SetFromBuffer(
		ReflectUsedEntriesTempData& tempData,
		const std::string& path,
		const std::vector<uint8_t>& data
	) {
		const auto pathSpec = xivres::path_spec(path);

		const auto entryIt = tempData.Replacements.find(pathSpec);
		if (entryIt == tempData.Replacements.end())
			return;

		std::get<1>(entryIt->second) = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(path, std::make_shared<xivres::memory_stream>(data));
		// std::get<1>(entryIt->second) = std::make_shared<xivres::placeholder_packed_stream>(path, std::make_shared<xivres::memory_stream>(data));
		std::get<2>(entryIt->second) = "Metadata";
	}

	void CheckTtmpAllocation(TtmpSet& item) {
		item.Allocated = item.ForEachEntryInterruptible(false, [&](const auto& entry) {
			const auto pathSpec = xivres::path_spec(entry.FullPath);
			const auto pack = Queue.Find(pathSpec);
			const auto views = pack ? Queue.EnsureBuilt(*pack) : nullptr;
			if (!views)
				return false;

			auto entryIt = views->HashOnlyEntries.find(pathSpec);
			if (entryIt == views->HashOnlyEntries.end()) {
				entryIt = views->FullPathEntries.find(pathSpec);
				if (entryIt == views->FullPathEntries.end()) {
					return false;
				}
			}

			return entryIt->second.size() >= entry.ModSize;
		});
	}
};

XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::VirtualSqPacks(App& app, std::filesystem::path sqpackPath, SqpackRebuildLock& ioGate)
	: m_pImpl(std::make_unique<Implementation>(app, this, std::move(sqpackPath), ioGate)) {}

XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::~VirtualSqPacks() = default;

std::shared_ptr<xivres::stream> XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::OpenStream(const std::filesystem::path& path) {
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

		std::shared_ptr<xivres::stream> stream;
		for (auto& [packIndexPath, pack] : m_pImpl->Queue.All()) {
			if ((exists(indexFile) && equivalent(packIndexPath, indexFile))
				|| (exists(index2File) && equivalent(packIndexPath, index2File))) {
				const auto views = m_pImpl->Queue.EnsureBuilt(pack);
				if (!views)
					break;

				switch (pathType) {
					case Implementation::PathTypeIndex:
						stream = views->Index1;
						break;

					case Implementation::PathTypeIndex2:
						stream = views->Index2;
						break;

					default:
						if (pathType < 0 || static_cast<size_t>(pathType) >= views->Data.size())
							throw std::runtime_error("invalid #");
						stream = views->Data[pathType];
				}
				break;
			}
		}

		m_pImpl->Logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
			"Taking control of {}/{} (parent: {}/{}, type: {})",
			fileToOpen.parent_path().filename(), fileToOpen.filename(),
			indexFile.parent_path().filename(), indexFile.filename(),
			pathType);

		return stream;
	} catch (const Utils::Win32::Error& e) {
		m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, L"CreateFileW: {}, Message: {}", path.wstring(), e.what());
	} catch (const std::exception& e) {
		m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks, "CreateFileW: {}, Message: {}", path.wstring(), e.what());
	}
	return nullptr;
}

bool XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::EntryExists(const xivres::path_spec& pathSpec) const {
	const auto pack = m_pImpl->Queue.Find(pathSpec);
	const auto views = pack ? m_pImpl->Queue.EnsureBuilt(*pack) : nullptr;
	if (!views)
		return false;
	return views->HashOnlyEntries.find(pathSpec) != views->HashOnlyEntries.end()
		|| (pathSpec.has_original() && views->FullPathEntries.find(pathSpec) != views->FullPathEntries.end());
}

std::shared_ptr<xivres::stream> XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::GetOriginalEntry(const xivres::path_spec& pathSpec) const {
	return m_pImpl->GetOriginalEntry(pathSpec);
}

std::string XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::FindFutureReservationFor(const xivres::path_spec& pathSpec) const {
	// A pack not built yet has not said what waits on a stand-in.
	if (const auto pack = m_pImpl->Queue.Find(pathSpec))
		m_pImpl->Queue.EnsureBuilt(*pack);
	return m_pImpl->Reservations.Find(pathSpec);
}

std::string XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::ReserveCrossSqpack(const xivres::path_spec& requested, const xivres::path_spec& target) const {
	const auto requestedPack = m_pImpl->Queue.Find(requested);
	const auto targetPack = m_pImpl->Queue.Find(target);
	if (!requestedPack || !targetPack || requestedPack == targetPack)
		return {};

	const auto targetViews = m_pImpl->Queue.EnsureBuilt(*targetPack);
	if (!targetViews || !m_pImpl->Queue.EnsureBuilt(*requestedPack))
		return {};

	const auto entry = targetViews->find_entry(target);
	if (!entry)
		return {};

	// The views outlive the stand-ins, and reading the entry itself gives whatever it carries at the time.
	return m_pImpl->Reservations.Lend(requested, target, std::shared_ptr<const xivres::packed_stream>(std::shared_ptr<void>(), entry));
}

std::unique_lock<std::recursive_mutex> XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::LockTtmps() const {
	return m_pImpl->Queue.Lock();
}

void XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::AddNewTtmp(const std::filesystem::path& ttmplPath, bool reflectImmediately, Window::ProgressPopupWindow& progressWindow) {
	// Whether it fits where its files go is judged from every pack, which cannot be waited on under the lock.
	m_pImpl->Queue.EnsureAllBuilt();

	{
		const auto lock = LockTtmps();
		const auto added = m_pImpl->Library.Add(ttmplPath, progressWindow);
		if (!added)
			return;

		m_pImpl->CheckTtmpAllocation(*added->Ttmp);
	}

	if (reflectImmediately)
		m_pImpl->ReflectUsedEntries();
}

void XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::DeleteTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately) {
	{
		const auto lock = LockTtmps();
		m_pImpl->Library.Delete(ttmpl);
	}
	if (reflectImmediately)
		m_pImpl->ReflectUsedEntries();
}

void XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::RescanTtmp(Window::ProgressPopupWindow& progressWindow) {
	{
		const auto lock = LockTtmps();
		m_pImpl->Library.Rescan(progressWindow);
	}
	m_pImpl->ReflectUsedEntries();
}

std::shared_ptr<XivAlexander::Apps::MainApp::Features::Modding::NestedTtmp> XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::GetTtmps() const {
	return m_pImpl->Library.Root();
}

void XivAlexander::Apps::MainApp::Features::Modding::VirtualSqPacks::ApplyTtmpChanges(NestedTtmp& nestedTtmp, bool announce) {
	{
		const auto lock = LockTtmps();
		m_pImpl->Library.SaveChoices(nestedTtmp);
	}
	if (announce)
		m_pImpl->ReflectUsedEntries();
}
