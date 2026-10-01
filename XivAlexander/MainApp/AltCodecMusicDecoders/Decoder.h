#pragma once

#include <algorithm>
#include <span>

#include <FLAC/format.h>

#include "Game/AsiStream.h"

namespace XivAlexander::Apps::MainApp {
	using Game::AsiFetchCallback;
	using Game::AsiStream;
	using Game::AsiStreamAttributeFn;
	using Game::AsiStreamFlag;
	using Game::AsiStreamOpenFn;
	using Game::AsiStreamProcessFn;
	using Game::AsiStreamResetFn;
	using Game::AsiStreamSetUpDecoderFn;
	using Game::AsiStreamUserFfxiv;
	using Game::NoFetchOffset;

	constexpr auto MaxChannelCount = 8;
	constexpr auto TargetBitDepthInBytes = 2;

	constexpr auto MaxDecodedFrameLength = std::max<size_t>({
		1, // wav/pcm: 1 "frame" = 1 sample
		FLAC__MAX_BLOCK_SIZE
	}) * MaxChannelCount * TargetBitDepthInBytes;

	inline const uint8_t* VorbisChannelOrderFromPcm(uint32_t channels) {
		static constexpr uint8_t ch2_1[] = {0, 2, 1};
		static constexpr uint8_t ch4_1[] = {0, 2, 1, 3, 4};
		static constexpr uint8_t ch5_1[] = {0, 2, 1, 4, 5, 3};
		static constexpr uint8_t ch6_1[] = {0, 2, 1, 5, 6, 4, 3};
		static constexpr uint8_t ch7_1[] = {0, 2, 1, 6, 7, 4, 5, 3};
		switch (channels) {
			case 3: return ch2_1;
			case 5: return ch4_1;
			case 6: return ch5_1;
			case 7: return ch6_1;
			case 8: return ch7_1;
			default: return nullptr;
		}
	}

	class Decoder {
		uint32_t m_channelCount = 2;
		uint32_t m_bitDepth = 16;
		uint32_t m_samplingRate = 44100;
		bool m_headerParsed = false;

	public:
		virtual ~Decoder() = default;

		[[nodiscard]] virtual const char* Name() const = 0;

		// matches AsiStreamBlahFn
		virtual uint32_t Process(AsiStream& stream, void* buffer, uint32_t bufferSize) = 0;
		virtual uint32_t Attribute(AsiStream& stream, int attrib) = 0;
		virtual void Reset(AsiStream& stream) = 0;

		void ParseHeader(std::span<const uint8_t> peek) {
			if (m_headerParsed)
				return;
			m_headerParsed = ParseHeaderInternal(peek);
		}

		[[nodiscard]] uint32_t ChannelCount() const { return m_channelCount; }
		[[nodiscard]] uint32_t BitDepth() const { return m_bitDepth; }
		[[nodiscard]] uint32_t SamplingRate() const { return m_samplingRate; }

	protected:
		[[nodiscard]] virtual bool ParseHeaderInternal(std::span<const uint8_t> peek) = 0;

		void SetFormat(uint32_t channelCount, uint32_t bitDepth, uint32_t samplingRate) {
			m_channelCount = channelCount;
			m_bitDepth = bitDepth;
			m_samplingRate = samplingRate;
		}
	};
}
