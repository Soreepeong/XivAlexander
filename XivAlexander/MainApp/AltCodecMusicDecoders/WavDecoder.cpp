#include "pch.h"

#include <mmreg.h>

#include "WavDecoder.h"

bool XivAlexander::Apps::MainApp::WavDecoder::IsWav(std::span<const uint8_t> peek) {
	return peek.size() >= 12
		&& std::memcmp(peek.data(), "RIFF", 4) == 0
		&& std::memcmp(peek.data() + 8, "WAVE", 4) == 0;
}

const char* XivAlexander::Apps::MainApp::WavDecoder::Name() const {
	return "WAV";
}

bool XivAlexander::Apps::MainApp::WavDecoder::ParseHeaderInternal(std::span<const uint8_t> peek) {
	if (!IsWav(peek))
		return false;

	size_t offset = 12;
	while (offset + 8 <= peek.size()) {
		uint32_t chunkSize{};
		std::memcpy(&chunkSize, peek.data() + offset + 4, sizeof chunkSize);
		if (std::memcmp(peek.data() + offset, "fmt ", 4) == 0) {
			if (offset + 8 + sizeof(WAVEFORMATEX) > peek.size())
				return false;
			WAVEFORMATEX wf{};
			std::memcpy(&wf, peek.data() + offset + 8, sizeof wf);
			if ((wf.wFormatTag != WAVE_FORMAT_PCM && wf.wFormatTag != WAVE_FORMAT_EXTENSIBLE)
				|| wf.nChannels == 0
				|| wf.nChannels > 8
				|| (wf.wBitsPerSample != 8 && wf.wBitsPerSample != 16
					&& wf.wBitsPerSample != 24 && wf.wBitsPerSample != 32))
				return false;

			if (wf.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
				const auto subFormat = offset + 8 + sizeof(WAVEFORMATEX) + 6;
				if (wf.cbSize < 22 || subFormat + sizeof(uint16_t) > peek.size())
					return false;
				uint16_t subTag{};
				std::memcpy(&subTag, peek.data() + subFormat, sizeof subTag);
				if (subTag != WAVE_FORMAT_PCM)
					return false;
			}

			m_sourceBits = wf.wBitsPerSample;
			SetFormat(wf.nChannels, TargetBitDepthInBytes * 8, wf.nSamplesPerSec);
		} else if (std::memcmp(peek.data() + offset, "data", 4) == 0) {
			m_payloadLength = chunkSize;
			return true;
		}

		if (chunkSize > peek.size())
			return false;

		offset += 8 + chunkSize + (chunkSize & 1);
	}
	return false;
}

bool XivAlexander::Apps::MainApp::WavDecoder::Finished() const {
	return m_payloadLength != 0 && m_payloadConsumed >= m_payloadLength;
}

void XivAlexander::Apps::MainApp::WavDecoder::ResetBufferInternal() {
	m_partialSize = 0;
	m_payloadConsumed = 0;
}

std::pair<uint32_t, uint32_t> XivAlexander::Apps::MainApp::WavDecoder::Decode(std::span<const uint8_t> data, std::span<uint8_t> out) {
	if (m_payloadLength != 0)
		data = data.subspan(0, std::min(data.size(), m_payloadLength - m_payloadConsumed));
	const auto result = DecodeSamples(data, out);
	m_payloadConsumed += result.first;
	return result;
}

std::pair<uint32_t, uint32_t> XivAlexander::Apps::MainApp::WavDecoder::DecodeSamples(std::span<const uint8_t> data, std::span<uint8_t> out) {
	if (m_sourceBits == 16) {
		const auto len = std::min(data.size(), out.size());
		std::memcpy(out.data(), data.data(), len);
		return {static_cast<uint32_t>(len), static_cast<uint32_t>(len)};
	}

	const auto sourceBytes = m_sourceBits / 8;
	uint32_t consumed = 0;
	uint32_t written = 0;

	const auto convert = [&](auto&& narrow) {
		if (m_partialSize && m_partialSize < sourceBytes) {
			const auto take = std::min<size_t>(sourceBytes - m_partialSize, data.size());
			std::memcpy(m_partial.data() + m_partialSize, data.data(), take);
			m_partialSize += static_cast<uint32_t>(take);
			consumed += static_cast<uint32_t>(take);
			data = data.subspan(take);
		}

		if (m_partialSize == sourceBytes && out.size() >= sizeof(int16_t)) {
			const auto value = narrow(m_partial.data());
			std::memcpy(out.data(), &value, sizeof value);
			written += sizeof value;
			m_partialSize = 0;
		}

		const auto count = std::min(data.size() / sourceBytes, (out.size() - written) / sizeof(int16_t));
		for (size_t i = 0; i < count; i++) {
			const auto value = narrow(data.data() + i * sourceBytes);
			std::memcpy(out.data() + written + i * sizeof value, &value, sizeof value);
		}
		consumed += static_cast<uint32_t>(count * sourceBytes);
		written += static_cast<uint32_t>(count * sizeof(int16_t));
		data = data.subspan(count * sourceBytes);

		if (m_partialSize == 0 && !data.empty() && data.size() < sourceBytes) {
			std::memcpy(m_partial.data(), data.data(), data.size());
			m_partialSize = static_cast<uint32_t>(data.size());
			consumed += static_cast<uint32_t>(data.size());
		}
	};

	if (m_sourceBits == 8) {
		convert([](const uint8_t* src) { return static_cast<int16_t>((static_cast<int>(*src) - 128) << 8); });
	} else {
		convert([n = sourceBytes](const uint8_t* src) {
			return static_cast<int16_t>(static_cast<uint16_t>(src[n - 2] | (src[n - 1] << 8)));
		});
	}

	return {consumed, written};
}
