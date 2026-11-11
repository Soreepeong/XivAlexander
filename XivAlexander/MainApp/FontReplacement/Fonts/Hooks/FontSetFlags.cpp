#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Hooks/FontSetFlags.h"

#include "MainApp/FontReplacement/GameCode.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::FontSetFlags::FontSetFlags() {
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

void FontReplacement::FontSetFlags::SetStateItalic(uintptr_t state, bool value) const {
	if (m_stateFlags < 0 || !state)
		return;
	auto& flags = At<uint32_t>(state + m_stateFlags);
	flags = value ? flags | ItalicFlag : flags & ~ItalicFlag;
}

void FontReplacement::FontSetFlags::ApplyNodeBold(GameFontSet* set, uintptr_t node) const {
	if (m_setFlags < 0)
		return;
	auto& flags = At<uint32_t>(set, m_setFlags);
	flags = At<uint8_t>(node + m_nodeTextFlags) & m_nodeBoldFlag ? flags | BoldFlag : flags & ~BoldFlag;
}

float FontReplacement::FontSetFlags::NodeScaleOf(uintptr_t node) const {
	const auto m = reinterpret_cast<const float*>(node + m_nodeTransform);
	return std::sqrt(m[2] * m[2] + m[3] * m[3]);
}
