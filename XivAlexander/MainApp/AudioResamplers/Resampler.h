#pragma once

#include <cstdint>
#include <vector>

namespace XivAlexander::Apps::MainApp::AudioResamplers {
	enum class SampleFormat : uint8_t {
		Int16,
		Float,
	};

	class Resampler {
	public:
		virtual ~Resampler() = default;

		[[nodiscard]] virtual const char* Name() const = 0;

		virtual bool Process(const void* in, size_t frames, std::vector<float>& out) = 0;
		virtual bool Drain(std::vector<float>& out) = 0;
		virtual void Reset() = 0;

		[[nodiscard]] virtual double HeldBack() const = 0;
	};
}
