#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <xivres/sqpack.generator.h>
#include <xivres/stream.h>

#include "MainApp/Modding/TtmpLibrary.h"

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Misc {
	class Logger;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class FutureReservations;

	class PackSources {
	public:
		struct ReplacementFile {
			std::filesystem::path File;
			std::shared_ptr<const xivres::packed_stream> Stream;
		};

		using ReplacementFiles = std::map<xivres::path_spec, ReplacementFile, xivres::path_spec::AllHashComparator>;

	private:
		const std::shared_ptr<Config> m_config;
		const std::shared_ptr<Misc::Logger> m_logger;
		const std::vector<std::filesystem::path> m_additionalGameDirectories;

		mutable std::mutex m_replacementMtx;
		std::vector<std::filesystem::path> m_replacementRoots;
		uint64_t m_replacementGeneration = 0;
		mutable std::map<std::string, std::shared_ptr<const ReplacementFiles>> m_replacementFiles;

	public:
		PackSources();
		~PackSources();

		std::shared_ptr<const xivres::stream> Populate(
			xivres::sqpack::generator& creator,
			const std::filesystem::path& indexFile,
			const TtmpLibrary::Reservations& modpackReservations,
			FutureReservations& futureReservations) const;

		[[nodiscard]] std::shared_ptr<const ReplacementFiles> GetReplacementFiles(const std::string& packKey, const std::filesystem::path& indexPath) const;
		void RescanReplacementRoots();

	private:
		[[nodiscard]] std::vector<std::filesystem::path> CollectReplacementRoots() const;
		[[nodiscard]] std::vector<std::filesystem::path> CollectAdditionalGameDirectories() const;
		[[nodiscard]] ReplacementFiles ScanReplacementFiles(const std::vector<std::filesystem::path>& roots, const std::string& packKey, const std::filesystem::path& indexPath) const;
	};
}
