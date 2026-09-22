#pragma once

#include <filesystem>
#include <functional>
#include <memory>

#include <xivres/stream.h>
#include <xivres/textools.h>

namespace XivAlexander::Apps::MainApp::Features::Modding {
	struct TtmpSet {
		bool Allocated = false;
		std::filesystem::path ListPath;
		xivres::textools::mod_pack_json List;
		std::filesystem::path DataPath;
		std::shared_ptr<xivres::stream> DataStream;
		nlohmann::json Choices;

		void FixChoices();

		void ForEachEntry(bool choiceOnly, std::function<void(const xivres::textools::mods_json&)> cb) const;
		bool ForEachEntryInterruptible(bool choiceOnly, std::function<bool(const xivres::textools::mods_json&)> cb) const;

		void TryCleanupUnusedFiles();
	};
}
