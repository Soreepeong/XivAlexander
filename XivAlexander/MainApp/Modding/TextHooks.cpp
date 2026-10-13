#include "pch.h"
#include "MainApp/Modding/TextHooks.h"

#include <deque>
#include <map>
#include <mutex>

#include <xivres/util.on_dtor.h>
#include <xivres/xivstring.h>

#include "Config.h"
#include "Game/SignatureDefinitions.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	namespace {
		/// "_rsv_<row>_<subrow>_<language>_..." with the language field blanked; nullopt if not shaped so.
		std::optional<std::string> RsvKeyWithoutLanguage(std::string_view key) {
			constexpr std::string_view Prefix = "_rsv_";
			if (!key.starts_with(Prefix))
				return std::nullopt;
			size_t start = Prefix.size();
			for (auto field = 0; field < 2; field++) {
				start = key.find('_', start);
				if (start == std::string_view::npos)
					return std::nullopt;
				start++;
			}
			const auto end = key.find('_', start);
			if (end == std::string_view::npos)
				return std::nullopt;
			return std::format("{}{}", key.substr(0, start), key.substr(end));
		}
	}

	struct TextHooks::Implementation {
		const std::shared_ptr<Config> Config;
		const std::shared_ptr<Misc::Logger> Logger;

		std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::CutSceneLanguageGetterFn>> CutSceneLanguageGetter;
		std::deque<Misc::Hooks::PointerFunctionOf<Game::Resolved::StringIndirectionResolverFn>> FoundStringIndirectionResolverFunctions{};
		std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::AddRsvStringFn>> AddRsvString;
		std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::ResolveRsvStringFn>> ResolveRsvString;

		std::mutex RsvMutex;
		// Received keys by the key without its language, for cells whose key was received in another language.
		std::map<std::string, std::string> RsvReceivedKeys;

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
				// The game's language index is game_language - 1; other voice languages have no lip sync, so the game's own stays.
				Cleanup += CutSceneLanguageGetter->SetHook([this](void* p) -> int {
					using Languages = RuntimeConfigRepository::ModdingGroup::LanguagesGroup;
					if (const auto language = Config->Runtime.Modding.Languages.VoiceResourceOverride.Value();
						std::ranges::find(Languages::LipSyncLanguages, language) != std::end(Languages::LipSyncLanguages))
						return static_cast<int>(language) - 1;
					return CutSceneLanguageGetter->bridge(p);
				});
			}

			if (functions.RsvStrings) {
				const auto rsv = *functions.RsvStrings;
				AddRsvString.emplace("LayoutWorld::AddRsvString", rsv.Add);
				Cleanup += AddRsvString->SetHook([this](void* layoutWorld, const char* key, const uint8_t* value, size_t valueLength) {
					// The packet holds the key in 0x30 bytes.
					const auto keyView = std::string_view(key, strnlen(key, 0x30));
					if (auto general = RsvKeyWithoutLanguage(keyView)) {
						const auto lock = std::lock_guard(RsvMutex);
						RsvReceivedKeys.insert_or_assign(std::move(*general), std::string(keyView));
					}
					return AddRsvString->bridge(layoutWorld, key, value, valueLength);
				});

				ResolveRsvString.emplace("ResolveRsvString", rsv.Resolve);
				Cleanup += ResolveRsvString->SetHook([this](const char* key) {
					const auto result = ResolveRsvString->bridge(key);
					if (*result)
						return result;
					if (const auto general = RsvKeyWithoutLanguage(key))
						if (const auto received = ResolveInOtherLanguage(*general, key); *received)
							return received;
					return result;
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

		/// The text received for the same cell in another language, which is closer than nothing; empty if none.
		const char* ResolveInOtherLanguage(const std::string& general, const char* key) {
			std::string received;
			{
				const auto lock = std::lock_guard(RsvMutex);
				if (const auto it = RsvReceivedKeys.find(general); it != RsvReceivedKeys.end() && it->second != key)
					received = it->second;
			}
			return received.empty() ? "" : ResolveRsvString->bridge(received.c_str());
		}

		~Implementation() {
			Cleanup.clear();
		}
	};

	TextHooks::TextHooks()
		: m_pImpl(std::make_unique<Implementation>()) {}

	TextHooks::~TextHooks() = default;
}
