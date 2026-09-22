#include "pch.h"
#include "MainApp/Modding/TtmpSet.h"

#include "Config.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	void TtmpSet::FixChoices() {
		if (!Choices.is_array())
			Choices = nlohmann::json::array();
		for (size_t pageObjectIndex = 0; pageObjectIndex < List.ModPackPages.size(); ++pageObjectIndex) {
			const auto& modGroups = List.ModPackPages[pageObjectIndex].ModGroups;
			if (modGroups.empty())
				continue;

			while (Choices.size() <= pageObjectIndex)
				Choices.emplace_back(nlohmann::json::array());

			auto& pageChoices = Choices.at(pageObjectIndex);
			if (!pageChoices.is_array())
				pageChoices = nlohmann::json::array();

			for (size_t modGroupIndex = 0; modGroupIndex < modGroups.size(); ++modGroupIndex) {
				const auto& modGroup = modGroups[modGroupIndex];
				if (modGroups.empty())
					continue;

				while (pageChoices.size() <= modGroupIndex)
					pageChoices.emplace_back(modGroup.SelectionType == "Multi" ? nlohmann::json::array() : nlohmann::json::array({0}));

				auto& modGroupChoice = pageChoices.at(modGroupIndex);
				if (!modGroupChoice.is_array())
					modGroupChoice = nlohmann::json::array({modGroupChoice});

				for (auto& e : modGroupChoice) {
					if (!e.is_number_unsigned())
						e = 0;
					else if (e.get<size_t>() >= modGroup.OptionList.size())
						e = modGroup.OptionList.size() - 1;
				}
				modGroupChoice = modGroupChoice.get<std::set<size_t>>();
			}
		}
	}

	bool TtmpSet::ForEachEntryInterruptible(bool choiceOnly, std::function<bool(const xivres::textools::mods_json&)> cb) const {
		return List.for_each_breakable(cb, choiceOnly ? Choices : nlohmann::json{});
	}

	void TtmpSet::ForEachEntry(bool choiceOnly, std::function<void(const xivres::textools::mods_json&)> cb) const {
		List.for_each(cb, choiceOnly ? Choices : nlohmann::json{});
	}

	void TtmpSet::TryCleanupUnusedFiles() {
		DataStream.reset();
		std::vector paths{
			ListPath.parent_path() / "TTMPD.mpd",
			ListPath.parent_path() / "compression",
		};
		for (const auto& profile : Config::Acquire()->Runtime.TtmpChoicesFiles.Value()) {
			if (profile.FileName.empty())
				continue;
			paths.emplace_back(ListPath.parent_path() / profile.FileName);
			paths.emplace_back(ListPath.parent_path() / (profile.FileName + ".disable"));
		}
		paths.emplace_back(ListPath.parent_path() / "disable");
		paths.emplace_back(ListPath.parent_path());
		for (const auto& path : paths) {
			try {
				remove(path);
			} catch (...) {
				// pass
			}
		}
	}
}
