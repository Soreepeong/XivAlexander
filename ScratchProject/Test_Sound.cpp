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

struct MusicImportItem {
	std::map<std::string, std::vector<std::string>> source;
	std::vector<MusicImportTarget> target;
};

void from_json(const nlohmann::json& j, MusicImportItem& o) {
	const char* lastAttempt;
	try {
		if (const auto it = j.find(lastAttempt = "source"); it == j.end())
			throw std::invalid_argument("required key missing");
		else if (it->is_object())
			o.source = it->get<std::map<std::string, std::vector<std::string>>>();
		else if (it->is_array())
			o.source = { {"source", it->get<std::vector<std::string>>()} };
		else if (it->is_string())
			o.source = { {"source", {it->get<std::string>()} } };
		else
			throw std::invalid_argument("only array, object, or string is accepted");

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

std::vector<uint8_t> ToOgg(std::span<float> samples, uint32_t channels, uint32_t samplingRate, uint64_t loopStart, uint64_t loopEnd) {
	std::vector<uint8_t> result;

	ogg_stream_state os; /* take physical pages, weld into a logical
							stream of packets */
	ogg_page         og; /* one Ogg bitstream page.  Vorbis packets are inside */
	ogg_packet       op; /* one raw packet of data for decode */

	vorbis_info      vi; /* struct that stores all the static vorbis bitstream
							settings */
	vorbis_comment   vc; /* struct that stores all the user comments */

	vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
	vorbis_block     vb; /* local working space for packet->PCM decode */

	vorbis_info_init(&vi);
	if (const auto res = vorbis_encode_init_vbr(&vi, channels, samplingRate, 1))
		throw std::runtime_error(std::format("vorbis_encode_init_vbr: {}", res));

	vorbis_comment_init(&vc);
	vorbis_comment_add_tag(&vc, "LoopStart", std::format("{}", loopStart).c_str());
	vorbis_comment_add_tag(&vc, "LoopEnd", std::format("{}", loopEnd).c_str());

	vorbis_analysis_init(&vd, &vi);
	vorbis_block_init(&vd, &vb);
	ogg_stream_init(&os, 1);

	{
		ogg_packet header;
		ogg_packet headerComments;
		ogg_packet headerCode;
		vorbis_analysis_headerout(&vd, &vc, &header, &headerComments, &headerCode);
		ogg_stream_packetin(&os, &header);
		ogg_stream_packetin(&os, &headerComments);
		ogg_stream_packetin(&os, &headerCode);

		while (const auto flushResult = ogg_stream_flush(&os, &og)) {
			result.reserve(result.size() + og.header_len + og.body_len);
			result.insert(result.end(), og.header, og.header + og.header_len);
			result.insert(result.end(), og.body, og.body + og.body_len);
		}
	}

	const size_t BlockCountPerRead = 8192;
	for (size_t sampleBlockIndex = 0; sampleBlockIndex < loopEnd; ) {
		auto readSampleBlocks = std::min(BlockCountPerRead, loopEnd - sampleBlockIndex);

		if (loopStart && sampleBlockIndex < loopStart && loopStart < sampleBlockIndex + readSampleBlocks)
			readSampleBlocks = loopStart - sampleBlockIndex;

		const auto readSamples = readSampleBlocks * channels;
		auto buffer = vorbis_analysis_buffer(&vd, static_cast<int>(readSampleBlocks));
		for (size_t ptr = sampleBlockIndex * channels, i = 0; i < readSampleBlocks; ++sampleBlockIndex, ++i) {
			for (size_t channelIndex = 0; channelIndex < channels; ++channelIndex, ++ptr) {
				buffer[channelIndex][i] = samples[ptr];
			}
		}
		vorbis_analysis_wrote(&vd, static_cast<int>(readSamples / channels));
		if (sampleBlockIndex == loopEnd)
			vorbis_analysis_wrote(&vd, 0);

		while (vorbis_analysis_blockout(&vd, &vb) == 1 && !ogg_page_eos(&og)) {
			vorbis_analysis(&vb, nullptr);
			vorbis_bitrate_addblock(&vb);
			while (vorbis_bitrate_flushpacket(&vd, &op) && !ogg_page_eos(&og)) {
				ogg_stream_packetin(&os, &op);

				while (!ogg_page_eos(&og)) {
					if (const auto r = ogg_stream_flush_fill(&os, &og, 0); r == 0)
						break;
						
					result.reserve(result.size() + og.header_len + og.body_len);
					result.insert(result.end(), og.header, og.header + og.header_len);
					result.insert(result.end(), og.body, og.body + og.body_len);
				}
			}
		}
	}
	ogg_stream_clear(&os);
	vorbis_block_clear(&vb);
	vorbis_dsp_clear(&vd);
	vorbis_comment_clear(&vc);
	vorbis_info_clear(&vi);

	return result;
}
class MusicImporter {
	class FloatPcmSource {
		Utils::Win32::Process m_hReaderProcess;
		Utils::Win32::File m_hStdoutReader;
		Utils::Win32::Thread m_hStdinWriterThread;

		std::vector<float> m_buffer;

	public:
		FloatPcmSource(const std::filesystem::path& path, const std::filesystem::path& ffmpegPath, int forceSamplingRate = 0, std::string audioFilters = {}) {
			auto [hStdoutRead, hStdoutWrite] = Utils::Win32::File::CreatePipe();
			auto builder = Utils::Win32::ProcessBuilder();
			builder
				.WithPath(ffmpegPath)
				.WithStdout(std::move(hStdoutWrite));
				
			builder.WithAppendArgument("-hide_banner");
			builder.WithAppendArgument("-i").WithAppendArgument(path.wstring());
			builder.WithAppendArgument("-f").WithAppendArgument("f32le");
			if (forceSamplingRate)
				builder.WithAppendArgument("-ar").WithAppendArgument("{}", forceSamplingRate);
			if (!audioFilters.empty())
				builder.WithAppendArgument("-filter:a").WithAppendArgument(audioFilters);
			builder.WithAppendArgument("-");

			m_hReaderProcess = builder.Run().first;
			m_hStdoutReader = std::move(hStdoutRead);
		}
		
		FloatPcmSource(std::string originalFormat, std::function<std::span<uint8_t>(size_t len, bool throwOnIncompleteRead)> linearReader, const std::filesystem::path& ffmpegPath, int forceSamplingRate = 0, std::string audioFilters = {}) {
			auto [hStdinRead, hStdinWrite] = Utils::Win32::File::CreatePipe();
			auto [hStdoutRead, hStdoutWrite] = Utils::Win32::File::CreatePipe();
			auto builder = Utils::Win32::ProcessBuilder();
			builder
				.WithPath(ffmpegPath)
				.WithStdin(std::move(hStdinRead))
				.WithStdout(std::move(hStdoutWrite));
				
			builder.WithAppendArgument("-hide_banner");
			builder.WithAppendArgument("-f").WithAppendArgument(originalFormat);
			builder.WithAppendArgument("-i").WithAppendArgument("-");
			builder.WithAppendArgument("-f").WithAppendArgument("f32le");
			if (forceSamplingRate)
				builder.WithAppendArgument("-ar").WithAppendArgument("{}", forceSamplingRate);
			if (!audioFilters.empty())
				builder.WithAppendArgument("-filter:a").WithAppendArgument("aresample=resampler=soxr, " + audioFilters);
			else
				builder.WithAppendArgument("-filter:a").WithAppendArgument("aresample=resampler=soxr");
			builder.WithAppendArgument("-");

			m_hReaderProcess = builder.Run().first;
			m_hStdoutReader = std::move(hStdoutRead);

			m_hStdinWriterThread = Utils::Win32::Thread(L"MusicImporter::Source", [hStdinWriter = std::move(hStdinWrite), linearReader = std::move(linearReader)] {
				try {
					while (true) {
						const auto read = linearReader(8192, false);
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
			return std::span(m_buffer).subspan(0, m_hStdoutReader.Read(0, std::span(m_buffer), throwOnIncompleteRead ? Utils::Win32::File::PartialIoMode::AlwaysFull : Utils::Win32::File::PartialIoMode::AllowPartial));
		}
	};

	static nlohmann::json RunProbe(const std::filesystem::path& path, const std::filesystem::path& ffprobePath) {
		auto [hStdoutRead, hStdoutWrite] = Utils::Win32::File::CreatePipe();
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
				str.resize(mark + hStdoutRead.Read(0, &str[mark], str.size() - mark, Utils::Win32::File::PartialIoMode::AllowPartial));
				if (str.size() == mark)
					break;
			}
		} catch (const Utils::Win32::Error& e) {
			if (e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA)
				throw;
		}
		return nlohmann::json::parse(str);
	}

	static nlohmann::json RunProbe(std::string originalFormat, std::function<std::span<uint8_t>(size_t len, bool throwOnIncompleteRead)> linearReader, const std::filesystem::path& ffprobePath) {
		auto [hStdoutRead, hStdoutWrite] = Utils::Win32::File::CreatePipe();
		auto [hStdinRead, hStdinWrite] = Utils::Win32::File::CreatePipe();
		auto process = Utils::Win32::ProcessBuilder()
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
				str.resize(mark + hStdoutRead.Read(0, &str[mark], str.size() - mark, Utils::Win32::File::PartialIoMode::AllowPartial));
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

	std::map<std::string, std::vector<std::wregex>> m_sourceNamePatterns;
	std::map<std::string, std::filesystem::path> m_sources;
	std::vector<std::vector<std::shared_ptr<Sqex::Sound::ScdReader>>> m_targetOriginals;
	
	struct SourceSet {
		uint32_t Rate;
		uint32_t Channels;

		std::unique_ptr<FloatPcmSource> Reader;
		std::vector<float> ReadBuf;
		size_t ReadBufPtr;
			
		std::vector<int> FirstBlocks;
	};
	std::map<std::string, SourceSet> m_sourceInfo;

public:
	MusicImporter(MusicImportItem config, std::filesystem::path ffmpeg, std::filesystem::path ffprobe)
		: m_config(std::move(config))
		, m_ffmpeg(std::move(ffmpeg))
		, m_ffprobe(std::move(ffprobe)) {
		for (const auto& [sourceName, fileNamePatterns] : m_config.source) {
			auto& patterns = m_sourceNamePatterns[sourceName];
			for (const auto& pattern : fileNamePatterns) {
				patterns.emplace_back(std::wregex(Utils::FromUtf8(pattern), std::wregex::icase));
			}
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
			for (const auto& [sourceName, fileNamePatterns] : m_sourceNamePatterns) {
				if (m_sources.contains(sourceName))
					continue;
				
				for (const auto& pattern : fileNamePatterns) {
					if (std::regex_search(item.path().filename().wstring(), pattern)) {
						try {
							const auto probe = RunProbe(item.path(), m_ffprobe).at("streams").at(0);
							m_sourceInfo[sourceName] = {
								.Rate = std::strtoul(probe.at("sample_rate").get<std::string>().c_str(), nullptr, 10),
								.Channels = probe.at("channels").get<uint32_t>(),
							};
							m_sources[sourceName] = item.path();
							break;
						} catch (const std::exception&) {
							// TODO: notify user
						}
					}
				}
				if (m_sources.contains(sourceName))
					continue;

				allFound = false;
			}
		}
		return allFound;
	}

	void Merge(const std::function<void(std::string ex, std::filesystem::path path, std::vector<uint8_t>)>& cb) {
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
		const auto originalEntry = scdReader->GetSoundEntry(0);
		const auto originalOgg = originalEntry.GetOggFile();
		const auto originalOggStream = Sqex::MemoryRandomAccessStream(originalOgg);

		uint32_t loopStartBlockIndex = 0;
		uint32_t loopEndBlockIndex = 0;
		{
			const auto originalProbe = RunProbe("ogg", originalOggStream.AsLinearReader<uint8_t>(), m_ffprobe).at("streams").at(0);
			originalInfo = {
				.Rate = std::strtoul(originalProbe.at("sample_rate").get<std::string>().c_str(), nullptr, 10),
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

		uint32_t targetRate = 0;
		for (const auto& targetInfo : m_sourceInfo | std::views::values)
			targetRate = std::max(targetRate, targetInfo.Rate);
		loopStartBlockIndex = static_cast<uint32_t>(1ULL * loopStartBlockIndex * targetRate / originalInfo.Rate);
		loopEndBlockIndex = static_cast<uint32_t>(1ULL * loopEndBlockIndex * targetRate / originalInfo.Rate);
		
		if (targetSet.loopOffsetDelta) {
			loopStartBlockIndex -= static_cast<uint32_t>(targetRate * targetSet.loopOffsetDelta);
			loopEndBlockIndex -= static_cast<uint32_t>(targetRate * targetSet.loopOffsetDelta);
		}
		if (targetSet.loopLengthDivisor != 1) {
			loopEndBlockIndex = loopStartBlockIndex + (loopEndBlockIndex - loopStartBlockIndex) / targetSet.loopLengthDivisor;
		}

		if (targetSet.segments.empty()) {
			if (m_sources.size() > 1)
				throw std::invalid_argument("segments must be set when there are more than one source");
			targetSet.segments = std::vector<MusicImportSegmentItem>{
				{
					.channels = std::vector<MusicImportTargetChannel>{
						{.source = m_sources.begin()->first, .channel = 0},
						{.source = m_sources.begin()->first, .channel = 1},
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
		std::vector<uint8_t> result;

		vorbis_info vi;
		vorbis_info_init(&vi);
		if (const auto res = vorbis_encode_init_vbr(&vi, originalInfo.Channels, targetRate, 1))
			throw std::runtime_error(std::format("vorbis_encode_init_vbr: {}", res));
		const auto viCleanup = Utils::CallOnDestruction([&vi] { vorbis_info_clear(&vi); });

		vorbis_dsp_state vd;
		if (const auto res = vorbis_analysis_init(&vd, &vi))
			throw std::runtime_error(std::format("vorbis_analysis_init: {}", res));
		const auto vdCleanup = Utils::CallOnDestruction([&vd] { vorbis_dsp_clear(&vd); });

		vorbis_block vb;
		if (const auto res = vorbis_block_init(&vd, &vb))
			throw std::runtime_error(std::format("vorbis_block_init: {}", res));
		const auto vbCleanup = Utils::CallOnDestruction([&vb] { vorbis_block_clear(&vb); });

		ogg_stream_state os;
		if (const auto res = ogg_stream_init(&os, 0))
			throw std::runtime_error(std::format("ogg_stream_init: {}", res));
		const auto osCleanup = Utils::CallOnDestruction([&os] { ogg_stream_clear(&os); });

		ogg_page og;
		{
			vorbis_comment vc;
			vorbis_comment_init(&vc);
			const auto vcCleanup = Utils::CallOnDestruction([&vc] { vorbis_comment_clear(&vc); });
			if (loopStartBlockIndex || loopEndBlockIndex) {
				vorbis_comment_add_tag(&vc, "LoopStart", std::format("{}", loopStartBlockIndex).c_str());
				vorbis_comment_add_tag(&vc, "LoopEnd", std::format("{}", loopEndBlockIndex).c_str());
			}

			ogg_packet header;
			ogg_packet headerComments;
			ogg_packet headerCode;
			vorbis_analysis_headerout(&vd, &vc, &header, &headerComments, &headerCode);
			ogg_stream_packetin(&os, &header);
			ogg_stream_packetin(&os, &headerComments);
			ogg_stream_packetin(&os, &headerCode);

			while (const auto flushResult = ogg_stream_flush(&os, &og)) {
				result.reserve(result.size() + og.header_len + og.body_len);
				result.insert(result.end(), og.header, og.header + og.header_len);
				result.insert(result.end(), og.body, og.body + og.body_len);
			}
		}

		ogg_packet op;
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
				if (name == "target") {
					info.Reader = std::make_unique<FloatPcmSource>("ogg", originalOggStream.AsLinearReader<uint8_t>(), m_ffmpeg, targetRate, ffmpegFilter);
					minBlockIndex = currentBlockIndex;
				} else {
					info.Reader = std::make_unique<FloatPcmSource>(m_sources[name], m_ffmpeg, targetRate, ffmpegFilter);
					if (segment.sourceOffsets.contains(name))
						minBlockIndex = static_cast<uint32_t>(targetRate * segment.sourceOffsets.at(name));
				}
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
				if (name == "target")
					continue;

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
				sourceSetsByIndex.push_back(&m_sourceInfo[segment.channels[i].source]);
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
								throw std::runtime_error(std::format("not expecting eof yet ({}/{})", currentBlockIndex, segmentEndBlockIndex));
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
					
				if (bufptr == BufferedBlockCount || stopSegment) {
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
						
								result.reserve(result.size() + og.header_len + og.body_len);
								result.insert(result.end(), og.header, og.header + og.header_len);
								result.insert(result.end(), og.body, og.body + og.body_len);
							}
						}
					}
				}
			}
		}

		auto e = Sqex::Sound::ScdWriter::SoundEntry::FromOgg(Sqex::MemoryRandomAccessStream(result).AsLinearReader<uint8_t>());
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
	//auto rdr = Sqex::Sound::ScdReader(std::make_shared<Sqex::FileRandomAccessStream>(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\ex3\0c0300\music\ex3\bgm_ex3_myc_01.scd)"));
	//auto ent = rdr.GetSoundEntry(0);
	//ent.GetOggFile();
	const Sqex::Sqpack::Reader readers[4]{
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0c0000.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex1\0c0100.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex2\0c0200.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex3\0c0300.win32.index)"},
	};
	
	const auto confFile = LR"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\MusicImportConfig\Heavensward.json)";
	const auto sourceFilesDir = LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 3.0 - Heavensward)";
	
	for (const auto [confFile, sourceFilesDir] : std::vector<std::pair<std::filesystem::path, std::filesystem::path>> {
		{LR"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\MusicImportConfig\Heavensward.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 3.0 - Heavensward)"},
		{LR"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\MusicImportConfig\Shadowbringers.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers)"},
		{LR"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\MusicImportConfig\Death Unto Dawn.json)", LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn)"},
	}) {
		MusicImportConfig conf;
		from_json(Utils::ParseJsonFromFile(confFile), conf);

		// auto tp = Utils::Win32::TpEnvironment(IsDebuggerPresent() ? 1 : 0);
		auto tp = Utils::Win32::TpEnvironment();
		for (const auto& item : conf.items) {
			tp.SubmitWork([&item, &readers, &sourceFilesDir] {
				try {
					bool allTargetExists = true;
					MusicImporter importer(item, LR"(C:\Windows\ffmpeg.exe)", LR"(C:\Windows\ffprobe.exe)");
					importer.LoadTargetInfo([&readers, &allTargetExists](std::string ex, std::filesystem::path path) -> std::shared_ptr<Sqex::Sound::ScdReader> {
						std::filesystem::path targetPath;
						if (ex == "ffxiv")
							targetPath = std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\ffxiv\0c0000\{0})", path.wstring());
						else
							targetPath = std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\ex{0}\0c0{0}00\{1})", ex.substr(2, 1), path.wstring());
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
						std::filesystem::path targetPath;
						if (ex == "ffxiv")
							targetPath = std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\ffxiv\0c0000\{0})", path.wstring());
						else
							targetPath = std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\ex{0}\0c0{0}00\{1})", ex.substr(2, 1), path.wstring());
						Utils::Win32::File::Create(targetPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0).Write(0, std::span(data));
					});
				} catch (const std::exception& e) {
					std::cout << std::format("Error on {}: {}\n", item.source.begin()->second.front(), e.what());
				}
			});
		}
		tp.WaitOutstanding();
	}
	return 0;
}
