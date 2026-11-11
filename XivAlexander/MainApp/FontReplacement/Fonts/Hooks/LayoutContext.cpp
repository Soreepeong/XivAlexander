#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Hooks/LayoutContext.h"

#include "MainApp/FontReplacement/Fonts/Hooks/UiScaleWrapMeasurer.h"
#include "MainApp/FontReplacement/GameCode.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::LayoutContext::LayoutContext() {
	const auto set = ResolveGameCode(Game::Resolved::Fonts::FontSet);
	const auto functions = ResolveGameCode(Game::Resolved::Fonts::Replacer);
	const auto measure = ResolveGameCode(Game::Resolved::Fonts::Measure);

	// Optional: without them, italics are the game's shear.
	m_stateFlags = functions.StateFlags.value_or(-1);
	m_setFlags = set.DrawFlags.value_or(-1);
	m_nodeTextFlags = measure.NodeTextFlags;
	m_nodeBoldFlag = measure.NodeBoldFlag;
	m_nodeTransform = measure.NodeTransform;
	m_setUnscaledSizeY = measure.SetUnscaledSizeY;
	m_setPickScale = measure.SetPickScale;
}

bool FontReplacement::LayoutContext::HasPickScale(GameFontSet* set) const {
	const auto scale = PickScaleOf(set);
	return scale > 0 && std::isfinite(scale);
}

FontReplacement::LayoutContext::Call FontReplacement::LayoutContext::EnterCharacter(const uint8_t* character, bool measuringOnly, uintptr_t state) {
	return std::exchange(m_call, {.Character = character, .MeasuringOnly = measuringOnly, .State = state});
}

void FontReplacement::LayoutContext::LeaveCharacter(const Call& outer) {
	// The italic macro reads the bit when italics end (the glyph after moves by the font's italic correction).
	if (m_call.ItalicCleared)
		SetStateItalic(m_call.State, true);
	m_call = outer;
}

bool FontReplacement::LayoutContext::EnterWrap(bool wrapping) {
	return std::exchange(m_wrapping, wrapping);
}

bool FontReplacement::LayoutContext::IsWrapMeasure(GameFontSet* set) const {
	return m_wrapping && set->NodeScaleX() == 1 && set->NodeScaleY() == 1 && !HasPickScale(set);
}

float FontReplacement::LayoutContext::PickSizeOf(GameFontSet* set) const {
	if (IsWrapMeasure(set))
		return set->ScaledSizeY() * UiScaleWrapMeasurer::LargestScale;
	return set->NodeScaleX() == 1 && set->NodeScaleY() == 1 && HasPickScale(set) ? At<float>(set, m_setUnscaledSizeY) * PickScaleOf(set) : set->ScaledSizeY();
}

FontReplacement::ItalicMode FontReplacement::LayoutContext::ItalicModeOf(GameFontSet* set) const {
	if (m_stateFlags < 0 || !m_call.State || !(At<uint32_t>(m_call.State + m_stateFlags) & ItalicFlag))
		return ItalicMode::Upright;
	return set && m_setFlags >= 0 && (At<uint32_t>(set, m_setFlags) & ItalicFlag) ? ItalicMode::Images : ItalicMode::Real;
}

void FontReplacement::LayoutContext::ClearItalic() {
	SetStateItalic(m_call.State, false);
	m_call.ItalicCleared = true;
}

void FontReplacement::LayoutContext::ApplyNodeBold(GameFontSet* set, uintptr_t node) const {
	if (m_setFlags < 0)
		return;
	auto& flags = At<uint32_t>(set, m_setFlags);
	flags = At<uint8_t>(node + m_nodeTextFlags) & m_nodeBoldFlag ? flags | BoldFlag : flags & ~BoldFlag;
}

float FontReplacement::LayoutContext::NodeScaleOf(uintptr_t node) const {
	const auto m = reinterpret_cast<const float*>(node + m_nodeTransform);
	return std::sqrt(m[2] * m[2] + m[3] * m[3]);
}

void FontReplacement::LayoutContext::SetStateItalic(uintptr_t state, bool value) const {
	if (m_stateFlags < 0 || !state)
		return;
	auto& flags = At<uint32_t>(state + m_stateFlags);
	flags = value ? flags | ItalicFlag : flags & ~ItalicFlag;
}
