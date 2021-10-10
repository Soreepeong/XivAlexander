#include "pch.h"

#include <mmreg.h>
#include <XivAlexanderCommon/Sqex_Sound_Reader.h>
#include <XivAlexanderCommon/Sqex_Sound_Writer.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Utils_Win32_Process.h>
#include <XivAlexanderCommon/Utils_Win32_ThreadPool.h>

std::pair<uint32_t, uint32_t> DecodeOgg(const Sqex::RandomAccessStream& stream, std::function<bool(int rate, int channels, int blocks, float** samples)> cb) {
	ogg_sync_state oy{};
	ogg_sync_init(&oy);
	const auto oyCleanup = Utils::CallOnDestruction([&oy] { ogg_sync_clear(&oy); });

	vorbis_info vi{};
	vorbis_info_init(&vi);
	const auto viCleanup = Utils::CallOnDestruction([&vi] { vorbis_info_clear(&vi); });

	vorbis_comment vc{};
	vorbis_comment_init(&vc);
	const auto vcCleanup = Utils::CallOnDestruction([&vc] { vorbis_comment_clear(&vc); });

	ogg_stream_state os{};
	Utils::CallOnDestruction osCleanup;

	vorbis_dsp_state vd{};
	Utils::CallOnDestruction vdCleanup;

	vorbis_block vb;
	vorbis_block_init(&vd, &vb);
	const auto vbCleanup = Utils::CallOnDestruction([&vb] { vorbis_block_clear(&vb); });

	uint32_t loopStartSample = 0, loopEndSample = 0;
	ogg_page og{};
	ogg_packet op{};
	for (size_t packetIndex = 0, pageIndex = 0, ptr = 0, to_ = static_cast<size_t>(stream.StreamSize()); ptr < to_; ) {
		const auto available = static_cast<uint32_t>(std::min<size_t>(4096, to_ - ptr));
		if (const auto buffer = ogg_sync_buffer(&oy, available))
			stream.ReadStream(ptr, buffer, available);
		else
			throw std::runtime_error("ogg_sync_buffer failed");
		ptr += available;
		if (0 != ogg_sync_wrote(&oy, available))
			throw std::runtime_error("ogg_sync_wrote failed");

		for (;; ++pageIndex) {
			if (auto r = ogg_sync_pageout(&oy, &og); r == -1)
				throw std::invalid_argument("ogg_sync_pageout failed");
			else if (r == 0)
				break;

			if (pageIndex == 0) {
				if (0 != ogg_stream_init(&os, ogg_page_serialno(&og)))
					throw std::runtime_error("ogg_stream_init failed");
				osCleanup = [&os] { ogg_stream_clear(&os); };
			}

			if (0 != ogg_stream_pagein(&os, &og))
				throw std::runtime_error("ogg_stream_pagein failed");

			for (;; ++packetIndex) {
				if (auto r = ogg_stream_packetout(&os, &op); r == -1)
					throw std::runtime_error("ogg_stream_packetout failed");
				else if (r == 0)
					break;

				if (packetIndex < 3) {
					if (const auto res = vorbis_synthesis_headerin(&vi, &vc, &op))
						throw std::runtime_error(std::format("vorbis_synthesis_headerin failed: {}", res));

					if (packetIndex == 2) {
						char** comments = vc.user_comments;
						while (*comments) {
							if (_strnicmp(*comments, "LoopStart=", 10) == 0)
								loopStartSample = std::strtoul(*comments + 10, nullptr, 10);
							else if (_strnicmp(*comments, "LoopEnd=", 8) == 0)
								loopEndSample = std::strtoul(*comments + 8, nullptr, 10);
							++comments;
						}
						if (vorbis_synthesis_init(&vd, &vi))
							throw std::runtime_error("vorbis_synthesis_init failed");
						vdCleanup = [&vd] { vorbis_dsp_clear(&vd); };
					}
				} else {
					float** pcm;
					int blocks;

					vorbis_synthesis(&vb, &op);
					vorbis_synthesis_blockin(&vd, &vb);
					while ((blocks = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
						if (cb(vi.rate, vi.channels, blocks, pcm))
							return { loopStartSample, loopEndSample };
						vorbis_synthesis_read(&vd, blocks);
					}
				}
			}

			if (ogg_page_eos(&og)) {
				return { loopStartSample, loopEndSample };
			}
		}
	}

	throw std::invalid_argument("ogg: eos not found");
}

template<typename T>
std::vector<uint8_t> ToWav(std::span<T> samples, uint32_t samplingRate, uint16_t wFormatTag) {
	WAVEFORMATEX wf{
		.wFormatTag = wFormatTag,
		.nChannels = 2,
		.nSamplesPerSec = samplingRate,
		.nAvgBytesPerSec = sizeof T * wf.nChannels * samplingRate,
		.nBlockAlign = sizeof T * wf.nChannels,
		.wBitsPerSample = sizeof T * 8,
	};

	const auto header = std::span(reinterpret_cast<const uint8_t*>(&wf), sizeof wf);
	const auto data = std::span(reinterpret_cast<const uint8_t*>(&samples[0]), samples.size_bytes());
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
	return res;
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

	const size_t BlockCountPerRead = 4096;
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
					if (const auto r = ogg_stream_pageout(&os, &og); r == 0)
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

std::tuple<Utils::Win32::Process, Utils::Win32::File, Utils::Win32::File> RunAndReadOutput(std::filesystem::path program, std::initializer_list<std::wstring> args) {
	auto [hStdinRead, hStdinWrite] = Utils::Win32::File::CreatePipe();
	auto [hStdoutRead, hStdoutWrite] = Utils::Win32::File::CreatePipe();
	return {
		Utils::Win32::ProcessBuilder()
			.WithPath(program)
			.WithAppendArgument(args)
			.WithStdin(hStdinRead)
			.WithStdout(hStdoutWrite)
			.Run()
			.first,
		hStdinWrite,
		hStdoutRead,
	};
}

nlohmann::json ParseSubprocessJson(std::filesystem::path program, std::initializer_list<std::wstring> args) {
	const auto r = std::get<2>(RunAndReadOutput(program, args));
	std::string str;
	try {
		while (true) {
			const auto buf = r.Read<char>(0, 65536, Utils::Win32::File::PartialIoMode::AllowPartial);
			if (buf.empty())
				break;
			str.insert(str.end(), buf.begin(), buf.end());
		}
	}
	catch (const Utils::Win32::Error& e) {
		if (e.Code() != ERROR_BROKEN_PIPE)
			throw;
	}
	return nlohmann::json::parse(str);
}

std::vector<uint8_t> Convert(const Sqex::Sqpack::Reader& sqpackReader, std::filesystem::path srcFile, std::string scdf, uint32_t findFromBlockIndex, float loopDelta, int loopDivider) {
	const auto reader = Sqex::Sound::ScdReader(sqpackReader[scdf]);
	const auto entry = reader.GetSoundEntry(0);

	uint32_t referenceFirstBlock = 0, referenceRate = 0, referenceChannels = 0;
	auto [referenceLoopStartBlock, referenceLoopEndBlock] = DecodeOgg(Sqex::MemoryRandomAccessStream(entry.GetOggFile()), [&](int rate, int channels, int blocks, float** samples) {
		referenceRate = rate;
		referenceChannels = channels;
		for (auto i = 0; i < blocks; ++i, ++referenceFirstBlock)
			if (samples[0][i] >= 0.1)
				return true;
		return false;
	});
	if (const auto loopLength = (referenceLoopEndBlock - referenceLoopStartBlock) / loopDivider) {
		referenceLoopStartBlock -= static_cast<int>(referenceRate * loopDelta);
		referenceLoopStartBlock %= loopLength;
		referenceLoopEndBlock = referenceLoopStartBlock + loopLength;
	}

	const auto srcInfo = ParseSubprocessJson(LR"(C:\Windows\ffprobe.exe)", { L"-i", srcFile, L"-print_format", L"json", L"-show_streams", L"-select_streams", L"a:0" }).at("streams").at(0);
	const auto srcRate = std::strtoul(srcInfo.at("sample_rate").get<std::string>().c_str(), nullptr, 10);
	const auto srcChannels = srcInfo.at("channels").get<uint32_t>();
	const auto srcLoopStartBlock = static_cast<uint32_t>(1ULL * referenceLoopStartBlock * srcRate / referenceRate);
	const auto srcLoopEndBlock = static_cast<uint32_t>(1ULL * referenceLoopEndBlock * srcRate / referenceRate);

	auto [proc, hDecoderInWrite, hDecoderOutRead] = RunAndReadOutput(LR"(C:\Windows\ffmpeg.exe)", { L"-i", srcFile, L"-f", L"f32le", L"-" });
	hDecoderInWrite.Clear();
	std::vector<float> srcSamples;
	uint32_t srcFirstBlock = 0;
	while (true) {
		size_t mark = srcSamples.size();
		try {
			srcSamples.resize(mark + 65536);
			srcSamples.resize(mark + hDecoderOutRead.Read(0, std::span(srcSamples).subspan(mark), Utils::Win32::File::PartialIoMode::AllowPartial));
			if (srcSamples.size() == mark)
				break;
		} catch (const Utils::Win32::Error& e) {
			if (e.Code() != ERROR_BROKEN_PIPE)
				throw;
		}
		auto found = false;
		for (auto i = mark; i < srcSamples.size(); (i += srcChannels), (++srcFirstBlock)) {
			if (srcFirstBlock >= findFromBlockIndex && srcSamples[i] >= 0.1) {
				found = true;
				break;
			}
		}
		if (found)
			break;
	}

	if (const auto srcCopyFromOffset = static_cast<int>(srcChannels) * static_cast<int32_t>(1LL * referenceFirstBlock * srcRate / referenceRate - static_cast<int64_t>(srcFirstBlock));
		srcCopyFromOffset < 0) {
		srcSamples.erase(srcSamples.begin(), srcSamples.begin() - srcCopyFromOffset);
	} else {
		srcSamples.resize(srcSamples.size() + srcCopyFromOffset);
		std::move(srcSamples.begin(), srcSamples.end() - srcCopyFromOffset, srcSamples.begin() + srcCopyFromOffset);
		std::fill_n(srcSamples.begin(), srcCopyFromOffset, 0.f);
	}

	auto [proc2, hEncoderInWrite, hEncoderOutRead] = RunAndReadOutput(LR"(C:\Windows\ffmpeg.exe)", srcLoopStartBlock || srcLoopEndBlock ? std::initializer_list<std::wstring>{
		L"-f", L"f32le",
		L"-ac", std::format(L"{}", srcChannels),
		L"-ar", std::format(L"{}", srcRate),
		L"-i", L"-",
		L"-f", L"ogg",
		L"-q:a", L"10",
		L"-metadata", std::format(L"LoopStart={}", srcLoopStartBlock),
		L"-metadata", std::format(L"LoopEnd={}", srcLoopEndBlock),
		L"-",
	} : std::initializer_list<std::wstring>{
		L"-f", L"f32le",
		L"-ac", std::format(L"{}", srcChannels),
		L"-ar", std::format(L"{}", srcRate),
		L"-i", L"-",
		L"-f", L"ogg",
		L"-q:a", L"10",
		L"-",
	});

	auto errorOccurred = false;
	const auto hDecoderToEncoder = Utils::Win32::Thread(L"DecoderToEncoder", [srcChannels, &errorOccurred, endSample = srcLoopEndBlock ? srcLoopEndBlock * srcChannels : UINT32_MAX, &srcSamples, hEncoderInWrite = std::move(hEncoderInWrite), hDecoderOutRead = std::move(hDecoderOutRead), &scdf]() {
		auto pos = srcSamples.size();
		try {
			if (!srcSamples.empty())
				hEncoderInWrite.Write(0, std::span(srcSamples));

			while (pos < endSample) {
				srcSamples.resize(std::min<size_t>(8192, endSample - pos));
				srcSamples.resize(hDecoderOutRead.Read(0, std::span(srcSamples), Utils::Win32::File::PartialIoMode::AllowPartial));
				hEncoderInWrite.Write(0, std::span(srcSamples));
				pos += srcSamples.size();
			};
		} catch (const Utils::Win32::Error& e) {
			if (e.Code() != ERROR_BROKEN_PIPE || (pos < endSample && endSample != UINT32_MAX)) {
				std::cout << std::format("Error on {} ({}/{}): dec2enc {}\n", scdf, pos, endSample, e.what());
				errorOccurred = true;
			}
		}
	});

	const auto e = Sqex::Sound::ScdWriter::SoundEntry::FromOgg([&errorOccurred, hEncoderOutRead = std::move(hEncoderOutRead), buf = std::vector<uint8_t>(), &scdf](size_t len, bool throwOnIncompleteRead) mutable->std::span<uint8_t> {
		try {
			buf.resize(8192);
			buf.resize(hEncoderOutRead.Read(0, std::span(buf), Utils::Win32::File::PartialIoMode::AllowPartial));
			return std::span(buf);
		} catch (const Utils::Win32::Error& e) {
			if (e.Code() != ERROR_BROKEN_PIPE) {
				std::cout << std::format("Error on {}: {}\n", scdf, e.what());
				errorOccurred = true;
			}
			return {};
		}
	});

	proc.Terminate(0);
	proc2.Terminate(0);
	hDecoderToEncoder.Wait();
	if (errorOccurred)
		throw std::runtime_error("Error");

	Sqex::Sound::ScdWriter writer;
	writer.SetTable1(reader.ReadTable1Entries());
	writer.SetTable4(reader.ReadTable4Entries());
	writer.SetTable2(reader.ReadTable2Entries());
	writer.SetSoundEntry(0, e);
	return writer.Export();
}

int main() {
	const Sqex::Sqpack::Reader readers[4]{
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0c0000.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex1\0c0100.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex2\0c0200.win32.index)"},
		{LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex3\0c0300.win32.index)"},
	};

	Utils::Win32::TpEnvironment tp(IsDebuggerPresent() ? 1 : 0);
	for (const auto& [ex, srcPath, scdPath, findFromBlockIndex, loopDelta, divider] : std::vector<std::tuple<size_t, std::filesystem::path, std::string, uint32_t, float, int>>{
#pragma region Shadowbringers
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00000.flac)", "music/ex2/bgm_ex2_dan_d13.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00001.flac)", "music/ex2/bgm_ex2_event_38.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00002.flac)", "music/ex2/bgm_ex2_alpha_01.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00003.flac)", "music/ex2/bgm_ex2_alpha_02.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00004.flac)", "music/ex2/bgm_ex2_event_31.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00005.flac)", "music/ex2/bgm_ex2_alpha_03.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00006.flac)", "music/ex2/bgm_ex2_alpha_04.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00007.flac)", "music/ex2/bgm_ex2_event_32.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00008.flac)", "music/ex2/bgm_ex2_event_34.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00009.flac)", "music/ex2/bgm_ex2_dan_d14.scd", 0, 0, 1},

		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00010.flac)", "music/ex2/bgm_ex2_ban_24.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00011.flac)", "music/ex2/bgm_ex2_ban_25.scd", 0, 0, 1},
		// {2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00012.flac)", "music/ex2/bgm_season_xmascho.scd", 0, 0, 1}, // (TODO: 4ch)
		// {2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00013.flac)", "music/ex2/bgm_season_xmascho.scd", 0, 0, 1},
		{0, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00014.flac)", "music/ffxiv/bgm_emj.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00015.flac)", "music/ex2/bgm_ex2_eu_dungeon.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00016.flac)", "music/ex2/bgm_ex2_eu_boss.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00017.flac)", "music/ex2/bgm_ex2_eu_lb.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00018.flac)", "music/ex2/bgm_ex2_rti_26.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00019.flac)", "music/ex2/bgm_ex2_rti_28.scd", 0, 0, 1},

		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00020.flac)", "music/ex2/bgm_ex2_rti_29.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00021.flac)", "music/ex2/bgm_ex2_rti_27.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00022.flac)", "music/ex2/bgm_ex2_rti_30.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00023.flac)", "music/ex2/bgm_ex2_rti_19.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00024.flac)", "music/ex2/bgm_ex2_rti_32.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00025.flac)", "music/ex2/bgm_ex2_rti_33.scd", 0, 0, 1},
		// {2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00026.flac)", "music/ex2/bgm_ex2_rti_34.scd", 0, 0, 1}, // concat with below
		// {2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00027.flac)", "music/ex2/bgm_ex2_rti_34.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00028.flac)", "music/ex2/bgm_ex2_rti_23.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00029.flac)", "music/ex2/bgm_ex2_rti_24.scd", 0, 0, 1},

		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00030.flac)", "music/ex2/bgm_ex2_rti_20.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00031.flac)", "music/ex2/bgm_ex2_rti_21.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00032.flac)", "music/ex2/bgm_ex2_rti_22.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00033.flac)", "music/ex2/bgm_ex2_rti_25.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00034.flac)", "music/ex2/bgm_ex2_ban_26.scd", 0, 0, 1},
		{2, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00035.flac)", "music/ex2/bgm_ex2_dan_d15.scd", 0, 0, 1},
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00036.flac)", "music/ex3/.scd", 0, 0, 1}, // audio track in the movie has voices in it, so not replaceable
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00037.flac)", "music/ex3/bgm_ex3_system_title.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00038.flac)", "music/ex3/bgm_ex3_town_c_day.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00039.flac)", "music/ex3/bgm_ex3_field_kor_day2.scd", 0, 0, 1},

		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00040.flac)", "music/ex3/bgm_ex3_field_ama_battle.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00040.flac)", "music/ex3/bgm_ex3_field_ilm_battle.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00040.flac)", "music/ex3/bgm_ex3_field_kor_battle.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00040.flac)", "music/ex3/bgm_ex3_field_lak_battle.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00040.flac)", "music/ex3/bgm_ex3_field_rak_battle.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00041.flac)", "music/ex3/bgm_ex3_field_safe_03.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00042.flac)", "music/ex3/bgm_ex3_town_y_day2.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00043.flac)", "music/ex3/bgm_ex3_field_ama_day.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00044.flac)", "music/ex3/bgm_ex3_field_safe_02.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00045.flac)", "music/ex3/bgm_ex3_event_08.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00046.flac)", "music/ex3/bgm_ex3_field_lak_day.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00047.flac)", "music/ex3/bgm_ex3_dan_d01.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00048.flac)", "music/ex3/bgm_ex3_boss_battle01.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00049.flac)", "music/ex3/bgm_ex3_field_lak_night.scd", 0, 0, 1},

		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00050.flac)", "music/ex3/bgm_ex3_mount01.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00051.flac)", "music/ex3/bgm_ex3_town_c_night.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00052.flac)", "music/ex3/bgm_ex3_event_06.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00053.flac)", "music/ex3/bgm_ex3_field_ilm_day.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00055.flac)", "music/ex3/bgm_ex3_field_safe_01.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00056.flac)", "music/ex3/bgm_ex3_dan_d02.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00057.flac)", "music/ex3/bgm_ex3_ban_01.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00058.flac)", "music/ex3/bgm_ex3_field_ilm_night.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00059.flac)", "music/ex3/bgm_ex3_field_rak_day.scd", 0, 0, 1},

		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00060.flac)", "music/ex3/bgm_ex3_dan_d03.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00061.flac)", "music/ex3/bgm_ex3_field_rak_night.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00062.flac)", "music/ex3/bgm_ex3_event_02.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00063.flac)", "music/ex3/bgm_ex3_dan_d04.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00064.flac)", "music/ex3/bgm_ex3_field_ama_night.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00065.flac)", "music/ex3/bgm_ex3_event_03.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00066.flac)", "music/ex3/bgm_ex3_event_05.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00067.flac)", "music/ex3/bgm_ex3_dan_d05.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00068.flac)", "music/ex3/bgm_ex3_ban_02.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00069.flac)", "music/ex3/bgm_ex3_event_04.scd", 0, 0, 1},

		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00070.flac)", "music/ex3/bgm_ex3_field_tem_day.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00071.flac)", "music/ex3/bgm_ex3_field_tem_night.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00072.flac)", "music/ex3/bgm_ex3_event_07.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00073.flac)", "music/ex3/bgm_ex3_dan_d06.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00074.flac)", "music/ex3/bgm_ex3_ban_03.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00075.flac)", "music/ex3/bgm_ex3_ban_04.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00076.flac)", "music/ex3/bgm_ex3_event_01.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00077.flac)", "music/ex3/bgm_ex3_field_kor_night.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00078.flac)", "music/ex3/bgm_ex3_field_kor_day.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00079.flac)", "music/ex3/bgm_ex3_event_12.scd", 0, 0, 1},

		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00080.flac)", "music/ex3/bgm_ex3_town_y_night.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00081.flac)", "music/ex3/bgm_ex3_town_y_day.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00082.flac)", "music/ex3/bgm_ex3_dan_d07.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00083.flac)", "music/ex3/bgm_ex3_dan_d08.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00084.flac)", "music/ex3/bgm_ex3_event_14.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00085.flac)", "music/ex3/bgm_ex3_raid_01.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00086.flac)", "music/ex3/bgm_ex3_raid_02.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00087.flac)", "music/ex3/bgm_ex3_raid_04.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.0 - Shadowbringers\00088.flac)", "music/ex3/bgm_ex3_raid_03.scd", 0, 0, 1},
