#include "pch.h"

#include <mmreg.h>
#include <XivAlexanderCommon/Sqex_Sound_Reader.h>
#include <XivAlexanderCommon/Sqex_Sound_Writer.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>

auto DecodeOgg(std::span<const uint8_t> bytes) {
	ogg_sync_state oy;

	ogg_sync_init(&oy);
	auto buffer = ogg_sync_buffer(&oy, bytes.size());
	memcpy(buffer, &bytes[0], bytes.size());
	ogg_sync_wrote(&oy, bytes.size());

	vorbis_info vi;
	vorbis_comment vc;
	vorbis_info_init(&vi);
	vorbis_comment_init(&vc);

	ogg_stream_state os{};
	ogg_page og;
	ogg_packet op;
	// 1. initial header
	for (auto i = 0; i < 3; i++) {
		ogg_sync_pageout(&oy, &og);
		if (!os.body_data)
			ogg_stream_init(&os, ogg_page_serialno(&og));
		ogg_stream_pagein(&os, &og);
		if (0 == ogg_stream_packetout(&os, &op))
			break;
		vorbis_synthesis_headerin(&vi, &vc, &op);
	}
	uint64_t lstart = 0, lend = 0;
	{
		char** ptr = vc.user_comments;
		while (*ptr) {
			if (strncmp(*ptr, "LoopStart=", 10) == 0)
				lstart = std::strtoull(*ptr + 10, nullptr, 10);
			else if (strncmp(*ptr, "LoopEnd=", 8) == 0)
				lend = std::strtoull(*ptr + 8, nullptr, 10);
			fprintf(stderr, "%s\n", *ptr);
			++ptr;
		}
		fprintf(stderr, "\nBitstream is %d channel, %ldHz\n", vi.channels, vi.rate);
		fprintf(stderr, "Encoded by: %s\n\n", vc.vendor);
	}

	std::vector<float> pcmStream;
	vorbis_dsp_state vd;
	vorbis_block vb;
	vorbis_synthesis_init(&vd, &vi);
	vorbis_block_init(&vd, &vb);
	while (!ogg_page_eos(&og)) {
		while (ogg_sync_pageout(&oy, &og) > 0) {
			ogg_stream_pagein(&os, &og);
			while (ogg_stream_packetout(&os, &op) > 0) {
				float** pcm;
				int samples;

				vorbis_synthesis(&vb, &op);
				vorbis_synthesis_blockin(&vd, &vb);
				while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
					for (size_t i = 0; i < samples; ++i) {
						for (size_t j = 0; j < vi.channels; ++j) {
							pcmStream.push_back(pcm[j][i]);
						}
					}
					vorbis_synthesis_read(&vd, samples);
				}
			}
		}
	}

	auto res = std::make_tuple(pcmStream, vi.channels, vi.rate, lstart, lend);
	vorbis_block_clear(&vb);
	vorbis_dsp_clear(&vd);
	ogg_stream_clear(&os);
	vorbis_comment_clear(&vc);
	vorbis_info_clear(&vi);
	ogg_sync_clear(&oy);

	return res;
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
		auto buffer = vorbis_analysis_buffer(&vd, readSampleBlocks);
		for (size_t ptr = sampleBlockIndex * channels, i = 0; i < readSampleBlocks; ++sampleBlockIndex, ++i) {
			for (size_t channelIndex = 0; channelIndex < channels; ++channelIndex, ++ptr) {
				buffer[channelIndex][i] = samples[ptr];
			}
		}
		vorbis_analysis_wrote(&vd, readSamples / channels);
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

int main() {
	// const auto wavf = LR"(Z:\00037.wav)";
	// const auto scdf = "music/ex3/bgm_ex3_system_title.scd";

	const auto wavf = LR"(Z:\00050.wav)";
	const auto scdf = "music/ex3/bgm_ex3_mount01.scd";

	const Sqex::Sqpack::Reader readerC(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ex3\0c0300.win32.index)");
	const auto rc = Sqex::Sound::ScdReader(readerC[scdf]);
	const auto entry = rc.GetSoundEntry(0);

	const auto seekHeader = entry.GetOggSeekTableHeader();
	const auto seekTable = entry.GetOggSeekTable();

	auto bytes = entry.GetOggFile();
	auto [stream2, channel2, rate2, start2, end2] = DecodeOgg(bytes);

	const auto wav = Utils::Win32::File::Create(wavf, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0).Read<char>(0, 512 * 1048576, Utils::Win32::File::PartialIoMode::AllowPartial);
	std::vector<uint8_t> wfexBuf;
	WAVEFORMATEXTENSIBLE* pwf = nullptr;
	std::vector<float> stream1;
	for (size_t ptr = 0; ptr < wav.size(); ) {
		std::string_view type(&wav[ptr], 4);
		uint32_t len = *reinterpret_cast<const uint32_t*>(&wav[ptr + 4]);
		if (type == "fmt ") {
			wfexBuf.resize(len);
			memcpy(&wfexBuf[0], &wav[ptr + 8], len);
			pwf = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(&wfexBuf[0]);
			if (pwf->SubFormat != KSDATAFORMAT_SUBTYPE_PCM)
				std::abort();
		}
		else if (type == "data") {
			std::span data(reinterpret_cast<const uint8_t*>(&wav[ptr + 8]), len);
			stream1.reserve(data.size() / 3);
			for (size_t i = 0; i < data.size(); i += 3) {
				const auto v1 = static_cast<int>((data[i + 2] << 24) | (data[i + 1] << 16) | (data[i] << 8)) >> 8;
				stream1.push_back(v1 / static_cast<float>(0x800000));
			}
		}
		if (type == "RIFF")
			ptr += 12;
		else
			ptr += 8 + len;
	}
	if (!pwf)
		return -1;

	const auto start1 = static_cast<uint32_t>(1ULL * start2 * pwf->Format.nSamplesPerSec / rate2);
	const auto end1 = static_cast<uint32_t>(1ULL * end2 * pwf->Format.nSamplesPerSec / rate2);

	size_t i1 = 0, i2 = 0;
	for (; i1 < stream1.size(); i1 += 2) {
		if (stream1[i1] >= 0.01)
			break;
	}
	for (; i2 < stream2.size(); i2 += 2) {
		if (stream2[i2] >= 0.01)
			break;
	}
	const auto begin = static_cast<uint32_t>(static_cast<uint64_t>(i1) / 2 - 1ULL * pwf->Format.nSamplesPerSec * i2 / 2 / rate2);

	const auto ogg = ToOgg(std::span(stream1).subspan(begin * 2), pwf->Format.nChannels, pwf->Format.nSamplesPerSec, start1, end1);

	auto e = Sqex::Sound::ScdWriter::SoundEntry::FromOgg(Sqex::MemoryRandomAccessStream(ogg));

	Sqex::Sound::ScdWriter writer;
	writer.SetTable1(rc.ReadTable1Entries());
	writer.SetTable4(rc.ReadTable4Entries());
	writer.SetTable2(rc.ReadTable2Entries());
	writer.SetSoundEntry(0, e);

	const auto v2 = writer.Export();
	Utils::Win32::File::Create(std::format(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\ex3\0c0300\{})", scdf),
		GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0
	).Write(0, std::span(v2));
	return 0;
}
