#pragma once

#include <srell.hpp>

#include "Sqex_Sound_Reader.h"
#include "Utils_ListenerManager.h"
#include "Utils_Win32_Process.h"

namespace Sqex::Sound {

	struct MusicImportTargetChannel {
		std::string source;
		uint32_t channel;
	};

	void from_json(const nlohmann::json& j, MusicImportTargetChannel& o);

	struct MusicImportSegmentItem {
		std::vector<MusicImportTargetChannel> channels;
		std::map<std::string, double> sourceOffsets;
		std::map<std::string, double> sourceThresholds;
		std::map<std::string, std::string> sourceFilters;
		std::optional<double> length;  // in seconds
	};

	void from_json(const nlohmann::json& j, MusicImportSegmentItem& o);

	struct MusicImportTarget {
		std::vector<std::filesystem::path> path;
		std::vector<uint32_t> sequentialToFfmpegChannelIndexMap;
		float loopOffsetDelta;
		int loopLengthDivisor;
		std::vector<MusicImportSegmentItem> segments;
		bool enable;
	};

	void from_json(const nlohmann::json& j, MusicImportTarget& o);

	struct MusicImportSourceItemInputFile {
		std::optional<std::string> directory;
		std::string pattern;

		explicit MusicImportSourceItemInputFile(std::string pattern = {}, std::optional<std::string> directory = std::nullopt)
			: directory(directory)
			, pattern(pattern) {}

		const srell::u16wregex& GetCompiledPattern() const;

	private:
		mutable std::optional<srell::u16wregex> m_patternCompiled;
	};

	void from_json(const nlohmann::json& j, MusicImportSourceItemInputFile& o);

	struct MusicImportSourceItem {
		std::vector<std::vector<MusicImportSourceItemInputFile>> inputFiles;
		std::string filterComplex;
		std::string filterComplexOutName;
	};

	void from_json(const nlohmann::json& j, MusicImportSourceItem& o);

	struct MusicImportItem {
		std::map<std::string, MusicImportSourceItem> source;
		std::vector<MusicImportTarget> target;
	};

	void from_json(const nlohmann::json& j, MusicImportItem& o);

	struct MusicImportSearchDirectory {
		bool default_;
		std::map<std::string, std::string> purchaseLinks;
	};

	void from_json(const nlohmann::json& j, MusicImportSearchDirectory& o);

	struct MusicImportConfig {
		std::string name;
		std::map<std::string, MusicImportSearchDirectory> searchDirectories;
		std::vector<MusicImportItem> items;
	};

	void from_json(const nlohmann::json& j, MusicImportConfig& o);

	class MusicImporter {
		struct Implementation;
		friend struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		MusicImporter(std::map<std::string, MusicImportSourceItem> sourceItems, MusicImportTarget target, std::filesystem::path ffmpeg, std::filesystem::path ffprobe, Utils::Win32::Event cancelEvent);

		~MusicImporter();

		void SetSamplingRate(int samplingRate);

		void AppendReader(std::shared_ptr<Sqex::Sound::ScdReader> reader);

		bool ResolveSources(std::string dirName, const std::filesystem::path& dir);

		void Merge(const std::function<void(const std::filesystem::path& path, std::vector<uint8_t>)>& cb);

		Utils::ListenerManager<Implementation, void, const std::string&> OnWarningLog;
	};

}
