#pragma once

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// What the replacement keeps of a font set and the layout state, in their flags and the set's pick scale, at offsets from the game's code.
	class FontSetFlags {
		static constexpr uint32_t ItalicFlag = 0x08000000;

		// Set by a text node's bold (when drawing) and the bold macro. The game reads it nowhere.
		static constexpr uint32_t BoldFlag = 0x04000000;

		// An offset of -1 is unknown, leaving italics sheared.
		int m_stateFlags = -1;
		int m_setFlags = -1;

		int m_nodeTextFlags = 0;
		uint8_t m_nodeBoldFlag = 0;
		int m_nodeTransform = 0;
		int m_setUnscaledSizeY = 0;
		int m_setPickScale = 0;

	public:
		// Throws if this game version can't be worked with.
		FontSetFlags();

		// Where positive, the game picks its font for this times the unscaled size, and scales the advances back.
		[[nodiscard]] float& PickScaleOf(GameFontSet* set) const { return At<float>(set, m_setPickScale); }

		[[nodiscard]] float UnscaledSizeYOf(GameFontSet* set) const { return At<float>(set, m_setUnscaledSizeY); }

		[[nodiscard]] bool IsBold(uintptr_t state) const { return m_stateFlags >= 0 && state && (At<uint32_t>(state + m_stateFlags) & BoldFlag); }

		// The macro's italics, which measuring sees too.
		[[nodiscard]] bool IsStateItalic(uintptr_t state) const { return m_stateFlags >= 0 && state && (At<uint32_t>(state + m_stateFlags) & ItalicFlag); }

		void SetStateItalic(uintptr_t state, bool value) const;

		// A text node's italics, which only drawing sees.
		[[nodiscard]] bool IsSetItalic(GameFontSet* set) const { return set && m_setFlags >= 0 && (At<uint32_t>(set, m_setFlags) & ItalicFlag); }

		// The game measures a node's bold upright, as it draws it.
		void ApplyNodeBold(GameFontSet* set, uintptr_t node) const;

		// The length of the node transform's second row, its vertical scale.
		[[nodiscard]] float NodeScaleOf(uintptr_t node) const;
	};
}
