#pragma once

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Swaps a shader of ours into a game Kernel::PixelShader, which the game may swap out (or free) at any time.
	class PixelShaderSwap {
		ID3D11PixelShaderPtr m_pluginShader;

		// The reference the slot held, kept raw because it goes back into the slot.
		ID3D11PixelShader* m_originalShader = nullptr;
		uintptr_t m_target = 0;

	public:
		PixelShaderSwap() = default;
		PixelShaderSwap(const PixelShaderSwap&) = delete;
		PixelShaderSwap& operator=(const PixelShaderSwap&) = delete;
		~PixelShaderSwap();

		// The Kernel::PixelShader it is installed in; 0 if none.
		[[nodiscard]] uintptr_t Target() const { return m_target; }

		[[nodiscard]] ID3D11PixelShaderPtr& PluginShader() { return m_pluginShader; }

		// What a Kernel::PixelShader binds.
		[[nodiscard]] static ID3D11PixelShader* ShaderOf(uintptr_t target);

		// On the device of the game's shader.
		[[nodiscard]] static ID3D11PixelShaderPtr CreateShader(ID3D11PixelShader* game, std::span<const uint8_t> code);

		[[nodiscard]] bool IsIn(uintptr_t target) const;

		// A UI shader reload drops the object (a new one may take its address) or makes it bind another shader: its game shader is ours to release.
		void ForgetIfReplacedElsewhere(uintptr_t current);

		// The object owns a reference to what it binds: it gets one of ours.
		void Install(uintptr_t target);

		void Restore(uintptr_t current);

		// An embedded RCDATA resource.
		[[nodiscard]] static std::vector<uint8_t> ReadShaderCode(int resourceId);

		// Replaces each word of the code chunk equal to a sentinel (a float's bits) with its value and updates the checksum; whether every sentinel
		// was found exactly once.
		static bool PatchConstants(std::span<uint8_t> dxbc, std::initializer_list<std::pair<uint32_t, float>> values);

		// The DXBC container's checksum, of everything after it.
		[[nodiscard]] static std::array<uint8_t, 16> ComputeChecksum(std::span<const uint8_t> dxbc);

	private:
		[[nodiscard]] static ID3D11PixelShader** Slot(uintptr_t target);
	};
}
