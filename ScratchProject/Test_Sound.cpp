#include "pch.h"

#include <XivAlexanderCommon/Sqex_Sound_Reader.h>
#include <XivAlexanderCommon/Sqex_Sound_Writer.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Utils_Win32_Process.h>
#include <XivAlexanderCommon/Utils_Win32_ThreadPool.h>

struct MusicImportTargetChannel {
	std::optional<std::string> source;
	uint32_t channel;
};

void from_json(const nlohmann::json& j, MusicImportTargetChannel& o) {
	const char* lastAttempt;
	try {
		if (const auto it = j.find(lastAttempt = "source"); it == j.end())
			o.source = std::nullopt;
		else
			o.source = it->get<std::string>();

		o.channel = j.at(lastAttempt = "channel").get<uint32_t>();
	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

struct MusicImportTargetConcatItem {
	std::optional<std::string> source;
	std::optional<double> from;  // in seconds
	std::optional<double> length;  // in seconds
};

void from_json(const nlohmann::json& j, MusicImportTargetConcatItem& o) {
	const char* lastAttempt;
	try {
		if (const auto it = j.find(lastAttempt = "source"); it == j.end())
			o.source = std::nullopt;
		else
			o.source = it->get<std::string>();

		if (const auto it = j.find(lastAttempt = "from"); it == j.end())
			o.from = std::nullopt;
		else
			o.from = it->get<double>();

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
	std::optional<std::vector<MusicImportTargetChannel>> channels;
	std::optional<std::vector<MusicImportTargetConcatItem>> concat;
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

		if (const auto it = j.find(lastAttempt = "channels"); it == j.end())
			o.channels = std::nullopt;
		else
			o.channels = it->get<std::vector<MusicImportTargetChannel>>();

		if (const auto it = j.find(lastAttempt = "concat"); it == j.end())
			o.concat = std::nullopt;
		else
			o.concat = it->get<std::vector<MusicImportTargetConcatItem>>();

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

class MusicImporter {
	class FloatPcmSource {
		Utils::Win32::Process m_hReaderProcess;
		Utils::Win32::File m_hStdoutReader;
		Utils::Win32::Thread m_hStdinWriterThread;

		std::vector<float> m_buffer;

	public:
		FloatPcmSource(const std::filesystem::path& path, const std::filesystem::path& ffmpegPath, int forceSamplingRate = 0) {
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
			builder.WithAppendArgument("-");

			m_hReaderProcess = builder.Run().first;
			m_hStdoutReader = std::move(hStdoutRead);
		}
		
		FloatPcmSource(std::string originalFormat, std::function<std::span<uint8_t>(size_t len, bool throwOnIncompleteRead)> linearReader, const std::filesystem::path& ffmpegPath, int forceSamplingRate = 0) {
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
				if (targetSet.concat) {
					throw std::runtime_error("TODO: concat");
				} else {
					res = MergeChannels(targetSet, i, j);
				}	
				cb(Utils::ToUtf8(Utils::StringSplit<std::wstring>(m_config.target[i].path[j].wstring(), L"/")[1]), m_config.target[i].path[j], std::move(res));
			}
		}
	}

private:
	std::vector<uint8_t> MergeChannels(MusicImportTarget& targetSet, size_t targetIndex, size_t pathIndex) {
		auto& originalInfo = m_sourceInfo["target"];
		auto& scdReader = m_targetOriginals[targetIndex][pathIndex];
		const auto originalOgg = scdReader->GetSoundEntry(0).GetOggFile();
		const auto originalOggStream = Sqex::MemoryRandomAccessStream(originalOgg);
		uint32_t LoopStartBlock = 0;
		uint32_t LoopEndBlock = 0;
		{
			const auto originalProbe = RunProbe("ogg", originalOggStream.AsLinearReader<uint8_t>(), m_ffprobe).at("streams").at(0);
			originalInfo = {
				.Rate = std::strtoul(originalProbe.at("sample_rate").get<std::string>().c_str(), nullptr, 10),
				.Channels = originalProbe.at("channels").get<uint32_t>(),
			};
			if (const auto it = originalProbe.find("tags"); it != originalProbe.end()) {
				for (const auto& item : it->get<nlohmann::json::object_t>()) {
					if (_strnicmp(item.first.c_str(), "LoopStart", 9) == 0)
						LoopStartBlock = std::strtoul(item.second.get<std::string>().c_str(), nullptr, 10);
					else if (_strnicmp(item.first.c_str(), "LoopEnd", 7) == 0)
						LoopEndBlock = std::strtoul(item.second.get<std::string>().c_str(), nullptr, 10);
				}
			}
		}

		uint32_t targetRate = 0;
		for (const auto& targetInfo : m_sourceInfo | std::views::values)
			targetRate = std::max(targetRate, targetInfo.Rate);
		LoopStartBlock = static_cast<uint32_t>(1ULL * LoopStartBlock * targetRate / originalInfo.Rate);
		LoopEndBlock = static_cast<uint32_t>(1ULL * LoopEndBlock * targetRate / originalInfo.Rate);

		if (!targetSet.channels.has_value()) {
			if (m_sources.size() > 1)
				throw std::invalid_argument("channels must be set when there are more than one source");
			targetSet.channels = std::vector<MusicImportTargetChannel>{
				{.source = m_sources.begin()->first, .channel = 0},
				{.source = m_sources.begin()->first, .channel = 1},
			};
		}

		if (originalInfo.Channels != targetSet.channels->size())
			throw std::invalid_argument(std::format("originalChannels={}, expected={}", originalInfo.Channels, targetSet.channels ? targetSet.channels->size() : 2));

		for (auto& [name, info] : m_sourceInfo) {
			if (name == "target")
				info.Reader = std::make_unique<FloatPcmSource>("ogg", originalOggStream.AsLinearReader<uint8_t>(), m_ffmpeg, targetRate);
			else
				info.Reader = std::make_unique<FloatPcmSource>(m_sources[name], m_ffmpeg, targetRate);
			info.FirstBlocks.clear();
			info.FirstBlocks.resize(info.Channels, INT32_MAX);
			uint32_t blockIndex = 0;
			auto pending = true;
			while (pending) {
				const auto buf = (*info.Reader)(info.Channels * static_cast<size_t>(8192), false);
				if (buf.empty())
					break;

				info.ReadBuf.insert(info.ReadBuf.end(), buf.begin(), buf.end());
				for (size_t i = 0, to = buf.size(); pending && i < to; ++blockIndex) {
					pending = false;
					for (size_t c = 0; c < info.Channels; ++c, ++i) {
						if (info.FirstBlocks[c] != INT32_MAX)
							continue;
						if (buf[i] >= 0.1)
							info.FirstBlocks[c] = blockIndex;
						else
							pending = true;
					}
				}
			}
		}
		
		for (auto& [name, info] : m_sourceInfo) {
			if (name != "target") {
				for (size_t channelIndex = 0; channelIndex < targetSet.channels->size(); ++channelIndex) {
					const auto& channelMap = (*targetSet.channels)[channelIndex];
					if (channelMap.source == name) {
						if (const auto srcCopyFromOffset = static_cast<int>(info.Channels) * (originalInfo.FirstBlocks[channelIndex] - info.FirstBlocks[channelMap.channel]);
							srcCopyFromOffset < 0) {
							info.ReadBuf.erase(info.ReadBuf.begin(), info.ReadBuf.begin() - srcCopyFromOffset);
						} else {
							info.ReadBuf.resize(info.ReadBuf.size() + srcCopyFromOffset);
							std::move(info.ReadBuf.begin(), info.ReadBuf.end() - srcCopyFromOffset, info.ReadBuf.begin() + srcCopyFromOffset);
							std::fill_n(info.ReadBuf.begin(), srcCopyFromOffset, 0.f);
						}
						break;
					}
				}
			}
		}
		
		auto [hEncoderOutRead, hEncoderOutWrite] = Utils::Win32::File::CreatePipe();
		auto [hEncoderInRead, hEncoderInWrite] = Utils::Win32::File::CreatePipe();
		Utils::Win32::Process encoderProcess;
		{
			auto builder = Utils::Win32::ProcessBuilder();
			builder
				.WithPath(m_ffmpeg)
				.WithStdin(hEncoderInRead)
				.WithStdout(hEncoderOutWrite)
				.WithAppendArgument("-hide_banner")
				.WithAppendArgument("-f").WithAppendArgument("f32le")
				.WithAppendArgument("-ac").WithAppendArgument("{}", originalInfo.Channels)
				.WithAppendArgument("-ar").WithAppendArgument("{}", targetRate)
				.WithAppendArgument("-i").WithAppendArgument("-")
				.WithAppendArgument("-f").WithAppendArgument("ogg")
				.WithAppendArgument("-q:a").WithAppendArgument("10");
			if (LoopStartBlock || LoopEndBlock) {
				builder.WithAppendArgument("-metadata").WithAppendArgument(L"LoopStart={}", LoopStartBlock);
				builder.WithAppendArgument("-metadata").WithAppendArgument(L"LoopEnd={}", LoopEndBlock);
			}
			builder.WithAppendArgument("-");
			encoderProcess = builder.Run().first;
			hEncoderInRead.Clear();
			hEncoderOutWrite.Clear();
		}

		std::string error;
		const auto hDecoderToEncoder = Utils::Win32::Thread(L"DecoderToEncoder", [this, &LoopEndBlock, &error, &targetSet, &originalInfo, targetRate, hEncoderInWrite = std::move(hEncoderInWrite)]() {
			uint32_t currentBlock = 0;

			std::vector<float> buf;
			const auto BufferedBlockCount = 65536;
			buf.reserve(BufferedBlockCount * targetSet.channels->size());

			auto endBlock = LoopEndBlock ? LoopEndBlock : UINT32_MAX;
			std::vector<SourceSet*> sourceSetsByIndex;
			for (size_t i = 0; i < targetSet.channels->size(); ++i)
				sourceSetsByIndex.push_back((*targetSet.channels)[i].source ? &m_sourceInfo[*(*targetSet.channels)[i].source] : &originalInfo);
			size_t wrote = 0;
			try {
				while (currentBlock < endBlock) {
					for (size_t i = 0; i < targetSet.channels->size(); ++i) {
						const auto pSource = sourceSetsByIndex[i];
						const auto sourceChannelIndex = (*targetSet.channels)[i].channel;
						if (pSource->ReadBufPtr + sourceChannelIndex >= pSource->ReadBuf.size()) {
							pSource->ReadBufPtr = 0;
							const auto readReqSize = std::min<size_t>(8192, endBlock - currentBlock) * pSource->Channels;
							auto empty = false;
							try {
								const auto read = (*pSource->Reader)(readReqSize, false);
								pSource->ReadBuf.clear();
								pSource->ReadBuf.insert(pSource->ReadBuf.end(), read.begin(), read.end());
								empty = read.empty();
							} catch (const Utils::Win32::Error& e) {
								if (e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA) 
									throw;
								empty = true;
							}
							if (empty) {
								if (endBlock == UINT32_MAX) {
									endBlock = currentBlock;
									break;
								} else
									throw std::runtime_error(std::format("not expecting eof yet ({}/{})", currentBlock, endBlock));
							}
						}
						buf.push_back(pSource->ReadBuf[pSource->ReadBufPtr + sourceChannelIndex]);
					}
					if (currentBlock < endBlock) {
						for (auto& source : m_sourceInfo | std::views::values)
							source.ReadBufPtr += source.Channels;
						currentBlock++;
					}
					
					if (buf.size() == BufferedBlockCount * targetSet.channels->size() || currentBlock == endBlock) {
						hEncoderInWrite.Write(0, std::span(buf));
						buf.clear();
					}
				}

			} catch (const Utils::Win32::Error& e) {
				if ((e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA) || (endBlock != UINT32_MAX && currentBlock < endBlock)) {
					error = e.what();
				}
			}
		});

		const auto e = Sqex::Sound::ScdWriter::SoundEntry::FromOgg([&error, hEncoderOutRead = std::move(hEncoderOutRead), buf = std::vector<uint8_t>()](size_t len, bool throwOnIncompleteRead) mutable->std::span<uint8_t> {
			try {
				buf.resize(8192);
				buf.resize(hEncoderOutRead.Read(0, std::span(buf), Utils::Win32::File::PartialIoMode::AllowPartial));
				return std::span(buf);
			} catch (const Utils::Win32::Error& e) {
				if (e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA) {
					error = e.what();
				}
				return {};
			}
		});

		encoderProcess.Terminate(0);
		for (auto& info : m_sourceInfo | std::views::values)
			info.Reader = nullptr;
		hDecoderToEncoder.Wait();
		if (!error.empty())
			throw std::runtime_error(std::format("Error: {}", error));

		Sqex::Sound::ScdWriter writer;
		writer.SetTable1(scdReader->ReadTable1Entries());
		writer.SetTable4(scdReader->ReadTable4Entries());
		writer.SetTable2(scdReader->ReadTable2Entries());
		writer.SetSoundEntry(0, e);
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

	MusicImportConfig conf;
	from_json(Utils::ParseJsonFromFile(LR"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\MusicImportConfig\Shadowbringers.json)"), conf);

	auto tp = Utils::Win32::TpEnvironment(IsDebuggerPresent() ? 1 : 0);
	for (const auto& item : conf.items) {
		tp.SubmitWork([&item, &readers] {
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
			
			importer.ResolveSources(LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers)");
			try {
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
	return 0;
}
