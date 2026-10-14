#pragma once

#include "MainApp/FontReplacement/GlyphRasterizer.h"
#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander {
	enum class FontReplacementSubpixelMode : uint8_t;
	struct FontReplacementClearTypeConfig;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct SubpixelSettings {
		SubpixelOrder Order = SubpixelOrder::None;

		// ClearType level, 0 to 1.
		float Level = 1;

		// k of coverage * (k + 1) / (coverage * k + 1).
		float Contrast = 0.5f;

		// The display's gamma, which blending corrects for.
		float Gamma = 1.8f;

		// Level, contrast, gamma and stripe order from Windows for the window's monitor (the ClearType Text Tuner's).
		[[nodiscard]] static SubpixelSettings OfWindows(HWND window);

		// System takes everything from Windows; RGB and BGR take level, contrast and gamma from clearType.
		[[nodiscard]] static SubpixelSettings Of(FontReplacementSubpixelMode mode, const FontReplacementClearTypeConfig& clearType, HWND window);

		bool operator==(const SubpixelSettings&) const = default;
	};

	// Dual-source blending (SRC1_COLOR / INV_SRC_COLOR) of subpixel pages drawn by the text, highlight and shadow passes, whose shaders become FontSubpixelPS.
	class SubpixelText {
		struct DrawState {
			uint32_t Blend;
			uint32_t Padding;
			uintptr_t Texture;
			uint32_t Unknown10;
			uint32_t Unknown14;
		};

		struct PassShader {
			int Pass = 0;
			std::string File;
			bool Highlight = false;

			uintptr_t PatchedObject = 0;

			// Raw: it goes back into the slot.
			ID3D11PixelShader* Original = nullptr;
			ID3D11PixelShaderPtr Shader;
		};

		bool m_resolved = false;
		int m_pixelShaderFiles = 0;
		int m_pixelShaders = 0;
		int m_d3dShader = 0;
		int m_textureWidth = 0;
		int m_renderingToTexture = 0;

		std::optional<Host::Hook<void, uintptr_t, int, uint8_t>> m_bindShadersHook;
		std::optional<Host::Hook<void, uintptr_t, DrawState*, uint8_t>> m_setDrawStateHook;

		// Set runs on the game's thread, the hooks where draw commands are processed.
		std::mutex m_mutex;
		std::atomic<bool> m_enabled = false;
		SubpixelSettings m_settings;
		std::vector<PassShader> m_passes;

		// From the last BindShaders.
		uintptr_t m_server = 0;
		int m_pass = -1;
		bool m_alternate = false;
		bool m_passActive = false;

		// The game's last state, forgotten at EndFrame as its texture may be gone by then.
		uintptr_t m_lastServer = 0;
		DrawState m_lastState{};
		uint8_t m_lastDivisor = 1;
		bool m_lastSubpixelTexture = false;
		uint32_t m_stagedBlend = 0;
		bool m_hasState = false;
		bool m_warnedBlend = false;

	public:
		SubpixelText() = default;
		SubpixelText(const SubpixelText&) = delete;
		SubpixelText& operator=(const SubpixelText&) = delete;
		~SubpixelText();

		// Game thread. Throws if a signature isn't found; Order None puts the game's shaders back.
		void Set(const SubpixelSettings& settings);

		// FontSubpixelPS's bytecode with the settings' level, contrast and gamma.
		[[nodiscard]] static std::vector<uint8_t> GetShaderCode(const SubpixelSettings& settings, bool highlight);

		// On, with the hooks in place. Game thread.
		[[nodiscard]] bool Active() const { return m_enabled; }

		// Between frames, after the UI's draw commands are processed.
		void EndFrame();

	private:
		void Resolve();

		void BindShadersDetour(uintptr_t server, int pass, uint8_t alternate);
		void SetDrawStateDetour(uintptr_t server, DrawState* state, uint8_t divisor);

		uint32_t Decide(uintptr_t server, const DrawState& state, bool subpixelTexture);

		// Puts this one's shader in if the game's is there (again after a UI shader reload).
		bool EnsureShader(uintptr_t server, PassShader& pass);

		[[nodiscard]] uintptr_t GetShaderObject(uintptr_t server, int pass, bool check, const std::string& file) const;

		ID3D11PixelShaderPtr CreateShader(ID3D11PixelShader* game, bool highlight) const;

		void Restore(PassShader& pass);
		void RestoreAll();
	};
}
