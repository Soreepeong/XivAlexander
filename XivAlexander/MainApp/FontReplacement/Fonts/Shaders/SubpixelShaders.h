#pragma once

#include "MainApp/FontReplacement/Fonts/Enums/SubpixelOrder.h"
#include "MainApp/FontReplacement/Fonts/Shaders/PixelShaderSwap.h"
#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander {
	enum class FontReplacementSubpixelMode : uint8_t;
	struct FontReplacementClearTypeConfig;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Dual-source blending (SRC1_COLOR / INV_SRC_COLOR) of subpixel pages drawn by the text, highlight and shadow passes, whose shaders become FontSubpixelPS.
	class SubpixelShaders {
	public:
		// FontSubpixelPS.hlsl's ClearTypeLevel (0.234567891f), Contrast (0.345678912f) and Gamma (0.456789123f), as float bits.
		static constexpr uint32_t ClearTypeLevelSentinel = 0x3E703291;
		static constexpr uint32_t ContrastSentinel = 0x3EB0FCD4;
		static constexpr uint32_t GammaSentinel = 0x3EE9E044;

		struct SettingsRecord {
			SubpixelOrder Order = SubpixelOrder::None;

			// ClearType level, 0 to 1.
			float Level = 1;

			// k of coverage * (k + 1) / (coverage * k + 1).
			float Contrast = 0.5f;

			// The display's gamma, which blending corrects for.
			float Gamma = 1.8f;

			// Level, contrast, gamma and stripe order from Windows for the window's monitor (the ClearType Text Tuner's).
			[[nodiscard]] static SettingsRecord FromWindow(HWND window);

			// System takes everything from Windows (grayscale unless ClearType is on); the stripe orders take level, contrast and gamma from clearType.
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
			std::string File;
			PixelShaderSwap Swap;

			PassShader(ShaderPass pass, std::string file)
				: Pass(pass)
				, File(std::move(file)) {}
		};

		int m_pixelShaderFiles = 0;
		int m_pixelShaders = 0;
		int m_textureWidth = 0;
		int m_renderingToTexture = 0;

		std::optional<Host::Hook<void, uintptr_t, int, uint8_t>> m_bindShadersHook;
		std::optional<Host::Hook<void, uintptr_t, DrawState*, uint8_t>> m_setDrawStateHook;

		// Apply runs on the game's thread, the hooks where draw commands are processed.
		std::mutex m_mutex;
		std::array<PassShader, 3> m_passes{
			PassShader{ShaderPass::Font, "FontPS.shcd"},
			PassShader{ShaderPass::FontHighlight, "FontHighlightPS.shcd"},
			PassShader{ShaderPass::FontEmboss, "FontEmbossPS.shcd"},
		};
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
		SubpixelShaders() = default;
		SubpixelShaders(const SubpixelShaders&) = delete;
		SubpixelShaders& operator=(const SubpixelShaders&) = delete;
		~SubpixelShaders();

		// On, with the hooks in place. Game thread.
		[[nodiscard]] bool Active() const { return m_enabled; }

		// Game thread; the hooks are made the first time it is on (throws if a signature isn't found). Order None puts the game's shaders back.
		void Apply(const SettingsRecord& value);

		// Between frames, after the UI's draw commands are processed.
		void EndFrame();

	private:
		void Resolve();

		[[nodiscard]] PassShader* FindPass(int pass);

		void BindShadersDetour(uintptr_t server, int pass, uint8_t alternate);
		void SetDrawStateDetour(uintptr_t server, DrawState* state, uint8_t divisor);

		[[nodiscard]] uint32_t Decide(uintptr_t server, const DrawState& state, bool subpixelTexture);

		// Puts this one's shader in if the game's is there (again after a UI shader reload).
		bool EnsureShaderSwap(uintptr_t server, PassShader& p);

		// With file, only if the pass's shader is made from it; 0 if absent.
		[[nodiscard]] uintptr_t GetShaderObjectOrNull(uintptr_t server, ShaderPass pass, const char* file) const;

		[[nodiscard]] ID3D11PixelShaderPtr CreateShader(ID3D11PixelShader* game, bool highlight) const;

		void RestoreAndReleaseAllInLock();

		[[nodiscard]] static bool SameBlend(uint32_t a, uint32_t b) { return a == b; }

		[[nodiscard]] static std::string Describe(uint32_t blend);
		[[nodiscard]] static std::string DescribeBlendOp(uint32_t op);
		[[nodiscard]] static std::string DescribeBlend(uint32_t blend);
	};
}
