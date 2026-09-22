#include "pch.h"
#include "Enums.h"

void XivAlexander::to_json(nlohmann::json& j, const Language& value) {
	switch (value) {
		case Language::English:
			j = "English";
			break;

		case Language::Korean:
			j = "Korean";
			break;

		case Language::Japanese:
			j = "Japanese";
			break;

		case Language::SystemDefault:
		default:
			j = "SystemDefault";
	}
}

void XivAlexander::from_json(const nlohmann::json& it, Language& value) {
	auto newValueString = xivres::util::unicode::convert<std::wstring>(it.get<std::string>());
	CharLowerW(&newValueString[0]);

	value = Language::SystemDefault;
	if (newValueString.empty())
		return;

	if (newValueString.substr(0, std::min<size_t>(7, newValueString.size())) == L"english")
		value = Language::English;
	else if (newValueString.substr(0, std::min<size_t>(6, newValueString.size())) == L"korean")
		value = Language::Korean;
	else if (newValueString.substr(0, std::min<size_t>(8, newValueString.size())) == L"japanese")
		value = Language::Japanese;
}

void XivAlexander::to_json(nlohmann::json& j, const ThemeMode& value) {
	switch (value) {
		case ThemeMode::Light:
			j = "Light";
			break;
		case ThemeMode::Dark:
			j = "Dark";
			break;
		case ThemeMode::System:
		default:
			j = "System";
	}
}

void XivAlexander::from_json(const nlohmann::json& it, ThemeMode& value) {
	auto s = xivres::util::unicode::convert<std::wstring>(it.get<std::string>());
	CharLowerW(&s[0]);
	if (s.substr(0, 5) == L"light")
		value = ThemeMode::Light;
	else if (s.substr(0, 4) == L"dark")
		value = ThemeMode::Dark;
	else
		value = ThemeMode::System;
}

void XivAlexander::to_json(nlohmann::json& j, const HighLatencyMitigationMode& value) {
	switch (value) {
		case HighLatencyMitigationMode::SubtractLatency:
			j = "SubtractLatency";
			break;

		case HighLatencyMitigationMode::SimulateRtt:
			j = "SimulateRtt";
			break;

		case HighLatencyMitigationMode::SimulateNormalizedRttAndLatency:
		default:
			j = "SimulateNormalizedRttAndLatency";
	}
}

void XivAlexander::from_json(const nlohmann::json& it, HighLatencyMitigationMode& value) {
	auto newValueString = xivres::util::unicode::convert<std::wstring>(it.get<std::string>());
	CharLowerW(&newValueString[0]);

	value = HighLatencyMitigationMode::SimulateNormalizedRttAndLatency;
	if (newValueString.empty())
		return;

	if (newValueString.substr(0, std::min<size_t>(31, newValueString.size())) == L"subtractnormalizedrttandlatency")
		value = HighLatencyMitigationMode::SimulateNormalizedRttAndLatency;
	else if (newValueString.substr(0, std::min<size_t>(11, newValueString.size())) == L"simulatertt")
		value = HighLatencyMitigationMode::SimulateRtt;
	else if (newValueString.substr(0, std::min<size_t>(16, newValueString.size())) == L"subtractlatency")
		value = HighLatencyMitigationMode::SubtractLatency;
}

void XivAlexander::to_json(nlohmann::json& j, const GameWindowTitleMode& value) {
	switch (value) {
		case GameWindowTitleMode::None:
		default:
			j = "None";
			break;

		case GameWindowTitleMode::Prefix:
			j = "Prefix";
			break;

		case GameWindowTitleMode::Suffix:
			j = "Suffix";
			break;
	}
}

void XivAlexander::from_json(const nlohmann::json& it, GameWindowTitleMode& value) {
	if (it.is_string()) {
		const auto s = it.get<std::string>();
		if (s == "Prefix")
			value = GameWindowTitleMode::Prefix;
		else if (s == "Suffix")
			value = GameWindowTitleMode::Suffix;
		else
			value = GameWindowTitleMode::None;
	} else if (it.is_boolean()) {
		value = it.get<bool>() ? GameWindowTitleMode::Suffix : GameWindowTitleMode::None;
	} else {
		value = GameWindowTitleMode::None;
	}
}

void XivAlexander::to_json(nlohmann::json& j, const AudioResamplerEngine& value) {
	switch (value) {
		case AudioResamplerEngine::Disabled:
		default:
			j = "Disabled";
			break;

		case AudioResamplerEngine::Soxr:
			j = "Soxr";
			break;

		case AudioResamplerEngine::WindowedSinc:
			j = "WindowedSinc";
			break;

		case AudioResamplerEngine::R8brain:
			j = "R8brain";
			break;

		case AudioResamplerEngine::Art:
			j = "Art";
			break;
	}
}

void XivAlexander::from_json(const nlohmann::json& it, AudioResamplerEngine& value) {
	const auto s = it.is_string() ? it.get<std::string>() : std::string();
	if (s == "Soxr")
		value = AudioResamplerEngine::Soxr;
	else if (s == "WindowedSinc")
		value = AudioResamplerEngine::WindowedSinc;
	else if (s == "R8brain")
		value = AudioResamplerEngine::R8brain;
	else if (s == "Art")
		value = AudioResamplerEngine::Art;
	else
		value = AudioResamplerEngine::Disabled;
}
