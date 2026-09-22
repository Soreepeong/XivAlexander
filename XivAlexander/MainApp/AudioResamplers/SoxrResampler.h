#pragma once

#include <string>

#include <soxr.h>

#include "Config/AudioResamplerConfigs.h"

#include "Resampler.h"

namespace XivAlexander::Apps::MainApp::AudioResamplers {
	class SoxrResampler final : public Resampler {
		soxr_t m_soxr{};
		size_t m_channels;
		size_t m_inFrameBytes;
		double m_ratio;
		bool m_drained = false;

	public:
		SoxrResampler(uint32_t inRate, uint32_t outRate, uint32_t channels, SampleFormat format, const SoxrResamplerConfig& config, std::string& error);
		SoxrResampler(const SoxrResampler&) = delete;
		SoxrResampler& operator=(const SoxrResampler&) = delete;
		~SoxrResampler() override;

		[[nodiscard]] const char* Name() const override;

		bool Process(const void* in, size_t frames, std::vector<float>& out) override;
		bool Drain(std::vector<float>& out) override;
		void Reset() override;

		[[nodiscard]] double HeldBack() const override;

	private:
		bool Run(const uint8_t* in, size_t frames, std::vector<float>& out);
	};
}
