#pragma once

#include <xivres/util.pixel_formats.h>

#include "MainApp/FontReplacement/Host.h"

_COM_SMARTPTR_TYPEDEF(ID3D11BlendState, __uuidof(ID3D11BlendState));
_COM_SMARTPTR_TYPEDEF(ID3D11InputLayout, __uuidof(ID3D11InputLayout));
_COM_SMARTPTR_TYPEDEF(ID3D11RasterizerState, __uuidof(ID3D11RasterizerState));
_COM_SMARTPTR_TYPEDEF(ID3D11RenderTargetView, __uuidof(ID3D11RenderTargetView));
_COM_SMARTPTR_TYPEDEF(ID3D11SamplerState, __uuidof(ID3D11SamplerState));
_COM_SMARTPTR_TYPEDEF(ID3D11VertexShader, __uuidof(ID3D11VertexShader));

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Font previews drawn with the real edge shader (FontEdgePS.hlsl) and a game-like atlas (one channel per glyph, 1-texel padding), pixels 1:1 to texels.
	// Uses WARP so nothing depends on the GPU or the game's device; made, used and released only on the constructing (previewer) thread.
	class PreviewRenderer {
	public:
		// Coverage at its place in the image; the edge is kept within this rectangle.
		struct Glyph {
			int X = 0;
			int Y = 0;
			int Width = 0;
			int Height = 0;
			std::vector<uint8_t> Alpha;
		};

		struct Colors {
			COLORREF Text = 0;
			COLORREF Edge = 0;
			COLORREF Background = 0;
		};

	private:
		ID3D11DevicePtr m_device;
		ID3D11DeviceContextPtr m_context;
		ID3D11VertexShaderPtr m_vertexShader;
		ID3D11PixelShaderPtr m_edgeShader;
		ID3D11PixelShaderPtr m_textShader;
		ID3D11InputLayoutPtr m_inputLayout;
		ID3D11SamplerStatePtr m_sampler;
		ID3D11BlendStatePtr m_blend;
		ID3D11RasterizerStatePtr m_rasterizer;

		// Remade when the size changes.
		SIZE m_targetSize{};
		ID3D11Texture2DPtr m_target;
		ID3D11RenderTargetViewPtr m_targetView;
		ID3D11Texture2DPtr m_readback;

	public:
		PreviewRenderer();
		PreviewRenderer(const PreviewRenderer&) = delete;
		PreviewRenderer& operator=(const PreviewRenderer&) = delete;
		~PreviewRenderer();

		// radius is in pixels (FontEdgePS's one texel of the claimed texture width, in atlas texels); pixels are rows from the top.
		void Draw(std::span<const Glyph> glyphs, float radius, const Colors& colors, int width, int height, std::span<xivres::util::b8g8r8a8> pixels);

		// CPU fallback for when the device can't be made.
		static void DrawWithoutEdge(std::span<const Glyph> glyphs, const Colors& colors, int width, int height, std::span<xivres::util::b8g8r8a8> pixels);

	private:
		void EnsureTarget(int width, int height);
	};
}
