#pragma once

#include <memory>
#include <vector>

#include "Config/AudioResamplerConfigs.h"

#include "Resampler.h"

namespace XivAlexander::Apps::MainApp::AudioResamplers {
	class WindowedSincResampler final : public Resampler {
		struct Table {
			int HalfTaps{};
			int Taps{};
			int Phases{};
			std::vector<float> Weights;
		};

		uint32_t m_inRate;
		uint32_t m_outRate;
		size_t m_channels;
		SampleFormat m_format;
		std::shared_ptr<const Table> m_table;

		std::vector<float> m_history;
		int64_t m_historyStart = 0;
		uint64_t m_inputFrames = 0;
		uint64_t m_outputFrame = 0;

	public:
		WindowedSincResampler(uint32_t inRate, uint32_t outRate, uint32_t channels, SampleFormat format, const WindowedSincResamplerConfig& config);

		[[nodiscard]] const char* Name() const override;

		bool Process(const void* in, size_t frames, std::vector<float>& out) override;
		bool Drain(std::vector<float>& out) override;
		void Reset() override;

		[[nodiscard]] double HeldBack() const override;

	private:
		void Produce(std::vector<float>& out);

		static std::shared_ptr<const Table> TableFor(double scale, const WindowedSincResamplerConfig& config);
		static std::shared_ptr<const Table> MakeTable(double scale, const WindowedSincResamplerConfig& config);
		static double BesselI0(double x);
	};
}
