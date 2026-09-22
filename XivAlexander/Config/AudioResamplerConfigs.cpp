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

XivAlexander::WindowedSincResamplerConfig XivAlexander::WindowedSincResamplerConfig::Sanitized() const {
	auto v = *this;
	v.HalfTaps = std::clamp<uint32_t>(v.HalfTaps, 2, 256);
	v.Phases = std::clamp<uint32_t>(v.Phases, 16, 65536);
	v.KaiserBeta = std::clamp(v.KaiserBeta, 0.0, 30.0);
	v.Cutoff = std::clamp(v.Cutoff, 0.5, 1.0);
	return v;
}

XivAlexander::R8brainResamplerConfig XivAlexander::R8brainResamplerConfig::Sanitized() const {
	auto v = *this;
	v.TransitionBand = std::clamp(v.TransitionBand, 0.5, 45.0);
	v.Attenuation = std::clamp(v.Attenuation, 49.0, 218.0);
	v.BlockFrames = std::clamp<uint32_t>(v.BlockFrames, 64, 65536);
	return v;
}

XivAlexander::ArtResamplerConfig XivAlexander::ArtResamplerConfig::Sanitized() const {
	auto v = *this;
	v.Taps = std::clamp<uint32_t>(v.Taps & ~3U, 4, 1024);
	v.Filters = std::clamp<uint32_t>(v.Filters, 1, 1024);
	return v;
}

void XivAlexander::to_json(nlohmann::json& j, const SoxrResamplerConfig& v) {
	j = nlohmann::json::object({
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

void XivAlexander::to_json(nlohmann::json& j, const WindowedSincResamplerConfig& v) {
	j = nlohmann::json::object({
		{"HalfTaps", v.HalfTaps},
		{"Phases", v.Phases},
		{"KaiserBeta", v.KaiserBeta},
		{"Cutoff", v.Cutoff},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, WindowedSincResamplerConfig& v) {
	v = {};
	Read(j, "HalfTaps", v.HalfTaps);
	Read(j, "Phases", v.Phases);
	Read(j, "KaiserBeta", v.KaiserBeta);
	Read(j, "Cutoff", v.Cutoff);
}

void XivAlexander::to_json(nlohmann::json& j, const R8brainResamplerConfig& v) {
	j = nlohmann::json::object({
		{"TransitionBand", v.TransitionBand},
		{"Attenuation", v.Attenuation},
		{"Phase", v.Phase},
		{"BlockFrames", v.BlockFrames},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, R8brainResamplerConfig& v) {
	v = {};
	Read(j, "TransitionBand", v.TransitionBand);
	Read(j, "Attenuation", v.Attenuation);
	Read(j, "Phase", v.Phase);
	Read(j, "BlockFrames", v.BlockFrames);
}

void XivAlexander::to_json(nlohmann::json& j, const ArtResamplerConfig& v) {
	j = nlohmann::json::object({
		{"Taps", v.Taps},
		{"Filters", v.Filters},
		{"FilterWindow", v.FilterWindow},
		{"IncludeLowpass", v.IncludeLowpass},
		{"LowpassFrequency", v.LowpassFrequency},
		{"SubsampleInterpolate", v.SubsampleInterpolate},
		{"ExtendedPrecision", v.ExtendedPrecision},
		{"Multithreaded", v.Multithreaded},
	});
}

void XivAlexander::from_json(const nlohmann::json& j, ArtResamplerConfig& v) {
	v = {};
	Read(j, "Taps", v.Taps);
	Read(j, "Filters", v.Filters);
	Read(j, "FilterWindow", v.FilterWindow);
	Read(j, "IncludeLowpass", v.IncludeLowpass);
	Read(j, "LowpassFrequency", v.LowpassFrequency);
	Read(j, "SubsampleInterpolate", v.SubsampleInterpolate);
	Read(j, "ExtendedPrecision", v.ExtendedPrecision);
	Read(j, "Multithreaded", v.Multithreaded);
}
