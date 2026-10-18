#pragma once

#include "MainApp/FontReplacement/Fonts/Shaders/PixelShaderSwap.h"
#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Swaps FFXIV-FontChanger's FontEdgePS.hlsl (round edge of any width, per font by its claimed texture width) into the edge pass's Kernel::PixelShader.
	// AtkServer keeps a shader pair per pass; only LoadShaders remakes them.
	class EdgeShader {
	public:
		// The bits of FontEdgePS.hlsl's Softness (0.123456789f), compiled as immediate operands.
		static constexpr uint32_t Sentinel = 0x3DFCD6EA;

		static constexpr float MaxSoftness = 8;

	private:
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

		// The plugin's edge shader with a softness, or (nullopt) the game's. Game thread; throws if the shader can't be made.
		void SetSoftness(std::optional<float> value);

	private:
		// With checkFontEdgePs, only if it is made from FontEdgePS; 0 if absent.
		[[nodiscard]] uintptr_t GetEdgeShaderObjectOrNull(bool checkFontEdgePs) const;

		// False while the game hasn't loaded its edge shader.
		bool CreateFromGame();

		void Install();

		void Restore();

		// A UI shader/font reload drops the patched object, so the old object's game shader is ours to release.
		uint8_t LoadShadersDetour(uintptr_t server);
	};
}
