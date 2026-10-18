#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Shaders/PixelShaderSwap.h"

#include "MainApp/FontReplacement/GameCode.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::PixelShaderSwap::~PixelShaderSwap() {
	m_target = 0;
	if (m_originalShader)
		m_originalShader->Release();
	m_originalShader = nullptr;
	m_pluginShader = nullptr;
}

ID3D11PixelShader** FontReplacement::PixelShaderSwap::Slot(uintptr_t target) {
	static const auto s_offset = ResolveGameCode(Game::Resolved::Fonts::ShaderTables).D3DShader;
	return reinterpret_cast<ID3D11PixelShader**>(target + s_offset);
}

ID3D11PixelShader* FontReplacement::PixelShaderSwap::ShaderOf(uintptr_t target) {
	return *Slot(target);
}

ID3D11PixelShaderPtr FontReplacement::PixelShaderSwap::CreateShader(ID3D11PixelShader* game, std::span<const uint8_t> code) {
	ID3D11PixelShaderPtr shader;
	ThrowOnError(GetDevice(game)->CreatePixelShader(code.data(), code.size(), nullptr, &shader), "CreatePixelShader");
	return shader;
}

bool FontReplacement::PixelShaderSwap::IsIn(uintptr_t target) const {
	return target && target == m_target && *Slot(target) == m_pluginShader.GetInterfacePtr();
}

void FontReplacement::PixelShaderSwap::ForgetIfReplacedElsewhere(uintptr_t current) {
	if (!m_target || IsIn(current))
		return;
	m_target = 0;
	if (m_originalShader)
		m_originalShader->Release();
	m_originalShader = nullptr;
}

void FontReplacement::PixelShaderSwap::Install(uintptr_t target) {
	const auto slot = Slot(target);
	m_target = target;
	m_originalShader = *slot;
	m_pluginShader->AddRef();
	*slot = m_pluginShader.GetInterfacePtr();
}

void FontReplacement::PixelShaderSwap::Restore(uintptr_t current) {
	if (!m_target)
		return;
	if (IsIn(current)) {
		*Slot(current) = std::exchange(m_originalShader, nullptr);
		m_pluginShader->Release();
	} else if (m_originalShader) {
		// The slot doesn't hold ours any more, so the game shader's reference is ours to release.
		m_originalShader->Release();
		m_originalShader = nullptr;
	}
	m_target = 0;
}
