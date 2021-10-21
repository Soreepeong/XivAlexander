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
		class FloatPcmSource {
			Utils::Win32::Process m_hReaderProcess;
			Utils::Win32::Handle m_hStdoutReader;
			Utils::Win32::Thread m_hStdinWriterThread;
			Utils::Win32::Thread m_hStderrReaderThread;

			std::vector<uint8_t> m_buffer;
			size_t m_unusedBytes = 0;

		public:
			FloatPcmSource(
				const MusicImportSourceItem& sourceItem,
				std::vector<std::filesystem::path> resolvedPaths,
				std::function<std::span<uint8_t>(size_t len, bool throwOnIncompleteRead)> linearReader, const char* linearReaderType,
				const std::filesystem::path& ffmpegPath,
				std::function<void(const std::string&)> stderrCallback,
				int forceSamplingRate = 0, std::string audioFilters = {}
			);

			~FloatPcmSource();

			std::span<float> operator()(size_t len, bool throwOnIncompleteRead);
		};

		static nlohmann::json RunProbe(const std::filesystem::path& path, const std::filesystem::path& ffprobePath, std::function<void(const std::string&)> stderrCallback);

		static nlohmann::json RunProbe(const char* originalFormat, std::function<std::span<uint8_t>(size_t len, bool throwOnIncompleteRead)> linearReader, const std::filesystem::path& ffprobePath, std::function<void(const std::string&)> stderrCallback);

		std::map<std::string, MusicImportSourceItem> m_sourceItems;
		MusicImportTarget m_target;
		const std::filesystem::path m_ffmpeg, m_ffprobe;

		std::map<std::string, std::vector<std::filesystem::path>> m_sourcePaths;
		std::vector<std::shared_ptr<Sqex::Sound::ScdReader>> m_targetOriginals;

		struct SourceSet {
			uint32_t Rate{};
			uint32_t Channels{};

			std::unique_ptr<FloatPcmSource> Reader;
			std::vector<float> ReadBuf;
			size_t ReadBufPtr{};

			std::vector<int> FirstBlocks;
		};
		std::map<std::string, SourceSet> m_sourceInfo;

		static constexpr auto OriginalSource = "target";

		Utils::Win32::Event m_cancelEvent;

	public:
		MusicImporter(std::map<std::string, MusicImportSourceItem> sourceItems, MusicImportTarget target, std::filesystem::path ffmpeg, std::filesystem::path ffprobe, Utils::Win32::Event cancelEvent);

		void AppendReader(std::shared_ptr<Sqex::Sound::ScdReader> reader);

		bool ResolveSources(std::string dirName, const std::filesystem::path& dir);

		void Merge(const std::function<void(const std::filesystem::path& path, std::vector<uint8_t>)>& cb);

		Utils::ListenerManager<MusicImporter, void, const std::string&> OnWarningLog;

	private:
		void ShowWarningLog(const std::string& s) {
			OnWarningLog(s);
		}
	};

}
