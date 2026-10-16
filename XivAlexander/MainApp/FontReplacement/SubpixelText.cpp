#include "pch.h"
#include "MainApp/FontReplacement/SubpixelText.h"

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/EdgeShaderCode.h"
#include "MainApp/FontReplacement/GameCode.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Utilities.h"
#include "resource.h"
#include "Utils/StringUtils.h"
#include "XivAlexander.h"

_COM_SMARTPTR_TYPEDEF(IDWriteFactory, __uuidof(IDWriteFactory));
_COM_SMARTPTR_TYPEDEF(IDWriteRenderingParams, __uuidof(IDWriteRenderingParams));

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// FontSubpixelPS.hlsl's ClearTypeLevel, Contrast and Gamma, as float bits.
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

	std::vector<uint8_t> ReadShaderCode(int resourceId) {
		const auto module = static_cast<HMODULE>(Dll::Module());
		const auto resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
		if (!resource)
			throw std::runtime_error("FontSubpixelPS.cso isn't embedded.");
		const auto begin = static_cast<const uint8_t*>(LockResource(LoadResource(module, resource)));
		return {begin, begin + SizeofResource(module, resource)};
	}
}

FontReplacement::SubpixelSettings FontReplacement::SubpixelSettings::OfWindows(HWND window) {
	SubpixelSettings res;
	auto geometry = DWRITE_PIXEL_GEOMETRY_RGB;
	try {
		IDWriteFactoryPtr factory;
		ThrowOnError(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)), "DWriteCreateFactory");
		IDWriteRenderingParamsPtr params;
		const auto monitor = window ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : nullptr;
		if (monitor)
			ThrowOnError(factory->CreateMonitorRenderingParams(monitor, &params), "CreateMonitorRenderingParams");
		else
			ThrowOnError(factory->CreateRenderingParams(&params), "CreateRenderingParams");
		geometry = params->GetPixelGeometry();
		res.Level = std::clamp(params->GetClearTypeLevel(), 0.f, 1.f);
		res.Contrast = (std::max)(params->GetEnhancedContrast(), 0.f);
		res.Gamma = (std::max)(params->GetGamma(), 1.f);
	} catch (const std::exception& e) {
		Host::Warning("Windows' ClearType settings couldn't be read; using the defaults: {}", e.what());
	}
	res.Order = geometry == DWRITE_PIXEL_GEOMETRY_RGB ? SubpixelOrder::Rgb : geometry == DWRITE_PIXEL_GEOMETRY_BGR ? SubpixelOrder::Bgr : SubpixelOrder::None;
	return res;
}

FontReplacement::SubpixelSettings FontReplacement::SubpixelSettings::Of(FontReplacementSubpixelMode mode, const FontReplacementClearTypeConfig& clearType, FontReplacementVerticalSubpixelRenderer vertical, HWND window) {
	if (mode == FontReplacementSubpixelMode::Off)
		return {};

	auto res = OfWindows(window);
	res.VerticalWithFreeType = vertical == FontReplacementVerticalSubpixelRenderer::FreeType;
	if (mode == FontReplacementSubpixelMode::System) {
		UINT smoothing = 0, type = 0;
		if (!SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0) || !smoothing
			|| !SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0) || type != FE_FONTSMOOTHINGCLEARTYPE)
			res.Order = SubpixelOrder::None;
		return res;
	}

	switch (mode) {
		case FontReplacementSubpixelMode::Bgr:
			res.Order = SubpixelOrder::Bgr;
			break;
		case FontReplacementSubpixelMode::VerticalRgb:
			res.Order = SubpixelOrder::VerticalRgb;
			break;
		case FontReplacementSubpixelMode::VerticalBgr:
			res.Order = SubpixelOrder::VerticalBgr;
			break;
		default:
			res.Order = SubpixelOrder::Rgb;
	}
	res.Level = clearType.Level;
	res.Contrast = clearType.Contrast;
	res.Gamma = clearType.Gamma;
	return res;
}

