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

		/// Resample voices with soxr instead of the game's linear interpolation.
		bool Enabled = false;

		// Defaults measure at about 132 dB SINAD with a 1.8 ms delay, far past audible at little CPU.
		QualityPreset Quality = QualityPreset::Bits20;
		PhaseResponse Phase = PhaseResponse::Minimum;
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
		uint32_t Log2MinDftSize = 8;
		uint32_t Log2LargeDftSize = 8;

		[[nodiscard]] SoxrResamplerConfig Sanitized() const;
		bool operator==(const SoxrResamplerConfig&) const = default;
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

	void to_json(nlohmann::json&, const SoxrResamplerConfig&);
	void from_json(const nlohmann::json&, SoxrResamplerConfig&);
}
