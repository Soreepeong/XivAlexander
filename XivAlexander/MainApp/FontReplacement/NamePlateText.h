#pragma once

#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontReplacer;
	struct GameFontSet;

	// Nameplate text with replaced fonts, baked at S = on-screen scale at 100% rather than unscaled
	// (region, font set node scale, and the cached draw's size/offset times S, its transform divided by S).
	class NamePlateText {
		FontReplacer& m_replacer;

		// The scale each object's current region was baked at.
		std::map<uintptr_t, float> m_bakeScales;

		// AddonNamePlate: embedded BakePlateRenderer and the NamePlateObject array.
		int m_addonBakePlate = 0;
		int m_addonObjects = 0;
		int m_objectSize = 0;
		int m_objectCount = 0;

		// NamePlateObject, then BakeData; BakeData is at offset 0, so a BakeData* is its object's.
		int m_objectNameText = 0;
		int m_objectTextW = 0;
		int m_objectTextH = 0;
		int m_objectNeedsToBeBaked = 0;
		int m_bakeTextYOffset = 0;
		int m_bakeAlpha = 0;

		int m_rendererCurrentBakeData = 0;

		// AtkResNode; the transform is a row-major 2x2 float matrix.
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

		// Plates are reached via the last renderer the hooks saw (embedded in the NamePlate addon) if that addon is still loaded, else at the next draw.
		void ForceRebake();

	private:
		// Also performs rebakes requested before any renderer was seen.
		void SeeRenderer(uintptr_t renderer);

		[[nodiscard]] bool NeedsToBeBaked(uintptr_t obj) const;
		void SetNeedsToBeBaked(uintptr_t obj) const;
		[[nodiscard]] float& Transform(uintptr_t node, int index) const;

		[[nodiscard]] std::optional<float> GetShownScale(uintptr_t obj) const;

		// Shown scale divided by the distance factor, which OnRequestedUpdate stored this update as the bake alpha (factor^(1/4) * 255).
		[[nodiscard]] std::optional<float> GetFullSizeScale(uintptr_t obj) const;

		uint8_t AllocateBakeDetour(uintptr_t renderer, uintptr_t obj);
		void PrepareDetour(uintptr_t renderer, float* rect, GameFontSet* set, uintptr_t node);
		void DrawBakedDetour(uintptr_t renderer, uintptr_t node, uintptr_t bake);
	};
}
