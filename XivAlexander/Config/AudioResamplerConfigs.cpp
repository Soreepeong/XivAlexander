#include "pch.h"
#include "AudioResamplerConfigs.h"

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

XivAlexander::SoxrResamplerConfig XivAlexander::SoxrResamplerConfigGroup::FilterGroup::Snapshot() const {
	return SoxrResamplerConfig{
		.Quality = Quality.Value(),
		.Phase = Phase.Value(),
		.SteepFilter = SteepFilter.Value(),
		.PassbandEnd = PassbandEnd.Value(),
		.StopbandBegin = StopbandBegin.Value(),
		.PassbandRolloff = PassbandRolloff.Value(),
		.DoublePrecision = DoublePrecision.Value(),
		.HighPrecisionClock = HighPrecisionClock.Value(),
		.Log2MinDftSize = Log2MinDftSize.Value(),
		.Log2LargeDftSize = Log2LargeDftSize.Value(),
	}.Sanitized();
}