FontReplacement::SubpixelText::~SubpixelText() {
	m_bindShadersHook.reset();
	m_setDrawStateHook.reset();
	const auto lock = std::scoped_lock(m_mutex);
	RestoreAll();
}

void FontReplacement::SubpixelText::Set(const SubpixelSettings& settings) {
	if (settings.Order != SubpixelOrder::None && !m_resolved)
		Resolve();

	const auto lock = std::scoped_lock(m_mutex);
	const auto shaderChanged = settings.Level != m_settings.Level || settings.Contrast != m_settings.Contrast || settings.Gamma != m_settings.Gamma;
	m_settings = settings;
	m_enabled = settings.Order != SubpixelOrder::None;

	// Shaders go in (again) at the next BindShaders of their pass.
	if (!m_enabled || shaderChanged) {
		RestoreAll();
		for (auto& pass : m_passes)
			pass.Shader = nullptr;
	}
}

void FontReplacement::SubpixelText::EndFrame() {
	const auto lock = std::scoped_lock(m_mutex);
	m_hasState = false;
}

void FontReplacement::SubpixelText::Resolve() {
	Game::Resolved::SubpixelTextFunctions functions;
	if (const auto status = Game::Resolved::SubpixelText.Resolve(functions); status != Game::Signatures::ResolveError::Ok)
		throw std::runtime_error(status.Detail);
	m_textureWidth = static_cast<int>(functions.TextureWidth);
	m_renderingToTexture = static_cast<int>(functions.RenderingToTexture);

	GameUi::ResolveResourceHandles();
	const auto tables = ResolveGameCode(Game::Resolved::Fonts::ShaderTables);
	const auto bindShaders = reinterpret_cast<uintptr_t>(tables.BindFontShaders);
	m_pixelShaderFiles = tables.PixelShaderFiles;
	m_pixelShaders = tables.PixelShaders;
	m_d3dShader = tables.D3DShader;
	const auto setDrawState = reinterpret_cast<uintptr_t>(functions.SetDrawState);

	// AtkServer's font shader pairs: 4 text, 5 edge, 6 glare, 7 highlight, 8 emboss (the drop shadow, FontPS's bytecode).
	std::vector<PassShader> passes;
	passes.push_back({.Pass = 4, .File = "FontPS.shcd"});
	passes.push_back({.Pass = 7, .File = "FontHighlightPS.shcd", .Highlight = true});
	passes.push_back({.Pass = 8, .File = "FontEmbossPS.shcd"});

	{
		const auto lock = std::scoped_lock(m_mutex);
		m_passes = std::move(passes);
	}
	m_setDrawStateHook.emplace("AtkServer::SetDrawState", setDrawState, [this](uintptr_t server, DrawState* state, uint8_t divisor) { SetDrawStateDetour(server, state, divisor); });
	m_bindShadersHook.emplace("AtkServer::BindShaders", bindShaders, [this](uintptr_t server, int pass, uint8_t alternate) { BindShadersDetour(server, pass, alternate); });
	m_resolved = true;
}

