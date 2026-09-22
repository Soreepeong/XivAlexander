#pragma once

#include <nlohmann/json_fwd.hpp>

namespace XivAlexander {
	enum class Language : uint8_t {
		SystemDefault,
		English,
		Korean,
		Japanese,
	};

	enum class ThemeMode : uint8_t {
		System,
		Light,
		Dark,
	};

	enum class HighLatencyMitigationMode : uint8_t {
		SubtractLatency,
		SimulateRtt,
		SimulateNormalizedRttAndLatency,
	};

	enum class GameWindowTitleMode : uint8_t {
		None,
		Prefix,
		Suffix,
	};

	enum class AudioResamplerEngine : uint8_t {
		Disabled,
		Soxr,
		WindowedSinc,
		R8brain,
		Art,
	};

	void to_json(nlohmann::json&, const Language&);
	void from_json(const nlohmann::json&, Language&);
	void to_json(nlohmann::json&, const ThemeMode&);
	void from_json(const nlohmann::json&, ThemeMode&);
	void to_json(nlohmann::json&, const HighLatencyMitigationMode&);
	void from_json(const nlohmann::json&, HighLatencyMitigationMode&);
	void to_json(nlohmann::json&, const GameWindowTitleMode&);
	void from_json(const nlohmann::json&, GameWindowTitleMode&);
	void to_json(nlohmann::json&, const AudioResamplerEngine&);
	void from_json(const nlohmann::json&, AudioResamplerEngine&);
}
