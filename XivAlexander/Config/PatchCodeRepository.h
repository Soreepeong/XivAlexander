#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <xivres/util.listener_manager.h>

#include "PatchInstruction.h"
#include "Utils/Win32/Handle.h"

namespace XivAlexander {
	namespace Misc {
		class Logger;
	}

	class PatchCodeRepository {
	public:
		struct Entry {
			std::filesystem::path Path;
			std::string FileName;
			PatchInstruction Patch;
			std::string Digest;

			bool operator==(const Entry&) const = default;
		};

	private:
		const std::shared_ptr<Misc::Logger> m_logger;
		const std::filesystem::path m_directory;

		mutable std::mutex m_mtx;
		std::shared_ptr<const std::vector<Entry>> m_entries;

		const Utils::Win32::Event m_stopWatching;
		Utils::Win32::Thread m_watcher;

	public:
		explicit PatchCodeRepository(std::filesystem::path directory);
		~PatchCodeRepository();

		xivres::util::listener_manager<PatchCodeRepository, void> OnChange;

		[[nodiscard]] const std::filesystem::path& GetDirectory() const { return m_directory; }
		[[nodiscard]] std::shared_ptr<const std::vector<Entry>> GetEntries() const;

		void Reload();
		void StartWatching();
	};
}
