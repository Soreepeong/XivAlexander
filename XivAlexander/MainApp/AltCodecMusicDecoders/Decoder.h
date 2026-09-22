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
