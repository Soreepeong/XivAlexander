#pragma once

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// The game may replace the swapped shader (or free its Kernel::PixelShader) at any time, when it reloads its shaders.
	class PixelShaderSwap {
		ID3D11PixelShaderPtr m_pluginShader;

		// Kept raw because it goes back into the slot.
		ID3D11PixelShader* m_originalShader = nullptr;
		uintptr_t m_target = 0;

	public:
		PixelShaderSwap() = default;
		PixelShaderSwap(const PixelShaderSwap&) = delete;
		PixelShaderSwap& operator=(const PixelShaderSwap&) = delete;
		~PixelShaderSwap();

		[[nodiscard]] uintptr_t Target() const { return m_target; }

		[[nodiscard]] ID3D11PixelShaderPtr& PluginShader() { return m_pluginShader; }

		[[nodiscard]] static ID3D11PixelShader* ShaderOf(uintptr_t target);

		// game gives the device, which must be the one the game draws with.
		[[nodiscard]] static ID3D11PixelShaderPtr CreateShader(ID3D11PixelShader* game, std::span<const uint8_t> code);

		[[nodiscard]] bool IsIn(uintptr_t target) const;

		// A UI shader reload drops the object (a new one may take its address) or makes it bind another shader, leaving its game shader ours to release.
		void ForgetIfReplacedElsewhere(uintptr_t current);

		// The object owns a reference to what it binds, so it gets one of ours.
		void Install(uintptr_t target);

		void Restore(uintptr_t current);

		[[nodiscard]] static std::vector<uint8_t> ReadShaderCode(int resourceId);

		// Returns whether every sentinel (a float's bits, compiled as an immediate operand) was found exactly once.
		static bool PatchConstants(std::span<uint8_t> dxbc, std::initializer_list<std::pair<uint32_t, float>> values);

	private:
		[[nodiscard]] static ID3D11PixelShader** Slot(uintptr_t target);

		// Once the slot no longer holds ours, the game shader's reference is ours to release.
		void Forget();
	};
}
