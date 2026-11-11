#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Records/WatchList.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::WatchList FontReplacement::WatchList::Of(
	std::map<std::filesystem::path, std::optional<std::filesystem::file_time_type>> presets, const std::vector<std::filesystem::path>& glyphImageFolders) {
	WatchList res;
	std::error_code ec;
	for (const auto& path : presets | std::views::keys) {
		if (const auto folder = path.parent_path(); is_directory(folder, ec))
			res.PresetFolders.insert(folder);
	}
	for (const auto& folder : glyphImageFolders) {
		if (is_directory(folder, ec))
			res.GlyphImageFolders.insert(folder);
	}
	res.Presets = std::move(presets);
	return res;
}
