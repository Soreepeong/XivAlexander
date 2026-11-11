#pragma once

#include "MainApp/FontReplacement/Fonts/Enums/SubpixelOrder.h"
#include "MainApp/FontReplacement/Fonts/Shaders/PixelShaderSwap.h"
#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander {
	enum class FontReplacementSubpixelMode : uint8_t;
	struct FontReplacementClearTypeConfig;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FrameState;

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

			// Contrast for glyphs drawn scaled, which are blended as grayscale.
			float GrayscaleContrast = 1;

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

		int m_textureWidth = 0;
		int m_renderingToTexture = 0;

		std::optional<Host::Hook<void, uintptr_t, int, uint8_t>> m_bindShadersHook;
		std::optional<Host::Hook<void, uintptr_t, DrawState*, uint8_t>> m_setDrawStateHook;

		// The game's last SetDrawState, valid until the frame ends.
		struct DrawStateMemo {
			bool Has = false;
			uintptr_t Server = 0;
			DrawState State{};
			uint8_t Divisor = 1;
			bool SubpixelTexture = false;

			// The blend state given to the game for it.
			uint32_t Staged = 0;
		};

		FrameState& m_state;
		std::array<PassShader, 3> m_passes;

		// The settings, with the shader swaps made for them.
		SettingsRecord m_settings;

		// From the last BindShaders.
		bool m_alternate = false;
		bool m_passActive = false;

		DrawStateMemo m_last;
		bool m_warnedBlend = false;

	public:
		explicit SubpixelShaders(FrameState& state);
		SubpixelShaders(const SubpixelShaders&) = delete;
		SubpixelShaders& operator=(const SubpixelShaders&) = delete;
		~SubpixelShaders();

		[[nodiscard]] bool Active() const { return Enabled(); }

		// Turned off if value can't be applied.
		void TryApply(const SettingsRecord& value);

		// Once a frame, as the game takes the shaders out when it loads its shaders again, and has none before it first loads them.
		// If the shaders can't be made, it is off until font replacement is set up again, as after a failure in the hooks.
		void Ensure();

		// At Present, as the texture of the game's last draw state may be gone by the next frame.
		void ForgetDrawState() { m_last.Has = false; }

	private:
		[[nodiscard]] bool Enabled() const { return m_settings.Order != SubpixelOrder::None; }

		// The hooks are made the first time it is on, which throws if a signature isn't found.
		void Apply(const SettingsRecord& value);

		void Resolve();

		[[nodiscard]] PassShader* FindPass(int pass);

		void BindShadersDetour(uintptr_t server, int pass, uint8_t alternate);
		void SetDrawStateDetour(uintptr_t server, DrawState* state, uint8_t divisor);

		[[nodiscard]] uint32_t Decide(uintptr_t server, const DrawState& state, bool subpixelTexture);

		// Off at once, and the failure keeps it off until font replacement is set up again.
		void Fail(const std::exception& e);

		void RestoreAndReleaseAll();
	};
}
