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

			if (Game::Resolved::CutSceneLanguageGetterFn getter; Game::Resolved::CutSceneLanguageGetterFunction.Resolve(getter) == Game::Signatures::ResolveError::Ok) {
				CutSceneLanguageGetter.emplace("FFXIV::GetCutSceneLanguage", getter);
				Cleanup += CutSceneLanguageGetter->SetHook([this](void* p) -> int {
					if (const auto forced = Config->Runtime.ForcedCharacterLanguageLipSync.Value(); forced != -1)
						return forced;
					return CutSceneLanguageGetter->bridge(p);
				});
			}

			std::vector<Game::Resolved::StringIndirectionResolverFn> resolvers;
			if (Game::Resolved::StringIndirectionResolverFunctions.Resolve(resolvers) != Game::Signatures::ResolveError::Ok)
				resolvers.clear();

			for (const auto ptr : resolvers) {
				auto& hook = FoundStringIndirectionResolverFunctions.emplace_back("FFXIV::StringIndirectionResolverFunctions", ptr);
				Cleanup += hook.SetHook([this, ptr, self = &hook](const char8_t* s) {
					auto error = true;

					__try {
						s = self->bridge(s);
						error = false;
						goto done;
					} __except(EXCEPTION_EXECUTE_HANDLER) {
						// pass
					}

					Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "Invalid ptr to string indirection resolving function(0x{:x}); trying again with -0xE", reinterpret_cast<size_t>(ptr));
					__try {
						s = self->bridge(s - 0x0e);
						goto done;
					} __except (EXCEPTION_EXECUTE_HANDLER) {
						// pass
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
							// pass
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
