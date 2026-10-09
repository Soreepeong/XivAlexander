#pragma once

#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Puts a pixel shader of FFXIV-FontChanger's (FontEdgePS.hlsl) in place of the game's FontEdgePS, for a round edge
	// outline of any width, set per font by the texture width it claims.
	//
	// The UI's font shaders are nine vertex/pixel shader pairs (AtkServer.LoadShaders) in the AtkServer of AtkModule: shader
	// file resource handles (VS, PS), and the Kernel shaders made from them, indexed by a draw command's pass (4 text, 5
	// edge, 6 glare, 7 highlight, 8 emboss). A Kernel::PixelShader binds its ID3D11PixelShader (ImmediateContextDX11.
	// SetPixelShader), which is swapped here; the replacement has the same input signature and bindings (t0, s0), so the
	// shader's binding list stays valid. AtkModule embeds the font manager AtkStage points to. The game makes its shaders
	// again only in LoadShaders, which puts this one in place again.
	class EdgeShader {
		// The edge pass, and the file name of its pixel shader; AtkModule: its font manager (embedded) and AtkServer*;
		// AtkServer: its PS file resource handles and Kernel pixel shaders; Kernel::PixelShader: its ID3D11PixelShader.
		// Resolved at the first use.
		bool m_resolved = false;
		int m_edgePass = 0;
		std::string m_shaderFile;
		int m_moduleFontManager = 0;
		int m_moduleServer = 0;
		int m_pixelShaderFiles = 0;
		int m_pixelShaders = 0;
		int m_d3dShader = 0;

		std::optional<Host::Hook<uint8_t, uintptr_t>> m_loadShadersHook;
		ID3D11PixelShaderPtr m_shader;
		bool m_used = false;

		// The Kernel::PixelShader patched, and the game's ID3D11PixelShader it had (the reference its slot held, raw: it goes
		// back to the slot).
		uintptr_t m_patchedObject = 0;
		ID3D11PixelShader* m_original = nullptr;

	public:
		EdgeShader() = default;
		EdgeShader(const EdgeShader&) = delete;
		EdgeShader& operator=(const EdgeShader&) = delete;
		~EdgeShader();

		// Uses this shader (its radius comes from each font's claimed texture width), or the game's. Game thread. Throws if
		// the shader can't be made.
		void Set(bool use);

	private:
		// Puts the shader in place, if it isn't.
		void Install();

		// Gets the Kernel::PixelShader of the edge pass, if check only if it is made from FontEdgePS; 0 if it isn't there.
		uintptr_t GetEdgeShaderObject(bool check) const;

		// The game made its shaders again (a reload of the UI's shaders and fonts), letting go of the patched object: this
		// shader goes in the new one. The game's shader the old one had is this one's to release.
		uint8_t LoadShadersDetour(uintptr_t server);

		// Makes the shader on the device of the game's; its bytecode is compiled at build time.
		static ID3D11PixelShaderPtr Create(ID3D11PixelShader* game);

		// Gives the game its shader back, if the object still binds this one.
		void Restore();
	};
}
