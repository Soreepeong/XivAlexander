#pragma once

#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontReplacer;
	struct GameFontSet;

	// Nameplate text with the replaced fonts.
	//
	// The game bakes each plate's name into one shared render texture (AddonNamePlate::BakePlateRenderer), once per text
	// change, and draws that region every frame under the plate's transform. OnRequestedUpdate scales each plate's root
	// component to max(percent / 100, 0.05) * size (SetCommonNamePlate's body, inlined in 7.56h; the function itself has no
	// callers) and stores (percent / 100)^(1/4) * 255 as the bake alpha. The bake is made at the text node's unscaled size,
	// so the plate is resampled at every distance.
	//
	// Baking at full size: the bake scale S is the text node's on-screen scale at 100 percent: its current scale (the product
	// of its and its ancestors' scales) divided by the current max(percent / 100, 0.05), the percent taken back from the
	// alpha. The region is allocated S times larger (FUN_141326FF0 with TextW/TextH scaled), the text is baked with the font
	// set's node scale S instead of 1 (BakePlateRenderer vf3), and the cached draw (FUN_141327640) is given the node's size
	// and the text offset times S and its transform divided by S (exactly 1 when shown at S), so it draws the region at the
	// plate's size. A plate shown larger than its bake is baked again at that size.
	class NamePlateText {
		FontReplacer& m_replacer;

		// The scale each object's current region was baked at.
		std::map<uintptr_t, float> m_bakeScales;

		// AddonNamePlate: its BakePlateRenderer (embedded), and its NamePlateObjects (an array of NamePlateObjectArraySize
		// bytes, NamePlateObjectSize each).
		int m_addonBakePlate = 0;
		int m_addonObjects = 0;
		int m_objectSize = 0;
		int m_objectCount = 0;

		// NamePlateObject: its BakeData (at 0, so a BakeData* is its object's), text node, text size, and whether it needs a
		// bake. BakeData: the text's vertical offset and the bake alpha.
		int m_objectNameText = 0;
		int m_objectTextW = 0;
		int m_objectTextH = 0;
		int m_objectNeedsToBeBaked = 0;
		int m_bakeTextYOffset = 0;
		int m_bakeAlpha = 0;

		// BakePlateRenderer: the BakeData being baked.
		int m_rendererCurrentBakeData = 0;

		// AtkResNode: its transform (a 2x2 matrix of floats, row by row), size, parent and scale.
		int m_nodeTransform = 0;
		int m_nodeWidth = 0;
		int m_nodeHeight = 0;
		int m_nodeParent = 0;
		int m_nodeScaleX = 0;

		// The renderer last seen by a hook, and whether every plate must be baked again once one is seen.
		uintptr_t m_lastRenderer = 0;
		bool m_rebakePending = false;

		std::optional<Host::Hook<uint8_t, uintptr_t, uintptr_t>> m_allocateBakeHook;
		std::optional<Host::Hook<void, uintptr_t, float*, GameFontSet*, uintptr_t>> m_prepareHook;
		std::optional<Host::Hook<void, uintptr_t, uintptr_t, uintptr_t>> m_drawBakedHook;

	public:
		explicit NamePlateText(FontReplacer& replacer);
		NamePlateText(const NamePlateText&) = delete;
		NamePlateText& operator=(const NamePlateText&) = delete;
		~NamePlateText();

		// Makes every plate allocate and bake its region again at its next update. The plates are reached from the renderer
		// the hooks see (it is embedded in the NamePlate addon): through the one seen last if that addon is still loaded, else
		// at the next draw.
		void ForceRebake();

	private:
		// Remembers the renderer a hook was called with, and makes the bakes asked for before it was seen.
		void SeeRenderer(uintptr_t renderer);

		[[nodiscard]] bool NeedsToBeBaked(uintptr_t obj) const;
		void SetNeedsToBeBaked(uintptr_t obj) const;
		[[nodiscard]] float& Transform(uintptr_t node, int index) const;

		// Gets the text node's on-screen scale as its nodes are now: the product of its and its ancestors' scales.
		[[nodiscard]] std::optional<float> GetShownScale(uintptr_t obj) const;

		// Gets the text node's on-screen scale with the plate at 100 percent: its current scale divided by the plate's current
		// distance factor, which OnRequestedUpdate stored as the bake alpha (factor^(1/4) * 255) this update.
		[[nodiscard]] std::optional<float> GetFullSizeScale(uintptr_t obj) const;

		uint8_t AllocateBakeDetour(uintptr_t renderer, uintptr_t obj);
		void PrepareDetour(uintptr_t renderer, float* rect, GameFontSet* set, uintptr_t node);
		void DrawBakedDetour(uintptr_t renderer, uintptr_t node, uintptr_t bake);
	};
}
