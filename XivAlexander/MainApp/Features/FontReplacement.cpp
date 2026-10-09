#include "pch.h"
#include "MainApp/Features/FontReplacement.h"

#include "Config.h"
#include "MainApp/App.h"
#include "MainApp/FontReplacement/FontReplacer.h"
#include "MainApp/FontReplacement/GameLayout.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/LineBreaker.h"
#include "MainApp/FontReplacement/NamePlateText.h"
#include "MainApp/FontReplacement/PresetController.h"

namespace XivAlexander::Apps::MainApp::Features {
	using namespace MainApp::FontReplacement;
}

struct XivAlexander::Apps::MainApp::Features::FontReplacement::Implementation {
	App& App;
	const std::shared_ptr<Config> Config;

	std::unique_ptr<FontReplacer> Replacer;

	// Null if unsupported by this game version: text is still replaced, but nameplates are as the game bakes them and words split by its rules.
	std::unique_ptr<NamePlateText> NamePlates;
	std::unique_ptr<LineBreaker> Breaker;

	// Changes since the last frame, applied on the game's thread between frames.
	std::mutex PendingMutex;
	std::optional<std::pair<Presets::Faces, bool>> PendingPreset;
	bool PendingSettings = true;

	// Only one is set: the hook of the game's call of Present, or DXGI's Present where that isn't found.
	std::optional<Host::PresentCallHook> PresentCall;
	std::optional<Host::PresentHook> Present;
	std::unique_ptr<PresetController> Controller;  // With PendingMutex.
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
		void(GameLayout::TryGet("GameFont"));
		const auto window = App.GetGameWindowHandle(true);
		if (!window)
			return;

		std::optional<std::string> error;
		App.RunOnGameLoop([&] {
			try {
				Replacer = std::make_unique<FontReplacer>();
				Replacer->SetEdge(Config->Runtime.FontReplacement.Edge.Value());
				NamePlates = Optional<NamePlateText>("Nameplate text");
				Breaker = Optional<LineBreaker>("Line breaking");
				Replacer->TextInvalidated = [this] {
					if (NamePlates)
						NamePlates->ForceRebake();
				};
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

		// Subscribed before the controller loads the settings, so a change in between is loaded or reloaded; the edge and nameplates apply before the next frame.
		auto& settings = Config->Runtime.FontReplacement;
		Cleanup += settings.Faces.OnChange([this] {
			const auto lock = std::scoped_lock(PendingMutex);
			if (Controller)
				Controller->Reload();
		});
		const auto markPending = [this] {
			const auto lock = std::scoped_lock(PendingMutex);
			PendingSettings = true;
		};
		Cleanup += settings.Edge.OnChange(markPending);

		auto controller = std::make_unique<PresetController>(Config, [this](Presets::Faces faces, bool systemFallback) {
			const auto lock = std::scoped_lock(PendingMutex);
			PendingPreset.emplace(std::move(faces), systemFallback);
		});
		const auto lock = std::scoped_lock(PendingMutex);
		Controller = std::move(controller);
	}

	~Implementation() {
		InitThread.join();
		Cleanup.clear();
		Controller.reset();
		PresentCall.reset();
		Present.reset();

		// Between frames on the game's thread.
		if (Replacer || NamePlates || Breaker)
			App.RunOnGameLoop([this] { TearDown(); });
	}

	template<typename T>
	std::unique_ptr<T> Optional(const char* part) {
		try {
			return std::make_unique<T>(*Replacer);
		} catch (const std::exception& e) {
			Host::Warning("{} is off: {}", part, e.what());
			return nullptr;
		}
	}

	void TearDown() {
		const auto step = [](const char* name, auto&& fn) {
			try {
				fn();
			} catch (const std::exception& e) {
				Host::Error("Teardown step failed: {}: {}", name, e.what());
			}
		};
		step("line breaker", [this] { Breaker.reset(); });
		step("nameplate text", [this] { NamePlates.reset(); });
		step("font replacer", [this] { Replacer.reset(); });
	}

	void BeforePresent() {
		if (!Replacer)
			return;

		std::optional<std::pair<Presets::Faces, bool>> preset;
		auto settings = false;
		{
			const auto lock = std::scoped_lock(PendingMutex);
			preset = std::move(PendingPreset);
			PendingPreset.reset();
			settings = std::exchange(PendingSettings, false);
		}

		if (settings) {
			const auto& current = Config->Runtime.FontReplacement;
			Replacer->SetEdge(current.Edge.Value());
		}

		if (preset) {
			try {
				Replacer->SetPreset(preset->first, preset->second);
			} catch (const std::exception& e) {
				Host::Error("Applying the preset failed: {}", e.what());
			}
		}

		Replacer->Upload();
	}
};

XivAlexander::Apps::MainApp::Features::FontReplacement::FontReplacement(App& app)
	: m_pImpl(std::make_unique<Implementation>(app)) {
}

XivAlexander::Apps::MainApp::Features::FontReplacement::~FontReplacement() = default;
