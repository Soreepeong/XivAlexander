#pragma once

#include <string>
#include <vector>

#include <audio-resampler/resampler.h>

#include "Config/AudioResamplerConfigs.h"

#include "Resampler.h"

namespace XivAlexander::Apps::MainApp::AudioResamplers {
	class ArtResampler final : public Resampler {
		int m_taps;
		Resample* m_cxt{};
		size_t m_channels;
		SampleFormat m_format;
		double m_ratio;
		std::vector<float> m_input;
		uint64_t m_inputFrames = 0;
		uint64_t m_outputFrames = 0;

	public:
		ArtResampler(uint32_t inRate, uint32_t outRate, uint32_t channels, SampleFormat format, const ArtResamplerConfig& config, std::string& error);
		ArtResampler(const ArtResampler&) = delete;
		ArtResampler& operator=(const ArtResampler&) = delete;
		~ArtResampler() override;

		[[nodiscard]] const char* Name() const override;

		bool Process(const void* in, size_t frames, std::vector<float>& out) override;
		bool Drain(std::vector<float>& out) override;
		void Reset() override;

		[[nodiscard]] double HeldBack() const override;

	private:
		void Start();
		bool Run(const float* in, int frames, std::vector<float>& out);
	};
}
