#include "pch.h"
#include "MainApp/FontReplacement/EdgeShader.h"

#include "MainApp/FontReplacement/EdgeShaderCode.h"
#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/GameCode.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Utilities.h"
#include "Utils/StringUtils.h"
#include "Utils/Win32/LoadedModule.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::EdgeShader::~EdgeShader() {
	m_loadShadersHook.reset();
	Restore();
}

void FontReplacement::EdgeShader::Set(bool use, float softness) {
	m_used = use;
	if (!use) {
		Restore();
		return;
	}

	if (!m_resolved) {
		GameFontManager::Resolve();
		GameUi::ResolveResourceHandles();
		const auto functions = ResolveGameCode(Game::Resolved::Fonts::EdgeShader);
		const auto tables = ResolveGameCode(Game::Resolved::Fonts::ShaderTables);
		m_edgePass = 5;
		m_shaderFile = "FontEdgePS.shcd";
		const auto loadShaders = reinterpret_cast<uintptr_t>(functions.LoadShaders);
		m_moduleFontManager = functions.ModuleFontManager;
		m_moduleServer = functions.ModuleServer;
		m_pixelShaderFiles = tables.PixelShaderFiles;
		m_pixelShaders = tables.PixelShaders;
		m_d3dShader = tables.D3DShader;
		m_loadShadersHook.emplace("AtkServer::LoadShaders", loadShaders, [this](uintptr_t server) { return LoadShadersDetour(server); });
		m_resolved = true;
	}

	if (m_shader && softness != m_softness) {
		Restore();
		m_shader = nullptr;
	}
	m_softness = softness;

	// Before the game has loaded its shaders, LoadShadersDetour installs it once it does.
	if (!m_shader && !CreateFromGame())
		return;

	Install();
}

bool FontReplacement::EdgeShader::CreateFromGame() {
	const auto target = GetEdgeShaderObject(true);
	if (!target)
		return false;
	m_shader = Create(*reinterpret_cast<ID3D11PixelShader**>(target + m_d3dShader), m_softness);
	return true;
}

void FontReplacement::EdgeShader::Install() {
	const auto target = GetEdgeShaderObject(true);
	const auto slot = reinterpret_cast<ID3D11PixelShader**>(target + m_d3dShader);
	if (!target || (target == m_patchedObject && *slot == m_shader.GetInterfacePtr()))
		return;

	// The game's object owns a reference to what it binds: it gets one of this shader.
	m_patchedObject = target;
	m_original = *slot;
	m_shader->AddRef();
	*slot = m_shader.GetInterfacePtr();
}

uintptr_t FontReplacement::EdgeShader::GetEdgeShaderObject(bool check) const {
	const auto fontManager = GameFontManager::Instance();
	const auto server = fontManager ? *reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(fontManager) - m_moduleFontManager + m_moduleServer) : 0;
	if (!server)
		return 0;
	const auto file = *reinterpret_cast<const uintptr_t*>(server + m_pixelShaderFiles + static_cast<size_t>(m_edgePass) * 8);
	if (check && (!file || !Utils::EndsWithI(GameUi::GetFileName(file), m_shaderFile)))
		return 0;
	return *reinterpret_cast<const uintptr_t*>(server + m_pixelShaders + static_cast<size_t>(m_edgePass) * 8);
}

uint8_t FontReplacement::EdgeShader::LoadShadersDetour(uintptr_t server) {
	const auto result = m_loadShadersHook->Original(server);
	try {
		if (m_patchedObject && GetEdgeShaderObject(false) != m_patchedObject) {
			if (m_original)
				m_original->Release();
			m_patchedObject = 0;
			m_original = nullptr;
		}

		if (m_used && (m_shader || CreateFromGame()))
			Install();
	} catch (const std::exception& e) {
		Host::Error("Installing the edge shader again failed: {}", e.what());
	}
	return result;
}

ID3D11PixelShaderPtr FontReplacement::EdgeShader::Create(ID3D11PixelShader* game, float softness) {
	std::string failure;
	const auto code = EdgeShaderCode::Get(softness, failure);
	if (!failure.empty())
		Host::Error("{}", failure);

	ID3D11DevicePtr device;
	game->GetDevice(&device);
	ID3D11PixelShaderPtr shader;
	ThrowOnError(device->CreatePixelShader(code.data(), code.size(), nullptr, &shader), "Making the edge shader");
	return shader;
}

void FontReplacement::EdgeShader::Restore() {
	if (!m_patchedObject)
		return;
	const auto slot = reinterpret_cast<ID3D11PixelShader**>(m_patchedObject + m_d3dShader);
	if (GetEdgeShaderObject(false) == m_patchedObject && *slot == m_shader.GetInterfacePtr()) {
		*slot = m_original;
		m_shader->Release();
	}

	m_patchedObject = 0;
	m_original = nullptr;
}
