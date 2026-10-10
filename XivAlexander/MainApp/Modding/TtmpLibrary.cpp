#include "pch.h"
#include "MainApp/Modding/TtmpLibrary.h"

#include <xivres/image_change_data.h>
#include <xivres/packed_stream.h>
#include <xivres/stream.oplocking.h>
#include <xivres/unpacked_stream.h>

#include "MainApp/Modding/StreamTags.h"
#include "MainApp/Windows/ProgressPopupWindow.h"
#include "Config.h"
#include "Misc/Logger.h"
#include "resource.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	namespace {
		void CollectReservationsFromTtmp(TtmpLibrary::Reservations& into, const xivres::textools::mod_pack_json& ttmpl, const std::shared_ptr<xivres::stream>& ttmpd, const std::shared_ptr<Misc::Logger>& logger) {
			if (!ttmpd || !ttmpd->size())
				return;

			ttmpl.for_each([&](const xivres::textools::mods_json& entry) {
				const xivres::path_spec entryPathSpec(entry.FullPath);
				if (entry.ModSize > UINT32_MAX)
					return;

				auto& reservations = into[entryPathSpec.packname()];

				if (entry.is_textools_metadata()) {
					const auto packed = std::make_shared<xivres::stream_as_packed_stream>(entry.FullPath, std::shared_ptr<const xivres::stream>(ttmpd->substream(entry.ModOffset, entry.ModSize)));
					std::vector<uint8_t> metaData;
					try {
						metaData = xivres::unpacked_stream(packed).read_vector<uint8_t>();
					} catch (const std::exception& e) {
						logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
							"[{}] {}: metadata at offset {} of {}, from a {} byte file: {}",
							ttmpl.Name, entry.FullPath, entry.ModOffset, entry.ModSize, ttmpd->size(), e.what());
						return;
					}

					if (metaData.size() <= sizeof(uint32_t)) {
						logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
							"[{}] {}: metadata read {} bytes at offset {} of {}, from a {} byte file",
							ttmpl.Name, entry.FullPath, metaData.size(), entry.ModOffset, entry.ModSize, ttmpd->size());
						return;
					}

					const auto metadata = xivres::textools::metafile(entry.FullPath, xivres::memory_stream(std::span(metaData)));
					if (!metadata.get_span<xivres::image_change_data::entry>(xivres::textools::metafile::meta_types::Imc).empty())
						reservations.emplace_back(metadata.TargetImcPath, 65536);
					if (const auto eqdpedit = metadata.get_span<xivres::textools::metafile::equipment_deformer_parameter_entry>(xivres::textools::metafile::meta_types::Eqdp); !eqdpedit.empty()) {
						for (const auto& v : eqdpedit)
							reservations.emplace_back(xivres::textools::metafile::equipment_deformer_parameter_path(metadata.ItemType, v.RaceCode), 1048576);
					}
					return;
				}

				reservations.emplace_back(entry.FullPath, static_cast<uint32_t>(entry.ModSize));
			});
		}

		// Something else still having a file open when a directory is renamed is usually brief: a read that was already
		// past the gate, an antivirus scan, or Explorer generating a thumbnail.
		constexpr auto RenameRetryTimeout = std::chrono::seconds(3);
		constexpr DWORD RenameRetryIntervalMs = 50;

		std::string ToUtf8(const std::filesystem::path& path) {
			return xivres::util::unicode::convert<std::string>(path.wstring());
		}

		/// The message of an exception that is shown to the user as it is, in the language of the interface.
		template<typename... Args>
		std::string Message(UINT id, Args&&... args) {
			return Config::Acquire()->Runtime.FormatStringResUtf8(id, std::forward<Args>(args)...);
		}

		void ValidateName(const std::wstring& name) {
			if (name.empty())
				throw std::invalid_argument(Message(IDS_TTMP_ERROR_NAME_EMPTY));
			if (name == L"." || name == L"..")
				throw std::invalid_argument(Message(IDS_TTMP_ERROR_NAME_DOTS));
			if (name.size() > 255)
				throw std::invalid_argument(Message(IDS_TTMP_ERROR_NAME_TOOLONG));
			if (std::ranges::any_of(name, [](wchar_t c) { return c < 32 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos; }))
				throw std::invalid_argument(Message(IDS_TTMP_ERROR_NAME_INVALIDCHARS));

			// Windows would silently drop these, so the directory would end up with a name different from what was asked.
			if (name.back() == L' ' || name.back() == L'.')
				throw std::invalid_argument(Message(IDS_TTMP_ERROR_NAME_TRAILING));

			auto stem = name.substr(0, name.find(L'.'));
			while (!stem.empty() && stem.back() == L' ')
				stem.pop_back();
			std::ranges::transform(stem, stem.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
			if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL"
				|| (stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) && stem[3] >= L'1' && stem[3] <= L'9'))
				throw std::invalid_argument(Message(IDS_TTMP_ERROR_NAME_RESERVED));
		}

		/// \returns Where path ends up when the directory from, which contains it, is moved to to.
		std::filesystem::path Rebase(const std::filesystem::path& path, const std::filesystem::path& from, const std::filesystem::path& to) {
			const auto relative = path.lexically_relative(from);
			if (relative.empty() || relative == L".")
				return to;
			return to / relative;
		}

		/// Releases the data files of the packs, so that their directories can be renamed.
		void ReleaseDataStreams(const std::vector<NestedTtmp*>& packs) {
			for (const auto pack : packs) {
				if (const auto stream = dynamic_cast<const xivres::oplocking_file_stream*>(pack->Ttmp->DataStream.get()))
					stream->release();
			}
		}

		/// Renames a directory, retrying for a while if a file inside is still open.
		/// \param beforeEachTry Called before every attempt, to let go of the files again in case something opened them.
		void RenameDirectory(const std::filesystem::path& from, const std::filesystem::path& to, const std::function<void()>& beforeEachTry) {
			const auto until = std::chrono::steady_clock::now() + RenameRetryTimeout;
			while (true) {
				beforeEachTry();
				if (MoveFileExW(from.c_str(), to.c_str(), 0))
					return;

				const auto error = GetLastError();
				if (error == ERROR_NOT_SAME_DEVICE)
					throw std::runtime_error(Message(IDS_TTMP_ERROR_DIFFERENTDRIVE));
				if ((error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
					|| std::chrono::steady_clock::now() >= until)
					throw Utils::Win32::Error(error, Message(IDS_TTMP_ERROR_MOVEFAILED, from.wstring(), to.wstring()));
				Sleep(RenameRetryIntervalMs);
			}
		}

		/// \returns Contents of order.json in dir: names of the directories in it, mapped to where each goes among its
		/// siblings, lower first. An empty object if there is no such file.
		nlohmann::json LoadOrderFile(const std::filesystem::path& dir) {
			const auto path = dir / "order.json";
			if (!exists(path))
				return nlohmann::json::object();
			auto order = Utils::ParseJsonFromFile(path);
			if (!order.is_object())
				throw std::runtime_error("not a JSON object");
			return order;
		}

		/// Renames or removes a name in order.json of dir, if it is there.
		void UpdateOrderFile(const std::filesystem::path& dir, const std::filesystem::path& oldName, const std::optional<std::filesystem::path>& newName, const std::shared_ptr<Misc::Logger>& logger) {
			try {
				auto order = LoadOrderFile(dir);
				const auto it = order.find(ToUtf8(oldName));
				if (it == order.end())
					return;

				auto index = *it;
				order.erase(it);
				if (newName)
					order[ToUtf8(*newName)] = std::move(index);
				Utils::SaveJsonToFile(dir / "order.json", order);
			} catch (const std::exception& e) {
				logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"Failed to update {}: {}", (dir / "order.json").wstring(), e.what());
			}
		}
	}

	TtmpLibrary::TtmpLibrary(std::filesystem::path sqpackPath)
		: m_config(Config::Acquire())
		, m_logger(Misc::Logger::Acquire())
		, m_sqpackPath(std::move(sqpackPath))
		, m_root(std::make_shared<NestedTtmp>(NestedTtmp{
			.Children = std::vector<std::shared_ptr<NestedTtmp>>{},
		})) {}

	TtmpLibrary::~TtmpLibrary() = default;

	void TtmpLibrary::Scan(Window::ProgressPopupWindow& progressWindow) {
		{
			const auto loaderThread = Utils::Win32::Thread(L"TTMP Scanner", [&] {
				for (const auto& dir : GetPossibleTtmpDirs()) {
					if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
						throw std::runtime_error("Cancelled");
					RescanTree(dir, m_root, progressWindow);
				}
			});
			do {
				progressWindow.UpdateMessage(m_config->Runtime.GetStringRes(IDS_TITLE_DISCOVERINGFILES));
			} while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, {loaderThread}));
		}

		if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
			throw std::runtime_error("Cancelled");
	}

	void TtmpLibrary::Rescan(Window::ProgressPopupWindow& progressWindow) {
		for (const auto& dir : GetPossibleTtmpDirs())
			RescanTree(dir, m_root, progressWindow);
	}

	TtmpLibrary::Reservations TtmpLibrary::CollectReservations(Window::ProgressPopupWindow& progressWindow) const {
		Reservations reservations;
		if (m_root->TraverseInterruptible(false, [&](const NestedTtmp& nestedTtmp) {
			if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
				return NestedTtmp::Break;

			if (nestedTtmp.Ttmp)
				CollectReservationsFromTtmp(reservations, nestedTtmp.Ttmp->List, nestedTtmp.Ttmp->DataStream, m_logger);
			return NestedTtmp::Continue;
		}) == NestedTtmp::Break)
			throw std::runtime_error("Cancelled");
		return reservations;
	}

	NestedTtmp* TtmpLibrary::Add(const std::filesystem::path& ttmplPath, Window::ProgressPopupWindow& progressWindow) {
		auto folder = FindContainer(ttmplPath, true);
		if (!folder || folder->Find(ttmplPath.parent_path()))  // already exists
			return nullptr;

		std::shared_ptr<NestedTtmp> added;
		try {
			added = AddFromTtmpl(ttmplPath, folder);
			folder->Sort();
		} catch (const std::exception& e) {
			m_root->RemoveEmptyChildren();
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to load TexTools ModPack from {}: {}", ttmplPath.wstring(), e.what());
			return nullptr;
		}
		if (added)
			AppendLast(folder, added);
		return added.get();
	}

	void TtmpLibrary::Delete(const std::filesystem::path& ttmplPath) {
		auto folder = FindContainer(ttmplPath, false);
		if (!folder)
			return;
		auto ttmp = folder->Find(ttmplPath.parent_path());
		if (!ttmp || !ttmp->Ttmp)
			return;
		remove(ttmp->Ttmp->ListPath);
		m_root->RemoveEmptyChildren();
	}

	void TtmpLibrary::ReconcileFiles() {
		// A pack whose list is gone was deleted. This runs once what it replaced is known to be put back, so its data
		// file can go too; the node goes as well, as its data stream is no more. Its name goes from its folder's order
		// once its directory is gone.
		std::vector<std::shared_ptr<NestedTtmp>> parents;
		m_root->TraverseInterruptible(false, [&parents](NestedTtmp& nestedTtmp) {
			if (!nestedTtmp.Ttmp || exists(nestedTtmp.Ttmp->ListPath))
				return NestedTtmp::Continue;

			nestedTtmp.Ttmp->TryCleanupUnusedFiles();
			if (nestedTtmp.Parent && std::ranges::find(parents, nestedTtmp.Parent) == parents.end())
				parents.push_back(nestedTtmp.Parent);
			nestedTtmp.Parent = nullptr;
			return NestedTtmp::Delete;
		});
		for (const auto& parent : parents)
			ForgetMissingInOrder(*parent);
		m_root->RemoveEmptyChildren();
	}

	void TtmpLibrary::SaveChoices(NestedTtmp& ttmp) const {
		const auto choicesFileName = ResolveChoicesFileName();
		const auto profileMarkers = TtmpSet::DisableMarkerNames(choicesFileName);
		const auto choicesPath = ttmp.Path / choicesFileName;

		if (ttmp.Enabled) {
			for (const auto& markers : {TtmpSet::DisableMarkerNames(), profileMarkers}) {
				for (const auto& name : markers) {
					if (const auto path = ttmp.Path / name; exists(path))
						remove(path);
				}
			}
		} else if (!IsDisabled(ttmp.Path))
			void(std::ofstream(ttmp.Path / profileMarkers.front()));

		if (ttmp.Ttmp)
			Utils::SaveJsonToFile(choicesPath, ttmp.Ttmp->Choices);
	}

	void TtmpLibrary::ReloadChoices() {
		m_root->Traverse(false, [this](NestedTtmp& nestedTtmp) {
			nestedTtmp.Enabled = !IsDisabled(nestedTtmp.Path);
			if (!nestedTtmp.Ttmp)
				return;

			auto& set = *nestedTtmp.Ttmp;
			set.Choices = nlohmann::json{};
			if (const auto choicesPath = set.ListPath.parent_path() / ResolveChoicesFileName(); exists(choicesPath)) {
				try {
					set.Choices = Utils::ParseJsonFromFile(choicesPath);
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
						"Failed to load choices from {}: {}", choicesPath.wstring(), e.what());
				}
			}
			set.FixChoices();
		});
	}

	std::vector<std::filesystem::path> TtmpLibrary::GetPossibleTtmpDirs() const {
		std::vector<std::filesystem::path> dirs;
		for (const auto& dir : m_config->Runtime.Modding.Ttmp.SearchDirectories.Value()) {
			if (!dir.empty())
				dirs.emplace_back(m_config->TranslateDirectoryPath(dir, m_sqpackPath));
		}

		for (auto it = dirs.begin(); it != dirs.end();) {
			if (it->empty() || !is_directory(*it))
				it = dirs.erase(it);
			else
				++it;
		}
		return dirs;
	}

	std::string TtmpLibrary::ResolveChoicesFileName() const {
		const auto& profiles = m_config->Runtime.Modding.Ttmp.ChoicesFiles.Value();
		for (const auto& profile : profiles) {
			if (profile.Active && !profile.FileName.empty())
				return profile.FileName;
		}
		for (const auto& profile : profiles) {
			if (!profile.FileName.empty())
				return profile.FileName;
		}
		return "choices.json";
	}

	bool TtmpLibrary::IsDisabled(const std::filesystem::path& dir) const {
		for (const auto& markers : {TtmpSet::DisableMarkerNames(), TtmpSet::DisableMarkerNames(ResolveChoicesFileName())}) {
			if (std::ranges::any_of(markers, [&dir](const auto& name) { return exists(dir / name); }))
				return true;
		}
		return false;
	}

	void TtmpLibrary::RescanTree(const std::filesystem::path& path, std::shared_ptr<NestedTtmp> parent, Window::ProgressPopupWindow& progressWindow) {
		try {
			for (const auto& iter : std::filesystem::directory_iterator(path)) {
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					return;

				if (!iter.is_directory())
					continue;

				std::shared_ptr<NestedTtmp> current;
				for (auto& child : *parent->Children) {
					if (std::error_code ec; equivalent(child->Path, iter.path(), ec) && !ec) {
						current = child;
						break;
					}
				}
				if (const auto ttmplPath = iter.path() / "TTMPL.mpl"; exists(ttmplPath)) {
					if (!current)
						AddFromTtmpl(ttmplPath, parent);
				} else {
					if (!current)
						current = parent->Children->emplace_back(std::make_shared<NestedTtmp>(NestedTtmp{
							.Path = iter.path(),
							.Parent = parent,
							.Children = std::vector<std::shared_ptr<NestedTtmp>>{},
						}));
					RescanTree(iter.path(), current, progressWindow);
				}
			}
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to list items in {}: {}",
				path.wstring(), e.what());
		}

		if (const auto orderingFile = path / "order.json"; exists(orderingFile)) {
			try {
				// Kept alive while it is read: structured bindings over items() of a temporary read the values as null. A value
				// that is not a position is skipped, so that one bad entry does not lose the order of the rest.
				const auto order = LoadOrderFile(path);
				std::map<std::filesystem::path, uint64_t> orderMap;
				for (auto it = order.begin(); it != order.end(); ++it) {
					if (it.value().is_number_unsigned() || (it.value().is_number_integer() && it.value().get<int64_t>() >= 0))
						orderMap.emplace(std::filesystem::path(xivres::util::unicode::convert<std::wstring>(it.key())), it.value().get<uint64_t>());
					else
						m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
							"Ignored \"{}\" in {}: not a position", it.key(), orderingFile.wstring());
				}
				m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
					"Ordering file loaded from {}", orderingFile.wstring());
				for (auto& child : *parent->Children) {
					if (const auto it = orderMap.find(child->Path.filename()); it != orderMap.end())
						child->Index = it->second;
				}
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"Failed to load the order from {}: {}", orderingFile.wstring(), e.what());
			}
		}

		parent->Enabled = !IsDisabled(path);

		parent->RemoveEmptyChildren();
		parent->Sort();
	}

	std::shared_ptr<NestedTtmp> TtmpLibrary::AddFromTtmpl(const std::filesystem::path& ttmplPath, const std::shared_ptr<NestedTtmp>& parent) {
		const auto ttmpDir = ttmplPath.parent_path();
		const auto ttmpdPath = ttmpDir / "TTMPD.mpd";
		if (ttmplPath.filename() != "TTMPL.mpl")
			return nullptr;

		std::shared_ptr<NestedTtmp> added;
		try {
			auto list = xivres::textools::mod_pack_json::from_stream(xivres::file_stream(ttmplPath));

			auto dataStream = std::make_shared<xivres::oplocking_file_stream>(ttmpdPath, false);
			if (dataStream->done())
				throw std::runtime_error(std::format("failed to open {}", ttmpdPath.string()));
			dataStream->emplace_tag<ModpackNameTag>(list.Name.empty() ? xivres::util::unicode::convert<std::string>(ttmpDir.filename().wstring()) : list.Name);

			added = parent->Children->emplace_back(std::make_shared<NestedTtmp>(NestedTtmp{
				.Path = ttmpDir,
				.Parent = parent,
				.Enabled = !IsDisabled(ttmpDir),
				.Ttmp = TtmpSet{
					.Allocated = true,
					.ListPath = ttmplPath,
					.List = std::move(list),
					.DataPath = ttmpdPath,
					.DataStream = std::move(dataStream),
				},
			}));
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to load TexTools ModPack from {}: {}", ttmplPath.wstring(), e.what());
			return nullptr;
		}
		if (const auto choicesPath = ttmpDir / ResolveChoicesFileName(); exists(choicesPath)) {
			try {
				added->Ttmp->Choices = Utils::ParseJsonFromFile(choicesPath);
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"Failed to load choices from {}: {}", choicesPath.wstring(), e.what());
			}
		}
		added->Ttmp->FixChoices();
		return added;
	}

	std::shared_ptr<NestedTtmp> TtmpLibrary::FindContainer(const std::filesystem::path& ttmpl, bool create) {
		// The folder holding the directory holding TTMPL.mpl.
		const auto folderDir = ttmpl.parent_path().parent_path();
		if (!ResolveFolderChain(folderDir)) {
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"{} is not in one of ttmp root folders.", ttmpl.wstring());
			return nullptr;
		}
		return FindFolder(folderDir, create);
	}

	std::shared_ptr<NestedTtmp> TtmpLibrary::FindFolder(const std::filesystem::path& dir, bool create) {
		const auto chain = ResolveFolderChain(dir);
		if (!chain)
			return nullptr;

		std::shared_ptr folder{m_root};
		for (const auto& subdir : std::ranges::reverse_view(chain->Dirs)) {
			auto subfolder = folder->Find(subdir);
			if (!subfolder) {
				if (!create || exists(subdir / "TTMPL.mpl"))
					return nullptr;
				subfolder = folder->Children->emplace_back(std::make_shared<NestedTtmp>(NestedTtmp{
					.Index = LookupOrderIndex(*folder, subdir.filename()),
					.Path = subdir,
					.Parent = folder,
					.Enabled = !IsDisabled(subdir),
					.Children = std::vector<std::shared_ptr<NestedTtmp>>{},
				}));
				folder->Sort();
				if (subfolder->Index == UINT64_MAX)
					AppendLast(folder, subfolder);
			}
			if (!subfolder->IsGroup())
				return nullptr;
			folder = std::move(subfolder);
		}

		return folder;
	}

	std::optional<TtmpLibrary::FolderChain> TtmpLibrary::ResolveFolderChain(const std::filesystem::path& dir) const {
		for (const auto& root : GetPossibleTtmpDirs()) {
			FolderChain chain{.Root = root};
			for (auto current = dir;; current = current.parent_path()) {
				if (std::error_code ec; equivalent(root, current, ec) && !ec)
					return chain;
				if (current == current.parent_path())
					break;
				chain.Dirs.emplace_back(current);
			}
		}
		return std::nullopt;
	}

	bool TtmpLibrary::Contains(const NestedTtmp& item) const {
		for (auto current = &item; current != m_root.get();) {
			const auto parent = current->Parent.get();
			if (!parent || !parent->Children || std::ranges::none_of(*parent->Children, [current](const auto& c) { return c.get() == current; }))
				return false;
			current = parent;
		}
		return true;
	}

	uint64_t TtmpLibrary::LookupOrderIndex(const NestedTtmp& folder, const std::filesystem::path& name) const {
		// Same as RescanTree: order.json of every search directory applies to the whole top level, later ones winning.
		auto index = UINT64_MAX;
		for (const auto& dir : &folder == m_root.get() ? GetPossibleTtmpDirs() : std::vector{folder.Path}) {
			try {
				const auto order = LoadOrderFile(dir);
				if (const auto it = order.find(ToUtf8(name)); it != order.end())
					index = it->get<uint64_t>();
			} catch (const std::exception&) {
				// RescanTree reports these.
			}
		}
		return index;
	}

	void TtmpLibrary::ValidateRename(const NestedTtmp& item, const std::wstring& newName) const {
		if (&item == m_root.get() || !Contains(item))
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_ITEMGONE));
		ValidateName(newName);

		const auto newPath = item.Path.parent_path() / newName;
		if (std::error_code ec; exists(newPath) && !(equivalent(newPath, item.Path, ec) && !ec))
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_NAMETAKEN, newName));
	}

	bool TtmpLibrary::Rename(const std::shared_ptr<NestedTtmp>& item, const std::wstring& newName) {
		ValidateRename(*item, newName);

		const auto oldPath = item->Path;
		if (oldPath.filename() == newName)
			return false;

		const auto newPath = oldPath.parent_path() / newName;
		try {
			Relocate(*item, newPath);
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to rename {} to {}: {}", oldPath.wstring(), newName, e.what());
			throw;
		}
		m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
			"Renamed {} to {}", oldPath.wstring(), newName);

		UpdateOrderFile(oldPath.parent_path(), oldPath.filename(), newPath.filename(), m_logger);
		ForgetMissingInOrder(*item->Parent);
		item->Parent->Sort();
		return true;
	}

	void TtmpLibrary::ValidateMove(const NestedTtmp& item, const std::filesystem::path& folderDir) const {
		if (&item == m_root.get() || !Contains(item))
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_ITEMGONE));
		if (!is_directory(folderDir))
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_DESTINATIONMISSING));

		const auto chain = ResolveFolderChain(folderDir);
		if (!chain)
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_DESTINATIONOUTSIDE));
		for (const auto& dir : chain->Dirs) {
			if (std::error_code ec; equivalent(dir, item.Path, ec) && !ec)
				throw std::invalid_argument(Message(IDS_TTMP_ERROR_MOVEINTOSELF));
			if (exists(dir / "TTMPL.mpl"))
				throw std::invalid_argument(Message(IDS_TTMP_ERROR_MOVEINTOMODPACK));
		}

		if (std::error_code ec; equivalent(folderDir, item.Path.parent_path(), ec) && !ec)
			return;
		if (exists(folderDir / item.Path.filename()))
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_NAMETAKENATDESTINATION, item.Path.filename().wstring()));
	}

	bool TtmpLibrary::Move(const std::shared_ptr<NestedTtmp>& item, const std::filesystem::path& folderDir) {
		ValidateMove(*item, folderDir);
		if (std::error_code ec; equivalent(folderDir, item->Path.parent_path(), ec) && !ec)
			return false;

		const auto oldPath = item->Path;
		const auto name = oldPath.filename();
		const auto folder = FindFolder(folderDir, true);
		try {
			if (!folder)
				throw std::runtime_error(Message(IDS_TTMP_ERROR_DESTINATIONNOTFOUND));

			// Spelled the way the scanner would have it, so that paths in the tree keep sharing their prefixes.
			Relocate(*item, (folder == m_root ? ResolveFolderChain(folderDir)->Root : folder->Path) / name);
		} catch (const std::exception& e) {
			// Drop the folders FindFolder may have just added for the destination.
			m_root->RemoveEmptyChildren();
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to move {} into {}: {}", oldPath.wstring(), folderDir.wstring(), e.what());
			throw;
		}
		m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
			"Moved {} to {}", oldPath.wstring(), item->Path.wstring());

		UpdateOrderFile(oldPath.parent_path(), name, std::nullopt, m_logger);
		ForgetMissingInOrder(*item->Parent);

		auto& oldSiblings = *item->Parent->Children;
		oldSiblings.erase(std::ranges::find(oldSiblings, item));
		item->Parent = folder;
		folder->Children->emplace_back(item);
		AppendLast(folder, item);
		return true;
	}

	std::filesystem::path TtmpLibrary::CreateFolder(const std::filesystem::path& parentDir, const std::wstring& name) const {
		ValidateName(name);
		if (!is_directory(parentDir))
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_PARENTMISSING));

		const auto chain = ResolveFolderChain(parentDir);
		if (!chain)
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_PARENTOUTSIDE));
		if (std::ranges::any_of(chain->Dirs, [](const auto& dir) { return exists(dir / "TTMPL.mpl"); }))
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_FOLDERINMODPACK));

		const auto path = parentDir / name;
		if (exists(path))
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_NAMETAKEN, name));
		if (std::error_code ec; !create_directory(path, ec)) {
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to create folder {}: {}", path.wstring(), ec.message());
			throw std::runtime_error(Message(IDS_TTMP_ERROR_CREATEFOLDER, path.wstring(), Utils::FromAnsi(ec.message())));
		}

		m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks, "Created folder {}", path.wstring());
		return path;
	}

	void TtmpLibrary::ValidateOrder(const NestedTtmp& folder, const std::vector<std::shared_ptr<NestedTtmp>>& children) const {
		if (!folder.IsGroup() || !Contains(folder))
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_FOLDERGONE));

		std::set<const NestedTtmp*> remaining;
		for (const auto& child : *folder.Children)
			remaining.insert(child.get());
		for (const auto& child : children) {
			if (!remaining.erase(child.get()))
				throw std::invalid_argument(Message(IDS_TTMP_ERROR_ORDERINCOMPLETE));
		}
		if (!remaining.empty())
			throw std::invalid_argument(Message(IDS_TTMP_ERROR_ORDERINCOMPLETE));
	}

	void TtmpLibrary::SetOrder(const std::shared_ptr<NestedTtmp>& folder, const std::vector<std::shared_ptr<NestedTtmp>>& children) {
		ValidateOrder(*folder, children);
		WriteOrder(folder, children);
	}

	void TtmpLibrary::WriteOrder(const std::shared_ptr<NestedTtmp>& folder, const std::vector<std::shared_ptr<NestedTtmp>>& children) {
		// The top level gathers several search directories, each with its own order.json; every child goes into the
		// one of the directory it is in, with its position counted across the whole top level.
		std::map<std::filesystem::path, std::vector<std::pair<std::filesystem::path, uint64_t>>> entriesByDir;
		for (size_t i = 0; i < children.size(); ++i)
			entriesByDir[children[i]->Path.parent_path()].emplace_back(children[i]->Path.filename(), i);

		for (const auto& [dir, entries] : entriesByDir) {
			const auto orderPath = dir / "order.json";
			try {
				auto order = nlohmann::json::object();
				try {
					order = LoadOrderFile(dir);
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
						"Replacing unreadable {}: {}", orderPath.wstring(), e.what());
				}

				// Keep the places of directories not in the tree, such as packs that failed to load, but forget those
				// of directories that are gone, so that whatever gets that name later does not inherit the place.
				for (auto it = order.begin(); it != order.end();) {
					if (is_directory(dir / xivres::util::unicode::convert<std::wstring>(it.key())))
						++it;
					else
						it = order.erase(it);
				}
				for (const auto& [name, index] : entries)
					order[ToUtf8(name)] = index;
				Utils::SaveJsonToFile(orderPath, order);
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"Failed to save {}: {}", orderPath.wstring(), e.what());
				throw std::runtime_error(Message(IDS_TTMP_ERROR_SAVEORDER, orderPath.wstring(), xivres::util::unicode::convert<std::wstring>(e.what())));
			}
		}

		for (size_t i = 0; i < children.size(); ++i)
			children[i]->Index = i;
		folder->Sort();
		m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
			"Saved the order of {} items in {}", children.size(), folder->Path.empty() ? std::wstring(L"the top level") : folder->Path.wstring());
	}

	void TtmpLibrary::AppendLast(const std::shared_ptr<NestedTtmp>& folder, const std::shared_ptr<NestedTtmp>& item) {
		// The siblings as they are sorted now, all of them listed, then the item; a failure to save leaves the change made.
		folder->Sort();
		std::vector<std::shared_ptr<NestedTtmp>> children;
		for (const auto& child : *folder->Children) {
			if (child != item)
				children.push_back(child);
		}
		children.push_back(item);
		try {
			WriteOrder(folder, children);
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to put {} last in its folder's order: {}", item->Path.wstring(), e.what());
			folder->Sort();
		}
	}

	void TtmpLibrary::ForgetMissingInOrder(const NestedTtmp& folder) const {
		for (const auto& dir : &folder == m_root.get() ? GetPossibleTtmpDirs() : std::vector{folder.Path}) {
			try {
				auto order = LoadOrderFile(dir);
				auto changed = false;
				for (auto it = order.begin(); it != order.end();) {
					if (is_directory(dir / xivres::util::unicode::convert<std::wstring>(it.key()))) {
						++it;
					} else {
						it = order.erase(it);
						changed = true;
					}
				}
				if (changed)
					Utils::SaveJsonToFile(dir / "order.json", order);
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"Failed to update {}: {}", (dir / "order.json").wstring(), e.what());
			}
		}
	}

	void TtmpLibrary::Relocate(NestedTtmp& item, const std::filesystem::path& newPath) {
		const auto oldPath = item.Path;

		std::vector<NestedTtmp*> packs;
		item.Traverse(false, [&packs](NestedTtmp& t) {
			if (t.Ttmp && t.Ttmp->DataStream)
				packs.emplace_back(&t);
		});

		// No directory with an open file inside can be renamed. Reads are stopped by the caller, so once released,
		// the data files stay closed; the old streams, if ever read again, find nothing and read zeroes.
		RenameDirectory(oldPath, newPath, [&packs] { ReleaseDataStreams(packs); });

		// Open the data files at their new place before touching the tree, so that if one fails, moving the directory
		// back leaves everything as it was.
		std::vector<std::shared_ptr<xivres::stream>> streams;
		try {
			for (const auto pack : packs) {
				const auto dataPath = Rebase(pack->Ttmp->DataPath, oldPath, newPath);
				auto stream = std::make_shared<xivres::oplocking_file_stream>(dataPath, false);
				if (stream->done())
					throw std::runtime_error(Message(IDS_TTMP_ERROR_OPENDATA, dataPath.wstring()));
				stream->emplace_tag<ModpackNameTag>(pack->Ttmp->List.Name.empty() ? ToUtf8(dataPath.parent_path().filename()) : pack->Ttmp->List.Name);
				streams.emplace_back(std::move(stream));
			}
		} catch (const std::exception& e) {
			// The new streams hold the files open.
			streams.clear();
			try {
				RenameDirectory(newPath, oldPath, [] {});
			} catch (const std::exception& e2) {
				m_logger->Format<LogLevel::Error>(LogCategory::VirtualSqPacks,
					"Failed to move {} back to {}: {}", newPath.wstring(), oldPath.wstring(), e2.what());
				throw std::runtime_error(Message(IDS_TTMP_ERROR_MOVEBACKFAILED,
					xivres::util::unicode::convert<std::wstring>(e.what()), xivres::util::unicode::convert<std::wstring>(e2.what())));
			}
			throw;
		}

		item.Traverse(false, [&](NestedTtmp& t) {
			t.Path = Rebase(t.Path, oldPath, newPath);
		});
		for (size_t i = 0; i < packs.size(); ++i) {
			auto& ttmp = *packs[i]->Ttmp;
			ttmp.ListPath = Rebase(ttmp.ListPath, oldPath, newPath);
			ttmp.DataPath = Rebase(ttmp.DataPath, oldPath, newPath);
			ttmp.DataStream = std::move(streams[i]);
		}
	}
}
