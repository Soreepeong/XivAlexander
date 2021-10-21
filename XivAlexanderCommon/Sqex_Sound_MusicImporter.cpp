#include "pch.h"
#include "Sqex_Sound_MusicImporter.h"

#include "Sqex_Sound_Writer.h"
#include "Utils_Win32_ThreadPool.h"

void Sqex::Sound::from_json(const nlohmann::json& j, MusicImportTargetChannel& o) {
	const char* lastAttempt = "from_json";
	try {
		o.source = j.at(lastAttempt = "source").get<std::string>();
		o.channel = j.at(lastAttempt = "channel").get<uint32_t>();
	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

void Sqex::Sound::from_json(const nlohmann::json& j, MusicImportSegmentItem& o) {
	const char* lastAttempt = "from_json";
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

void Sqex::Sound::from_json(const nlohmann::json& j, MusicImportTarget& o) {
	const char* lastAttempt = "from_json";
	try {
		if (const auto it = j.find(lastAttempt = "path"); it == j.end())
			throw std::invalid_argument("required key missing");
		else if (it->is_array())
			o.path = it->get<std::vector<std::filesystem::path>>();
		else if (it->is_string())
			o.path = { it->get<std::filesystem::path>() };
		else
			throw std::invalid_argument("only array or string is accepted");

		o.sequentialToFfmpegChannelIndexMap = j.value(lastAttempt = "sequentialToFfmpegChannelIndexMap", decltype(o.sequentialToFfmpegChannelIndexMap)());
		o.loopOffsetDelta = j.value(lastAttempt = "loopOffsetDelta", 0.f);
		o.loopLengthDivisor = j.value(lastAttempt = "loopLengthDivisor", 1);

		if (const auto it = j.find(lastAttempt = "segments"); it == j.end())
			o.segments = {};
		else
			o.segments = it->get<std::vector<MusicImportSegmentItem>>();

		o.enable = j.value(lastAttempt = "enable", true);
	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

void Sqex::Sound::from_json(const nlohmann::json& j, MusicImportSourceItemInputFile& o) {
	const char* lastAttempt = "from_json";
	try {
		if (j.is_string()) {
			lastAttempt = "<string>";
			o.pattern = j.get<std::string>();
		} else {
			if (const auto it = j.find(lastAttempt = "directory"); it == j.end() || it->is_null())
				o.directory = std::nullopt;
			else
				o.directory = it->get<std::string>();
			o.pattern = j.value(lastAttempt = "pattern", decltype(o.pattern)());
		}

	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}

}

void Sqex::Sound::from_json(const nlohmann::json& j, MusicImportSourceItem& o) {
	const char* lastAttempt = "from_json";
	try {
		if (j.is_array()) {
			lastAttempt = "<array>";
			if (j.empty())
				return;
			if (j.at(0).is_array())
				o.inputFiles = j.get<decltype(o.inputFiles)>();
			else
				o.inputFiles = { j.get<std::vector<MusicImportSourceItemInputFile>>() };
		} else if (j.is_string()) {
			lastAttempt = "<string>";
			o.inputFiles = { { Sqex::Sound::MusicImportSourceItemInputFile(j.get<std::string>()) } };
		} else {
			if (const auto it = j.find(lastAttempt = "inputFiles"); it == j.end())
				throw std::invalid_argument("required key missing");
			else if (it->is_array()) {
				if (!it->empty()) {
					for (const auto& item : *it) {
						o.inputFiles.emplace_back();
						for (const auto& item2 : item) {
							o.inputFiles.back().emplace_back();
							from_json(item2, o.inputFiles.back().back());
						}
					}
				}
			} else if (it->is_string()) {
				o.inputFiles.emplace_back();
				from_json(*it, o.inputFiles.back().back());
			} else
				throw std::invalid_argument("only array, object, or string is accepted");

			o.filterComplex = j.value(lastAttempt = "filterComplex", std::string());
			o.filterComplexOutName = j.value(lastAttempt = "filterComplexOutName", std::string());
		}
	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

void Sqex::Sound::from_json(const nlohmann::json& j, MusicImportItem& o) {
	const char* lastAttempt = "from_json";
	try {
		if (const auto it = j.find(lastAttempt = "source"); it == j.end())
			throw std::invalid_argument("required key missing");
		else if (it->is_object())
			o.source = it->get<std::map<std::string, MusicImportSourceItem>>();
		else if (it->is_array() || it->is_string())
			o.source = { { "source", it->get<MusicImportSourceItem>() } };
		else
			throw std::invalid_argument("only array or object is accepted");

		if (const auto it = j.find(lastAttempt = "target"); it == j.end())
			throw std::invalid_argument("required key missing");
		else if (it->is_array())
			o.target = it->get<std::vector<MusicImportTarget>>();
		else if (it->is_object())
			o.target = { it->get<MusicImportTarget>() };
		else
			throw std::invalid_argument("only array or object is accepted");
	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

void Sqex::Sound::from_json(const nlohmann::json& j, MusicImportSearchDirectory& o) {
	const char* lastAttempt = "from_json";
	try {
		o.default_ = j.value(lastAttempt = "default", false);
		o.purchaseLinks = j.value(lastAttempt = "purchaseLinks", decltype(o.purchaseLinks)());

	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

void Sqex::Sound::from_json(const nlohmann::json& j, MusicImportConfig& o) {
	const char* lastAttempt = "from_json";
	try {
		o.name = j.value(lastAttempt = "name", decltype(o.name)());
		o.searchDirectories = j.at(lastAttempt = "searchDirectories").get<decltype(o.searchDirectories)>();
		o.items = j.value(lastAttempt = "items", decltype(o.items)());
	} catch (const std::exception& e) {
		throw std::invalid_argument(std::format("[{}] {}", lastAttempt, e.what()));
	}
}

static Utils::Win32::Thread StderrRelayer(Utils::Win32::Handle hStderrRead, std::function<void(const std::string&)> stderrCallback) {
	return Utils::Win32::Thread(L"StderrRelayer", [hStderrRead = std::move(hStderrRead), stderrCallback = std::move(stderrCallback)]() {
		std::string buf(8192, '\0');
		size_t ptr = 0;
		try {
			while (const auto read = hStderrRead.Read(0, &buf[ptr], buf.size() - ptr, Utils::Win32::Handle::PartialIoMode::AllowPartial)) {
				ptr += read;
				size_t prevNewline = 0;
				for (size_t newline = 0; (newline = buf.find_first_of("\n\r", prevNewline)) != std::string::npos && newline < ptr; prevNewline = newline + 1) {
					if (prevNewline < newline)
						stderrCallback(buf.substr(prevNewline, newline - prevNewline));
				}
				buf.erase(buf.begin(), buf.begin() + prevNewline);
				ptr -= prevNewline;
				if (buf.size() - ptr < 8192)
					buf.resize(buf.size() + std::min<size_t>(buf.size(), 1048576));
			}
			buf.resize(ptr);
		} catch (const Utils::Win32::Error& e) {
			buf.resize(ptr);
			if (e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA)
				buf += std::format("\nstderr read error: {}", e.what());
		}
		size_t prevNewline = 0;
		for (size_t newline = 0; (newline = buf.find_first_of("\n\r", prevNewline)) != std::string::npos && newline < ptr; prevNewline = newline + 1) {
			if (prevNewline < newline)
				stderrCallback(buf.substr(prevNewline, newline - prevNewline));
		}
		if (prevNewline < ptr)
			stderrCallback(buf.substr(prevNewline, ptr - prevNewline));
	});
}

Sqex::Sound::MusicImporter::FloatPcmSource::FloatPcmSource(
	const MusicImportSourceItem& sourceItem,
	std::vector<std::filesystem::path> resolvedPaths,
	std::function<std::span<uint8_t>(size_t len, bool throwOnIncompleteRead)> linearReader, const char* linearReaderType,
	const std::filesystem::path& ffmpegPath, std::function<void(const std::string&)> stderrCallback,
	int forceSamplingRate, std::string audioFilters
) {
	auto [hStdinRead, hStdinWrite] = Utils::Win32::Handle::FromCreatePipe();
	auto [hStdoutRead, hStdoutWrite] = Utils::Win32::Handle::FromCreatePipe();
	auto [hStderrRead, hStderrWrite] = Utils::Win32::Handle::FromCreatePipe();
	auto builder = Utils::Win32::ProcessBuilder();
	builder
		.WithPath(ffmpegPath)
		.WithNoWindow()
		.WithAppendArgument("-hide_banner")
		.WithAppendArgument("-loglevel").WithAppendArgument("warning")
		.WithStdin(std::move(hStdinRead))
		.WithStdout(std::move(hStdoutWrite))
		.WithStderr(std::move(hStderrWrite));

	auto useStdinPipe = false;
	for (size_t i = 0; i < sourceItem.inputFiles.size(); ++i) {
		if (sourceItem.inputFiles.at(i).empty()) {
			builder.WithAppendArgument("-f").WithAppendArgument(linearReaderType);
			builder.WithAppendArgument("-i").WithAppendArgument("-");
			useStdinPipe = true;
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
	}
	if (forceSamplingRate)
		builder.WithAppendArgument("-ar").WithAppendArgument("{}", forceSamplingRate);
	builder.WithAppendArgument("-");

	m_hReaderProcess = builder.Run().first;
	m_hStdoutReader = std::move(hStdoutRead);
	m_hStderrReaderThread = StderrRelayer(std::move(hStderrRead), stderrCallback);

	if (!useStdinPipe)
		return;

	m_hStdinWriterThread = Utils::Win32::Thread(L"MusicImporter::Source", [hStdinWriter = std::move(hStdinWrite), linearReader = std::move(linearReader), stderrCallback]() {
		try {
			while (true) {
				const auto read = linearReader(8192, false);
				if (read.empty())
					break;
				hStdinWriter.Write(0, read);
			};
		} catch (const Utils::Win32::Error& e) {
			if (e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA) {
				stderrCallback(std::format("stdout write error: {}", e.what()));
			}
		}
	});
}

Sqex::Sound::MusicImporter::FloatPcmSource::~FloatPcmSource() {
	if (m_hReaderProcess)
		m_hReaderProcess.Terminate(0);
	if (m_hStdinWriterThread)
		m_hStdinWriterThread.Wait();
	if (m_hStderrReaderThread)
		m_hStderrReaderThread.Wait();
}

std::span<float> Sqex::Sound::MusicImporter::FloatPcmSource::operator()(size_t len, bool throwOnIncompleteRead) {
	std::move(m_buffer.end() - m_unusedBytes, m_buffer.end(), m_buffer.begin());
	m_buffer.resize(std::max(m_unusedBytes, len * sizeof(float)));
	try {
		while (const auto available = m_buffer.size() - m_unusedBytes) {
			const auto read = m_hStdoutReader.Read(0, &m_buffer[m_unusedBytes], available, Utils::Win32::Handle::PartialIoMode::AllowPartial);
			if (!read)
				break;
			m_unusedBytes += read;
		}
	} catch (const Utils::Win32::Error& e) {
		if (e.Code() != ERROR_BROKEN_PIPE && e.Code() != ERROR_NO_DATA)
			throw;
	}
	m_buffer.resize(m_unusedBytes);
	const auto availableSampleCount = m_unusedBytes / sizeof(float);
	m_unusedBytes -= availableSampleCount * sizeof(float);
	if (availableSampleCount != len && throwOnIncompleteRead)
		throw std::runtime_error("EOF");
	if (m_buffer.empty())
		return std::span<float>();
	return std::span(reinterpret_cast<float*>(&m_buffer[0]), m_buffer.size() * sizeof(m_buffer[0])).subspan(0, availableSampleCount);
}

nlohmann::json Sqex::Sound::MusicImporter::RunProbe(const std::filesystem::path& path, const std::filesystem::path& ffprobePath, std::function<void(const std::string&)> stderrCallback) {
	auto [hStdoutRead, hStdoutWrite] = Utils::Win32::Handle::FromCreatePipe();
	auto [hStderrRead, hStderrWrite] = Utils::Win32::Handle::FromCreatePipe();
	const auto process(std::move(Utils::Win32::ProcessBuilder()
		.WithPath(ffprobePath)
		.WithStdout(std::move(hStdoutWrite))
		.WithStderr(std::move(hStderrWrite))
		.WithNoWindow()
		.WithAppendArgument("-hide_banner")
		.WithAppendArgument("-loglevel").WithAppendArgument("warning")
		.WithAppendArgument("-i").WithAppendArgument(path.wstring())
		.WithAppendArgument("-show_streams")
		.WithAppendArgument("-select_streams").WithAppendArgument("a:0")
		.WithAppendArgument("-print_format").WithAppendArgument("json")
		.Run().first));

	const auto stderrReader = StderrRelayer(std::move(hStderrRead), std::move(stderrCallback));

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
	stderrReader.Wait();

	return nlohmann::json::parse(str);
}

nlohmann::json Sqex::Sound::MusicImporter::RunProbe(const char* originalFormat, std::function<std::span<uint8_t>(size_t len, bool throwOnIncompleteRead)> linearReader, const std::filesystem::path& ffprobePath, std::function<void(const std::string&)> stderrCallback) {
	auto [hStdoutRead, hStdoutWrite] = Utils::Win32::Handle::FromCreatePipe();
	auto [hStdinRead, hStdinWrite] = Utils::Win32::Handle::FromCreatePipe();
	auto [hStderrRead, hStderrWrite] = Utils::Win32::Handle::FromCreatePipe();
	const auto process(std::move(Utils::Win32::ProcessBuilder()
		.WithPath(ffprobePath)
		.WithStdin(std::move(hStdinRead))
		.WithStdout(std::move(hStdoutWrite))
		.WithStderr(std::move(hStderrWrite))
		.WithNoWindow()
		.WithAppendArgument("-hide_banner")
		.WithAppendArgument("-loglevel").WithAppendArgument("warning")
		.WithAppendArgument("-f").WithAppendArgument(originalFormat)
		.WithAppendArgument("-i").WithAppendArgument("-")
		.WithAppendArgument("-show_streams")
		.WithAppendArgument("-select_streams").WithAppendArgument("a:0")
		.WithAppendArgument("-print_format").WithAppendArgument("json")
		.Run().first));

	const auto stderrReader = StderrRelayer(std::move(hStderrRead), std::move(stderrCallback));
	const auto stdoutWriter = Utils::Win32::Thread(L"RunProbe", [hStdinWrite = std::move(hStdinWrite), linearReader = std::move(linearReader)]() {
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
	stderrReader.Wait();

	return nlohmann::json::parse(str);
}

Sqex::Sound::MusicImporter::MusicImporter(std::map<std::string, MusicImportSourceItem> sourceItems, MusicImportTarget target, std::filesystem::path ffmpeg, std::filesystem::path ffprobe, Utils::Win32::Event cancelEvent)
	: m_sourceItems(std::move(sourceItems))
	, m_target(std::move(target))
	, m_ffmpeg(std::move(ffmpeg))
	, m_ffprobe(std::move(ffprobe))
	, m_cancelEvent(std::move(cancelEvent)) {

	for (const auto& [sourceName, source] : m_sourceItems)
		m_sourcePaths[sourceName].resize(source.inputFiles.size());

	if (!m_sourceItems.contains(OriginalSource)) {
		m_sourceItems[OriginalSource] = MusicImportSourceItem{
			.inputFiles = { {} }
		};
		m_sourcePaths[OriginalSource] = { {} };
	}
}

void Sqex::Sound::MusicImporter::AppendReader(std::shared_ptr<Sqex::Sound::ScdReader> reader) {
	m_targetOriginals.emplace_back(std::move(reader));
}

bool Sqex::Sound::MusicImporter::ResolveSources(std::string dirName, const std::filesystem::path& dir) {
	auto allFound = true;
	for (const auto& [sourceName, source] : m_sourceItems) {
		for (size_t i = 0; i < source.inputFiles.size(); ++i) {
			if (m_cancelEvent.Wait(0) == WAIT_OBJECT_0)
				return false;
			if (!m_sourcePaths[sourceName][i].empty())
				continue;

			const auto& patterns = source.inputFiles.at(i);
			if (patterns.empty())
				continue;  // use original

			std::set<std::filesystem::path> occurrences;
			for (const auto& item : std::filesystem::directory_iterator(dir)) {
				for (const auto& pattern : patterns) {
					if (pattern.directory != dirName)
						continue;
					if (srell::regex_search(item.path().filename().wstring(), pattern.GetCompiledPattern()))
						occurrences.insert(item.path());
				}
			}

			auto found = false;
			if (occurrences.size() == 1) {
				try {
					const auto probe(RunProbe(*occurrences.begin(), m_ffprobe, [this](const std::string& msg) { OnWarningLog(msg); }).at("streams").at(0));
					m_sourceInfo[sourceName] = {
						.Rate = static_cast<uint32_t>(std::strtoul(probe.at("sample_rate").get<std::string>().c_str(), nullptr, 10)),
						.Channels = probe.at("channels").get<uint32_t>(),
					};
					m_sourcePaths[sourceName][i] = *occurrences.begin();
					found = true;
				} catch (const std::exception& e) {
					ShowWarningLog(std::format("Error when trying to check {}: {}", *occurrences.begin(), e.what()));
				}
			} else if (occurrences.size() >= 2) {
				std::string buf;
				for (const auto& o : occurrences) {
					if (!buf.empty())
						buf += ", ";
					buf += Utils::ToUtf8(relative(o, dir).wstring());
				}
				throw std::runtime_error(std::format("Multiple occurrences exist: {}", buf));
			}

			allFound &= found;
		}
	}
	return allFound;
}

void Sqex::Sound::MusicImporter::Merge(const std::function<void(const std::filesystem::path& path, std::vector<uint8_t>)>& cb) {
	std::string lastStepDescription;

	try {
		lastStepDescription = "LoadOriginal";
		auto& originalInfo = m_sourceInfo[OriginalSource];
		if (!m_targetOriginals.front())
			throw std::runtime_error(std::format("file {} not found", m_target.path[0].wstring()));
		const auto originalEntry = m_targetOriginals.front()->GetSoundEntry(0);
		const char* originalEntryFormat = nullptr;

		std::vector<uint8_t> originalData;
		switch (originalEntry.Header->Format) {
		case Sqex::Sound::SoundEntryHeader::EntryFormat_WaveFormatAdpcm:
			originalData = originalEntry.GetMsAdpcmWavFile();
			originalEntryFormat = "wav";
			break;

		case Sqex::Sound::SoundEntryHeader::EntryFormat_Ogg:
			originalData = originalEntry.GetOggFile();
			originalEntryFormat = "ogg";
			break;
		}
		const auto originalDataStream = Sqex::MemoryRandomAccessStream(originalData);

		lastStepDescription = "ProbeOriginal";
		uint32_t loopStartBlockIndex = 0;
		uint32_t loopEndBlockIndex = 0;
		{
			const auto originalProbe(RunProbe(originalEntryFormat, originalDataStream.AsLinearReader<uint8_t>(), m_ffprobe, [this](const std::string& msg) { OnWarningLog(msg); }).at("streams").at(0));
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

		lastStepDescription = "ResolveSampleRate";
		uint32_t targetRate = 0;
		for (const auto& targetInfo : m_sourceInfo | std::views::values)
			targetRate = std::max(targetRate, targetInfo.Rate);
		loopStartBlockIndex = static_cast<uint32_t>(1ULL * loopStartBlockIndex * targetRate / originalInfo.Rate);
		loopEndBlockIndex = static_cast<uint32_t>(1ULL * loopEndBlockIndex * targetRate / originalInfo.Rate);

		lastStepDescription = "ResolveLoop";
		if (!!m_target.loopOffsetDelta) {
			loopStartBlockIndex -= static_cast<uint32_t>(static_cast<double>(targetRate) * m_target.loopOffsetDelta);
			loopEndBlockIndex -= static_cast<uint32_t>(static_cast<double>(targetRate) * m_target.loopOffsetDelta);
		}
		if (m_target.loopLengthDivisor != 1) {
			loopEndBlockIndex = loopStartBlockIndex + (loopEndBlockIndex - loopStartBlockIndex) / m_target.loopLengthDivisor;
		}

		lastStepDescription = "EnsureSegments";
		if (m_target.segments.empty()) {
			if (m_sourcePaths.size() != 2)  // user-specified and OriginalSource
				throw std::invalid_argument("segments must be set when there are more than one source");
			m_target.segments = std::vector<MusicImportSegmentItem>{ { .channels = {
				{
					.source = m_sourcePaths.begin()->first == OriginalSource ? m_sourcePaths.rbegin()->first : m_sourcePaths.begin()->first,
					.channel = 0
				},
				{
					.source = m_sourcePaths.begin()->first == OriginalSource ? m_sourcePaths.rbegin()->first : m_sourcePaths.begin()->first,
					.channel = 1
				},
			} } };
		}

		lastStepDescription = "EnsureChannels";
		for (const auto& segment : m_target.segments) {
			if (originalInfo.Channels > segment.channels.size())
				throw std::invalid_argument(std::format("originalChannels={} > expected={}", originalInfo.Channels, segment.channels.size()));
		}

		uint32_t currentBlockIndex = 0;
		const auto endBlockIndex = loopEndBlockIndex ? loopEndBlockIndex : UINT32_MAX;

		lastStepDescription = "EncodeInit";
		const auto BufferedBlockCount = 8192;
		std::vector<uint8_t> headerBuffer;
		std::deque<std::vector<uint8_t>> dataBuffers;
		uint32_t dataBufferTotalSize = 0;
		uint32_t loopStartOffset = 0;
		uint32_t loopEndOffset = 0;

		std::vector<uint32_t> oggDataSeekTable;
		vorbis_info vi{};
		vorbis_dsp_state vd{};
		vorbis_block vb{};
		ogg_stream_state os{};
		ogg_page og{};
		ogg_packet op{};
		Utils::CallOnDestruction::Multiple oggCleanup;
		uint64_t oggGranulePosOffset = 0;

		std::vector<std::vector<float>> wavBuffers;
		std::vector<float*> wavBufferPtrs;
		if (originalEntry.Header->Format == Sqex::Sound::SoundEntryHeader::EntryFormat_Ogg) {
			vorbis_info_init(&vi);
			if (const auto res = vorbis_encode_init_vbr(&vi, originalInfo.Channels, targetRate, 1))
				throw std::runtime_error(std::format("vorbis_encode_init_vbr: {}", res));
			oggCleanup += Utils::CallOnDestruction([&vi] { vorbis_info_clear(&vi); });

			if (const auto res = vorbis_analysis_init(&vd, &vi))
				throw std::runtime_error(std::format("vorbis_analysis_init: {}", res));
			oggCleanup += Utils::CallOnDestruction([&vd] { vorbis_dsp_clear(&vd); });

			if (const auto res = vorbis_block_init(&vd, &vb))
				throw std::runtime_error(std::format("vorbis_block_init: {}", res));
			oggCleanup += Utils::CallOnDestruction([&vb] { vorbis_block_clear(&vb); });

			if (const auto res = ogg_stream_init(&os, 0))
				throw std::runtime_error(std::format("ogg_stream_init: {}", res));
			oggCleanup += Utils::CallOnDestruction([&os] { ogg_stream_clear(&os); });

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
		} else {
			wavBuffers.resize(originalInfo.Channels);
			wavBufferPtrs.resize(originalInfo.Channels);
		}

		for (size_t segmentIndex = 0; segmentIndex < m_target.segments.size(); ++segmentIndex) {
			const auto& segment = m_target.segments[segmentIndex];
			std::set<std::string> usedSources;
			for (const auto& name : m_sourceInfo | std::views::keys) {
				if (name == OriginalSource || std::ranges::any_of(segment.channels, [&name](const auto& ch) { return ch.source == name; }))
					usedSources.insert(name);
			}

			for (const auto& name : usedSources) {
				lastStepDescription = std::format("EncodeVorbisOpenSource(segment={}, name={})", segmentIndex, name);
				auto& info = m_sourceInfo.at(name);
				info.FirstBlocks.clear();
				info.FirstBlocks.resize(info.Channels, INT32_MAX);

				const auto ffmpegFilter = segment.sourceFilters.contains(name) ? segment.sourceFilters.at(name) : std::string();
				uint32_t minBlockIndex = 0;
				double threshold = 0.1;
				info.Reader = std::make_unique<FloatPcmSource>(m_sourceItems.at(name), m_sourcePaths.at(name), originalDataStream.AsLinearReader<uint8_t>(), originalEntryFormat, m_ffmpeg, [this](const std::string& msg) { OnWarningLog(msg); }, targetRate, ffmpegFilter);
				if (segment.sourceOffsets.contains(name))
					minBlockIndex = static_cast<uint32_t>(targetRate * segment.sourceOffsets.at(name));
				else if (name == OriginalSource)
					minBlockIndex = currentBlockIndex;

				info.ReadBuf.clear();
				info.ReadBufPtr = 0;
				if (segment.sourceThresholds.contains(name))
					threshold = segment.sourceThresholds.at(name);

				std::vector<uint32_t> channelMap;
				if (m_target.sequentialToFfmpegChannelIndexMap.empty() || name != OriginalSource) {
					for (uint32_t i = 0; i < info.Channels; ++i)
						channelMap.push_back(i);
				} else {
					channelMap = m_target.sequentialToFfmpegChannelIndexMap;
				}

				lastStepDescription = std::format("EncodeVorbisProbeSourceOffset(segment={}, name={})", segmentIndex, name);
				size_t blockIndex = 0;
				auto pending = true;
				while (pending) {
					const auto buf = (*info.Reader)(info.Channels * static_cast<size_t>(8192), false);
					if (buf.empty())
						break;

					if (const auto trimBlocks = std::min<size_t>(minBlockIndex, buf.size() / info.Channels)) {
						info.ReadBuf.insert(info.ReadBuf.end(), buf.begin() + trimBlocks * info.Channels, buf.end());
						minBlockIndex -= static_cast<uint32_t>(trimBlocks);
					} else
						info.ReadBuf.insert(info.ReadBuf.end(), buf.begin(), buf.end());
					for (; pending && (blockIndex + 1) * info.Channels - 1 < info.ReadBuf.size(); ++blockIndex) {
						if (m_cancelEvent.Wait(0) == WAIT_OBJECT_0)
							return;

						pending = false;
						for (size_t c = 0; c < info.Channels; ++c) {
							if (info.FirstBlocks[c] != INT32_MAX)
								continue;

							if (info.ReadBuf[blockIndex * info.Channels + channelMap[c]] >= threshold)
								info.FirstBlocks[c] = static_cast<uint32_t>(blockIndex);
							else
								pending = true;
						}
					}
				}
				if (pending)
					throw std::runtime_error(std::format("first audio sample above {} not found", threshold));
			}

			for (const auto& name : usedSources) {
				lastStepDescription = std::format("EncodeVorbisOffsetSource(segment={}, name={})", segmentIndex, name);
				auto& info = m_sourceInfo.at(name);
				for (size_t channelIndex = 0; channelIndex < segment.channels.size(); ++channelIndex) {
					const auto& channelMap = segment.channels[channelIndex];
					if (channelMap.source == name && name != OriginalSource) {
						auto srcCopyFromOffset = static_cast<SSIZE_T>(originalInfo.FirstBlocks[channelIndex]) - info.FirstBlocks[channelMap.channel];
						srcCopyFromOffset *= info.Channels;

						if (srcCopyFromOffset < 0) {
							info.ReadBuf.erase(info.ReadBuf.begin(), info.ReadBuf.begin() - srcCopyFromOffset);
							srcCopyFromOffset = 0;
						} else {
							info.ReadBuf.resize(info.ReadBuf.size() + srcCopyFromOffset);
							std::move(info.ReadBuf.begin(), info.ReadBuf.end() - srcCopyFromOffset, info.ReadBuf.begin() + srcCopyFromOffset);
							std::fill_n(info.ReadBuf.begin(), srcCopyFromOffset, 0.f);
						}
						break;
					}
				}
			}

			lastStepDescription = std::format("Encode");
			std::vector<SourceSet*> sourceSetsByIndex;
			std::vector<char> channelRequiresMapping;
			for (size_t i = 0; i < originalInfo.Channels; ++i) {
				sourceSetsByIndex.push_back(&m_sourceInfo.at(segment.channels[i].source));
				channelRequiresMapping.push_back(segment.channels[i].source == OriginalSource);
			}

			size_t wrote = 0;
			const auto segmentEndBlockIndex = segment.length ? currentBlockIndex + static_cast<uint32_t>(targetRate * *segment.length) : endBlockIndex;
			auto stopSegment = currentBlockIndex >= segmentEndBlockIndex;
			float** buf = nullptr;
			uint32_t bufptr = 0;
			while (!stopSegment) {
				if (buf == nullptr) {
					if (m_cancelEvent.Wait(0) == WAIT_OBJECT_0)
						return;

					if (originalEntry.Header->Format == Sqex::Sound::SoundEntryHeader::EntryFormat_Ogg)
						buf = vorbis_analysis_buffer(&vd, BufferedBlockCount);
					else {
						wavBufferPtrs.clear();
						for (auto& wavBuffer : wavBuffers) {
							wavBuffer.resize(BufferedBlockCount);
							wavBufferPtrs.push_back(&wavBuffer[0]);
						}
						buf = &wavBufferPtrs[0];
					}
					if (!buf)
						throw std::runtime_error("vorbis_analysis_buffer: fail");
					bufptr = 0;
				}
				for (size_t i = 0; i < originalInfo.Channels; ++i) {
					const auto pSource = sourceSetsByIndex[i];
					const auto sourceChannelIndex = channelRequiresMapping[i] && !m_target.sequentialToFfmpegChannelIndexMap.empty() ? m_target.sequentialToFfmpegChannelIndexMap[segment.channels[i].channel] : segment.channels[i].channel;
					if (pSource->ReadBufPtr + sourceChannelIndex >= pSource->ReadBuf.size()) {
						const auto readReqSize = std::min<uint32_t>(8192, segmentEndBlockIndex - currentBlockIndex) * pSource->Channels;
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

				if (bufptr == BufferedBlockCount || stopSegment || currentBlockIndex == loopStartBlockIndex) {

					if (originalEntry.Header->Format == Sqex::Sound::SoundEntryHeader::EntryFormat_Ogg) {
						if (const auto res = vorbis_analysis_wrote(&vd, bufptr); res < 0)
							throw std::runtime_error(std::format("vorbis_analysis_wrote: {}", res));
						buf = nullptr;

						if (stopSegment && segmentIndex == m_target.segments.size() - 1) {
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
									if (const auto res = ogg_stream_pageout_fill(&os, &og, 65307);
										res < 0)
										throw std::runtime_error(std::format("ogg_stream_pageout_fill(65307): {}", res));
									else if (res == 0)
										break;

									dataBuffers.emplace_back();
									dataBuffers.back().reserve(static_cast<size_t>(og.header_len) + og.body_len);
									dataBuffers.back().insert(dataBuffers.back().end(), og.header, og.header + og.header_len);
									dataBuffers.back().insert(dataBuffers.back().end(), og.body, og.body + og.body_len);
									oggDataSeekTable.push_back(dataBufferTotalSize);
									dataBufferTotalSize += og.header_len + og.body_len;
								}
							}
						}

						if (currentBlockIndex == loopStartBlockIndex) {
							while (!ogg_page_eos(&og)) {
								if (const auto res = ogg_stream_flush_fill(&os, &og, 0); res < 0)
									throw std::runtime_error(std::format("ogg_stream_flush_fill(0): {}", res));
								else if (res == 0)
									break;

								dataBuffers.emplace_back();
								dataBuffers.back().reserve(static_cast<size_t>(og.header_len) + og.body_len);
								dataBuffers.back().insert(dataBuffers.back().end(), og.header, og.header + og.header_len);
								dataBuffers.back().insert(dataBuffers.back().end(), og.body, og.body + og.body_len);
								oggDataSeekTable.push_back(dataBufferTotalSize);
								dataBufferTotalSize += og.header_len + og.body_len;
							}

							loopStartOffset = oggDataSeekTable.empty() ? 0 : oggDataSeekTable.back();

							if (const auto offset = static_cast<uint32_t>(loopStartBlockIndex - ogg_page_granulepos(&og))) {
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
									throw std::runtime_error(std::format("ogg_stream_init(reloop): {}", res));
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
										throw std::runtime_error(std::format("ogg_stream_flush_fill(reloop): {}", res));
									else if (res == 0)
										break;

									headerBuffer.insert(headerBuffer.end(), og.header, og.header + og.header_len);
									headerBuffer.insert(headerBuffer.end(), og.body, og.body + og.body_len);
								}
							}
						}
					} else {
						dataBuffers.emplace_back();
						dataBuffers.back().resize(sizeof int16_t * originalInfo.Channels * bufptr);
						const auto view = std::span(reinterpret_cast<int16_t*>(&dataBuffers.back()[0]), bufptr * originalInfo.Channels);
						for (size_t i = 0, ptr = 0; i < bufptr; ++i) {
							for (size_t ch = 0; ch < originalInfo.Channels; ++ch)
								view[ptr++] = (static_cast<int16_t>(buf[ch][i] * 32767.));
						}
						buf = nullptr;

					}
				}
			}
		}

		if (loopEndBlockIndex && !loopEndOffset)
			loopEndOffset = dataBufferTotalSize;

		dataBuffers[0].reserve(dataBufferTotalSize);
		for (size_t i = 1; i < dataBuffers.size(); ++i) {
			dataBuffers[0].insert(dataBuffers[0].end(), dataBuffers[i].begin(), dataBuffers[i].end());
			dataBuffers[i].clear();
		}

		lastStepDescription = std::format("ToSoundEntry");
		Sqex::Sound::ScdWriter::SoundEntry soundEntry;

		if (originalEntry.Header->Format == Sqex::Sound::SoundEntryHeader::EntryFormat_Ogg) {
			soundEntry = Sqex::Sound::ScdWriter::SoundEntry::FromOgg(
				std::move(headerBuffer), std::move(dataBuffers[0]),
				originalInfo.Channels, targetRate,
				loopStartOffset, loopEndOffset,
				std::span(oggDataSeekTable)
			);

		} else {
			WAVEFORMATEX wf{
				.wFormatTag = WAVE_FORMAT_PCM,
				.nChannels = static_cast<uint16_t>(originalInfo.Channels),
				.nSamplesPerSec = targetRate,
				.nAvgBytesPerSec = sizeof uint16_t * wf.nChannels * targetRate,
				.nBlockAlign = sizeof uint16_t * wf.nChannels,
				.wBitsPerSample = sizeof uint16_t * 8,
			};

			const auto header = std::span(reinterpret_cast<const uint8_t*>(&wf), sizeof wf);
			const auto data = std::span(dataBuffers[0]);
			std::vector<uint8_t> res;
			const auto insert = [&res](const auto& v) {
				res.insert(res.end(), reinterpret_cast<const uint8_t*>(&v), reinterpret_cast<const uint8_t*>(&v) + sizeof v);
			};
			const auto totalLength = static_cast<uint32_t>(0
				+ 12  // "RIFF"####"WAVE"
				+ 8 + header.size() // "fmt "####<header>
				+ 8 + data.size()  // "data"####<data>
				);
			res.reserve(totalLength);
			insert(Utils::LE(0x46464952U));  // "RIFF"
			insert(Utils::LE(totalLength - 8));
			insert(Utils::LE(0x45564157U));  // "WAVE"
			insert(Utils::LE(0x20746D66U));  // "fmt "
			insert(Utils::LE(static_cast<uint32_t>(header.size())));
			res.insert(res.end(), header.begin(), header.end());
			insert(Utils::LE(0x61746164U));  // "data"
			insert(Utils::LE(static_cast<uint32_t>(data.size())));
			res.insert(res.end(), data.begin(), data.end());

			const auto stream = Sqex::MemoryRandomAccessStream(res);
			soundEntry = Sqex::Sound::ScdWriter::SoundEntry::FromWave(stream.AsLinearReader<uint8_t>());
		}

		if (const auto marks = originalEntry.GetMarkedSampleBlockIndices(); !marks.empty()) {
			auto& buf = soundEntry.AuxChunks[std::string(Sqex::Sound::SoundEntryAuxChunk::Name_Mark, sizeof Sqex::Sound::SoundEntryAuxChunk::Name_Mark)];
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

		lastStepDescription = std::format("ToScd");
		for (size_t pathIndex = 0; pathIndex < m_target.path.size(); pathIndex++) {
			const auto& path = m_target.path.at(pathIndex);
			const auto& scdReader = m_targetOriginals.at(pathIndex);
			Sqex::Sound::ScdWriter writer;
			writer.SetTable1(scdReader->ReadTable1Entries());
			writer.SetTable4(scdReader->ReadTable4Entries());
			writer.SetTable2(scdReader->ReadTable2Entries());
			writer.SetSoundEntry(0, soundEntry);
			cb(path, writer.Export());
		}
	} catch (const std::runtime_error& e) {
		throw std::runtime_error(std::format("Failed to encode: {} (step: {})", e.what(), lastStepDescription));
	}
}

const srell::u16wregex& Sqex::Sound::MusicImportSourceItemInputFile::GetCompiledPattern() const {
	if (!m_patternCompiled)
		m_patternCompiled = srell::u16wregex(Utils::FromUtf8(pattern), srell::regex_constants::icase);
	return *m_patternCompiled;
}
