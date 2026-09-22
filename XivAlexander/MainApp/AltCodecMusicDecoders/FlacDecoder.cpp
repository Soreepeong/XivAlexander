#include "pch.h"

#include <array>

#include "FlacDecoder.h"

bool XivAlexander::Apps::MainApp::FlacDecoder::IsFlac(std::span<const uint8_t> peek) {
	return peek.size() >= FLAC__STREAM_SYNC_LENGTH
		&& std::memcmp(peek.data(), FLAC__STREAM_SYNC_STRING, FLAC__STREAM_SYNC_LENGTH) == 0;
}

XivAlexander::Apps::MainApp::FlacDecoder::FlacDecoder() {
	try {
		if (!Stream::is_valid())
			throw std::runtime_error("decoder allocation failed");
		Stream::set_md5_checking(false);

		if (const auto status = Stream::init(); status != FLAC__STREAM_DECODER_INIT_STATUS_OK)
			throw std::runtime_error(FLAC__StreamDecoderInitStatusString[status]);
	} catch (const std::exception& e) {
		m_progress = Progress::Unrecoverable;
		Misc::Logger::Acquire()->Format<LogLevel::Error>(LogCategory::AltCodecMusic, "FLAC decoder unavailable ({})", e.what());
	}
}

const char* XivAlexander::Apps::MainApp::FlacDecoder::Name() const { return "FLAC"; }

bool XivAlexander::Apps::MainApp::FlacDecoder::Finished() const { return m_progress == Progress::Unrecoverable; }

bool XivAlexander::Apps::MainApp::FlacDecoder::ParseHeaderInternal(std::span<const uint8_t> peek) {
	if (!IsFlac(peek))
		return false;

	auto found = false;
	for (auto offset = size_t{FLAC__STREAM_SYNC_LENGTH};
		offset + FLAC__STREAM_METADATA_HEADER_LENGTH <= peek.size();) {
		const auto last = (peek[offset] & MetadataLastFlag) != 0;
		const auto type = peek[offset] & MetadataTypeMask;
		const auto length = (static_cast<size_t>(peek[offset + 1]) << 16)
			| (static_cast<size_t>(peek[offset + 2]) << 8) | peek[offset + 3];
		const auto blockEnd = offset + FLAC__STREAM_METADATA_HEADER_LENGTH + length;

		if (type == FLAC__METADATA_TYPE_STREAMINFO && length == FLAC__STREAM_METADATA_STREAMINFO_LENGTH) {
			if (blockEnd > peek.size())
				return false;

			const auto* streamInfo = peek.data() + offset + FLAC__STREAM_METADATA_HEADER_LENGTH;
			const auto rate = 0u
				| (static_cast<uint32_t>(streamInfo[10]) << 12)
				| (static_cast<uint32_t>(streamInfo[11]) << 4)
				| (streamInfo[12] >> 4);
			const auto channels = static_cast<uint32_t>(((streamInfo[12] >> 1) & 0x07) + 1);
			const auto bits = 0u
				| static_cast<uint32_t>((((streamInfo[12] & 0x01) << 4)
					| (streamInfo[13] >> 4)) + 1);
			if (rate == 0
				|| channels > FLAC__MAX_CHANNELS
				|| bits < FLAC__MIN_BITS_PER_SAMPLE
				|| bits > FLAC__MAX_BITS_PER_SAMPLE)
				return false;

			m_sourceBits = bits;
			m_metadata.StreamInfoOffset = offset;
			SetFormat(channels, TargetBitDepthInBytes * 8, rate);
			found = true;
		} else if (blockEnd > peek.size()) {
			break;
		}

		if (last) {
			m_metadata.Complete = blockEnd <= peek.size();
			break;
		}

		offset = blockEnd;
	}

	if (!found || !QueueHeader(peek))
		return false;

	m_metadata.Length = peek.size();
	return true;
}

