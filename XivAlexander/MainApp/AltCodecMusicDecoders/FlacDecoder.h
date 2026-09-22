#pragma once

#include <bit>

#include <FLAC++/decoder.h>

#include "BufferedDecoder.h"

namespace XivAlexander::Apps::MainApp {
	constexpr auto MaxFlacFrameLength =
		/* max frame header length */ size_t{16} +
		/* samples */ FLAC__MAX_CHANNELS * (1 + FLAC__MAX_BLOCK_SIZE * FLAC__MAX_BITS_PER_SAMPLE / 8) +
		/* fixed frame footer length */ 2;

	constexpr auto FlacQueueLength = std::bit_ceil(MaxFlacFrameLength);

	class FlacDecoder final : public BufferedDecoder<FlacQueueLength>, FLAC::Decoder::Stream {
		static_assert(MaxChannelCount <= FLAC__MAX_CHANNELS);
		static_assert(TargetBitDepthInBytes <= FLAC__MAX_BITS_PER_SAMPLE);

		// names modeled from FLAC__STREAM_DECODER_...
		enum class Progress : uint8_t {
			AwaitingPayload,
			SearchForFrameSync,
			ReadFrame,
			Unrecoverable,
		};

		enum : uint8_t {
			MetadataLastFlag = 0x80,
			MetadataTypeMask = 0x7F,
		};

		struct {
			size_t Length = 0;
			size_t StreamInfoOffset = 0;
			bool Complete = false;
		} m_metadata;

		Progress m_progress = Progress::AwaitingPayload;
		uint32_t m_sourceBits = 16;

		Utils::RingBuffer<uint8_t, std::bit_ceil(MaxDecodedFrameLength)> m_decodeBuffer;
		// updates during libFLAC callbacks
		size_t m_queueOffset = 0;
		size_t m_streamOffset = 0;
		// values to assign to the above after finishing reading the current frame (libFLAC callbacks are complete)
		size_t m_queueOffsetNextFrame = 0;
		size_t m_streamOffsetNextFrame = 0;
		size_t m_garbageLength = 0;

	public:
		static bool IsFlac(std::span<const uint8_t> peek);

		FlacDecoder();

		[[nodiscard]] const char* Name() const override;

	protected:
		[[nodiscard]] bool Finished() const override;
		[[nodiscard]] bool ParseHeaderInternal(std::span<const uint8_t> peek) override;
		std::pair<uint32_t, uint32_t> Decode(std::span<const uint8_t>, std::span<uint8_t> out) override;
		void ResetBufferInternal() override;

		FLAC__StreamDecoderReadStatus read_callback(FLAC__byte buffer[], size_t* bytes) override;
		FLAC__StreamDecoderWriteStatus write_callback(const FLAC__Frame* frame, const FLAC__int32* const buffer[]) override;
		FLAC__StreamDecoderTellStatus tell_callback(FLAC__uint64* absolute_byte_offset) override;
		void error_callback(FLAC__StreamDecoderErrorStatus) override;
	};
}
