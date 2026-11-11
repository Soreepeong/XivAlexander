#include "pch.h"
#include "MainApp/Features/FontReplacement.h"

#include <execution>

#include "Config.h"
#include "Game/SignatureDefinitions.h"
#include "MainApp/App.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/PluginInterface/FrameCoordinator.h"
#include "MainApp/FontReplacement/PluginInterface/FontSourceEditor.h"

namespace XivAlexander::Apps::MainApp::Features {
	using namespace MainApp::FontReplacement;
}

struct XivAlexander::Apps::MainApp::Features::FontReplacement::Implementation {
	App& App;
	const std::shared_ptr<Config> Config;

	std::unique_ptr<FrameCoordinator> Coordinator;

	// As last read from the configuration, on the game's thread.
	FrameCoordinator::Settings Settings;

	// Changes since the last frame, applied on the game's thread between frames.
	std::mutex PendingMutex;
	bool PendingFonts = false;
	bool PendingSettings = true;

	// Only one is set: the hook of the game's call of Present, or DXGI's Present where that isn't found.
	std::optional<Host::PresentCallHook> PresentCall;
	std::optional<Host::PresentHook> Present;
	xivres::util::on_dtor::multi Cleanup;

	// Reading the game's executable and finding signatures takes a while, and the game's thread may not have its window yet.
	std::thread InitThread;

	explicit Implementation(MainApp::App& app)
		: App(app)
		, Config(Config::Acquire())
		, InitThread([this] {
			try {
				Initialize();
			} catch (const std::exception& e) {
				Host::Error("Font replacement is off: {}", e.what());
			}
		}) {
	}

	void Initialize() {
		// The parts resolve these on the game's thread, then from what is found here.
		namespace Fonts = Game::Resolved::Fonts;
		const std::initializer_list<const Game::Signatures::ComplexSignatureBase*> signatures{
			&Fonts::Stage, &Fonts::FontManager, &Fonts::FontSet, &Fonts::Font, &Fonts::Ui, &Fonts::ResourceHandles, &Fonts::Textures,
			&Fonts::Replacer, &Fonts::FontTables, &Fonts::NamePlates, &Fonts::ShaderTables, &Fonts::EdgeShader, &Fonts::LineBreaker, &Fonts::Measure,
			&Game::Resolved::SubpixelText,
		};
		std::for_each(std::execution::par, signatures.begin(), signatures.end(), [](const auto* signature) { void(signature->Check()); });
		const auto window = App.GetGameWindowHandle(true);
		if (!window)
			return;

		std::optional<std::string> error;
		App.RunOnGameLoop([&] {
			try {
				ReadSettings();
				Coordinator = std::make_unique<FrameCoordinator>(Config, Settings, window);
			} catch (const std::exception& e) {
				error = e.what();
				TearDown();
			}
		});
		if (error)
			throw std::runtime_error(*error);

		// The game calls Present on its thread after drawing the UI; glyphs added during the frame are uploaded then and show from the next one.
		try {
			try {
				PresentCall.emplace([this] { BeforePresent(); });
				Host::Information("Uploading glyphs from the game's call of Present.");
			} catch (const std::exception& e) {
				Host::Information("Uploading glyphs from a hook of IDXGISwapChain::Present, as the game's call of it can't be hooked: {}", e.what());
				Present.emplace(window, [this] { BeforePresent(); });
			}
		} catch (...) {
			App.RunOnGameLoop([this] { TearDown(); });
			throw;
		}

		try {
			// Marked once subscribed, so that a change made since the coordinator read the fonts isn't lost.
			auto& settings = Config->Runtime.FontReplacement;
			const auto markFontsPending = [this] {
				const auto lock = std::scoped_lock(PendingMutex);
				PendingFonts = true;
			};
			Cleanup += settings.Fonts.OnChange(markFontsPending);
			markFontsPending();
			const auto markPending = [this] {
				const auto lock = std::scoped_lock(PendingMutex);
				PendingSettings = true;
			};
			Cleanup += settings.Edge.OnChange(markPending);
			Cleanup += settings.Subpixel.OnChange(markPending);
			Cleanup += settings.ClearType.OnChange(markPending);
			Cleanup += settings.VerticalSubpixel.OnChange(markPending);
			Cleanup += settings.NamePlates.OnChange(markPending);
			Cleanup += settings.Atlas.OnChange(markPending);
		} catch (...) {
			Unhook();
			App.RunOnGameLoop([this] { TearDown(); });
			throw;
		}
	}

	~Implementation() {
		InitThread.join();
		Unhook();

		// Between frames on the game's thread.
		if (Coordinator)
			App.RunOnGameLoop([this] { TearDown(); });
	}

	// Comes before TearDown, so that nothing reaches the coordinator while it's torn down.
	void Unhook() {
		Cleanup.clear();
		PresentCall.reset();
		Present.reset();
	}

	void TearDown() {
		try {
			Coordinator.reset();
		} catch (const std::exception& e) {
			Host::Error("Teardown failed: {}", e.what());
		}
	}

	// Game thread.
	void ReadSettings() {
		const auto& settings = Config->Runtime.FontReplacement;
		Settings = {
			.Atlas = settings.Atlas.Value(),
			.Edge = settings.Edge.Value(),
			.Subpixel = settings.Subpixel.Value(),
			.ClearType = settings.ClearType.Value(),
			.VerticalSubpixel = settings.VerticalSubpixel.Value(),
			.NamePlates = settings.NamePlates.Value(),
		};
	}

	void BeforePresent() {
		if (!Coordinator)
			return;

		auto fonts = false;
		auto settings = false;
		{
			const auto lock = std::scoped_lock(PendingMutex);
			fonts = std::exchange(PendingFonts, false);
			settings = std::exchange(PendingSettings, false);
		}

		if (settings)
			ReadSettings();
		if (fonts)
			Coordinator->Presets().SettingsChanged();

		try {
			Coordinator->OnUpdate(Settings, App.GetGameWindowHandle());
			Coordinator->OnPresent();
		} catch (const std::exception& e) {
			Host::Error("Updating the font replacement failed: {}", e.what());
		}
	}
};

XivAlexander::Apps::MainApp::Features::FontReplacement::FontReplacement(App& app)
	: m_pImpl(std::make_unique<Implementation>(app)) {
}

XivAlexander::Apps::MainApp::Features::FontReplacement::~FontReplacement() = default;
