#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Fonts/Shaders/PixelShaderSwap.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class EdgeShader {
		PixelShaderSwap m_swap;

		// nullopt for the game's shader.
		std::optional<float> m_softness;

	public:
		EdgeShader() = default;
		EdgeShader(const EdgeShader&) = delete;
		EdgeShader& operator=(const EdgeShader&) = delete;
		~EdgeShader();

		// Game thread. The edge shader draws a round edge of any width unless edges are the game's own 1 px. The game's shader, widened only by
		// claiming a narrower texture (blocky past about 1.5 px), is the fallback. Ensure gets the shader in.
		void Apply(const FontReplacementEdgeConfig& value);

		// Once a frame, as the game takes the shader out when it loads its shaders again, and has none before it first loads them.
		// If the shader can't be made, the game's stays until another value is applied.
		void Ensure();

	private:
		// nullopt restores the game's shader. A shader made for another softness goes.
		void SetSoftness(std::optional<float> value);
	};
}
