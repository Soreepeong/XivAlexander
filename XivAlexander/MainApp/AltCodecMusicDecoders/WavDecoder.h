#pragma once

#include <array>

#include "BufferedDecoder.h"

namespace XivAlexander::Apps::MainApp {
	class WavDecoder final : public BufferedDecoder<262144> {
		uint32_t m_sourceBits = 16;
		std::array<uint8_t, 4> m_partial{};
		uint32_t m_partialSize = 0;
		size_t m_payloadLength = 0;
		size_t m_payloadConsumed = 0;

	public:
		static bool IsWav(std::span<const uint8_t> peek);

		[[nodiscard]] const char* Name() const override;

	protected:
		[[nodiscard]] bool ParseHeaderInternal(std::span<const uint8_t> peek) override;

		[[nodiscard]] bool Finished() const override;

		void ResetBufferInternal() override;

		std::pair<uint32_t, uint32_t> Decode(std::span<const uint8_t> data, std::span<uint8_t> out) override;

	private:
		std::pair<uint32_t, uint32_t> DecodeSamples(std::span<const uint8_t> data, std::span<uint8_t> out);
	};

	
}
