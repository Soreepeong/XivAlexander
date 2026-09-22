#include "pch.h"
#include "MainApp/Modding/PackSources.h"

#include <xivres/packed_stream.oplocking.h>
#include <xivres/packed_stream.standard.h>
#include <xivres/sound.h>
#include <xivres/textools.h>

#include "MainApp/Modding/FutureReservations.h"
#include "Config.h"
#include "Misc/Logger.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	namespace {
		constexpr auto ReplacementRootMarkerName = "replacement-root.marker";

		void CollectMarkedRoots(const std::filesystem::path& dir, const char* markerName, std::vector<std::filesystem::path>& roots) {
			std::error_code ec;
			if (!is_directory(dir, ec) || ec)
				return;

			if (exists(dir / markerName, ec) && !ec) {
				roots.emplace_back(dir);
				return;
			}

			for (const auto& iter : std::filesystem::directory_iterator(dir, ec)) {
				if (ec)
					return;
				if (iter.is_directory(ec) && !ec)
					CollectMarkedRoots(iter.path(), markerName, roots);
			}
		}
	}

	PackSources::PackSources()
		: m_config(Config::Acquire())
		, m_logger(Misc::Logger::Acquire())
		, m_additionalGameDirectories(CollectAdditionalGameDirectories())
		, m_replacementRoots(CollectReplacementRoots()) {}

	PackSources::~PackSources() = default;

	std::shared_ptr<const xivres::stream> PackSources::Populate(
		xivres::sqpack::generator& creator,
		const std::filesystem::path& indexFile,
		const TtmpLibrary::Reservations& modpackReservations,
		FutureReservations& futureReservations) const {

		if (const auto result = creator.add_sqpack(indexFile, true, true, true); result.any()) {
			m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
				"[{}/{}] Source: added {}, replaced {}, ignored {}, error {}",
				creator.DatExpac, creator.DatName,
				result.Added.size(), result.Replaced.size(), result.SkippedExisting.size(), result.Error.size());
			for (const auto& error : result.Error) {
				m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"\t=> Error processing {}: {}", error.first, error.second);
			}
		}

		if (creator.DatExpac != "ffxiv" || creator.DatName != "0a0000") {
			for (const auto& additionalGameDirectory : m_additionalGameDirectories) {
				const auto file = additionalGameDirectory / "sqpack" / indexFile.parent_path().filename() / indexFile.filename();
				if (!exists(file))
					continue;

				if (const auto result = creator.add_sqpack(file, false, false); result.any()) {
					m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
						"[{}/{}] {}: added {}, replaced {}, ignored {}, error {}",
						creator.DatExpac, creator.DatName, file,
						result.Added.size(), result.Replaced.size(), result.SkippedExisting.size(), result.Error.size());
					for (const auto& error : result.Error) {
						m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
							"	=> Error processing {}: {}", error.first, error.second);
					}
				}
			}
		}

		std::shared_ptr<const xivres::stream> emptyScd;
		if (creator.DatExpac == "ffxiv" && creator.DatName == "070000") {
			try {
				const auto sampleEntry = creator.find_entry("sound/system/sample_system.scd");
				if (!sampleEntry || !sampleEntry->base_stream())
					throw std::out_of_range("sample_system.scd not found");
				const auto reader = xivres::sound::reader(sampleEntry->base_stream()->make_unpacked_ptr());
				xivres::sound::writer writer;
				writer.set_table_1(reader.read_table_1());
				writer.set_table_2(reader.read_table_2());
				writer.set_table_4(reader.read_table_4());
				writer.set_table_5(reader.read_table_5());
				for (size_t i = 0; i < 256; ++i)
					writer.set_sound_item(i, xivres::sound::writer::sound_item::make_empty(std::chrono::milliseconds(100)));

				emptyScd = std::make_shared<xivres::memory_stream>(
					xivres::compressing_packed_stream<xivres::standard_compressing_packer>("sound/empty256.scd", std::make_shared<xivres::memory_stream>(writer.export_to_bytes()), Z_NO_COMPRESSION)
					.read_vector<uint8_t>(0));
			} catch (std::out_of_range&) {
				// ignore
			}
		}

		if (creator.DatName == "040000") {
			creator.reserve_space(xivres::textools::metafile::ex_skeleton_table_path(xivres::textools::metafile::est_types::Body), 1048576);
			creator.reserve_space(xivres::textools::metafile::ex_skeleton_table_path(xivres::textools::metafile::est_types::Face), 1048576);
			creator.reserve_space(xivres::textools::metafile::ex_skeleton_table_path(xivres::textools::metafile::est_types::Hair), 1048576);
			creator.reserve_space(xivres::textools::metafile::ex_skeleton_table_path(xivres::textools::metafile::est_types::Head), 1048576);
			creator.reserve_space(xivres::textools::metafile::EqpPath, 1048576);
			creator.reserve_space(xivres::textools::metafile::GmpPath, 1048576);
		}

		if (const auto it = modpackReservations.find(creator.DatName); it != modpackReservations.end()) {
			for (const auto& [pathSpec, size] : it->second)
				creator.reserve_space(pathSpec, size);
		}

		AddReplacementFiles(creator, indexFile);
		futureReservations.Reserve(creator);

		if (emptyScd) {
			const uint32_t voiceDirs[]{
				xivres::path_spec::hash("sound/voice/vo_battle"),
				xivres::path_spec::hash("sound/voice/vo_cm"),
				xivres::path_spec::hash("sound/voice/vo_emote"),
				xivres::path_spec::hash("sound/voice/vo_line"),
			};
			for (const auto& pathSpec : creator.all_path_spec()) {
				if (std::ranges::find(voiceDirs, pathSpec.path_hash()) != std::end(voiceDirs))
					creator.reserve_space(pathSpec, static_cast<uint32_t>(emptyScd->size()));
			}
		}

		return emptyScd;
	}

	std::vector<std::filesystem::path> PackSources::CollectReplacementRoots() const {
		std::vector<std::filesystem::path> roots;
		roots.emplace_back(m_config->Init.ResolveConfigStorageDirectoryPath() / "ReplacementFileEntries");
		for (const auto& dir : m_config->Runtime.AdditionalGameResourceFileEntryRootDirectories.Value()) {
			if (!dir.empty())
				roots.emplace_back(Config::TranslatePath(dir));
		}

		const auto configured = roots.size();
		for (size_t i = 0; i < configured; i++)
			CollectMarkedRoots(roots[i], ReplacementRootMarkerName, roots);

		if (roots.size() > configured) {
			m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
				"Found {} nested replacement root(s) marked by {}",
				roots.size() - configured,
				ReplacementRootMarkerName);
		}
		return roots;
	}

	std::vector<std::filesystem::path> PackSources::CollectAdditionalGameDirectories() const {
		std::vector<std::filesystem::path> dirs;
		for (const auto& dir : m_config->Runtime.AdditionalSqpackRootDirectories.Value()) {
			if (!dir.empty())
				dirs.emplace_back(Config::TranslatePath(dir));
		}
		return dirs;
	}

	void PackSources::AddReplacementFiles(xivres::sqpack::generator& creator, const std::filesystem::path& indexPath) const {
		std::vector<std::filesystem::path> rootDirs;
		rootDirs.emplace_back(indexPath.parent_path().parent_path());
		rootDirs.insert(rootDirs.end(), m_replacementRoots.begin(), m_replacementRoots.end());

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
				m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
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
					const auto result = creator.add(std::make_shared<xivres::oplocking_packed_stream>(file.lexically_relative(relativeTo), file), true);
					if (const auto item = result.any())
						m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks,
							"[{}/{}] {} file {}: (nameHash={:08x}, pathHash={:08x}, fullPathHash={:08x})",
							creator.DatName, creator.DatExpac,
							result.Added.empty() ? "Replaced" : "Added",
							item->path_spec().text(),
							item->path_spec().name_hash(),
							item->path_spec().path_hash(),
							item->path_spec().full_path_hash());
					else
						for (const auto& error : result.Error | std::views::values)
							throw std::runtime_error(error);
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
						"[{}/{}] Error processing {}: {}",
						creator.DatName, creator.DatExpac,
						file, e.what());
				}
			}
		}
	}
}