std::pair<uint32_t, uint32_t> XivAlexander::Apps::MainApp::FlacDecoder::Decode(std::span<const uint8_t>, std::span<uint8_t> out) {
	const auto wanted = out.size();

	if (m_progress == Progress::AwaitingPayload) {
		std::array<uint8_t, 16> probe{};
		if (!Queued().ReadExactlyAt(m_metadata.Length, probe))
			return {0, 0};

		m_progress = Progress::SearchForFrameSync;
		Queued().Compact();
		auto metadataSpan = Queued().Buffer().subspan(0, m_metadata.Length);
		auto it = std::ranges::search(metadataSpan, probe);
		if (!it.empty() && !std::ranges::search(std::ranges::subrange(std::ranges::next(it.begin()), metadataSpan.end()), probe).empty())
			it = {}; // give up if it isn't unique

		if (!it.empty()) {
			const auto offset = std::distance(metadataSpan.begin(), it.begin());
			m_metadata.Length -= Queued().Splice(offset, m_metadata.Length - offset);
		} else if (!m_metadata.Complete) {
			// only leave STREAMINFO and make it the last metadata block
			constexpr auto StreamInfoEnd = 0
				+ FLAC__STREAM_SYNC_LENGTH
				+ FLAC__STREAM_METADATA_HEADER_LENGTH
				+ FLAC__STREAM_METADATA_STREAMINFO_LENGTH;

			m_metadata.Length -= Queued().Splice(FLAC__STREAM_SYNC_LENGTH, m_metadata.StreamInfoOffset - FLAC__STREAM_SYNC_LENGTH);
			m_metadata.Length -= Queued().Splice(StreamInfoEnd, m_metadata.Length - StreamInfoEnd);
			Queued()[FLAC__STREAM_SYNC_LENGTH] |= MetadataLastFlag;
		}
	}

	if (m_progress != Progress::Unrecoverable) {
		do {
			if (const auto head = m_decodeBuffer.Head(); head.size()) {
				if (const auto taken = std::min(out.size(), head.size())) {
					std::memcpy(out.data(), head.data(), taken);
					out = out.subspan(taken);
					m_decodeBuffer.Consume(taken);
				}
			}

			if (out.empty())
				break;
			if (m_decodeBuffer.Readable() != 0)
				continue;

			if (get_state() == FLAC__STREAM_DECODER_END_OF_STREAM) {
				Stream::flush();
				m_queueOffset = m_queueOffsetNextFrame;
				m_streamOffset = m_streamOffsetNextFrame;
			}

			constexpr auto MaxFramesPerCall = 64;
			for (int i = 0; i < MaxFramesPerCall; i++) {
				Stream::process_single();
				if (m_decodeBuffer.Readable() != 0 || get_state() == FLAC__STREAM_DECODER_END_OF_STREAM)
					break;
			}
		} while (m_decodeBuffer.Readable());
	}

	const auto drop = std::min(m_queueOffsetNextFrame, m_queueOffset);
	m_queueOffset -= drop;
	m_queueOffsetNextFrame -= std::min(m_queueOffsetNextFrame, drop);
	auto consumed = static_cast<uint32_t>(drop);

	if (m_progress != Progress::Unrecoverable && wanted == out.size() && Queued().Writable() == 0) {
		const auto queued = Queued().Readable();
		m_garbageLength += queued;
		Stream::flush();
		m_streamOffset = m_streamOffsetNextFrame = m_streamOffset + (queued - m_queueOffset); // assume drained
		m_queueOffset = 0;
		m_queueOffsetNextFrame = 0;
		consumed += static_cast<uint32_t>(queued);

		if (m_garbageLength > MaxFlacFrameLength) {
			m_progress = Progress::Unrecoverable;
			Report("no frame in {} bytes; giving up on the stream (state {})",
				m_garbageLength, get_state().as_cstring());
		}
	}

	return {consumed, static_cast<uint32_t>(wanted - out.size())};
}

void XivAlexander::Apps::MainApp::FlacDecoder::ResetBufferInternal() {
	if (m_progress == Progress::AwaitingPayload)
		return;

	Stream::flush();
	m_queueOffset = 0;
	m_decodeBuffer.Clear();
	m_streamOffset = 0;
	m_queueOffsetNextFrame = 0;
	m_streamOffsetNextFrame = 0;
	m_garbageLength = 0;
}

FLAC__StreamDecoderReadStatus XivAlexander::Apps::MainApp::FlacDecoder::read_callback(FLAC__byte buffer[], size_t* bytes) {
	const auto available = Queued().Readable() - m_queueOffset;
	if (available == 0) {
		*bytes = 0;
		return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
	}

	const auto read = std::min<size_t>(*bytes, available);
	(void)Queued().ReadExactlyAt(m_queueOffset, {buffer, read});
	m_queueOffset += read;
	m_streamOffset += read;
	*bytes = read;
	return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderWriteStatus XivAlexander::Apps::MainApp::FlacDecoder::write_callback(const FLAC__Frame* frame, const FLAC__int32* const buffer[]) {
	const auto channels = frame->header.channels;
	if (channels != ChannelCount())
		return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;

	const auto bits = frame->header.bits_per_sample ? frame->header.bits_per_sample : m_sourceBits;
	const auto samples = frame->header.blocksize;
	const auto frameBytes = static_cast<size_t>(samples) * channels * 2;

	if (m_decodeBuffer.Readable() == 0)
		m_decodeBuffer.Clear();

	const auto out = m_decodeBuffer.Tail();
	if (out.size() < frameBytes)
		return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;

	auto* const dst = out.data();
	const auto interleave = [&](auto&& narrow) {
		size_t pos = 0;
		for (uint32_t i = 0; i < samples; i++) {
			for (uint32_t channel = 0; channel < channels; channel++, pos += sizeof(int16_t)) {
				const auto value = narrow(buffer[channel][i]);
				std::memcpy(dst + pos, &value, sizeof value);
			}
		}
	};

	if (bits == 16)
		interleave([](FLAC__int32 sample) { return static_cast<int16_t>(sample); });
	else if (bits > 16)
		interleave([shift = bits - 16](FLAC__int32 sample) { return static_cast<int16_t>(sample >> shift); });
	else
		interleave([shift = 16 - bits](FLAC__int32 sample) { return static_cast<int16_t>(sample << shift); });

	m_decodeBuffer.Commit(frameBytes);

	if (m_progress == Progress::SearchForFrameSync)
		m_progress = Progress::ReadFrame;
	m_garbageLength = 0;
	FLAC__uint64 position{};
	if (Stream::get_decode_position(&position) && position <= m_streamOffset && m_streamOffset - position <= m_queueOffset) {
		const auto libFlacDelta = m_streamOffset - position;
		m_queueOffsetNextFrame = m_queueOffset - libFlacDelta;
		m_streamOffsetNextFrame = position;
	} else {
		// keep the last boundary that could be mapped
	}
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

FLAC__StreamDecoderTellStatus XivAlexander::Apps::MainApp::FlacDecoder::tell_callback(FLAC__uint64* absolute_byte_offset) {
	*absolute_byte_offset = m_streamOffset;
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

void XivAlexander::Apps::MainApp::FlacDecoder::error_callback(FLAC__StreamDecoderErrorStatus) {
	// pass
}
