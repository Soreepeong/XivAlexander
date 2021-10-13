#include "pch.h"

#include <XivAlexanderCommon/Sqex_Sound_Reader.h>
#include <XivAlexanderCommon/Sqex_Sound_Writer.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Utils_Win32_Process.h>
#include <XivAlexanderCommon/Utils_Win32_ThreadPool.h>

struct MusicImportTargetChannel {
	std::string source;
	uint32_t channel;
};

void from_json(const nlohmann::json& j, MusicImportTargetChannel& o) {
	const char* lastAttempt;
	try {
		o.source = j.at(lastAttempt = "source").get<std::string>();
		o.channel = j.at(lastAttempt = "channel").get<uint32_t>();
	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

struct MusicImportSegmentItem {
	std::vector<MusicImportTargetChannel> channels;
	std::map<std::string, double> sourceOffsets;
	std::map<std::string, double> sourceThresholds;
	std::map<std::string, std::string> sourceFilters;
	std::optional<double> length;  // in seconds
};

void from_json(const nlohmann::json& j, MusicImportSegmentItem& o) {
	const char* lastAttempt;
	try {
		o.channels = j.at(lastAttempt = "channels").get<std::vector<MusicImportTargetChannel>>();
		o.sourceOffsets = j.value(lastAttempt = "sourceOffsets", decltype(o.sourceOffsets)());
		o.sourceThresholds = j.value(lastAttempt = "sourceThresholds", decltype(o.sourceThresholds)());
		o.sourceFilters = j.value(lastAttempt = "sourceFilters", decltype(o.sourceFilters)());

		if (const auto it = j.find(lastAttempt = "length"); it == j.end())
			o.length = std::nullopt;
		else
			o.length = it->get<double>();

	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

struct MusicImportTarget {
	std::vector<std::filesystem::path> path;
	float loopOffsetDelta;
	int loopLengthDivisor;
	std::vector<MusicImportSegmentItem> segments;
	bool enable;
};

void from_json(const nlohmann::json& j, MusicImportTarget& o) {
	const char* lastAttempt;
	try {
		if (const auto it = j.find(lastAttempt = "path"); it == j.end())
			throw std::invalid_argument("required key missing");
		else if(it->is_array())
			o.path = it->get<std::vector<std::filesystem::path>>();
		else if (it->is_string())
			o.path = { it->get<std::filesystem::path>() };
		else
			throw std::invalid_argument("only array or string is accepted");

		o.loopOffsetDelta = j.value("loopOffsetDelta", 0.f);
		o.loopLengthDivisor = j.value("loopLengthDivisor", 1);

		if (const auto it = j.find(lastAttempt = "segments"); it == j.end())
			o.segments = {};
		else
			o.segments = it->get<std::vector<MusicImportSegmentItem>>();

		o.enable = j.value("enable", true);
	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

struct MusicImportSourceItem {
	std::vector<std::vector<std::string>> inputFiles;
	std::string filterComplex;
	std::string filterComplexOutName;
};

void from_json(const nlohmann::json& j, MusicImportSourceItem& o) {
	const char* lastAttempt;
	try {
		if (j.is_array()) {
			lastAttempt = "<array>";
			if (j.empty())
				return;
			if (j.at(0).is_array())
				o.inputFiles = j.get<decltype(o.inputFiles)>();
			else
				o.inputFiles = { j.get<std::vector<std::string>>() };
		} else if (j.is_string()) {
			lastAttempt = "<string>";
			o.inputFiles = {{ j.get<std::string>() }};
		} else {
			if (const auto it = j.find(lastAttempt = "inputFiles"); it == j.end())
				throw std::invalid_argument("required key missing");
			else if (it->is_array()) {
				if (!it->empty()) {
					if (it->at(0).is_array())
						o.inputFiles = it->get<std::vector<std::vector<std::string>>>();
					else
						o.inputFiles = { it->get<std::vector<std::string>>() };
				}
			} else if (it->is_string())
				o.inputFiles = {{ it->get<std::string>() }};
			else
				throw std::invalid_argument("only array, object, or string is accepted");

			o.filterComplex = j.value(lastAttempt = "filterComplex", std::string());
			o.filterComplexOutName = j.value(lastAttempt = "filterComplexOutName", std::string());
		}
	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

struct MusicImportItem {
	std::map<std::string, MusicImportSourceItem> source;
	std::vector<MusicImportTarget> target;
};

void from_json(const nlohmann::json& j, MusicImportItem& o) {
	const char* lastAttempt;
	try {
		if (const auto it = j.find(lastAttempt = "source"); it == j.end())
			throw std::invalid_argument("required key missing");
		else if (it->is_object())
			o.source = it->get<std::map<std::string, MusicImportSourceItem>>();
		else if (it->is_array() || it->is_string())
			o.source = {{"source", it->get<MusicImportSourceItem>()}};
		else
			throw std::invalid_argument("only array or object is accepted");

		if (const auto it = j.find(lastAttempt = "target"); it == j.end())
			throw std::invalid_argument("required key missing");
		else if(it->is_array())
			o.target = it->get<std::vector<MusicImportTarget>>();
		else if (it->is_object())
			o.target = { it->get<MusicImportTarget>() };
		else
			throw std::invalid_argument("only array or object is accepted");
	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

struct MusicImportConfig {
	std::string name;
	std::map<std::string, std::string> purchaseLinks;
	std::vector<MusicImportItem> items;
};

void from_json(const nlohmann::json& j, MusicImportConfig& o) {
	const char* lastAttempt;
	try {
		o.name = j.value(lastAttempt = "name", decltype(o.name)());
		o.purchaseLinks = j.value(lastAttempt = "purchaseLinks", decltype(o.purchaseLinks)());
		o.items = j.value(lastAttempt = "items", decltype(o.items)());
	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

class MusicImporter {
	class FloatPcmSource {
		Utils::Win32::Process m_hReaderProcess;
		Utils::Win32::Handle m_hStdoutReader;
		Utils::Win32::Thread m_hStdinWriterThread;

		std::vector<float> m_buffer;

	public:
		FloatPcmSource(
			const MusicImportSourceItem& sourceItem,
			std::vector<std::filesystem::path> resolvedPaths,
			std::function<std::span<uint8_t>(size_t len, bool throwOnIncompleteRead)> linearOggReader,
			const std::filesystem::path& ffmpegPath, int forceSamplingRate = 0, std::string audioFilters = {}
		) {
			auto [hStdinRead, hStdinWrite] = Utils::Win32::Handle::FromCreatePipe();
			auto [hStdoutRead, hStdoutWrite] = Utils::Win32::Handle::FromCreatePipe();
			auto builder = Utils::Win32::ProcessBuilder();
			builder
				.WithPath(ffmpegPath)
				.WithStdin(std::move(hStdinRead))
				.WithStdout(std::move(hStdoutWrite));
			
			auto useOggPipe = false;
			builder.WithAppendArgument("-hide_banner");
			for (size_t i = 0; i < sourceItem.inputFiles.size(); ++i) {
				if (sourceItem.inputFiles.at(i).empty()) {
					builder.WithAppendArgument("-f").WithAppendArgument("ogg");
					builder.WithAppendArgument("-i").WithAppendArgument("-");
					useOggPipe = true;
				} else
					builder.WithAppendArgument("-i").WithAppendArgument(resolvedPaths[i].wstring());
			}
			builder.WithAppendArgument("-f").WithAppendArgument("f32le");
			if (sourceItem.filterComplex.empty()) {
				if (forceSamplingRate && audioFilters.empty())
					builder.WithAppendArgument("-filter:a").WithAppendArgument("aresample={}:resampler=soxr", forceSamplingRate);
				else if (forceSamplingRate && !audioFilters.empty())
					builder.WithAppendArgument("-filter:a").WithAppendArgument("aresample={}:resampler=soxr,{}", forceSamplingRate, audioFilters);
				else if (!forceSamplingRate && !audioFilters.empty())
					builder.WithAppendArgument("-filter:a").WithAppendArgument(audioFilters);
			} else {
				if (!forceSamplingRate && audioFilters.empty())
					builder.WithAppendArgument("-map").WithAppendArgument(sourceItem.filterComplexOutName);
				else if (forceSamplingRate && audioFilters.empty())
					builder.WithAppendArgument("-filter_complex").WithAppendArgument("{}; {} aresample={}:resampler=soxr [_xa_final_output]", sourceItem.filterComplex, sourceItem.filterComplexOutName, forceSamplingRate)
						.WithAppendArgument("-map").WithAppendArgument("[_xa_final_output]");
				else if (!forceSamplingRate && !audioFilters.empty())
					builder.WithAppendArgument("-filter_complex").WithAppendArgument("{}; {} {} [_xa_final_output]", sourceItem.filterComplex, sourceItem.filterComplexOutName, audioFilters)
					.WithAppendArgument("-map").WithAppendArgument("[_xa_final_output]");
				else if (forceSamplingRate && !audioFilters.empty())
					builder.WithAppendArgument("-filter_complex").WithAppendArgument("{}; {} aresample={}:resampler=soxr,{} [_xa_final_output]", sourceItem.filterComplex, sourceItem.filterComplexOutName, forceSamplingRate, audioFilters)
					.WithAppendArgument("-map").WithAppendArgument("[_xa_final_output]");
				builder.WithStderr(GetStdHandle(STD_ERROR_HANDLE));
			}
			if (forceSamplingRate)
				builder.WithAppendArgument("-ar").WithAppendArgument("{}", forceSamplingRate);
			builder.WithAppendArgument("-");

			m_hReaderProcess = builder.Run().first;
			m_hStdoutReader = std::move(hStdoutRead);

			if (!useOggPipe)
				return;

			m_hStdinWriterThread = Utils::Win32::Thread(L"MusicImporter::Source", [hStdinWriter = std::move(hStdinWrite), linearOggReader = std::move(linearOggReader)]{
				try {
					while (true) {
						const auto read = linearOggReader(8192, false);
						if (read.empty())
							break;
						hStdinWriter.Write(0, read);
					};
				} catch (const Utils::Win32::Error& e) {
					if (e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA) {
						std::cout << std::format("Error on MusicImporter::Source: {}\n", e.what());
					}
				}
			});
		}
		
		~FloatPcmSource() {
			if (m_hReaderProcess)
				m_hReaderProcess.Terminate(0);
			if (m_hStdinWriterThread)
				m_hStdinWriterThread.Wait();
		}

		std::span<float> operator()(size_t len, bool throwOnIncompleteRead) {
			m_buffer.resize(len);
			return std::span(m_buffer).subspan(0, m_hStdoutReader.Read(0, std::span(m_buffer), throwOnIncompleteRead ? Utils::Win32::Handle::PartialIoMode::AlwaysFull : Utils::Win32::Handle::PartialIoMode::AllowPartial));
		}
	};

	static nlohmann::json RunProbe(const std::filesystem::path& path, const std::filesystem::path& ffprobePath) {
		auto [hStdoutRead, hStdoutWrite] = Utils::Win32::Handle::FromCreatePipe();
		auto process = Utils::Win32::ProcessBuilder()
			.WithPath(ffprobePath)
			.WithStdout(std::move(hStdoutWrite))
			.WithAppendArgument("-hide_banner")
			.WithAppendArgument("-i").WithAppendArgument(path.wstring())
			.WithAppendArgument("-show_streams")
			.WithAppendArgument("-select_streams").WithAppendArgument("a:0")
			.WithAppendArgument("-print_format").WithAppendArgument("json")
			.Run().first;

		std::string str;
		try {
			while (true) {
				const auto mark = str.size();
				str.resize(mark + std::max<size_t>(8192, std::min<size_t>(65536, mark)));
				str.resize(mark + hStdoutRead.Read(0, &str[mark], str.size() - mark, Utils::Win32::Handle::PartialIoMode::AllowPartial));
				if (str.size() == mark)
					break;
			}
		} catch (const Utils::Win32::Error& e) {
			if (e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA)
				throw;
		}
		return nlohmann::json::parse(str);
	}

	static nlohmann::json RunProbe(const std::string& originalFormat, std::function<std::span<uint8_t>(size_t len, bool throwOnIncompleteRead)> linearReader, const std::filesystem::path& ffprobePath) {
		auto [hStdoutRead, hStdoutWrite] = Utils::Win32::Handle::FromCreatePipe();
		auto [hStdinRead, hStdinWrite] = Utils::Win32::Handle::FromCreatePipe();
		const auto process = Utils::Win32::ProcessBuilder()
			.WithPath(ffprobePath)
			.WithStdin(std::move(hStdinRead))
			.WithStdout(std::move(hStdoutWrite))
			.WithAppendArgument("-hide_banner")
			.WithAppendArgument("-f").WithAppendArgument(originalFormat)
			.WithAppendArgument("-i").WithAppendArgument("-")
			.WithAppendArgument("-show_streams")
			.WithAppendArgument("-select_streams").WithAppendArgument("a:0")
			.WithAppendArgument("-print_format").WithAppendArgument("json")
			.Run().first;

		const auto stdoutWriter = Utils::Win32::Thread(L"RunProbe", [hStdinWrite = std::move(hStdinWrite), linearReader = std::move(linearReader)] {
			try {
				while (true) {
					const auto read = linearReader(8192, false);
					if (read.empty())
						break;
					hStdinWrite.Write(0, read);
				};
			} catch (const Utils::Win32::Error& e) {
				if (e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA) {
					std::cout << std::format("Error on MusicImporter::Source: {}\n", e.what());
				}
			}
		});

		std::string str;
		try {
			while (true) {
				const auto mark = str.size();
				str.resize(mark + std::max<size_t>(8192, std::min<size_t>(65536, mark)));
				str.resize(mark + hStdoutRead.Read(0, &str[mark], str.size() - mark, Utils::Win32::Handle::PartialIoMode::AllowPartial));
				if (str.size() == mark)
					break;
			}
		} catch (const Utils::Win32::Error& e) {
			if (e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA)
				throw;
		}
		process.Terminate(0);
		stdoutWriter.Wait();

		return nlohmann::json::parse(str);
	}
	
	MusicImportItem m_config;
	const std::filesystem::path m_ffmpeg, m_ffprobe;

	std::map<std::string, std::vector<std::vector<std::wregex>>> m_sourceNamePatterns;
	std::map<std::string, std::vector<std::filesystem::path>> m_sources;
	std::vector<std::vector<std::shared_ptr<Sqex::Sound::ScdReader>>> m_targetOriginals;
	
	struct SourceSet {
		uint32_t Rate{};
		uint32_t Channels{};

		std::unique_ptr<FloatPcmSource> Reader;
		std::vector<float> ReadBuf;
		size_t ReadBufPtr{};
			
		std::vector<int> FirstBlocks;
	};
	std::map<std::string, SourceSet> m_sourceInfo;

public:
	MusicImporter(MusicImportItem config, std::filesystem::path ffmpeg, std::filesystem::path ffprobe)
		: m_config(std::move(config))
		, m_ffmpeg(std::move(ffmpeg))
		, m_ffprobe(std::move(ffprobe)) {
		for (const auto& [sourceName, source] : m_config.source) {
			m_sources[sourceName].resize(source.inputFiles.size());
			auto& compiledSources = m_sourceNamePatterns[sourceName];
			for (size_t i = 0; i < source.inputFiles.size(); ++i) {
				const auto& patterns = source.inputFiles.at(i);
				compiledSources.emplace_back();
				for (const auto& pattern : patterns)
					compiledSources.back().emplace_back(std::wregex(Utils::FromUtf8(pattern), std::wregex::icase));
			}
		}

		if (!m_config.source.contains("target")) {
			m_config.source["target"] = MusicImportSourceItem{
				.inputFiles = {{}}
			};
			m_sources["target"] = {};
		}

		m_targetOriginals.resize(m_config.target.size());
	}

	void LoadTargetInfo(const std::function<std::shared_ptr<Sqex::Sound::ScdReader>(std::string ex, std::filesystem::path path)>& cb) {
		for (size_t i = 0; i < m_config.target.size(); ++i) {
			if (!m_config.target[i].enable)
				continue;
			for (size_t j = m_targetOriginals[i].size(); j < m_config.target[i].path.size(); ++j) {
				m_targetOriginals[i].push_back(cb(Utils::ToUtf8(Utils::StringSplit<std::wstring>(m_config.target[i].path[j].wstring(), L"/")[1]), m_config.target[i].path[j]));
			}
		}
	}

	bool ResolveSources(const std::filesystem::path& dir) {
		auto allFound = true;
		for (const auto& item : std::filesystem::directory_iterator(dir)) {
			for (const auto& [sourceName, source] : m_sourceNamePatterns) {
				bool found = true;

				for (size_t i = 0; i < source.size(); ++i) {
					if (!m_sources[sourceName][i].empty())
						continue;
					found = false;
					const auto& patterns = source.at(i);
					for (const auto& pattern : patterns) {
						if (std::regex_search(item.path().filename().wstring(), pattern)) {
							try {
								const auto probe = RunProbe(item.path(), m_ffprobe).at("streams").at(0);
								m_sourceInfo[sourceName] = {
									.Rate = static_cast<uint32_t>(std::strtoul(probe.at("sample_rate").get<std::string>().c_str(), nullptr, 10)),
									.Channels = probe.at("channels").get<uint32_t>(),
								};
								m_sources[sourceName][i] = item.path();
								break;
							} catch (const std::exception&) {
								// TODO: notify user
							}
						}
					}
				}

				allFound &= found;
			}
		}
		return allFound;
	}

	void Merge(const std::function<void(const std::string& ex, const std::filesystem::path &path, std::vector<uint8_t>)>& cb) {
		for (size_t i = 0; i < m_config.target.size(); ++i) {
			auto& targetSet = m_config.target[i];
			if (!targetSet.enable)
				continue;

			for (size_t j = 0; j < targetSet.path.size(); ++j) {
				std::vector<uint8_t> res;
				std::cout << std::format("Working on {}...\n", targetSet.path[j].wstring());
				res = MergeSingle(targetSet, i, j);
				cb(Utils::ToUtf8(Utils::StringSplit<std::wstring>(m_config.target[i].path[j].wstring(), L"/")[1]), m_config.target[i].path[j], std::move(res));
			}
		}
	}

private:
	std::vector<uint8_t> MergeSingle(MusicImportTarget& targetSet, size_t targetIndex, size_t pathIndex) {
		auto& originalInfo = m_sourceInfo["target"];
		auto& scdReader = m_targetOriginals[targetIndex][pathIndex];
		if (!scdReader)
			throw std::runtime_error(std::format("file {} not found", m_config.target.at(targetIndex).path[pathIndex].wstring()));
		const auto originalEntry = scdReader->GetSoundEntry(0);
		const auto originalOgg = originalEntry.GetOggFile();
		const auto originalOggStream = Sqex::MemoryRandomAccessStream(originalOgg);

		uint32_t loopStartBlockIndex = 0;
		uint32_t loopEndBlockIndex = 0;
		{
			const auto originalProbe = RunProbe("ogg", originalOggStream.AsLinearReader<uint8_t>(), m_ffprobe).at("streams").at(0);
			originalInfo = {
				.Rate = static_cast<uint32_t>(std::strtoul(originalProbe.at("sample_rate").get<std::string>().c_str(), nullptr, 10)),
				.Channels = originalProbe.at("channels").get<uint32_t>(),
			};
			if (const auto it = originalProbe.find("tags"); it != originalProbe.end()) {
				for (const auto& item : it->get<nlohmann::json::object_t>()) {
					if (_strnicmp(item.first.c_str(), "LoopStart", 9) == 0)
						loopStartBlockIndex = std::strtoul(item.second.get<std::string>().c_str(), nullptr, 10);
					else if (_strnicmp(item.first.c_str(), "LoopEnd", 7) == 0)
						loopEndBlockIndex = std::strtoul(item.second.get<std::string>().c_str(), nullptr, 10);
				}
			}
		}
		
		// std::cout << std::format("{:.3f} -> {:.3f}\n", 1. * loopStartBlockIndex / originalInfo.Rate, 1. * loopEndBlockIndex / originalInfo.Rate);

		uint32_t targetRate = 0;
		for (const auto& targetInfo : m_sourceInfo | std::views::values)
			targetRate = std::max(targetRate, targetInfo.Rate);
		loopStartBlockIndex = static_cast<uint32_t>(1ULL * loopStartBlockIndex * targetRate / originalInfo.Rate);
		loopEndBlockIndex = static_cast<uint32_t>(1ULL * loopEndBlockIndex * targetRate / originalInfo.Rate);
		
		if (!!targetSet.loopOffsetDelta) {
			loopStartBlockIndex -= static_cast<uint32_t>(static_cast<double>(targetRate) * targetSet.loopOffsetDelta);
			loopEndBlockIndex -= static_cast<uint32_t>(static_cast<double>(targetRate) * targetSet.loopOffsetDelta);
		}
		if (targetSet.loopLengthDivisor != 1) {
			loopEndBlockIndex = loopStartBlockIndex + (loopEndBlockIndex - loopStartBlockIndex) / targetSet.loopLengthDivisor;
		}
		// std::cout << std::format("{:.3f} -> {:.3f}\n", 1. * loopStartBlockIndex / targetRate, 1. * loopEndBlockIndex / targetRate);

		if (targetSet.segments.empty()) {
			if (m_sources.size() != 2)  // user-specified and "target"
				throw std::invalid_argument("segments must be set when there are more than one source");
			targetSet.segments = std::vector<MusicImportSegmentItem>{
				{
					.channels = std::vector<MusicImportTargetChannel>{
						{.source = m_sources.begin()->first == "target" ? m_sources.rbegin()->first : m_sources.begin()->first, .channel = 0},
						{.source = m_sources.begin()->first == "target" ? m_sources.rbegin()->first : m_sources.begin()->first, .channel = 1},
					},
				},
			};
		}

		for (const auto& segment : targetSet.segments) {
			if (originalInfo.Channels != segment.channels.size())
				throw std::invalid_argument(std::format("originalChannels={}, expected={}", originalInfo.Channels, segment.channels.size()));
		}

		uint32_t currentBlockIndex = 0;
		const auto endBlockIndex = loopEndBlockIndex ? loopEndBlockIndex : UINT32_MAX;
		
		const auto BufferedBlockCount = 8192;
		std::vector<uint8_t> headerBuffer;
		std::deque<std::vector<uint8_t>> dataBuffer;

		vorbis_info vi{};
		vorbis_info_init(&vi);
		if (const auto res = vorbis_encode_init_vbr(&vi, originalInfo.Channels, targetRate, 1))
			throw std::runtime_error(std::format("vorbis_encode_init_vbr: {}", res));
		auto viCleanup = Utils::CallOnDestruction([&vi] { vorbis_info_clear(&vi); });

		vorbis_dsp_state vd{};
		if (const auto res = vorbis_analysis_init(&vd, &vi))
			throw std::runtime_error(std::format("vorbis_analysis_init: {}", res));
		auto vdCleanup = Utils::CallOnDestruction([&vd] { vorbis_dsp_clear(&vd); });

		vorbis_block vb{};
		if (const auto res = vorbis_block_init(&vd, &vb))
			throw std::runtime_error(std::format("vorbis_block_init: {}", res));
		auto vbCleanup = Utils::CallOnDestruction([&vb] { vorbis_block_clear(&vb); });

		ogg_stream_state os{};
		if (const auto res = ogg_stream_init(&os, 0))
			throw std::runtime_error(std::format("ogg_stream_init: {}", res));
		auto osCleanup = Utils::CallOnDestruction([&os] { ogg_stream_clear(&os); });

		uint32_t oggDataSize = 0;
		std::vector<uint32_t> oggDataSeekTable;
		uint32_t loopStartOffset = 0;
		uint32_t loopEndOffset = 0;

		ogg_page og{};
		{
			vorbis_comment vc{};
			vorbis_comment_init(&vc);
			const auto vcCleanup = Utils::CallOnDestruction([&vc] { vorbis_comment_clear(&vc); });
			if (loopStartBlockIndex || loopEndBlockIndex) {
				vorbis_comment_add_tag(&vc, "LoopStart", std::format("{}", loopStartBlockIndex).c_str());
				vorbis_comment_add_tag(&vc, "LoopEnd", std::format("{}", loopEndBlockIndex).c_str());
			}

			ogg_packet header{};
			ogg_packet headerComments{};
			ogg_packet headerCode{};
			vorbis_analysis_headerout(&vd, &vc, &header, &headerComments, &headerCode);
			ogg_stream_packetin(&os, &header);
			ogg_stream_packetin(&os, &headerComments);
			ogg_stream_packetin(&os, &headerCode);

			headerBuffer.reserve(8192);
			while (true) {
				if (const auto res = ogg_stream_flush_fill(&os, &og, 0); res < 0)
					throw std::runtime_error(std::format("ogg_stream_flush_fill: {}", res));
				else if (res == 0)
					break;

				headerBuffer.insert(headerBuffer.end(), og.header, og.header + og.header_len);
				headerBuffer.insert(headerBuffer.end(), og.body, og.body + og.body_len);
			}
		}

		ogg_packet op{};
		uint64_t granulePosOffset = 0;
		for (size_t segmentIndex = 0; segmentIndex < targetSet.segments.size(); ++segmentIndex) {
			const auto& segment = targetSet.segments[segmentIndex];
			std::set<std::string> usedSources;
			for (const auto& name : m_sourceInfo | std::views::keys) {
				if (name == "target" || std::ranges::any_of(segment.channels, [&name](const auto& ch) { return ch.source == name; }))
					usedSources.insert(name);
			}

			for (const auto& name : usedSources) {
				auto& info = m_sourceInfo.at(name);
				info.FirstBlocks.clear();
				info.FirstBlocks.resize(info.Channels, INT32_MAX);

				const auto ffmpegFilter = segment.sourceFilters.contains(name) ? segment.sourceFilters.at(name) : std::string();
				uint32_t minBlockIndex = 0;
				double threshold = 0.1;
				info.Reader = std::make_unique<FloatPcmSource>(m_config.source.at(name), m_sources.at(name), originalOggStream.AsLinearReader<uint8_t>(), m_ffmpeg, targetRate, ffmpegFilter);
				if (segment.sourceOffsets.contains(name))
					minBlockIndex = static_cast<uint32_t>(targetRate * segment.sourceOffsets.at(name));
				else if (name == "target")
					minBlockIndex = currentBlockIndex;

				info.ReadBuf.clear();
				info.ReadBufPtr = 0;
				if (segment.sourceThresholds.contains(name))
					threshold = segment.sourceThresholds.at(name);
						
				uint32_t blockIndex = 0;
				auto pending = true;
				while (pending) {
					const auto buf = (*info.Reader)(info.Channels * static_cast<size_t>(8192), false);
					if (buf.empty())
						break;

					info.ReadBuf.insert(info.ReadBuf.end(), buf.begin(), buf.end());
					for (; pending && (blockIndex + 1) * info.Channels - 1 < info.ReadBuf.size(); ++blockIndex) {
						pending = false;
						for (size_t c = 0; c < info.Channels; ++c) {
							if (info.FirstBlocks[c] != INT32_MAX)
								continue;
							if (info.ReadBuf[blockIndex * info.Channels + c] >= threshold && blockIndex >= minBlockIndex)
								info.FirstBlocks[c] = blockIndex;
							else
								pending = true;
						}
					}
				}
			}
		
			for (const auto& name : usedSources) {
				auto& info = m_sourceInfo.at(name);
				for (size_t channelIndex = 0; channelIndex < segment.channels.size(); ++channelIndex) {
					const auto& channelMap = segment.channels[channelIndex];
					if (channelMap.source == name) {
						auto srcCopyFromOffset = static_cast<SSIZE_T>(originalInfo.FirstBlocks[channelIndex]) - info.FirstBlocks[channelMap.channel] - currentBlockIndex;
						srcCopyFromOffset *= info.Channels;

						if (srcCopyFromOffset < 0) {
							info.ReadBuf.erase(info.ReadBuf.begin(), info.ReadBuf.begin() - srcCopyFromOffset);
						} else if (srcCopyFromOffset > 0) {
							info.ReadBuf.resize(info.ReadBuf.size() + srcCopyFromOffset);
							std::move(info.ReadBuf.begin(), info.ReadBuf.end() - srcCopyFromOffset, info.ReadBuf.begin() + srcCopyFromOffset);
							std::fill_n(info.ReadBuf.begin(), srcCopyFromOffset, 0.f);
						}
						break;
					}
				}
			}

			std::vector<SourceSet*> sourceSetsByIndex;
			for (size_t i = 0; i < originalInfo.Channels; ++i)
				sourceSetsByIndex.push_back(&m_sourceInfo.at(segment.channels[i].source));
			size_t wrote = 0;

			const auto segmentEndBlockIndex = segment.length ? currentBlockIndex + static_cast<uint32_t>(targetRate * *segment.length) : endBlockIndex;

			auto stopSegment = currentBlockIndex >= segmentEndBlockIndex;
			float** buf = nullptr;
			uint32_t bufptr = 0;
			while (!stopSegment) {
				if (buf == nullptr) {
					buf = vorbis_analysis_buffer(&vd, BufferedBlockCount);
					if (!buf)
						throw std::runtime_error("vorbis_analysis_buffer: fail");
					bufptr = 0;
				}
				for (size_t i = 0; i < originalInfo.Channels; ++i) {
					const auto pSource = sourceSetsByIndex[i];
					const auto sourceChannelIndex = segment.channels[i].channel;
					if (pSource->ReadBufPtr + sourceChannelIndex >= pSource->ReadBuf.size()) {
						const auto readReqSize = std::min<size_t>(8192, segmentEndBlockIndex - currentBlockIndex) * pSource->Channels;
						auto empty = false;
						try {
							const auto read = (*pSource->Reader)(readReqSize, false);
							pSource->ReadBuf.erase(pSource->ReadBuf.begin(), pSource->ReadBuf.begin() + pSource->ReadBufPtr);
							pSource->ReadBufPtr = 0;
							pSource->ReadBuf.insert(pSource->ReadBuf.end(), read.begin(), read.end());
							empty = read.empty();
						} catch (const Utils::Win32::Error& e) {
							if (e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA) 
								throw;
							empty = true;
						}
						if (empty) {
							if (segmentEndBlockIndex == UINT32_MAX) {
								stopSegment = true;
								break;
							} else
								throw std::runtime_error(std::format("not expecting eof yet ({}/{}: {:.3f}s)",
									currentBlockIndex, segmentEndBlockIndex,
									1. / targetRate * (segmentEndBlockIndex - currentBlockIndex)));
						}
					}
					buf[i][bufptr] = pSource->ReadBuf[pSource->ReadBufPtr + sourceChannelIndex];
				}
				if (!stopSegment) {
					for (auto& source : m_sourceInfo | std::views::values)
						source.ReadBufPtr += source.Channels;
					currentBlockIndex++;
					bufptr++;
				}
				stopSegment |= currentBlockIndex == segmentEndBlockIndex;
				stopSegment |= loopEndBlockIndex && currentBlockIndex == loopEndBlockIndex;
					
				if (bufptr == BufferedBlockCount || stopSegment || currentBlockIndex + 1 == loopStartBlockIndex) {

					if (const auto res = vorbis_analysis_wrote(&vd, bufptr); res < 0)
						throw std::runtime_error(std::format("vorbis_analysis_wrote: {}", res));
					buf = nullptr;

					if (stopSegment && segmentIndex == targetSet.segments.size() - 1) {
						if (const auto res = vorbis_analysis_wrote(&vd, 0); res < 0)
							throw std::runtime_error(std::format("vorbis_analysis_wrote: {}", res));
					}

					while (!ogg_page_eos(&og)) {
						if (const auto res = vorbis_analysis_blockout(&vd, &vb); res < 0)
							throw std::runtime_error(std::format("vorbis_analysis_blockout: {}", res)); 
						else if (res == 0)
							break;

						if (const auto res = vorbis_analysis(&vb, nullptr); res < 0)
							throw std::runtime_error(std::format("vorbis_analysis: {}", res)); 
						if (const auto res = vorbis_bitrate_addblock(&vb); res < 0)
							throw std::runtime_error(std::format("vorbis_bitrate_addblock: {}", res)); 
						
						while (!ogg_page_eos(&og)) {
							if (const auto res = vorbis_bitrate_flushpacket(&vd, &op); res < 0)
								throw std::runtime_error(std::format("vorbis_bitrate_flushpacket: {}", res)); 
							else if (res == 0)
								break;
							
							if (const auto res = ogg_stream_packetin(&os, &op); res < 0)
								throw std::runtime_error(std::format("ogg_stream_packetin: {}", res)); 

							while (!ogg_page_eos(&og)) {
								if (const auto res = ogg_stream_flush_fill(&os, &og, 0); res < 0)
									throw std::runtime_error(std::format("ogg_stream_flush_fill: {}", res)); 
								else if (res == 0)
									break;

								dataBuffer.emplace_back();
								dataBuffer.back().reserve(static_cast<size_t>(og.header_len) + og.body_len);
								dataBuffer.back().insert(dataBuffer.back().end(), og.header, og.header + og.header_len);
								dataBuffer.back().insert(dataBuffer.back().end(), og.body, og.body + og.body_len);
								oggDataSeekTable.push_back(oggDataSize);
								oggDataSize += og.header_len + og.body_len;
							}
						}
					}

					if (currentBlockIndex + 1 == loopStartBlockIndex) {
						loopStartOffset = oggDataSize;

						if (const auto offset = static_cast<uint32_t>(currentBlockIndex - ogg_page_granulepos(&og))) {
							// ogg packet sample block index and loop start don't align.
							// pull loop start forward so that it matches ogg packet sample block index.

							loopStartBlockIndex -= offset;
							loopEndBlockIndex -= offset;

							// adjust loopstart/loopend in ogg metadata.
							// unnecessary, but for the sake of completeness.

							vorbis_comment vc{};
							vorbis_comment_init(&vc);
							const auto vcCleanup = Utils::CallOnDestruction([&vc] { vorbis_comment_clear(&vc); });
							if (loopStartBlockIndex || loopEndBlockIndex) {
								vorbis_comment_add_tag(&vc, "LoopStart", std::format("{}", loopStartBlockIndex).c_str());
								vorbis_comment_add_tag(&vc, "LoopEnd", std::format("{}", loopEndBlockIndex).c_str());
							}

							ogg_stream_state os{};
							if (const auto res = ogg_stream_init(&os, 0))
								throw std::runtime_error(std::format("ogg_stream_init: {}", res));
							auto osCleanup = Utils::CallOnDestruction([&os] { ogg_stream_clear(&os); });

							ogg_packet header{};
							ogg_packet headerComments{};
							ogg_packet headerCode{};
							vorbis_analysis_headerout(&vd, &vc, &header, &headerComments, &headerCode);
							ogg_stream_packetin(&os, &header);
							ogg_stream_packetin(&os, &headerComments);
							ogg_stream_packetin(&os, &headerCode);

							headerBuffer.clear();
							while (true) {
								if (const auto res = ogg_stream_flush_fill(&os, &og, 0); res < 0)
									throw std::runtime_error(std::format("ogg_stream_flush_fill: {}", res));
								else if (res == 0)
									break;

								headerBuffer.insert(headerBuffer.end(), og.header, og.header + og.header_len);
								headerBuffer.insert(headerBuffer.end(), og.body, og.body + og.body_len);
							}
						}
					}
				}
			}
		}

		if (loopEndBlockIndex && !loopEndOffset)
			loopEndOffset = oggDataSize;

		std::vector<uint8_t> dataPages;
		dataPages.reserve(oggDataSize);
		for (const auto& buffer : dataBuffer)
			dataPages.insert(dataPages.end(), buffer.begin(), buffer.end());

		auto e = Sqex::Sound::ScdWriter::SoundEntry::FromOgg(
			std::move(headerBuffer), std::move(dataPages),
			originalInfo.Channels, targetRate,
			loopStartOffset, loopEndOffset,
			std::span(oggDataSeekTable)
		);
		if (const auto marks = originalEntry.GetMarkedSampleBlockIndices(); !marks.empty()) {
			auto& buf = e.AuxChunks[Sqex::Sound::SoundEntryAuxChunk::Name_Mark];
			buf.resize((3 + marks.size()) * 4);
			auto& markHeader = *reinterpret_cast<Sqex::Sound::SoundEntryAuxChunk::AuxChunkData::MarkChunkData*>(&buf[0]);
			markHeader = {
				.LoopStartSampleBlockIndex = loopStartBlockIndex,
				.LoopEndSampleBlockIndex = loopEndBlockIndex,
				.Count = static_cast<uint32_t>(marks.size()),
			};
			const auto span = std::span(reinterpret_cast<uint32_t*>(&buf[3 * 4]), marks.size());
			size_t i = 0;
			for (const auto blockIndex : originalEntry.GetMarkedSampleBlockIndices())
				span[i++] = static_cast<uint32_t>(1ULL * blockIndex * targetRate / originalInfo.Rate);
		}

		for (auto& info : m_sourceInfo | std::views::values)
			info.Reader = nullptr;

		Sqex::Sound::ScdWriter writer;
		writer.SetTable1(scdReader->ReadTable1Entries());
		writer.SetTable4(scdReader->ReadTable4Entries());
		writer.SetTable2(scdReader->ReadTable2Entries());
		writer.SetSoundEntry(0, std::move(e));
		return writer.Export();
	}
};

int main() {
	const Sqex::Sqpack::Reader readers[4]{
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0c0000.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex1\0c0100.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex2\0c0200.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex3\0c0300.win32.index)"},
	};
	
	for (auto& [confFile, sourceFilesDir] : std::vector<std::pair<std::filesystem::path, std::filesystem::path>>{
		{LR"(..\StaticData\MusicImportConfig\Before Meteor.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 1.0 - Before Meteor)"},
		{LR"(..\StaticData\MusicImportConfig\A Realm Reborn.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 2.0 - A Realm Reborn)"},
		{LR"(..\StaticData\MusicImportConfig\Before The Fall.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 2.5 - Before The Fall)"},
		{LR"(..\StaticData\MusicImportConfig\Heavensward.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 3.0 - Heavensward)"},
		{LR"(..\StaticData\MusicImportConfig\The Far Edge Of Fate.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 3.5 - The Far Edge Of Fate)"},
		{LR"(..\StaticData\MusicImportConfig\Stormblood.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 4.0 - Stormblood)"},
		{LR"(..\StaticData\MusicImportConfig\Monster Hunter World.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Monster Hunter World\Monster Hunter World Original Soundtrack)"},
		{LR"(..\StaticData\MusicImportConfig\Shadowbringers.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers)"},
		{LR"(..\StaticData\MusicImportConfig\Death Unto Dawn.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn)"},
	}) {
		try {
			confFile = canonical(confFile);
			sourceFilesDir = canonical(sourceFilesDir);
			MusicImportConfig conf;
			from_json(Utils::ParseJsonFromFile(confFile), conf);

			auto tp = Utils::Win32::TpEnvironment(IsDebuggerPresent() ? 1 : 0);
			// auto tp = Utils::Win32::TpEnvironment();
			for (const auto& item : conf.items) {
				tp.SubmitWork([&confFile, &item, &readers, &sourceFilesDir] {
					try {
						bool allTargetExists = true;
						MusicImporter importer(item, Utils::Win32::ResolvePathFromFileName("ffmpeg.exe"), Utils::Win32::ResolvePathFromFileName("ffprobe.exe"));
						importer.LoadTargetInfo([&readers, &allTargetExists](std::string ex, std::filesystem::path path) -> std::shared_ptr<Sqex::Sound::ScdReader> {
							const auto targetPath = std::filesystem::path(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\{0})", path.wstring()));
							allTargetExists &= exists(targetPath);

							if (ex == "ffxiv")
								return std::make_shared<Sqex::Sound::ScdReader>(readers[0][path]);
							else if (ex == "ex1")
								return std::make_shared<Sqex::Sound::ScdReader>(readers[1][path]);
							else if (ex == "ex2")
								return std::make_shared<Sqex::Sound::ScdReader>(readers[2][path]);
							else if (ex == "ex3")
								return std::make_shared<Sqex::Sound::ScdReader>(readers[3][path]);
							else
								return nullptr;
						});
						if (allTargetExists)
							return;
			
						importer.ResolveSources(sourceFilesDir);
						importer.Merge([](std::string ex, std::filesystem::path path, std::vector<uint8_t> data) {
							const auto targetPath = std::filesystem::path(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\{0})", path.wstring()));
							create_directories(targetPath.parent_path());
							Utils::Win32::Handle::FromCreateFile(targetPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0).Write(0, std::span(data));
							std::cout << std::format("Done: {}\n", path.wstring());
						});
					} catch (const std::exception& e) {
						std::cout << std::format("Error on {}:{}: {}\n", confFile.filename().wstring(), item.source.begin()->second.inputFiles.front().front(), e.what());
					}
				});
			}
			tp.WaitOutstanding();
		} catch (const std::exception& e) {
			std::cout << std::format("Error on {}: {}\n", confFile.filename().wstring(), e.what());
		}
	}
	return 0;
}
