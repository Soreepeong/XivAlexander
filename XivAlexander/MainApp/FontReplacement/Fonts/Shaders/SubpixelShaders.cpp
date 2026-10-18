#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Shaders/SubpixelShaders.h"

#include "MainApp/FontReplacement/GameCode.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "resource.h"
#include "Utils/StringUtils.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// The bits of FontSubpixelPS.hlsl's ClearTypeLevel (0.234567891f), Contrast (0.345678912f) and Gamma (0.456789123f), compiled as immediate operands.
	constexpr uint32_t ClearTypeLevelSentinel = 0x3E703291;
	constexpr uint32_t ContrastSentinel = 0x3EB0FCD4;
	constexpr uint32_t GammaSentinel = 0x3EE9E044;

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

FontReplacement::SubpixelShaders::SubpixelShaders()
	: m_passes{{
		{ShaderPass::Font, "FontPS.shcd", IDR_FONTSUBPIXELPS, "FontSubpixelPS.cso"},
		{ShaderPass::FontHighlight, "FontHighlightPS.shcd", IDR_FONTSUBPIXELHIGHLIGHTPS, "FontSubpixelHighlightPS.cso"},
		{ShaderPass::FontEmboss, "FontEmbossPS.shcd", IDR_FONTSUBPIXELPS, "FontSubpixelPS.cso"},
	}} {}

FontReplacement::SubpixelShaders::~SubpixelShaders() {
	m_bindShadersHook.reset();
	m_setDrawStateHook.reset();
	const auto lock = std::scoped_lock(m_mutex);
	RestoreAndReleaseAllInLock();
}

void FontReplacement::SubpixelShaders::Apply(const SettingsRecord& value) {
	if (m_settings == value)
		return;

	if (value.Order != SubpixelOrder::None && !m_bindShadersHook)
		Resolve();

	const auto lock = std::scoped_lock(m_mutex);
	const auto shaderChanged = value.Level != m_settings.Level || value.Contrast != m_settings.Contrast || value.Gamma != m_settings.Gamma;
	m_settings = value;
	m_enabled = value.Order != SubpixelOrder::None;

	// The next BindShaders makes the shaders with the new settings.
	if (!m_enabled || shaderChanged)
		RestoreAndReleaseAllInLock();
}

void FontReplacement::SubpixelShaders::EndFrame() {
	const auto lock = std::scoped_lock(m_mutex);
	m_hasState = false;
}

void FontReplacement::SubpixelShaders::Resolve() {
	Game::Resolved::SubpixelTextFunctions functions;
	if (const auto status = Game::Resolved::SubpixelText.Resolve(functions); status != Game::Signatures::ResolveError::Ok)
		throw std::runtime_error(status.Detail);
	m_textureWidth = static_cast<int>(functions.TextureWidth);
	m_renderingToTexture = static_cast<int>(functions.RenderingToTexture);

	GameUi::ResolveResourceHandles();
	const auto tables = ResolveGameCode(Game::Resolved::Fonts::ShaderTables);
	m_pixelShaderFiles = tables.PixelShaderFiles;
	m_pixelShaders = tables.PixelShaders;

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
	const auto lock = std::scoped_lock(m_mutex);
	try {
		m_server = server;
		m_alternate = alternate != 0;
		m_passActive = false;
		if (m_enabled && !m_alternate) {
			if (const auto p = FindPass(pass))
				m_passActive = EnsureShaderSwap(server, *p);
		}

		// SetDrawState isn't called when only the pass changes.
		if (m_hasState && m_lastServer == server) {
			if (const auto blend = Decide(server, m_lastState, m_lastSubpixelTexture); blend != m_stagedBlend) {
				auto state = m_lastState;
				state.Blend = blend;
				m_setDrawStateHook->Original(server, &state, m_lastDivisor);
				m_stagedBlend = blend;
			}
		}
	} catch (const std::exception& e) {
		// The shaders go back too, as the blend no longer changes.
		Host::Error("Subpixel text failed; turning it off: {}", e.what());
		m_enabled = false;
		RestoreAndReleaseAllInLock();
	}
}

void FontReplacement::SubpixelShaders::SetDrawStateDetour(uintptr_t server, DrawState* state, uint8_t divisor) {
	uint32_t blend;
	{
		const auto lock = std::scoped_lock(m_mutex);
		m_lastServer = server;
		m_lastState = *state;
		m_lastDivisor = divisor;
		m_lastSubpixelTexture = state->Texture && (*reinterpret_cast<const uint32_t*>(state->Texture + m_textureWidth) & 1) != 0;
		m_hasState = true;
		blend = Decide(server, *state, m_lastSubpixelTexture);
		m_stagedBlend = blend;
	}

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
	if (!m_enabled || !m_passActive || m_alternate || !subpixelTexture)
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

bool FontReplacement::SubpixelShaders::EnsureShaderSwap(uintptr_t server, PassShader& p) {
	const auto current = GetShaderObjectOrNull(server, p.Pass, nullptr);
	p.Swap.ForgetIfReplacedElsewhere(current);
	if (!current)
		return false;
	if (p.Swap.IsIn(current))
		return true;

	const auto game = PixelShaderSwap::ShaderOf(current);
	if (!game || !GetShaderObjectOrNull(server, p.Pass, p.File))
		return false;

	if (!p.Swap.PluginShader())
		p.Swap.PluginShader() = CreateShader(game, p);
	p.Swap.Install(current);
	return true;
}

uintptr_t FontReplacement::SubpixelShaders::GetShaderObjectOrNull(uintptr_t server, ShaderPass pass, const char* file) const {
	if (!server)
		return 0;
	const auto index = static_cast<size_t>(pass);
	if (file) {
		const auto handle = *reinterpret_cast<const uintptr_t*>(server + m_pixelShaderFiles + index * 8);
		if (!handle || !Utils::EndsWithI(GameUi::GetFileName(handle), file))
			return 0;
	}
	return *reinterpret_cast<const uintptr_t*>(server + m_pixelShaders + index * 8);
}

ID3D11PixelShaderPtr FontReplacement::SubpixelShaders::CreateShader(ID3D11PixelShader* game, const PassShader& p) const {
	auto code = PixelShaderSwap::ReadShaderCode(p.Code);
	if (!PixelShaderSwap::PatchConstants(code, {{ClearTypeLevelSentinel, m_settings.Level}, {ContrastSentinel, m_settings.Contrast}, {GammaSentinel, m_settings.Gamma}}))
		Host::Warning("Placeholder variables missing or repeated: {}", p.CodeFile);
	return PixelShaderSwap::CreateShader(game, code);
}

void FontReplacement::SubpixelShaders::RestoreAndReleaseAllInLock() {
	for (auto& p : m_passes) {
		if (p.Swap.Target())
			p.Swap.Restore(GetShaderObjectOrNull(m_server, p.Pass, nullptr));
		p.Swap.PluginShader() = nullptr;
	}
	m_passActive = false;
}
