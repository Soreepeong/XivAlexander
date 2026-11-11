#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Shaders/SubpixelShaders.h"

#include "MainApp/FontReplacement/Fonts/FrameState.h"
#include "MainApp/FontReplacement/GameCode.h"
#include "resource.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// The bits of FontSubpixelPS.hlsl's ClearTypeLevel (0.234567891f), Contrast (0.345678912f), Gamma (0.456789123f) and GrayscaleContrast
	// (0.567891234f), compiled as immediate operands.
	constexpr uint32_t ClearTypeLevelSentinel = 0x3E703291;
	constexpr uint32_t ContrastSentinel = 0x3EB0FCD4;
	constexpr uint32_t GammaSentinel = 0x3EE9E044;
	constexpr uint32_t GrayscaleContrastSentinel = 0x3F116152;

	// PackedBlendStateDesc fields hold D3D11_BLEND minus one.
	constexpr int SrcBlendShift = 4;
	constexpr int DestBlendShift = 8;
	constexpr uint32_t BlendMask = 0xF;

	uint32_t GetFactor(uint32_t blend, int shift) {
		return ((blend >> shift) & BlendMask) + 1;
	}

	uint32_t SetFactor(uint32_t blend, int shift, uint32_t factor) {
		return (blend & ~(BlendMask << shift)) | ((factor - 1) << shift);
	}

	std::string DescribeBlendOp(uint32_t op) {
		switch (op) {
			case D3D11_BLEND_OP_ADD: return "ADD";
			case D3D11_BLEND_OP_SUBTRACT: return "SUBTRACT";
			case D3D11_BLEND_OP_REV_SUBTRACT: return "REV_SUBTRACT";
			case D3D11_BLEND_OP_MIN: return "MIN";
			case D3D11_BLEND_OP_MAX: return "MAX";
			default: return std::format("unknown(0x{:08X})", op);
		}
	}

	std::string DescribeBlend(uint32_t blend) {
		switch (blend) {
			case D3D11_BLEND_ZERO: return "ZERO";
			case D3D11_BLEND_ONE: return "ONE";
			case D3D11_BLEND_SRC_COLOR: return "SRC_COLOR";
			case D3D11_BLEND_INV_SRC_COLOR: return "INV_SRC_COLOR";
			case D3D11_BLEND_SRC_ALPHA: return "SRC_ALPHA";
			case D3D11_BLEND_INV_SRC_ALPHA: return "INV_SRC_ALPHA";
			case D3D11_BLEND_DEST_ALPHA: return "DEST_ALPHA";
			case D3D11_BLEND_INV_DEST_ALPHA: return "INV_DEST_ALPHA";
			case D3D11_BLEND_DEST_COLOR: return "DEST_COLOR";
			case D3D11_BLEND_INV_DEST_COLOR: return "INV_DEST_COLOR";
			case D3D11_BLEND_SRC_ALPHA_SAT: return "SRC_ALPHA_SAT";
			case D3D11_BLEND_BLEND_FACTOR: return "BLEND_FACTOR";
			case D3D11_BLEND_INV_BLEND_FACTOR: return "INV_BLEND_FACTOR";
			case D3D11_BLEND_SRC1_COLOR: return "SRC1_COLOR";
			case D3D11_BLEND_INV_SRC1_COLOR: return "INV_SRC1_COLOR";
			case D3D11_BLEND_SRC1_ALPHA: return "SRC1_ALPHA";
			case D3D11_BLEND_INV_SRC1_ALPHA: return "INV_SRC1_ALPHA";
			default: return std::format("unknown(0x{:08X})", blend);
		}
	}

	std::string Describe(uint32_t blend) {
		// PackedBlendStateDesc: enable bit 0, op 1-3, src 4-7, dest 8-11, alpha op 12-14, alpha src 15-18, alpha dest 19-22; fields hold values minus one.
		const auto field = [blend](int shift, int bits) { return ((blend >> shift) & ((1u << bits) - 1)) + 1; };
		return std::format(
			"BlendState({}, op={}(src={}, dest={}), alpha={}(src={}, dest={}))",
			blend & 1 ? "enabled" : "disabled",
			DescribeBlendOp(field(1, 3)),
			DescribeBlend(field(4, 4)),
			DescribeBlend(field(8, 4)),
			DescribeBlendOp(field(12, 3)),
			DescribeBlend(field(15, 4)),
			DescribeBlend(field(19, 4)));
	}
}

