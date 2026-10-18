#pragma once

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

	public:
		EdgeShader() = default;
		EdgeShader(const EdgeShader&) = delete;
		EdgeShader& operator=(const EdgeShader&) = delete;
		~EdgeShader();

		// Game thread; nullopt restores the game's shader, and it throws if the shader can't be made.
		void SetSoftness(std::optional<float> value);

	private:
		[[nodiscard]] uintptr_t GetEdgeShaderObjectOrNull(bool checkFontEdgePs) const;

		bool CreateFromGame();

		void Install();

		void Restore();

		// A UI shader/font reload drops the patched object, so the old object's game shader is ours to release.
		uint8_t LoadShadersDetour(uintptr_t server);
	};
}
