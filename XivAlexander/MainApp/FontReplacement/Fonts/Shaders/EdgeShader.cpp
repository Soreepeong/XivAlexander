#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Shaders/EdgeShader.h"

#include "MainApp/FontReplacement/Host.h"
#include "resource.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// AtkServer's shader pairs are indexed by pass.
	constexpr int EdgePass = 5;

	// The bits of FontEdgePS.hlsl's Softness (0.123456789f), compiled as immediate operands.
	constexpr uint32_t SoftnessSentinel = 0x3DFCD6EA;
}

FontReplacement::EdgeShader::~EdgeShader() {
	SetSoftness(std::nullopt);
}

void FontReplacement::EdgeShader::Apply(const FontReplacementEdgeConfig& value) {
	const auto edge = value.Drawn();
	const auto softness = edge.Enabled && edge != FontReplacementEdgeConfig{.Enabled = true} ? std::optional(edge.Softness) : std::nullopt;
	try {
		if (softness)
			PixelShaderSwap::Resolve();
		SetSoftness(softness);
	} catch (const std::exception& e) {
		Host::Error("Using the edge shader failed; edges are widened with the game's: {}", e.what());
		SetSoftness(std::nullopt);
	}
}

void FontReplacement::EdgeShader::Ensure() {
	if (!m_softness)
		return;
	const auto server = PixelShaderSwap::GetServer();
	if (!server)
		return;
	try {
		m_swap.Ensure(
			PixelShaderSwap::GetShaderObject(server, EdgePass),
			PixelShaderSwap::GetShaderResource(server, EdgePass),
			"FontEdgePS.shcd",
			IDR_FONTEDGEPS,
			"FontEdgePS.cso",
			{{SoftnessSentinel, *m_softness}});
	} catch (const std::exception& e) {
		Host::Error("Using the edge shader failed; edges are widened with the game's: {}", e.what());
		SetSoftness(std::nullopt);
	}
}

void FontReplacement::EdgeShader::SetSoftness(std::optional<float> value) {
	if (value == m_softness)
		return;
	m_swap.Release(PixelShaderSwap::GetShaderObject(PixelShaderSwap::GetServer(), EdgePass));
	m_softness = value;
}