FontReplacement::SubpixelShaders::SubpixelShaders(FrameState& state)
	: m_state(state)
	, m_passes{{
		{ShaderPass::Font, "FontPS.shcd", IDR_FONTSUBPIXELPS, "FontSubpixelPS.cso"},
		{ShaderPass::FontHighlight, "FontHighlightPS.shcd", IDR_FONTSUBPIXELHIGHLIGHTPS, "FontSubpixelHighlightPS.cso"},
		{ShaderPass::FontEmboss, "FontEmbossPS.shcd", IDR_FONTSUBPIXELPS, "FontSubpixelPS.cso"},
	}} {}

FontReplacement::SubpixelShaders::~SubpixelShaders() {
	m_bindShadersHook.reset();
	m_setDrawStateHook.reset();
	RestoreAndReleaseAll();
}

void FontReplacement::SubpixelShaders::TryApply(const SettingsRecord& value) {
	try {
		Apply(value);
	} catch (const std::exception& e) {
		Host::Error("Subpixel text is off: {}", e.what());
		Apply({});
	}
}

void FontReplacement::SubpixelShaders::Ensure() {
	const auto server = Enabled() ? PixelShaderSwap::GetServer() : 0;
	if (!server)
		return;
	try {
		const auto& s = m_settings;
		for (auto& p : m_passes) {
			p.Swap.Ensure(
				PixelShaderSwap::GetShaderObject(server, static_cast<int>(p.Pass)),
				PixelShaderSwap::GetShaderResource(server, static_cast<int>(p.Pass)),
				p.File,
				p.Code,
				p.CodeFile,
				{
					{ClearTypeLevelSentinel, s.Level},
					{ContrastSentinel, s.Contrast},
					{GammaSentinel, s.Gamma},
					{GrayscaleContrastSentinel, s.GrayscaleContrast},
				});
		}
	} catch (const std::exception& e) {
		Fail(e);
	}
}

void FontReplacement::SubpixelShaders::Apply(const SettingsRecord& value) {
	if (m_settings == value)
		return;

	if (value.Order != SubpixelOrder::None && !m_bindShadersHook)
		Resolve();

	auto ordered = value;
	ordered.Order = m_settings.Order;
	const auto shaderChanged = ordered != m_settings;
	m_settings = value;

	// Ensure makes the shaders again with the new settings.
	if (!Enabled() || shaderChanged)
		RestoreAndReleaseAll();
}

void FontReplacement::SubpixelShaders::Resolve() {
	Game::Resolved::SubpixelTextFunctions functions;
	if (const auto status = Game::Resolved::SubpixelText.Resolve(functions); status != Game::Signatures::ResolveError::Ok)
		throw std::runtime_error(status.Detail);
	m_textureWidth = static_cast<int>(functions.TextureWidth);
	m_renderingToTexture = static_cast<int>(functions.RenderingToTexture);

	PixelShaderSwap::Resolve();
	const auto tables = ResolveGameCode(Game::Resolved::Fonts::ShaderTables);

	m_setDrawStateHook.emplace("AtkServer::SetDrawState", reinterpret_cast<uintptr_t>(functions.SetDrawState), [this](uintptr_t server, DrawState* state, uint8_t divisor) { SetDrawStateDetour(server, state, divisor); });
	m_bindShadersHook.emplace("AtkServer::BindShaders", reinterpret_cast<uintptr_t>(tables.BindFontShaders), [this](uintptr_t server, int pass, uint8_t alternate) { BindShadersDetour(server, pass, alternate); });
}

