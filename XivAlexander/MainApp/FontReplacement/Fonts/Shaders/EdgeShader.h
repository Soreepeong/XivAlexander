#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Fonts/Shaders/PixelShaderSwap.h"
#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// AtkServer keeps a shader pair per pass, and only LoadShaders remakes them.
	class EdgeShader {
		// Resolved at first use; AtkModule embeds the font manager AtkStage points to.
		int m_moduleFontManager = 0;
		int m_moduleServer = 0;
		int m_pixelShaderFiles = 0;
		int m_pixelShaders = 0;

		PixelShaderSwap m_swap;
		std::optional<Host::Hook<uint8_t, uintptr_t>> m_loadShadersHook;
		float m_softness = 0;
		bool m_used = false;
		std::optional<FontReplacementEdgeConfig> m_applied;

	public:
		EdgeShader() = default;
		EdgeShader(const EdgeShader&) = delete;
		EdgeShader& operator=(const EdgeShader&) = delete;
		~EdgeShader();

		// Game thread. The edge shader draws a round edge of any width unless edges are the game's own 1 px. The game's shader, widened only by
		// claiming a narrower texture (blocky past about 1.5 px), is the fallback. A value applied already, even if it failed, isn't applied again.
		void Apply(const FontReplacementEdgeConfig& value);

	private:
		// nullopt restores the game's shader. Throws if the shader can't be made.
		void SetSoftness(std::optional<float> value);

		[[nodiscard]] uintptr_t GetEdgeShaderObjectOrNull(bool checkFontEdgePs) const;

		bool CreateFromGame();

		void Install();

		void Restore();

		// A UI shader/font reload drops the patched object, so the old object's game shader is ours to release.
		uint8_t LoadShadersDetour(uintptr_t server);
	};
}
