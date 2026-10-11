#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <soxr.h>

#include "Config/AudioResamplerConfigs.h"

namespace XivAlexander::Apps::MainApp::AudioResamplers {
	enum class SampleFormat : uint8_t {
		Int16,
		Float,
	};

	class SoxrResampler {
		soxr_t m_soxr{};
		size_t m_frameBytes;
		double m_ratio;
		bool m_drained = false;

	public:
		SoxrResampler(uint32_t inRate, uint32_t outRate, uint32_t channels, SampleFormat format, const SoxrResamplerConfig& config, std::string& error);
		SoxrResampler(const SoxrResampler&) = delete;
		SoxrResampler& operator=(const SoxrResampler&) = delete;
		~SoxrResampler();

		bool Process(const void* in, size_t frames, std::vector<uint8_t>& out);
		bool Drain(std::vector<uint8_t>& out);
		void Reset();

	private:
		bool Run(const uint8_t* in, size_t frames, std::vector<uint8_t>& out);
	};
}
