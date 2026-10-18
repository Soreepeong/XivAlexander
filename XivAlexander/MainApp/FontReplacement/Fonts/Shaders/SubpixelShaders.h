#pragma once

#include "MainApp/FontReplacement/Fonts/Enums/SubpixelOrder.h"
#include "MainApp/FontReplacement/Fonts/Shaders/PixelShaderSwap.h"
#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander {
	enum class FontReplacementSubpixelMode : uint8_t;
	struct FontReplacementClearTypeConfig;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Per-channel coverage needs the SRC1_COLOR / INV_SRC_COLOR blend, so the text, highlight and shadow passes get shaders that output it.
	class SubpixelShaders {
	public:
		struct SettingsRecord {
			SubpixelOrder Order = SubpixelOrder::None;

			// 0 to 1.
			float Level = 1;

			// k of coverage * (k + 1) / (coverage * k + 1).
			float Contrast = 0.5f;

			// The display's gamma, which blending corrects for.
			float Gamma = 1.8f;

			[[nodiscard]] static SettingsRecord FromWindow(HWND window);

			[[nodiscard]] static SettingsRecord From(FontReplacementSubpixelMode mode, const FontReplacementClearTypeConfig& clearType, HWND window);

			bool operator==(const SettingsRecord&) const = default;

		private:
			[[nodiscard]] static SettingsRecord FromMonitor(HMONITOR monitor);
		};

	private:
		// AtkServer's font shader pairs: 4 text, 5 edge, 6 glare, 7 highlight, 8 emboss (the drop shadow, FontPS's bytecode).
		enum class ShaderPass : int {
			Font = 4,
			FontHighlight = 7,
			FontEmboss = 8,
		};

		struct DrawState {
			uint32_t Blend;
			uint32_t Padding;
			uintptr_t Texture;
			uint32_t Unknown10;
			uint32_t Unknown14;
		};

		struct PassShader {
			ShaderPass Pass;
			const char* File;
			int Code;
			const char* CodeFile;
			PixelShaderSwap Swap;
		};

		int m_pixelShaderFiles = 0;
		int m_pixelShaders = 0;
		int m_textureWidth = 0;
		int m_renderingToTexture = 0;

		std::optional<Host::Hook<void, uintptr_t, int, uint8_t>> m_bindShadersHook;
		std::optional<Host::Hook<void, uintptr_t, DrawState*, uint8_t>> m_setDrawStateHook;

		// Apply runs on the game's thread, the hooks where draw commands are processed.
		std::mutex m_mutex;
		std::array<PassShader, 3> m_passes;
		std::atomic<bool> m_enabled = false;
		SettingsRecord m_settings;

		// From the last BindShaders.
		uintptr_t m_server = 0;
		bool m_alternate = false;
		bool m_passActive = false;

		// The game's last state, forgotten at EndFrame as its texture may be gone by then.
		uintptr_t m_lastServer = 0;
		DrawState m_lastState{};
		uint8_t m_lastDivisor = 1;
		bool m_lastSubpixelTexture = false;
		bool m_hasState = false;
		bool m_warnedBlend = false;
		uint32_t m_stagedBlend = 0;

	public:
		SubpixelShaders();
		SubpixelShaders(const SubpixelShaders&) = delete;
		SubpixelShaders& operator=(const SubpixelShaders&) = delete;
		~SubpixelShaders();

		// Game thread.
		[[nodiscard]] bool Active() const { return m_enabled; }

		// Game thread; the hooks are made the first time it is on, which throws if a signature isn't found.
		void Apply(const SettingsRecord& value);

		// Between frames, after the UI's draw commands are processed.
		void EndFrame();

	private:
		void Resolve();

		[[nodiscard]] PassShader* FindPass(int pass);

		void BindShadersDetour(uintptr_t server, int pass, uint8_t alternate);
		void SetDrawStateDetour(uintptr_t server, DrawState* state, uint8_t divisor);

		[[nodiscard]] uint32_t Decide(uintptr_t server, const DrawState& state, bool subpixelTexture);

		// The game's shader may be back after a UI shader reload.
		bool EnsureShaderSwap(uintptr_t server, PassShader& p);

		[[nodiscard]] uintptr_t GetShaderObjectOrNull(uintptr_t server, ShaderPass pass, const char* file) const;

		[[nodiscard]] ID3D11PixelShaderPtr CreateShader(ID3D11PixelShader* game, const PassShader& p) const;

		void RestoreAndReleaseAllInLock();
	};
}
