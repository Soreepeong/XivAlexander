#pragma once

#include "MainApp/FontReplacement/Fonts/Enums/ItalicMode.h"
#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// What is being laid out, which is what the replacement keeps of each font set and the character LayOutCharacter is at.
	// A font set's marks are in its flags and pick scale in game memory, at offsets from the game's code.
	// The character is kept for GetGlyph, which isn't told which character of the text it looks up. One instance serves every layout, as the
	// game lays text out and draws only on its main thread.
	class LayoutContext {
	public:
		// The character LayOutCharacter is laying out.
		struct Call {
			const uint8_t* Character = nullptr;

			// Laid out by an analyzer that only measures, so its glyph needs no pixels yet.
			bool MeasuringOnly = false;

			uintptr_t State = 0;

			// The italic bit was cleared for a real italic glyph, to be set again once the character is laid out.
			bool ItalicCleared = false;
		};

	private:
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

		Call m_call;
		bool m_wrapping = false;

	public:
		// Throws if this game version can't be worked with.
		LayoutContext();
		LayoutContext(const LayoutContext&) = delete;
		LayoutContext& operator=(const LayoutContext&) = delete;

		// Whether the text wrapper is laying text out.
		[[nodiscard]] bool Wrapping() const { return m_wrapping; }

		// The character LayOutCharacter is at, nullptr outside it.
		[[nodiscard]] const uint8_t* Character() const { return m_call.Character; }

		[[nodiscard]] bool MeasuringOnly() const { return m_call.MeasuringOnly; }

		// Whether the character is in bold.
		[[nodiscard]] bool IsBold() const { return m_stateFlags >= 0 && m_call.State && (At<uint32_t>(m_call.State + m_stateFlags) & BoldFlag); }

		// Where positive, the game picks its font for this times the unscaled size, and scales the advances back.
		[[nodiscard]] float& PickScaleOf(GameFontSet* set) const { return At<float>(set, m_setPickScale); }

		[[nodiscard]] bool HasPickScale(GameFontSet* set) const;

		// Returns the call it replaces, for LeaveCharacter.
		Call EnterCharacter(const uint8_t* character, bool measuringOnly, uintptr_t state);

		void LeaveCharacter(const Call& outer);

		// Returns the outer value, for LeaveWrap.
		bool EnterWrap(bool wrapping);

		void LeaveWrap(bool outer) { m_wrapping = outer; }

		// Whether the text wrapper is laying out text it has no scale of, which is wrapped to fit at every UI scale (WrapMeasure).
		[[nodiscard]] bool IsWrapMeasure(GameFontSet* set) const;

		// The size a copy is picked for: the on-screen size, even where the game picked for another (a node's fixed font resolution, or a pick
		// scale of its own), so that the renderer doesn't scale the glyphs. Text laid out unscaled takes its pick scale's, or the largest UI
		// scale's while it is wrapped.
		[[nodiscard]] float PickSizeOf(GameFontSet* set) const;

		// The italics of the character. An italic macro sets the state's bit, which measuring sees too. A text node's italics are only in the
		// font set's own flags, which only drawing sees.
		[[nodiscard]] ItalicMode ItalicModeOf(GameFontSet* set) const;

		// Clears the italic bit until the character is laid out, so that a real italic glyph is drawn unsheared.
		void ClearItalic();

		// The game measures a node's bold upright, as it draws it.
		void ApplyNodeBold(GameFontSet* set, uintptr_t node) const;

		// The length of the node transform's second row, its vertical scale.
		[[nodiscard]] float NodeScaleOf(uintptr_t node) const;

	private:
		void SetStateItalic(uintptr_t state, bool value) const;
	};
}
