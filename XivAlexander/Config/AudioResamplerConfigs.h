#pragma once

#include <cstdint>

#include <nlohmann/json.hpp>

namespace XivAlexander {
	/// libsoxr. Zero in a number means "whatever the quality preset says".
	struct SoxrResamplerConfig {
		enum class QualityPreset : uint8_t {
			Quick,      // cubic interpolation
			Low,        // 16-bit, larger roll-off
			Medium,     // 16-bit, medium roll-off
			Bits16,
			Bits20,     // soxr's "high quality"
			Bits24,
			Bits28,     // soxr's "very high quality"
			Bits32,
		};

		enum class PhaseResponse : uint8_t {
			Linear,
			Intermediate,
			Minimum,
		};

		enum class Rolloff : uint8_t {
			Small,      // <= 0.01 dB
			Medium,     // <= 0.35 dB
			None,
		};

		QualityPreset Quality = QualityPreset::Bits20;
		PhaseResponse Phase = PhaseResponse::Linear;
		/// Moves the passband edge up towards Nyquist.
		bool SteepFilter = false;
		/// The 0 dB point as a fraction of the lower Nyquist, in (0, 1).
		double PassbandEnd = 0;
		/// Where attenuation begins, as a fraction of the lower Nyquist; above PassbandEnd.
		double StopbandBegin = 0;
		Rolloff PassbandRolloff = Rolloff::Small;
		bool DoublePrecision = false;
		/// More accurate positioning for rate ratios that are not simple fractions.
		bool HighPrecisionClock = false;
		/// log2 of the smallest and the large DFT sizes; they set most of soxr's hold-back. [8, 15] and [8, 20].
		uint32_t Log2MinDftSize = 0;
		uint32_t Log2LargeDftSize = 0;

		[[nodiscard]] SoxrResamplerConfig Sanitized() const;
		bool operator==(const SoxrResamplerConfig&) const = default;
	};

	/// The built-in Kaiser-windowed sinc.
	struct WindowedSincResamplerConfig {
		/// Taps on each side of an output sample, at the lower of the two rates; also the hold-back in input frames.
		uint32_t HalfTaps = 32;
		/// Filter phases in the table, linearly interpolated between.
		uint32_t Phases = 1024;
		/// Kaiser window beta; higher for a deeper stopband and a wider transition. 8 gives about 80 dB.
		double KaiserBeta = 8.0;
		/// Where the response is down 6 dB, as a fraction of the lower Nyquist.
		double Cutoff = 0.975;

		[[nodiscard]] WindowedSincResamplerConfig Sanitized() const;
		bool operator==(const WindowedSincResamplerConfig&) const = default;
	};

	/// r8brain-free-src.
	struct R8brainResamplerConfig {
		enum class PhaseResponse : uint8_t {
			Linear,
			Minimum,
		};

		/// Percent of the spectrum between the -3 dB point and Nyquist, [0.5, 45]; r8brain suggests 2-3.
		double TransitionBand = 6.0;
		/// Stopband attenuation in dB, [49, 218].
		double Attenuation = 120.0;
		PhaseResponse Phase = PhaseResponse::Linear;
		/// Input frames handed over per call; r8brain sizes its buffers by this.
		uint32_t BlockFrames = 1024;

		[[nodiscard]] R8brainResamplerConfig Sanitized() const;
		bool operator==(const R8brainResamplerConfig&) const = default;
	};

	/// ART (dbry/audio-resampler).
	struct ArtResamplerConfig {
		enum class Window : uint8_t {
			BlackmanHarris,
			Hann,
		};

		/// Taps per sinc filter, [4, 1024] in multiples of 4; half of them is the hold-back in input frames.
		uint32_t Taps = 380;
		/// Sinc filters across a sample period, [1, 1024].
		uint32_t Filters = 380;
		Window FilterWindow = Window::BlackmanHarris;
		/// Build a lowpass into the filters; needed for downsampling.
		bool IncludeLowpass = true;
		/// Lowpass frequency in Hz; 0 picks one from the rates and the filter length.
		uint32_t LowpassFrequency = 0;
		/// Interpolate between the two nearest filters.
		bool SubsampleInterpolate = true;
		/// Convolve in double precision.
		bool ExtendedPrecision = false;
		/// Run the channels on worker threads.
		bool Multithreaded = false;

		[[nodiscard]] ArtResamplerConfig Sanitized() const;
		bool operator==(const ArtResamplerConfig&) const = default;
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(SoxrResamplerConfig::QualityPreset, {
		{SoxrResamplerConfig::QualityPreset::Bits20, "Bits20"},
		{SoxrResamplerConfig::QualityPreset::Quick, "Quick"},
		{SoxrResamplerConfig::QualityPreset::Low, "Low"},
		{SoxrResamplerConfig::QualityPreset::Medium, "Medium"},
		{SoxrResamplerConfig::QualityPreset::Bits16, "Bits16"},
		{SoxrResamplerConfig::QualityPreset::Bits24, "Bits24"},
		{SoxrResamplerConfig::QualityPreset::Bits28, "Bits28"},
		{SoxrResamplerConfig::QualityPreset::Bits32, "Bits32"},
	})

	NLOHMANN_JSON_SERIALIZE_ENUM(SoxrResamplerConfig::PhaseResponse, {
		{SoxrResamplerConfig::PhaseResponse::Linear, "Linear"},
		{SoxrResamplerConfig::PhaseResponse::Intermediate, "Intermediate"},
		{SoxrResamplerConfig::PhaseResponse::Minimum, "Minimum"},
	})

	NLOHMANN_JSON_SERIALIZE_ENUM(SoxrResamplerConfig::Rolloff, {
		{SoxrResamplerConfig::Rolloff::Small, "Small"},
		{SoxrResamplerConfig::Rolloff::Medium, "Medium"},
		{SoxrResamplerConfig::Rolloff::None, "None"},
	})

	NLOHMANN_JSON_SERIALIZE_ENUM(R8brainResamplerConfig::PhaseResponse, {
		{R8brainResamplerConfig::PhaseResponse::Linear, "Linear"},
		{R8brainResamplerConfig::PhaseResponse::Minimum, "Minimum"},
	})

	NLOHMANN_JSON_SERIALIZE_ENUM(ArtResamplerConfig::Window, {
		{ArtResamplerConfig::Window::BlackmanHarris, "BlackmanHarris"},
		{ArtResamplerConfig::Window::Hann, "Hann"},
	})

	void to_json(nlohmann::json&, const SoxrResamplerConfig&);
	void from_json(const nlohmann::json&, SoxrResamplerConfig&);
	void to_json(nlohmann::json&, const WindowedSincResamplerConfig&);
	void from_json(const nlohmann::json&, WindowedSincResamplerConfig&);
	void to_json(nlohmann::json&, const R8brainResamplerConfig&);
	void from_json(const nlohmann::json&, R8brainResamplerConfig&);
	void to_json(nlohmann::json&, const ArtResamplerConfig&);
	void from_json(const nlohmann::json&, ArtResamplerConfig&);
}