FontReplacement::SubpixelShaders::PassShader* FontReplacement::SubpixelShaders::FindPass(int pass) {
	for (auto& p : m_passes) {
		if (static_cast<int>(p.Pass) == pass)
			return &p;
	}
	return nullptr;
}

void FontReplacement::SubpixelShaders::BindShadersDetour(uintptr_t server, int pass, uint8_t alternate) {
	m_bindShadersHook->Original(server, pass, alternate);
	if (!Enabled())
		return;
	try {
		// The blend is only changed for a pass whose shader is ours, which Ensure gets in.
		m_alternate = alternate != 0;
		const auto p = m_alternate ? nullptr : FindPass(pass);
		m_passActive = p && p->Swap.IsIn(PixelShaderSwap::GetShaderObject(server, pass));

		// SetDrawState isn't called when only the pass changes.
		if (auto& last = m_last; last.Has && last.Server == server) {
			if (const auto blend = Decide(server, last.State, last.SubpixelTexture); blend != last.Staged) {
				auto state = last.State;
				state.Blend = blend;
				m_setDrawStateHook->Original(server, &state, last.Divisor);
				last.Staged = blend;
			}
		}
	} catch (const std::exception& e) {
		Fail(e);
	}
}

void FontReplacement::SubpixelShaders::Fail(const std::exception& e) {
	m_settings = {};
	RestoreAndReleaseAll();
	m_state.Fail(Part::Subpixel, "Subpixel text failed", e);
}

void FontReplacement::SubpixelShaders::SetDrawStateDetour(uintptr_t server, DrawState* state, uint8_t divisor) {
	if (!Enabled()) {
		m_setDrawStateHook->Original(server, state, divisor);
		return;
	}

	auto& last = m_last;
	last.Server = server;
	last.State = *state;
	last.Divisor = divisor;
	last.SubpixelTexture = state->Texture && (*reinterpret_cast<const uint32_t*>(state->Texture + m_textureWidth) & 1) != 0;
	last.Has = true;
	const auto blend = Decide(server, *state, last.SubpixelTexture);
	last.Staged = blend;
	if (blend == state->Blend) {
		m_setDrawStateHook->Original(server, state, divisor);
		return;
	}

	// The caller batches later commands by comparing them with its state.
	const auto original = state->Blend;
	state->Blend = blend;
	m_setDrawStateHook->Original(server, state, divisor);
	state->Blend = original;
}

uint32_t FontReplacement::SubpixelShaders::Decide(uintptr_t server, const DrawState& state, bool subpixelTexture) {
	if (!Enabled() || !m_passActive || m_alternate || !subpixelTexture)
		return state.Blend;

	if (GetFactor(state.Blend, SrcBlendShift) != D3D11_BLEND_SRC_ALPHA) {
		if (!m_warnedBlend) {
			m_warnedBlend = true;
			Host::Warning("Subpixel text: a glyph is drawn with {}, which can't take subpixel coverage", Describe(state.Blend));
		}
		return state.Blend;
	}

	auto blend = SetFactor(state.Blend, SrcBlendShift, D3D11_BLEND_SRC1_COLOR);

	// A render texture's alpha is composited later, so its colour can't take coverage per channel.
	const auto toTexture = *reinterpret_cast<const uint8_t*>(server + m_renderingToTexture) != 0;
	if (!toTexture && GetFactor(blend, DestBlendShift) == D3D11_BLEND_INV_SRC_ALPHA)
		blend = SetFactor(blend, DestBlendShift, D3D11_BLEND_INV_SRC_COLOR);
	return blend;
}

void FontReplacement::SubpixelShaders::RestoreAndReleaseAll() {
	const auto server = PixelShaderSwap::GetServer();
	for (auto& p : m_passes)
		p.Swap.Release(PixelShaderSwap::GetShaderObject(server, static_cast<int>(p.Pass)));
	m_passActive = false;
}
