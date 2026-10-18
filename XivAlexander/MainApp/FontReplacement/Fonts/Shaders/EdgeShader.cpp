#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Shaders/EdgeShader.h"

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/GameCode.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "resource.h"
#include "Utils/StringUtils.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// AtkServer's shader pairs are indexed by pass.
	constexpr int EdgePass = 5;

	// The bits of FontEdgePS.hlsl's Softness (0.123456789f), compiled as immediate operands.
	constexpr uint32_t SoftnessSentinel = 0x3DFCD6EA;
}

FontReplacement::EdgeShader::~EdgeShader() {
	m_loadShadersHook.reset();
	Restore();
}

void FontReplacement::EdgeShader::SetSoftness(std::optional<float> value) {
	m_used = value.has_value();
	if (!value) {
		Restore();
		return;
	}

	if (!m_loadShadersHook) {
		GameFontManager::Resolve();
		GameUi::ResolveResourceHandles();
		const auto functions = ResolveGameCode(Game::Resolved::Fonts::EdgeShader);
		const auto tables = ResolveGameCode(Game::Resolved::Fonts::ShaderTables);
		m_moduleFontManager = functions.ModuleFontManager;
		m_moduleServer = functions.ModuleServer;
		m_pixelShaderFiles = tables.PixelShaderFiles;
		m_pixelShaders = tables.PixelShaders;
		m_loadShadersHook.emplace("AtkServer::LoadShaders", reinterpret_cast<uintptr_t>(functions.LoadShaders), [this](uintptr_t server) { return LoadShadersDetour(server); });
	}

	if (m_swap.PluginShader() && *value != m_softness) {
		Restore();
		m_swap.PluginShader() = nullptr;
	}
	m_softness = *value;

	// Before the game has loaded its shaders, LoadShadersDetour installs it once it does.
	if (CreateFromGame())
		Install();
}

uintptr_t FontReplacement::EdgeShader::GetEdgeShaderObjectOrNull(bool checkFontEdgePs) const {
	const auto fontManager = GameFontManager::Instance();
	const auto server = fontManager ? *reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(fontManager) - m_moduleFontManager + m_moduleServer) : 0;
	if (!server)
		return 0;
	const auto file = *reinterpret_cast<const uintptr_t*>(server + m_pixelShaderFiles + static_cast<size_t>(EdgePass) * 8);
	if (checkFontEdgePs && (!file || !Utils::EndsWithI(GameUi::GetFileName(file), "FontEdgePS.shcd")))
		return 0;
	return *reinterpret_cast<const uintptr_t*>(server + m_pixelShaders + static_cast<size_t>(EdgePass) * 8);
}

bool FontReplacement::EdgeShader::CreateFromGame() {
	if (m_swap.PluginShader())
		return true;
	const auto target = GetEdgeShaderObjectOrNull(true);
	if (!target)
		return false;

	auto code = PixelShaderSwap::ReadShaderCode(IDR_FONTEDGEPS);
	if (!PixelShaderSwap::PatchConstants(code, {{SoftnessSentinel, m_softness}}))
		Host::Error("FontEdgePS.cso has no Softness to set; the edge is drawn with the value it was compiled with.");
	m_swap.PluginShader() = PixelShaderSwap::CreateShader(PixelShaderSwap::ShaderOf(target), code);
	return true;
}

void FontReplacement::EdgeShader::Install() {
	const auto target = GetEdgeShaderObjectOrNull(true);
	m_swap.ForgetIfReplacedElsewhere(target);
	if (target && !m_swap.IsIn(target))
		m_swap.Install(target);
}

void FontReplacement::EdgeShader::Restore() {
	if (m_swap.Target())
		m_swap.Restore(GetEdgeShaderObjectOrNull(false));
}

uint8_t FontReplacement::EdgeShader::LoadShadersDetour(uintptr_t server) {
	const auto result = m_loadShadersHook->Original(server);
	try {
		m_swap.ForgetIfReplacedElsewhere(GetEdgeShaderObjectOrNull(false));
		if (m_used && CreateFromGame())
			Install();
	} catch (const std::exception& e) {
		Host::Error("Installing the edge shader again failed: {}", e.what());
	}
	return result;
}
