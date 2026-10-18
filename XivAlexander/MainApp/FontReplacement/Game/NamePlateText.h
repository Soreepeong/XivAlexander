#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontReplacer;
	struct GameFontSet;

	// The game bakes each plate's text once per change at the text node's unscaled size and resamples it at every distance, so this
	// bakes it at its on-screen size at 100 percent instead.
	class NamePlateText {
		FontReplacer& m_replacer;

		// Keyed by the object's BakeData, which is its first member.
		std::map<uintptr_t, float> m_bakeScales;

		int m_addonBakePlate = 0;
		int m_addonObjects = 0;
		int m_objectSize = 0;
		int m_objectCount = 0;

		int m_objectNameText = 0;
		int m_objectTextW = 0;
		int m_objectTextH = 0;
		int m_objectNeedsToBeBaked = 0;
		int m_bakeTextYOffset = 0;
		int m_bakeAlpha = 0;

		int m_rendererCurrentBakeData = 0;
		int m_rendererFixedFontResolution = 0;

		// The transform is a 2x2 matrix of floats, row by row.
		int m_nodeTransform = 0;
		int m_nodeWidth = 0;
		int m_nodeHeight = 0;
		int m_nodeParent = 0;
		int m_nodeScaleX = 0;

		uintptr_t m_lastRenderer = 0;
		bool m_rebakePending = false;

		FontReplacementNamePlateMode m_mode = FontReplacementNamePlateMode::BakedAtFullSize;

		std::optional<Host::Hook<uint8_t, uintptr_t, uintptr_t>> m_allocateBakeHook;
		std::optional<Host::Hook<void, uintptr_t, float*, GameFontSet*, uintptr_t>> m_prepareHook;
		std::optional<Host::Hook<void, uintptr_t, uintptr_t, uintptr_t>> m_drawBakedHook;

		// Null if the game's draw isn't found; the live mode then draws as the baked one.
		std::optional<Host::Hook<void, uintptr_t, uintptr_t>> m_bakePlateDrawHook;

	public:
		explicit NamePlateText(FontReplacer& replacer);
		NamePlateText(const NamePlateText&) = delete;
		NamePlateText& operator=(const NamePlateText&) = delete;
		~NamePlateText();

		// The plates are reachable only through the renderer a hook last saw, which is embedded in the NamePlate addon.
		void ForceRebake();

		// Game thread.
		void SetMode(FontReplacementNamePlateMode mode);

	private:
		[[nodiscard]] FontReplacementNamePlateMode GetEffectiveMode() const;

		// Rebakes requested while no renderer was reachable wait for this, as the plates are reachable only through one.
		void SeeRenderer(uintptr_t renderer);

		[[nodiscard]] bool NeedsToBeBaked(uintptr_t obj) const;
		void SetNeedsToBeBaked(uintptr_t obj) const;
		[[nodiscard]] float& Transform(uintptr_t node, int index) const;

		// OnRequestedUpdate scaled the plate by this update's distance factor, which it stored as the bake alpha, factor^(1/4) * 255.
		[[nodiscard]] std::optional<float> GetFullSizeScale(uintptr_t obj) const;

		uint8_t AllocateBakeDetour(uintptr_t renderer, uintptr_t obj);
		void PrepareDetour(uintptr_t renderer, float* rect, GameFontSet* set, uintptr_t node);
		void DrawBakedDetour(uintptr_t renderer, uintptr_t node, uintptr_t bake);
		void BakePlateDrawDetour(uintptr_t renderer, uintptr_t node);
	};
}