void FontReplacement::SubpixelText::BindShadersDetour(uintptr_t server, int pass, uint8_t alternate) {
	m_bindShadersHook->Original(server, pass, alternate);
	try {
		const auto lock = std::scoped_lock(m_mutex);
		m_server = server;
		m_pass = pass;
		m_alternate = alternate != 0;
		m_passActive = false;
		if (m_enabled && !m_alternate) {
			for (auto& p : m_passes) {
				if (p.Pass == pass) {
					m_passActive = EnsureShader(server, p);
					break;
				}
			}
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
		Host::Error("Subpixel text: {}; turning it off", e.what());
		const auto lock = std::scoped_lock(m_mutex);
		m_enabled = false;
		RestoreAll();
	}
}

void FontReplacement::SubpixelText::SetDrawStateDetour(uintptr_t server, DrawState* state, uint8_t divisor) {
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

uint32_t FontReplacement::SubpixelText::Decide(uintptr_t server, const DrawState& state, bool subpixelTexture) {
	if (!m_enabled || !m_passActive || m_alternate || !subpixelTexture)
		return state.Blend;

	if (GetFactor(state.Blend, SrcBlendShift) != D3D11_BLEND_SRC_ALPHA) {
		if (!m_warnedBlend) {
			m_warnedBlend = true;
			Host::Warning("Subpixel text: a glyph is drawn with blend state 0x{:08X}, which can't take subpixel coverage", state.Blend);
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

bool FontReplacement::SubpixelText::EnsureShader(uintptr_t server, PassShader& pass) {
	const auto current = GetShaderObject(server, pass.Pass, false, {});

	// A UI shader reload drops the patched object (a new one may take its address) or makes it bind another shader, so its
	// game shader is ours to release.
	if (pass.PatchedObject && (current != pass.PatchedObject
		|| *reinterpret_cast<ID3D11PixelShader**>(current + m_d3dShader) != pass.Shader.GetInterfacePtr())) {
		if (pass.Original)
			pass.Original->Release();
		pass.PatchedObject = 0;
		pass.Original = nullptr;
	}
	if (!current)
		return false;
	if (pass.PatchedObject)
		return true;

	const auto slot = reinterpret_cast<ID3D11PixelShader**>(current + m_d3dShader);

	if (!*slot || !GetShaderObject(server, pass.Pass, true, pass.File))
		return false;

	if (!pass.Shader)
		pass.Shader = CreateShader(*slot, pass.Highlight);

	// The object owns a reference to its shader.
	pass.PatchedObject = current;
	pass.Original = *slot;
	pass.Shader->AddRef();
	*slot = pass.Shader.GetInterfacePtr();
	return true;
}

uintptr_t FontReplacement::SubpixelText::GetShaderObject(uintptr_t server, int pass, bool check, const std::string& file) const {
	if (!server)
		return 0;
	if (check) {
		const auto handle = *reinterpret_cast<const uintptr_t*>(server + m_pixelShaderFiles + static_cast<size_t>(pass) * 8);
		if (!handle || !Utils::EndsWithI(GameUi::GetFileName(handle), file))
			return 0;
	}
	return *reinterpret_cast<const uintptr_t*>(server + m_pixelShaders + static_cast<size_t>(pass) * 8);
}

std::vector<uint8_t> FontReplacement::SubpixelText::GetShaderCode(const SubpixelSettings& settings, bool highlight) {
	auto code = ReadShaderCode(highlight ? IDR_FONTSUBPIXELHIGHLIGHTPS : IDR_FONTSUBPIXELPS);
	if (EdgeShaderCode::PatchConstants(code, {{ClearTypeLevelSentinel, settings.Level}, {ContrastSentinel, settings.Contrast}, {GammaSentinel, settings.Gamma}}) < 3)
		Host::Warning("FontSubpixelPS.cso doesn't have all of its settings to set; text is drawn with the values it was compiled with.");
	return code;
}

ID3D11PixelShaderPtr FontReplacement::SubpixelText::CreateShader(ID3D11PixelShader* game, bool highlight) const {
	const auto code = GetShaderCode(m_settings, highlight);
	ID3D11DevicePtr device;
	game->GetDevice(&device);
	ID3D11PixelShaderPtr shader;
	ThrowOnError(device->CreatePixelShader(code.data(), code.size(), nullptr, &shader), "Making the subpixel text shader");
	return shader;
}

void FontReplacement::SubpixelText::Restore(PassShader& pass) {
	if (!pass.PatchedObject)
		return;
	const auto slot = reinterpret_cast<ID3D11PixelShader**>(pass.PatchedObject + m_d3dShader);
	if (GetShaderObject(m_server, pass.Pass, false, {}) == pass.PatchedObject && *slot == pass.Shader.GetInterfacePtr()) {
		*slot = pass.Original;
		pass.Shader->Release();
	} else if (pass.Original) {
		// The slot doesn't hold ours any more, so the game shader's reference is ours to release.
		pass.Original->Release();
	}
	pass.PatchedObject = 0;
	pass.Original = nullptr;
}

void FontReplacement::SubpixelText::RestoreAll() {
	for (auto& pass : m_passes)
		Restore(pass);
	m_passActive = false;
}
