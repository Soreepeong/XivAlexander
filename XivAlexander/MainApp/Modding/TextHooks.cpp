#include "pch.h"
#include "MainApp/Modding/TextHooks.h"

#include <deque>

#include <xivres/util.on_dtor.h>
#include <xivres/xivstring.h>

#include "Config.h"
#include "Game/SignatureDefinitions.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	struct TextHooks::Implementation {
		const std::shared_ptr<Config> Config;
		const std::shared_ptr<Misc::Logger> Logger;

		std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::CutSceneLanguageGetterFn>> CutSceneLanguageGetter;
		std::deque<Misc::Hooks::PointerFunctionOf<Game::Resolved::StringIndirectionResolverFn>> FoundStringIndirectionResolverFunctions{};

		xivres::util::on_dtor::multi Cleanup;

		Implementation()
			: Config(Config::Acquire())
			, Logger(Misc::Logger::Acquire()) {

			// Never fails as a whole: the getter is optional, and there may be no resolvers.
			Game::Resolved::TextHooksFunctions functions;
			if (Game::Resolved::TextHooks.Resolve(functions) != Game::Signatures::ResolveError::Ok)
				functions = {};

			if (functions.CutSceneLanguageGetter) {
				CutSceneLanguageGetter.emplace("FFXIV::GetCutSceneLanguage", *functions.CutSceneLanguageGetter);
				// The game's language index is game_language - 1. The item only holds LipSyncLanguages; others may lack lip sync or voices here, so recheck.
				Cleanup += CutSceneLanguageGetter->SetHook([this](void* p) -> int {
					using Languages = RuntimeConfigRepository::ModdingGroup::LanguagesGroup;
					if (const auto language = Config->Runtime.Modding.Languages.LipSyncLanguage.Value();
						std::ranges::find(Languages::LipSyncLanguages, language) != std::end(Languages::LipSyncLanguages))
						return static_cast<int>(language) - 1;
					return CutSceneLanguageGetter->bridge(p);
				});
			}

			for (const auto ptr : functions.StringIndirectionResolvers) {
				auto& hook = FoundStringIndirectionResolverFunctions.emplace_back("FFXIV::StringIndirectionResolverFunctions", ptr);
				Cleanup += hook.SetHook([this, ptr, self = &hook](const char8_t* s) {
					auto error = true;

					__try {
						s = self->bridge(s);
						error = false;
						goto done;
					} __except(EXCEPTION_EXECUTE_HANDLER) {
					}

					Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "Invalid ptr to string indirection resolving function(0x{:X}); trying again with -0xE", reinterpret_cast<size_t>(ptr));
					__try {
						// A fallback for pointers past a string's start, found by trial in 6.1x; no instruction gives it.
						s = self->bridge(s - 0x0e);
						goto done;
					} __except (EXCEPTION_EXECUTE_HANDLER) {
					}

					Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "Attempt failed, just returning +2");
					s = s + 2;
					static const auto SeStringTester = [](const char8_t* ptr) {
						try {
							void(xivres::xivstring(reinterpret_cast<const char*>(ptr)).parsed());
							return true;
						} catch (...) {
							return false;
						}
					};

				done:
					if (error) {
						auto pass = false;
						__try {
							pass = SeStringTester(s);
						} __except (EXCEPTION_EXECUTE_HANDLER) {
						}

						if (pass)
							Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "Rolling with {}", (char*)s);
						else {
							s = u8"<error>";
							Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "Could not resolve text; displaying <error>");
						}
					}
					return s;
				});
			}
		}

		~Implementation() {
			Cleanup.clear();
		}
	};

	TextHooks::TextHooks()
		: m_pImpl(std::make_unique<Implementation>()) {}

	TextHooks::~TextHooks() = default;
}
