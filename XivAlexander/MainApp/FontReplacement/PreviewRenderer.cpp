#include "pch.h"
#include "MainApp/FontReplacement/PreviewRenderer.h"

#include "MainApp/FontReplacement/Utilities.h"
#include "resource.h"
#include "XivAlexander.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

_COM_SMARTPTR_TYPEDEF(ID3D11Buffer, __uuidof(ID3D11Buffer));
_COM_SMARTPTR_TYPEDEF(ID3D11ShaderResourceView, __uuidof(ID3D11ShaderResourceView));

namespace {
	// Around each glyph in its channel of the atlas: the texels the edge pass reads past its rectangle are empty.
	constexpr int AtlasPadding = 1;

	// The atlas is at least this wide; the step is relative to its width, whatever it is.
	constexpr int MinAtlasWidth = 256;

	// A vertex as FontPreviewVS.hlsl takes it: FontEdgePS.hlsl's PSInput, with the position in clip space.
	struct Vertex {
		float X, Y;
		float Color[4];
		float Channel[4];
		float U, V;
		float MinU, MinV;
		float MaxU, MaxV;
		float Step[4];
	};

	constexpr D3D11_INPUT_ELEMENT_DESC InputElements[]{
		{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
	};

	std::span<const uint8_t> GetShaderCode(UINT id, const char* name) {
		const auto module = static_cast<HMODULE>(Dll::Module());
		const auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
		if (!resource)
			throw std::runtime_error(std::format("{} isn't embedded.", name));
		return {static_cast<const uint8_t*>(LockResource(LoadResource(module, resource))), SizeofResource(module, resource)};
	}

	std::array<float, 4> ToFloat4(COLORREF color) {
		return {GetRValue(color) / 255.f, GetGValue(color) / 255.f, GetBValue(color) / 255.f, 1.f};
	}

	xivres::util::b8g8r8a8 ToPixel(COLORREF color) {
		return {GetRValue(color), GetGValue(color), GetBValue(color), 0xFF};
	}
}

FontReplacement::PreviewRenderer::PreviewRenderer() {
	// FontEdgePS is shader model 5.0.
	constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
	ThrowOnError(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &m_device, nullptr, &m_context), "Making a WARP device");

	const auto vs = GetShaderCode(IDR_FONTPREVIEWVS, "FontPreviewVS.cso");
	const auto edge = GetShaderCode(IDR_FONTEDGEPS, "FontEdgePS.cso");
	const auto text = GetShaderCode(IDR_FONTPREVIEWTEXTPS, "FontPreviewTextPS.cso");
	ThrowOnError(m_device->CreateVertexShader(vs.data(), vs.size(), nullptr, &m_vertexShader), "Making the preview's vertex shader");
	ThrowOnError(m_device->CreatePixelShader(edge.data(), edge.size(), nullptr, &m_edgeShader), "Making the edge shader");
	ThrowOnError(m_device->CreatePixelShader(text.data(), text.size(), nullptr, &m_textShader), "Making the preview's text shader");
	ThrowOnError(m_device->CreateInputLayout(InputElements, static_cast<UINT>(std::size(InputElements)), vs.data(), vs.size(), &m_inputLayout), "Making the preview's input layout");

	// FontEdgePS blends gathered texels as a linear sampler would: the game's font sampler is one.
	const D3D11_SAMPLER_DESC sampler{
		.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
		.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.MaxLOD = D3D11_FLOAT32_MAX,
	};
	ThrowOnError(m_device->CreateSamplerState(&sampler, &m_sampler), "Making the preview's sampler");

	// The shaders give straight alpha, blended over what is drawn.
	D3D11_BLEND_DESC blend{};
	blend.RenderTarget[0] = {
		.BlendEnable = TRUE,
		.SrcBlend = D3D11_BLEND_SRC_ALPHA,
		.DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
		.BlendOp = D3D11_BLEND_OP_ADD,
		.SrcBlendAlpha = D3D11_BLEND_ONE,
		.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA,
		.BlendOpAlpha = D3D11_BLEND_OP_ADD,
		.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
	};
	ThrowOnError(m_device->CreateBlendState(&blend, &m_blend), "Making the preview's blend state");

