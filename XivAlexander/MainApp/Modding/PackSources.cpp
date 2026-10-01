#include "pch.h"
#include "MainApp/Modding/PackSources.h"

#include <xivres/packed_stream.oplocking.h>
#include <xivres/packed_stream.standard.h>
#include <xivres/sound.h>
#include <xivres/textools.h>

#include "MainApp/Modding/FutureReservations.h"
#include "MainApp/Modding/StreamTags.h"
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

				auto packed = xivres::compressing_packed_stream<xivres::standard_compressing_packer>("sound/empty256.scd", std::make_shared<xivres::memory_stream>(writer.export_to_bytes()), Z_NO_COMPRESSION)
					.read_vector<uint8_t>();

				if (packed.empty())
					throw std::out_of_range("empty256.scd came out empty");
				auto stream = std::make_shared<xivres::memory_stream>(std::move(packed));
				stream->emplace_tag<SourceNoteTag>("muted");
				emptyScd = std::move(stream);
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

		for (const auto& [pathSpec, file] : *GetReplacementFiles(FutureReservations::PackKey(creator), indexFile)) {
			try {
				creator.reserve_space(pathSpec, static_cast<uint32_t>(file.Stream->size()));
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
					"[{}/{}] Error processing {}: {}",
					creator.DatExpac, creator.DatName,
					file.File, e.what());
			}
		}
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

	std::shared_ptr<const PackSources::ReplacementFiles> PackSources::GetReplacementFiles(const std::string& packKey, const std::filesystem::path& indexPath) const {
		std::vector<std::filesystem::path> roots;
		uint64_t generation;
		{
			const auto lock = std::lock_guard(m_replacementMtx);
			if (const auto it = m_replacementFiles.find(packKey); it != m_replacementFiles.end())
				return it->second;
			roots = m_replacementRoots;
			generation = m_replacementGeneration;
		}

		auto files = std::make_shared<const ReplacementFiles>(ScanReplacementFiles(roots, packKey, indexPath));

		const auto lock = std::lock_guard(m_replacementMtx);
		if (generation != m_replacementGeneration)
			return files;
		return m_replacementFiles.try_emplace(packKey, std::move(files)).first->second;
	}

	void PackSources::RescanReplacementRoots() {
		auto roots = CollectReplacementRoots();
		m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks, "Rescanning {} replacement root(s)", roots.size());

		const auto lock = std::lock_guard(m_replacementMtx);
		m_replacementRoots = std::move(roots);
		m_replacementFiles.clear();
		m_replacementGeneration++;
	}

	PackSources::ReplacementFiles PackSources::ScanReplacementFiles(const std::vector<std::filesystem::path>& roots, const std::string& packKey, const std::filesystem::path& indexPath) const {
		const auto slash = packKey.find('/');
		const auto datExpac = packKey.substr(0, slash);
		const auto datName = slash == std::string::npos ? std::string() : packKey.substr(slash + 1);

		std::vector<std::filesystem::path> rootDirs;
		rootDirs.emplace_back(indexPath.parent_path().parent_path());
		rootDirs.insert(rootDirs.end(), roots.begin(), roots.end());

		std::vector<std::pair<std::filesystem::path, std::filesystem::path>> dirs;
		for (const auto& dir : rootDirs) {
			dirs.emplace_back(dir / datExpac / datName, dir / datExpac / datName);
			dirs.emplace_back(dir / datExpac / std::format("{}.win32", datName), dir / datExpac / std::format("{}.win32", datName));
		}
		std::filesystem::path pathPrefix;
		if (const auto datType = indexPath.filename().wstring().substr(0, 2);
			lstrcmpiW(datType.c_str(), L"0c") == 0)
			pathPrefix = std::format("music/{}", datExpac);
		else if (datType == L"02")
			pathPrefix = std::format("bg/{}", datExpac);
		else if (datType == L"03")
			pathPrefix = std::format("cut/{}", datExpac);
		else if (datType == L"00" && datExpac == "ffxiv")
			pathPrefix = "common";
		else if (datType == L"01" && datExpac == "ffxiv")
			pathPrefix = "bgcommon";
		else if (datType == L"04" && datExpac == "ffxiv")
			pathPrefix = "chara";
		else if (datType == L"05" && datExpac == "ffxiv")
			pathPrefix = "shader";
		else if (datType == L"06" && datExpac == "ffxiv")
			pathPrefix = "ui";
		else if (datType == L"07" && datExpac == "ffxiv")
			pathPrefix = "sound";
		else if (datType == L"08" && datExpac == "ffxiv")
			pathPrefix = "vfx";
		else if (datType == L"0a" && datExpac == "ffxiv")
			pathPrefix = "exd";
		else if (datType == L"0b" && datExpac == "ffxiv")
			pathPrefix = "game_script";
		if (!pathPrefix.empty()) {
			for (const auto& dir : rootDirs)
				dirs.emplace_back(dir / pathPrefix, dir);
		}

		ReplacementFiles result;
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
					"[{}] Failed to list items in {}: {}",
					packKey, dir, e.what());
				continue;
			}

			// later ones win, as they did when added to the pack one by one
			std::ranges::sort(files);
			for (const auto& file : files) {
				try {
					const auto pathSpec = xivres::path_spec(file.lexically_relative(relativeTo));
					if (FutureReservations::PackKey(pathSpec) != packKey)
						throw std::runtime_error(std::format("belongs to {}", FutureReservations::PackKey(pathSpec)));

					auto stream = std::make_shared<xivres::oplocking_packed_stream>(pathSpec, file);
					result.insert_or_assign(pathSpec, ReplacementFile{.File = file, .Stream = std::move(stream)});
				} catch (const std::exception& e) {
					m_logger->Format<LogLevel::Warning>(LogCategory::VirtualSqPacks,
						"[{}] Error processing {}: {}",
						packKey, file, e.what());
				}
			}
		}

		if (!result.empty())
			m_logger->Format<LogLevel::Info>(LogCategory::VirtualSqPacks, "[{}] Found {} replacement file(s)", packKey, result.size());
		return result;
	}
}
