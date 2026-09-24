#include "pch.h"
#include "AudioResamplerConfigs.h"

namespace {
	/// Reads \p key into \p value when it is there, leaving the default otherwise.
	template<typename T>
	void Read(const nlohmann::json& j, const char* key, T& value) {
		if (const auto it = j.find(key); it != j.end())
			value = it->get<T>();
	}
}

XivAlexander::SoxrResamplerConfig XivAlexander::SoxrResamplerConfig::Sanitized() const {
	auto v = *this;
	if (v.PassbandEnd <= 0 || v.PassbandEnd >= 1)
		v.PassbandEnd = 0;
	if (v.StopbandBegin != 0 && v.StopbandBegin <= (v.PassbandEnd == 0 ? 0.5 : v.PassbandEnd))
		v.StopbandBegin = 0;
	if (v.Log2MinDftSize != 0)
		v.Log2MinDftSize = std::clamp<uint32_t>(v.Log2MinDftSize, 8, 15);
	if (v.Log2LargeDftSize != 0)
		v.Log2LargeDftSize = std::clamp<uint32_t>(v.Log2LargeDftSize, 8, 20);
	return v;
}

void XivAlexander::to_json(nlohmann::json& j, const SoxrResamplerConfig& v) {
	j = nlohmann::json::object({
		{"Enabled", v.Enabled},
		{"Quality", v.Quality},
		{"Phase", v.Phase},
		{"SteepFilter", v.SteepFilter},
		{"PassbandEnd", v.PassbandEnd},
		{"StopbandBegin", v.StopbandBegin},
		{"PassbandRolloff", v.PassbandRolloff},
		{"DoublePrecision", v.DoublePrecision},
		{"HighPrecisionClock", v.HighPrecisionClock},
		{"Log2MinDftSize", v.Log2MinDftSize},
		{"Log2LargeDftSize", v.Log2LargeDftSize},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, SoxrResamplerConfig& v) {
	v = {};
	Read(j, "Enabled", v.Enabled);
	Read(j, "Quality", v.Quality);
	Read(j, "Phase", v.Phase);
	Read(j, "SteepFilter", v.SteepFilter);
	Read(j, "PassbandEnd", v.PassbandEnd);
	Read(j, "StopbandBegin", v.StopbandBegin);
	Read(j, "PassbandRolloff", v.PassbandRolloff);
	Read(j, "DoublePrecision", v.DoublePrecision);
	Read(j, "HighPrecisionClock", v.HighPrecisionClock);
	Read(j, "Log2MinDftSize", v.Log2MinDftSize);
	Read(j, "Log2LargeDftSize", v.Log2LargeDftSize);
}
