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

		// Throws if this game version's UI shaders can't be found, in which case GetServer gives none.
		static void Resolve();

		// The server whose shaders draw the UI, 0 while the game has none.
		[[nodiscard]] static uintptr_t GetServer();

		// The game's shader object of a pass, 0 if the server is.
		[[nodiscard]] static uintptr_t GetShaderObject(uintptr_t server, int pass);

		// What the game loaded a pass's shader from, 0 if the server is.
		[[nodiscard]] static uintptr_t GetShaderResource(uintptr_t server, int pass);

		[[nodiscard]] bool IsIn(uintptr_t target) const;

		// Gets our shader into current (the game's shader object, 0 while it has none), making it first if it isn't made yet. A swap the game
		// replaced is forgotten. resource is what the game loaded its shader from, which must be file, as ours stands in for that one only.
		// code is the embedded shader's resource, codeFile its name, and constants what its sentinels become.
		void Ensure(uintptr_t current, uintptr_t resource, const char* file, int code, const char* codeFile, std::initializer_list<std::pair<uint32_t, float>> constants);

		// Puts the game's shader back if ours is still in current, and lets ours go.
		void Release(uintptr_t current);

	private:
		// game gives the device, which must be the one the game draws with.
		[[nodiscard]] static ID3D11PixelShaderPtr CreateShader(ID3D11PixelShader* game, std::span<const uint8_t> code);

		[[nodiscard]] static std::vector<uint8_t> ReadShaderCode(int resourceId);

		// Returns whether every sentinel (a float's bits, compiled as an immediate operand) was found exactly once.
		static bool PatchConstants(std::span<uint8_t> dxbc, std::initializer_list<std::pair<uint32_t, float>> values);

		[[nodiscard]] static ID3D11PixelShader** Slot(uintptr_t target);

		// Once the slot no longer holds ours, the game shader's reference is ours to release.
		void Forget();
	};
}
