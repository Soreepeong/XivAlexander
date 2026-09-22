#include "pch.h"
#include "MainApp/Modding/TtmpLibrary.h"

#include <xivres/image_change_data.h>
#include <xivres/packed_stream.h>
#include <xivres/stream.oplocking.h>
#include <xivres/unpacked_stream.h>

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
		if (!folder || folder->Find(ttmplPath))  // already exists
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
		return added.get();
	}

	void TtmpLibrary::Delete(const std::filesystem::path& ttmplPath) {
		auto folder = FindContainer(ttmplPath, false);
		if (!folder)
			return;
		auto ttmp = folder->Find(ttmplPath);
		if (!ttmp || !ttmp->Ttmp)
			return;
		remove(ttmp->Ttmp->ListPath);
		m_root->RemoveEmptyChildren();
	}

	void TtmpLibrary::ReconcileFiles() {
		m_root->Traverse(false, [this](NestedTtmp& nestedTtmp) { ReconcileFiles(nestedTtmp); });
		m_root->RemoveEmptyChildren();
	}

	void TtmpLibrary::ReconcileFiles(NestedTtmp& nestedTtmp) {
		if (!nestedTtmp.Ttmp)
			return;
		TtmpSet& ttmp = *nestedTtmp.Ttmp;

		if (!exists(ttmp.ListPath)) {
			ttmp.TryCleanupUnusedFiles();
			return;
		}
		if (!nestedTtmp.RenameTo)
			return;

		try {
			create_directories(*nestedTtmp.RenameTo);

			const auto renameToDirHandle = Utils::Win32::Handle::FromCreateFile(*nestedTtmp.RenameTo, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0);

			const auto newListPath = (*nestedTtmp.RenameTo / L"TTMPL.mpl").wstring();
			std::vector<char> renameInfoBuffer;
			renameInfoBuffer.resize(sizeof(FILE_RENAME_INFO) + newListPath.size());
			auto renameInfo = *reinterpret_cast<FILE_RENAME_INFO*>(&renameInfoBuffer[0]);
			wcsncpy_s(renameInfo.FileName, static_cast<DWORD>(newListPath.size()), newListPath.data(), newListPath.size());
			ttmp.DataStream.reset();
			{
				const auto dataFile = Utils::Win32::Handle::FromCreateFile(ttmp.DataPath, GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0);
				SetFileInformationByHandle(dataFile, FileRenameInfo, &renameInfoBuffer[0], static_cast<DWORD>(renameInfoBuffer.size()));
			}

			std::vector<std::string> sidecars{"TTMPD.mpd", "compression", "disable"};
			for (const auto& profile : m_config->Runtime.TtmpChoicesFiles.Value()) {
				if (profile.FileName.empty())
					continue;
				sidecars.emplace_back(profile.FileName);
				sidecars.emplace_back(profile.FileName + ".disable");
			}
			for (const auto& path : sidecars) {
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
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"Failed to move {} to {}: {}",
				ttmp.ListPath.wstring(), nestedTtmp.RenameTo->wstring(), e.what());
			nestedTtmp.RenameTo.reset();
		}
	}

	void TtmpLibrary::SaveChoices(NestedTtmp& ttmp) const {
		const auto choicesFileName = ResolveChoicesFileName();
		const auto disableFilePath = ttmp.Path / (choicesFileName + ".disable");
		const auto choicesPath = ttmp.Path / choicesFileName;

		if (ttmp.Enabled) {
			if (const auto unconditionalPath = ttmp.Path / "disable"; exists(unconditionalPath))
				remove(unconditionalPath);
			if (exists(disableFilePath))
				remove(disableFilePath);
		} else if (!exists(disableFilePath))
			void(std::ofstream(disableFilePath));

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
		dirs.emplace_back(m_config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods");
		dirs.emplace_back(m_sqpackPath / "TexToolsMods");

		for (const auto& dir : m_config->Runtime.AdditionalTexToolsModPackSearchDirectories.Value())
			dirs.emplace_back(Config::TranslatePath(dir));

		for (auto it = dirs.begin(); it != dirs.end();) {
			if (!exists(*it) || !is_directory(*it) || it->empty())
				it = dirs.erase(it);
			else
				++it;
		}
		return dirs;
	}

	std::string TtmpLibrary::ResolveChoicesFileName() const {
		const auto& profiles = m_config->Runtime.TtmpChoicesFiles.Value();
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
		return exists(MarkerPath(dir, {}))
			|| exists(MarkerPath(dir, ResolveChoicesFileName()));
	}

	std::filesystem::path TtmpLibrary::MarkerPath(const std::filesystem::path& dir, const std::string& choicesFileName) {
		return choicesFileName.empty() ? dir / "disable" : dir / (choicesFileName + ".disable");
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
					if (equivalent(child->Path, iter.path())) {
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
				std::map<std::filesystem::path, uint64_t> orderMap;
				for (const auto& [path, index] : Utils::ParseJsonFromFile(orderingFile).items()) {
					orderMap.emplace(std::filesystem::path(xivres::util::unicode::convert<std::wstring>(path)), index.get<uint64_t>());
				}
				m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
					"Ordering file loaded from {}", orderingFile.wstring());
				for (auto& child : *parent->Children) {
					if (const auto it = orderMap.find(child->Path.filename()); it != orderMap.end())
						child->Index = it->second;
				}
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"Failed to load choices from {}: {}", orderingFile.wstring(), e.what());
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
		std::vector<std::filesystem::path> dirStack;
		auto rooted = false;
		for (const auto& dir : GetPossibleTtmpDirs()) {
			dirStack.clear();
			auto ttmpRoot{ttmpl};
			while (ttmpRoot != ttmpRoot.parent_path()) {
				ttmpRoot = ttmpRoot.parent_path();

				std::error_code ec;
				if (equivalent(dir, ttmpRoot, ec) && !ec) {
					rooted = true;
					break;
				}
				dirStack.emplace_back(ttmpRoot);
			}
			if (rooted)
				break;
		}

		if (!rooted) {
			dirStack.clear();
			m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
				"{} is not in one of ttmp root folders.", ttmpl.wstring());
			return nullptr;
		}

		std::shared_ptr folder{m_root};
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
}
