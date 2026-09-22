#pragma once

#include <filesystem>
#include <memory>
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
		const std::shared_ptr<Config> m_config;
		const std::shared_ptr<Misc::Logger> m_logger;
		const std::vector<std::filesystem::path> m_additionalGameDirectories;
		const std::vector<std::filesystem::path> m_replacementRoots;

	public:
		PackSources();
		~PackSources();

		std::shared_ptr<const xivres::stream> Populate(
			xivres::sqpack::generator& creator,
			const std::filesystem::path& indexFile,
			const TtmpLibrary::Reservations& modpackReservations,
			FutureReservations& futureReservations) const;

	private:
		[[nodiscard]] std::vector<std::filesystem::path> CollectReplacementRoots() const;
		[[nodiscard]] std::vector<std::filesystem::path> CollectAdditionalGameDirectories() const;
		void AddReplacementFiles(xivres::sqpack::generator& creator, const std::filesystem::path& indexPath) const;
	};
}