#pragma endregion

#pragma region Death Unto Dawn
		// Ult Alex musics got different enough endings to be incompatible
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_001.flac)", "music/ex3/.scd", 0, 0, 1},  // Locus (Duality)
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_002.flac)", "music/ex3/bgm_ex3_ban_05.scd", 0, 0, 1},  // Metal - Brute Justice Mode (Journeys)
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_003.flac)", "music/ex3/bgm_ex3_ban_07.scd", 0, 0, 1},  // Rise (Journeys) + Statis Loop (4ch)
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_004.flac)", "music/ex3/bgm_ex3_ban_08.scd", 0, 0, 1},  // Moebius (Orchestral Version)
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_005.flac)", "music/ex3/bgm_pvp_battle_02.scd", 0, 0, 1},  // A Fine Air Forbiddeth (TODO: 6ch)
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_006.flac)", "music/ex3/bgm_pvp_battle_02.scd", 0, 0, 1},  // A Fierce Air Forceth
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_007.flac)", "music/ex3/bgm_ex3_banfort_fairy_good.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_008.flac)", "music/ex3/bgm_ex3_dan_d09.scd", 0, 0, 1},
		{1, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_009.flac)", "music/ex1/bgm_ex1_fate_hukko.scd", 0, 0, 1},

		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_010.flac)", "music/ex3/bgm_season_xmascho_02.scd", 0, 0, 1},  // Starlight de Chocobo (TODO: 4ch)
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_011.flac)", "music/ex3/bgm_season_xmascho_02.scd", 0, 0, 1},  // Starlight de Chocobo (?) (4ch)
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_012.flac)", "music/ex3/bgm_ex3_ytc_01.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_013.flac)", "music/ex3/bgm_ex3_ytc_03.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_014.flac)", "music/ex3/bgm_ex3_ytc_10.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_015.flac)", "music/ex3/bgm_ex3_ytc_06.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_016.flac)", "music/ex3/bgm_ex3_ytc_07.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_017.flac)", "music/ex3/bgm_ex3_ytc_08.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_018.flac)", "music/ex3/bgm_ex3_ytc_09.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_019.flac)", "music/ex3/bgm_ex3_ytc_29.scd", 0, 0, 1},

		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_020.flac)", "music/ex3/bgm_ex3_ytc_05.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_021.flac)", "music/ex3/bgm_ex3_dan_d10.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_022.flac)", "music/ex3/bgm_ex3_boss_battle03.scd", 0, 10, 1},  // Blu-ray version has an ending
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_023.flac)", "music/ex3/bgm_ex3_banfort_kkrn_good.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_024.flac)", "music/ex3/bgm_ex3_ban_09.scd", 0, 7, 1},  // Blu-ray version has an ending
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_025.flac)", "music/ex3/bgm_ex3_ban_10.scd", 0, 0, 1},
		{1, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_026.flac)", "music/ex1/bgm_ex1_hukko_finish01.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_027.flac)", "music/ex3/bgm_ex3_raid_05.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_028.flac)", "music/ex3/bgm_ex3_raid_06.scd", 0, 0, 1},
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_029.flac)", "music/ex3/.scd", 0, 0, 1},  // Footsteps in the Snow (ARR 2.5)

		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_030.flac)", "music/ex3/bgm_ex3_raid_08.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_031.flac)", "music/ex3/bgm_ex3_raid_07.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_032.flac)", "music/ex3/bgm_ex3_event_21.scd", 0, 0, 1},
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_033.flac)", "music/ex3/bgm_ex3_myc_01.scd", 0, 0, 1},  // Wind on the Plains (TODO: 6ch)
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_034.flac)", "music/ex3/bgm_ex3_myc_01.scd", 0, 0, 1},  // Blood on the Wind
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_035.flac)", "music/ex3/bgm_ex3_myc_06.scd", 0, 0, 2},  // One loop contains double
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_036.flac)", "music/ex3/bgm_ex3_myc_02.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_037.flac)", "music/ex3/bgm_ex3_myc_03.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_038.flac)", "music/ex3/bgm_ex3_myc_04.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_039.flac)", "music/ex3/bgm_ex3_myc_05.scd", 0, 0, 2},  // One loop contains double

		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_040.flac)", "music/ex3/bgm_ex3_banfort_dwarf_good.scd", 0, 0, 1},
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_041.flac)", "music/ex3/.scd", 0, 0, 1},  // TODO: The Isle of Endless Summer
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_042.flac)", "music/ex3/bgm_ex3_dan_d11.scd", 0, 0, 1},
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_043.flac)", "music/ex3/bgm_ex3_ban_12.scd", 0, 0, 1},  // TODO: figure these out
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_044.flac)", "music/ex3/bgm_ex3_ban_11.scd", 0, 0, 1},  // Blu-ray version has an ending
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_044.flac)", "music/ex3/bgm_ex3_ban_14.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_045.flac)", "music/ex3/bgm_ex3_event_22.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_046.flac)", "music/ex3/bgm_ex3_ytc_13.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_047.flac)", "music/ex3/bgm_ex3_ytc_14.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_048.flac)", "music/ex3/bgm_ex3_ytc_15.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_049.flac)", "music/ex3/bgm_ex3_ytc_16.scd", 0, 0, 1},

		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_050.flac)", "music/ex3/bgm_ex3_ytc_17.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_051.flac)", "music/ex3/bgm_ex3_ytc_11.scd", 0, 0, 1},
		// {1, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_052.flac)", "music/ex1/bgm_ex1_hukko_concert.scd", 0, 0, 1},  // New Foundation (TODO: 4ch)
		// {1, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_053.flac)", "music/ex1/bgm_ex1_hukko_concert.scd", 0, 0, 1},  // New Foundation (Groundbreaking)
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_054.flac)", "music/ex3/.scd", 0, 0, 1},  // TODO: Gogo's Theme
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_055.flac)", "music/ex3/bgm_ex3_dan_d12.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_056.flac)", "music/ex3/bgm_ex3_con_01.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_057.flac)", "music/ex3/bgm_ex3_raid_09.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_058.flac)", "music/ex3/bgm_ex3_raid_10.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_059.flac)", "music/ex3/bgm_ex3_event_29.scd", 0, 0, 1},

		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_060.flac)", "music/ex3/bgm_ex3_raid_11.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_061.flac)", "music/ex3/bgm_ex3_raid_12.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_062.flac)", "music/ex3/bgm_ex3_event_20.scd", 0, 0, 1},
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_063.flac)", "music/ex3/.scd", 0, 0, 1},  // TODO: Shuffle or Boogie
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_064.flac)", "music/ex3/bgm_ex3_myc_07.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_065.flac)", "music/ex3/bgm_ex3_myc_08.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_066.flac)", "music/ex3/bgm_ex3_myc_09.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_067.flac)", "music/ex3/bgm_ex3_myc_10.scd", 0, 0, 1},
		{1, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_068.flac)", "music/ex1/bgm_ex1_hukko_fes.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_069.flac)", "music/ex3/bgm_ex3_ytc_25.scd", 0, 0, 1},

		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_070.flac)", "music/ex3/bgm_ex3_ytc_26.scd", 0, 0, 1},  // TODO: figure these out
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_071.flac)", "music/ex3/bgm_ex3_ytc_27.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_072.flac)", "music/ex3/bgm_ex3_ytc_19.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_073.flac)", "music/ex3/bgm_ex3_ytc_18.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_074.flac)", "music/ex3/bgm_ex3_ytc_21.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_075.flac)", "music/ex3/bgm_ex3_ytc_22.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_076.flac)", "music/ex3/bgm_ex3_ytc_28.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_077.flac)", "music/ex3/bgm_ex3_ytc_23.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_078.flac)", "music/ex3/bgm_ex3_ytc_24.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_079.flac)", "music/ex3/bgm_ex3_ytc_12.scd", 0, 0, 1},
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_079.flac)", "music/ex3/bgm_ex3_ytc_02.scd", 0, 0, 1},  // lower volume version?

		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_080.flac)", "music/ex3/bgm_ex3_dan_d13.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_081.flac)", "music/ex3/bgm_ex3_field_welrit.scd", 0, 0, 1},
		{3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_082.flac)", "music/ex3/bgm_ex3_ban_15.scd", 0, 0, 1},
		// {3, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_083.flac)", "music/ex3/bgm_ex3_event_26.scd", 0, 0, 1},
		{0, LR"(D:\OneDrive\Musics\Sorted by OSTs\Final Fantasy XIV\Final Fantasy 14 - 5.5 - Death Unto Dawn\DUD_084.flac)", "music/ffxiv/bgm_ride_fanfes2021.scd", 0, 0, 1},
#pragma endregion
	}) {
		std::filesystem::path targetPath;
		if (ex == 0)
			targetPath = std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\ffxiv\0c0000\{0})", scdPath);
		else
			targetPath = std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\ex{0}\0c0{0}00\{1})", ex, scdPath);
		if (exists(targetPath))
			continue;

		tp.SubmitWork([targetPath, &readers, ex, srcPath, scdPath, findFromBlockIndex, loopDelta, divider] {
			std::cout << std::format("Working on {}...\n", scdPath);
			try {
				const auto v2 = Convert(readers[ex], srcPath, scdPath, findFromBlockIndex, loopDelta, divider);
				Utils::Win32::File::Create(targetPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0).Write(0, std::span(v2));
			} catch (const std::exception& e) {
				std::cout << std::format("Error on {}: {}\n", scdPath, e.what());
			}
		});
	}
	tp.WaitOutstanding();
	return 0;
}
