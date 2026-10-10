#include "pch.h"
#include "MainApp/FontReplacement/EdgeShader.h"

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/GameLayout.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Utilities.h"
#include "Utils/Win32/LoadedModule.h"
#include "resource.h"
#include "XivAlexander.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::EdgeShader::~EdgeShader() {
	m_loadShadersHook.reset();
	Restore();
}

void FontReplacement::EdgeShader::Set(bool use) {
	m_used = use;
	if (!use) {
		Restore();
		return;
	}

	if (!m_resolved) {
		uintptr_t loadShaders = 0;
		GameLayout::Resolve("The edge shader", [&] {
			GameFontManager::Resolve();
			GameUi::ResolveResourceHandles();
			m_edgePass = GameLayout::Get("AtkServer.EdgePass");
			m_shaderFile = GameLayout::GetString("AtkServer.EdgePixelShaderFile");
			loadShaders = GameLayout::Address("AtkModuleReloadFontShaders", "AtkServer.LoadShaders");
			m_moduleFontManager = GameLayout::Get("AtkModule.AtkFontManager");
			m_moduleServer = GameLayout::Get("AtkModule.AtkServer");
			m_pixelShaderFiles = GameLayout::Get("AtkServer.PixelShaderFiles");
			m_pixelShaders = GameLayout::Get("AtkServer.PixelShaders");
			m_d3dShader = GameLayout::Get("Kernel.PixelShader.D3DShader");
		});
		m_loadShadersHook.emplace("AtkServer::LoadShaders", loadShaders, [this](uintptr_t server) { return LoadShadersDetour(server); });
		m_resolved = true;
	}

	if (!m_shader) {
		const auto target = GetEdgeShaderObject(true);
		if (!target)
			throw std::runtime_error("The game's edge shader isn't there.");
		m_shader = Create(*reinterpret_cast<ID3D11PixelShader**>(target + m_d3dShader));
	}

	Install();
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
	if (check && (!file || !EndsWithIgnoringCase(GameUi::GetFileName(file), m_shaderFile)))
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

		if (m_used && m_shader)
			Install();
	} catch (const std::exception& e) {
		Host::Error("Installing the edge shader again failed: {}", e.what());
	}
	return result;
}

ID3D11PixelShaderPtr FontReplacement::EdgeShader::Create(ID3D11PixelShader* game) {
	const auto module = static_cast<HMODULE>(Dll::Module());
	const auto resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_FONTEDGEPS), RT_RCDATA);
	if (!resource)
		throw std::runtime_error("FontEdgePS.cso isn't embedded.");
	const auto code = LockResource(LoadResource(module, resource));
	const auto size = SizeofResource(module, resource);

	ID3D11DevicePtr device;
	game->GetDevice(&device);
	ID3D11PixelShaderPtr shader;
	ThrowOnError(device->CreatePixelShader(code, size, nullptr, &shader), "Making the edge shader");
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
