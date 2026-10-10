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
	// Draws the settings' font previews with the edge the game draws, with the replacement's edge shader (FontEdgePS.hlsl,
	// IDR_FONTEDGEPS) itself rather than an imitation of it, so that the edge settings can be judged from the preview.
	//
	// The glyphs' coverage goes into an atlas as the game's does: each glyph in one channel, with an empty texel around it.
	// The edge pass draws each glyph's rectangle and one pixel around it, as the game's renderer does (the replacement gives
	// glyphs margins for wider edges); the text pass then draws the glyphs over the edges. Pixels map to texels 1:1.
	//
	// The device is WARP's, Direct3D's software rasterizer, so that nothing depends on the GPU or on the game's device; it
	// is made, used and released on the thread that makes this object (the previewer's), and on no other.
	class PreviewRenderer {
	public:
		// A glyph's coverage at its place in the image: the rectangle the edge is kept within.
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

		// The image drawn into, and its copy that the CPU reads; made again when the size changes.
		SIZE m_targetSize{};
		ID3D11Texture2DPtr m_target;
		ID3D11RenderTargetViewPtr m_targetView;
		ID3D11Texture2DPtr m_readback;

	public:
		// Makes the device and the shaders. Throws if they can't be made.
		PreviewRenderer();
		PreviewRenderer(const PreviewRenderer&) = delete;
		PreviewRenderer& operator=(const PreviewRenderer&) = delete;
		~PreviewRenderer();

		// Draws the glyphs with an edge of a radius in pixels (FontEdgePS's: one texel of the claimed texture width, in the
		// atlas's texels) on the background, into an image of width x height pixels, rows from the top.
		void Draw(std::span<const Glyph> glyphs, float radius, const Colors& colors, int width, int height, std::span<xivres::util::b8g8r8a8> pixels);

		// Draws the glyphs without an edge on the CPU, for when the device can't be made.
		static void DrawWithoutEdge(std::span<const Glyph> glyphs, const Colors& colors, int width, int height, std::span<xivres::util::b8g8r8a8> pixels);

	private:
		void EnsureTarget(int width, int height);
	};
}