	const D3D11_RASTERIZER_DESC rasterizer{
		.FillMode = D3D11_FILL_SOLID,
		.CullMode = D3D11_CULL_NONE,
		.DepthClipEnable = TRUE,
	};
	ThrowOnError(m_device->CreateRasterizerState(&rasterizer, &m_rasterizer), "Making the preview's rasterizer state");
}

FontReplacement::PreviewRenderer::~PreviewRenderer() {
	if (m_context) {
		m_context->ClearState();
		m_context->Flush();
	}
}

void FontReplacement::PreviewRenderer::EnsureTarget(int width, int height) {
	if (m_target && m_targetSize.cx == width && m_targetSize.cy == height)
		return;
	m_targetView = nullptr;
	m_target = nullptr;
	m_readback = nullptr;

	D3D11_TEXTURE2D_DESC desc{
		.Width = static_cast<UINT>(width),
		.Height = static_cast<UINT>(height),
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_B8G8R8A8_UNORM,
		.SampleDesc = {1, 0},
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_RENDER_TARGET,
	};
	ThrowOnError(m_device->CreateTexture2D(&desc, nullptr, &m_target), "Making the preview's image");
	ThrowOnError(m_device->CreateRenderTargetView(m_target, nullptr, &m_targetView), "Making the preview's render target");
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ThrowOnError(m_device->CreateTexture2D(&desc, nullptr, &m_readback), "Making the preview's readback image");
	m_targetSize = {width, height};
}

