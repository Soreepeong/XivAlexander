#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// The files whose edits make the fonts again.
	struct WatchList {
		// The preset files, by when each was last written as of its reading (nullopt if missing then).
		std::map<std::filesystem::path, std::optional<std::filesystem::file_time_type>> Presets;

		// The folders they are in, of those that exist.
		std::set<std::filesystem::path> PresetFolders;

		// The presets' glyph image folders, of those that exist.
		std::set<std::filesystem::path> GlyphImageFolders;

		// nullopt if the file can't be asked.
		[[nodiscard]] static std::optional<std::filesystem::file_time_type> LastWriteOf(const std::filesystem::path& path) {
			std::error_code ec;
			const auto time = last_write_time(path, ec);
			return ec ? std::nullopt : std::optional(time);
		}

		// Looks for the folders, so it is for the loader's thread.
		[[nodiscard]] static WatchList Of(std::map<std::filesystem::path, std::optional<std::filesystem::file_time_type>> presets, const std::vector<std::filesystem::path>& glyphImageFolders);
	};
}
