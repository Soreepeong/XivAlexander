#pragma once

#include <memory>
#include <vector>

#include "Config/AudioResamplerConfigs.h"

#include "Resampler.h"

namespace r8b {
	class CDSPResampler;
}

namespace XivAlexander::Apps::MainApp::AudioResamplers {
	class R8brainResampler final : public Resampler {
		int m_blockFrames;
		std::vector<std::unique_ptr<r8b::CDSPResampler>> m_channels;
		SampleFormat m_format;
		uint32_t m_inRate;
		uint32_t m_outRate;
		std::vector<double> m_block;
		std::vector<double*> m_outputs;
		uint64_t m_inputFrames = 0;
		uint64_t m_outputFrames = 0;

	public:
		R8brainResampler(uint32_t inRate, uint32_t outRate, uint32_t channels, SampleFormat format, const R8brainResamplerConfig& config);
		~R8brainResampler() override;

		[[nodiscard]] const char* Name() const override;

		bool Process(const void* in, size_t frames, std::vector<float>& out) override;
		bool Drain(std::vector<float>& out) override;
		void Reset() override;

		[[nodiscard]] double HeldBack() const override;

	private:
		void Feed(const void* in, size_t at, int length, std::vector<float>& out, size_t limit);
	};
}