void FontReplacement::PreviewRenderer::Draw(std::span<const Glyph> glyphs, float radius, const Colors& colors, int width, int height, std::span<xivres::util::b8g8r8a8> pixels) {
	if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * height)
		throw std::invalid_argument("The preview's image is of another size");
	if (glyphs.empty()) {
		std::ranges::fill(pixels, ToPixel(colors.Background));
		return;
	}

	// Glyphs cycle through the four channels, each packed in its own rows, so neighbours overlap in different channels as in the game's atlas.
	auto atlasWidth = MinAtlasWidth;
	for (const auto& glyph : glyphs)
		atlasWidth = (std::max)(atlasWidth, static_cast<int>(std::bit_ceil(static_cast<unsigned>(glyph.Width + 2 * AtlasPadding))));
	struct Shelf {
		int X = 0;
		int Y = 0;
		int Height = 0;
	};
	std::array<Shelf, 4> shelves{};
	std::vector<POINT> places(glyphs.size());
	for (size_t i = 0; i < glyphs.size(); i++) {
		auto& shelf = shelves[i % 4];
		const auto w = glyphs[i].Width + 2 * AtlasPadding;
		const auto h = glyphs[i].Height + 2 * AtlasPadding;
		if (shelf.X + w > atlasWidth) {
			shelf.Y += shelf.Height;
			shelf.X = 0;
			shelf.Height = 0;
		}
		places[i] = {shelf.X + AtlasPadding, shelf.Y + AtlasPadding};
		shelf.X += w;
		shelf.Height = (std::max)(shelf.Height, h);
	}
	auto atlasHeight = 1;
	for (const auto& shelf : shelves)
		atlasHeight = (std::max)(atlasHeight, shelf.Y + shelf.Height);
	if (atlasHeight > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
		throw std::runtime_error("The preview's glyphs don't fit in a texture");

	// R8G8B8A8: channel c of a texel is byte c.
	std::vector<uint8_t> atlas(static_cast<size_t>(atlasWidth) * atlasHeight * 4);
	for (size_t i = 0; i < glyphs.size(); i++) {
		const auto& glyph = glyphs[i];
		const auto channel = i % 4;
		for (auto y = 0; y < glyph.Height; y++) {
			const auto source = glyph.Alpha.data() + static_cast<size_t>(y) * glyph.Width;
			auto target = atlas.data() + ((static_cast<size_t>(places[i].y) + y) * atlasWidth + places[i].x) * 4 + channel;
			for (auto x = 0; x < glyph.Width; x++, target += 4)
				*target = source[x];
		}
	}

	const D3D11_TEXTURE2D_DESC atlasDesc{
		.Width = static_cast<UINT>(atlasWidth),
		.Height = static_cast<UINT>(atlasHeight),
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.SampleDesc = {1, 0},
		.Usage = D3D11_USAGE_IMMUTABLE,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE,
	};
	const D3D11_SUBRESOURCE_DATA atlasData{.pSysMem = atlas.data(), .SysMemPitch = static_cast<UINT>(atlasWidth) * 4};
	ID3D11Texture2DPtr atlasTexture;
	ID3D11ShaderResourceViewPtr atlasView;
	ThrowOnError(m_device->CreateTexture2D(&atlasDesc, &atlasData, &atlasTexture), "Making the preview's atlas");
	ThrowOnError(m_device->CreateShaderResourceView(atlasTexture, nullptr, &atlasView), "Making the preview's atlas view");

	// Step is one texel of the claimed width as FontEdgeVS passes it; the shader scales it by the atlas width into the radius in texels (= pixels).
	const auto step = radius / static_cast<float>(atlasWidth);
	const auto edgeColor = ToFloat4(colors.Edge);
	const auto textColor = ToFloat4(colors.Text);
	std::vector<Vertex> vertices;
	vertices.reserve(glyphs.size() * 12);
	const auto addQuad = [&](const Glyph& glyph, POINT place, size_t channel, int grow, const std::array<float, 4>& color) {
		const auto minU = static_cast<float>(place.x) / static_cast<float>(atlasWidth);
		const auto minV = static_cast<float>(place.y) / static_cast<float>(atlasHeight);
		const auto maxU = static_cast<float>(place.x + glyph.Width) / static_cast<float>(atlasWidth);
		const auto maxV = static_cast<float>(place.y + glyph.Height) / static_cast<float>(atlasHeight);
		const auto corner = [&](int dx, int dy) {
			// dx, dy: the corner's offset from the glyph's top left, in pixels.
			Vertex v{
				.X = static_cast<float>(glyph.X + dx) / static_cast<float>(width) * 2.f - 1.f,
				.Y = 1.f - static_cast<float>(glyph.Y + dy) / static_cast<float>(height) * 2.f,
				.Channel = {},
				.U = static_cast<float>(place.x + dx) / static_cast<float>(atlasWidth),
				.V = static_cast<float>(place.y + dy) / static_cast<float>(atlasHeight),
				.MinU = minU,
				.MinV = minV,
				.MaxU = maxU,
				.MaxV = maxV,
				.Step = {step * 0.5f, step, step * 0.9f, step * -0.9f},
			};
			std::ranges::copy(color, v.Color);
			v.Channel[channel] = 1.f;
			return v;
		};
		const auto topLeft = corner(-grow, -grow);
		const auto topRight = corner(glyph.Width + grow, -grow);
		const auto bottomLeft = corner(-grow, glyph.Height + grow);
		const auto bottomRight = corner(glyph.Width + grow, glyph.Height + grow);
		vertices.insert(vertices.end(), {topLeft, topRight, bottomLeft, topRight, bottomRight, bottomLeft});
	};
	for (size_t i = 0; i < glyphs.size(); i++)
		addQuad(glyphs[i], places[i], i % 4, 1, edgeColor);
	const auto edgeVertexCount = static_cast<UINT>(vertices.size());
	for (size_t i = 0; i < glyphs.size(); i++)
		addQuad(glyphs[i], places[i], i % 4, 0, textColor);

	const D3D11_BUFFER_DESC bufferDesc{
		.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex)),
		.Usage = D3D11_USAGE_IMMUTABLE,
		.BindFlags = D3D11_BIND_VERTEX_BUFFER,
	};
	const D3D11_SUBRESOURCE_DATA bufferData{.pSysMem = vertices.data()};
	ID3D11BufferPtr buffer;
	ThrowOnError(m_device->CreateBuffer(&bufferDesc, &bufferData, &buffer), "Making the preview's vertices");

	EnsureTarget(width, height);
	const auto background = ToFloat4(colors.Background);
	m_context->ClearRenderTargetView(m_targetView, background.data());

	const auto pBuffer = buffer.GetInterfacePtr();
	const UINT stride = sizeof(Vertex);
	const UINT offset = 0;
	const auto pTargetView = m_targetView.GetInterfacePtr();
	const auto pAtlasView = atlasView.GetInterfacePtr();
	const auto pSampler = m_sampler.GetInterfacePtr();
	const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
	m_context->IASetInputLayout(m_inputLayout);
	m_context->IASetVertexBuffers(0, 1, &pBuffer, &stride, &offset);
	m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_context->VSSetShader(m_vertexShader, nullptr, 0);
	m_context->PSSetShaderResources(0, 1, &pAtlasView);
	m_context->PSSetSamplers(0, 1, &pSampler);
	m_context->RSSetState(m_rasterizer);
	m_context->RSSetViewports(1, &viewport);
	m_context->OMSetRenderTargets(1, &pTargetView, nullptr);
	m_context->OMSetBlendState(m_blend, nullptr, 0xFFFFFFFF);

	m_context->PSSetShader(m_edgeShader, nullptr, 0);
	m_context->Draw(edgeVertexCount, 0);
	m_context->PSSetShader(m_textShader, nullptr, 0);
	m_context->Draw(static_cast<UINT>(vertices.size()) - edgeVertexCount, edgeVertexCount);

	m_context->CopyResource(m_readback, m_target);
	D3D11_MAPPED_SUBRESOURCE mapped{};
	ThrowOnError(m_context->Map(m_readback, 0, D3D11_MAP_READ, 0, &mapped), "Reading the preview back");
	for (auto y = 0; y < height; y++) {
		const auto source = reinterpret_cast<const xivres::util::b8g8r8a8*>(static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch);
		std::copy_n(source, width, pixels.data() + static_cast<size_t>(y) * width);
	}
	m_context->Unmap(m_readback, 0);

	// Opaque, whatever blending left in alpha.
	for (auto& pixel : pixels)
		pixel.A = 0xFF;
}

void FontReplacement::PreviewRenderer::DrawWithoutEdge(std::span<const Glyph> glyphs, const Colors& colors, int width, int height, std::span<xivres::util::b8g8r8a8> pixels) {
	if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * height)
		throw std::invalid_argument("The preview's image is of another size");
	std::ranges::fill(pixels, ToPixel(colors.Background));
	const auto text = ToPixel(colors.Text);
	for (const auto& glyph : glyphs) {
		for (auto y = (std::max)(0, -glyph.Y); y < glyph.Height && glyph.Y + y < height; y++) {
			for (auto x = (std::max)(0, -glyph.X); x < glyph.Width && glyph.X + x < width; x++) {
				const auto a = glyph.Alpha[static_cast<size_t>(y) * glyph.Width + x];
				auto& p = pixels[static_cast<size_t>(glyph.Y + y) * width + glyph.X + x];
				p.R = static_cast<uint8_t>((p.R * (255 - a) + text.R * a) / 255);
				p.G = static_cast<uint8_t>((p.G * (255 - a) + text.G * a) / 255);
				p.B = static_cast<uint8_t>((p.B * (255 - a) + text.B * a) / 255);
			}
		}
	}
}
