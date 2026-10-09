#pragma once

#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Swaps FFXIV-FontChanger's FontEdgePS.hlsl (round edge of any width, per font by its claimed texture width) into the edge pass's Kernel::PixelShader.
	// AtkServer keeps a shader pair per pass; only LoadShaders remakes them.
	class EdgeShader {
		// Resolved at first use; AtkModule embeds the font manager AtkStage points to.
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

		// m_original is the reference the slot held, kept raw because it goes back into the slot.
		uintptr_t m_patchedObject = 0;
		ID3D11PixelShader* m_original = nullptr;

	public:
		EdgeShader() = default;
		EdgeShader(const EdgeShader&) = delete;
		EdgeShader& operator=(const EdgeShader&) = delete;
		~EdgeShader();

		// Game thread. Throws if the shader can't be made.
		void Set(bool use);

	private:
		void Install();

		// With check, only if it is made from FontEdgePS; 0 if absent.
		uintptr_t GetEdgeShaderObject(bool check) const;

		// A UI shader/font reload drops the patched object, so the old object's game shader is ours to release.
		uint8_t LoadShadersDetour(uintptr_t server);

		static ID3D11PixelShaderPtr Create(ID3D11PixelShader* game);

		void Restore();
	};
}
