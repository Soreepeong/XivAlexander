#include "pch.h"
#include "MainApp/Features/DisplayLanguage.h"

#include <atomic>
#include <map>
#include <mutex>
#include <utility>

#include <xivres/util.on_dtor.h>

#include "Game/CommandLine.h"
#include "Game/SignatureDefinitions.h"
#include "Misc/GameInstallationDetector.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"
#include "Config.h"
#include "XivAlexander.h"

namespace {
	using GameLanguageValue = xivres::game_language;
	using XivAlexander::TalkLineCount;

	// The game's language indices are game_language - 1.
	constexpr int32_t NoOverride = -1;
	constexpr int32_t GameJapanese = static_cast<int32_t>(GameLanguageValue::Japanese) - 1;
	constexpr int32_t GameEnglish = static_cast<int32_t>(GameLanguageValue::English) - 1;
	constexpr int32_t GameFrench = static_cast<int32_t>(GameLanguageValue::French) - 1;
	constexpr int32_t GameChineseSimplified = static_cast<int32_t>(GameLanguageValue::ChineseSimplified) - 1;
	constexpr int32_t GameChineseTraditional = static_cast<int32_t>(GameLanguageValue::ChineseTraditional) - 1;
	constexpr int32_t GameKorean = static_cast<int32_t>(GameLanguageValue::Korean) - 1;
	constexpr int32_t GameTraditionalChinese = static_cast<int32_t>(GameLanguageValue::TraditionalChinese) - 1;

	// The language the sheets load in, as the text merge takes it; unspecified if unknown.
	GameLanguageValue GameLanguage() {
		static const auto language = [] {
			try {
				switch (XivAlexander::Misc::GameInstallationDetector::GetGameReleaseInfo().Region) {
					case xivres::game_release_publisher::ShengquGames:
						return GameLanguageValue::ChineseSimplified;
					case xivres::game_release_publisher::ActozSoft:
						return GameLanguageValue::Korean;
					case xivres::game_release_publisher::UserjoyGames:
						return GameLanguageValue::TraditionalChinese;
					default:
						break;
				}
				if (!Dll::GetCommandLineError().empty())
					return GameLanguageValue::Unspecified;
				return XivAlexander::Game::CommandLine::WellKnown::GetEffectiveLanguage(XivAlexander::Game::CommandLine::FromString(Dll::GetGameCommandLine()));
			} catch (...) {
				return GameLanguageValue::Unspecified;
			}
		}();
		return language;
	}

	struct Settings {
		GameLanguageValue Game;
		GameLanguageValue Display;
		TalkLineCount TalkLines;
	};

	Settings ReadSettings() {
		const auto config = XivAlexander::Config::Acquire();
		const auto& languages = config->Runtime.Modding.Languages;
		Settings s{.Game = GameLanguage(), .Display = GameLanguageValue::Unspecified, .TalkLines = languages.TalkLines.Value()};
		// Without the game's language, nothing can be told apart from it.
		if (config->Runtime.Modding.Enabled && s.Game != GameLanguageValue::Unspecified)
			s.Display = languages.ResourceOverride.Value();
		if (s.Display == GameLanguageValue::Unspecified)
			s.Display = s.Game;
		return s;
	}

	// The language a noun macro handler runs for when it differs from the game's, for its Localize call; reset by nested handlers.
	thread_local int32_t t_macroLanguage = NoOverride;
	// Set while Localize::ProcessNoun runs for a shadow, for its language call.
	thread_local int32_t t_nounLanguage = NoOverride;
	thread_local int32_t t_returnedLanguage = 0;

	// A Localize with handler maps of its own, for one language; the game's handlers are only made for and kept by it.
	struct ShadowLocalize {
		std::unique_ptr<uint64_t[]> Object;
		std::unique_ptr<uint64_t[]> Head;
	};

	std::mutex s_shadowMutex;

	// Kept for the process's lifetime, as the handlers in them are the game's.
	std::map<std::pair<const void*, int32_t>, ShadowLocalize>& Shadows() {
		static auto& shadows = *new std::map<std::pair<const void*, int32_t>, ShadowLocalize>();
		return shadows;
	}
}

struct XivAlexander::Apps::MainApp::Features::DisplayLanguage::Implementation {
	using LanguageCallFunction = Misc::Hooks::CallSiteFunction<const int32_t*, void*>;

	const std::shared_ptr<Misc::Logger> Logger;
	Game::Resolved::DisplayLanguageFunctions Functions;

	// The 0-based display language when it differs from the game's.
	std::atomic<int32_t> Display{NoOverride};
	std::atomic<uint8_t*> ModuleWrapper{};
	std::atomic_flag JapaneseTalkLinesLogged;
	std::atomic_flag JapaneseFuriganaLogged;

	std::optional<LanguageCallFunction> NounLanguage;
	std::vector<std::unique_ptr<Misc::Hooks::PointerFunctionOf<Game::Resolved::TextModuleProcessNounFn>>> MacroHandlers;
	std::vector<std::unique_ptr<Misc::Hooks::CallSiteFunction<bool, void*, void*, void*>>> MacroLocalizeCalls;
	std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::TextWrapFn>> Wrap;
	std::optional<LanguageCallFunction> WrapLanguage;

	xivres::util::on_dtor::multi Cleanup;

	Implementation()
		: Logger(Misc::Logger::Acquire()) {
		if (const auto status = Game::Resolved::DisplayLanguage.Resolve(Functions); status != Game::Signatures::ResolveError::Ok) {
			Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "Display language: the game's text rules are left as they are: {}", status.Detail);
			return;
		}

		if (Functions.Nouns) {
			try {
				SetUpNouns(*Functions.Nouns);
			} catch (const std::exception& e) {
				Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "Display language: nouns are left as they are: {}", e.what());
			}
		}
		if (Functions.Wrap) {
			try {
				SetUpWrap(*Functions.Wrap);
			} catch (const std::exception& e) {
				Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "Display language: line breaking is left as it is: {}", e.what());
			}
		}
	}

	~Implementation() {
		Cleanup.clear();
		Display = NoOverride;
		RestoreKoreanFlag();
		if (Functions.TalkLines)
			ApplyBranch(*Functions.TalkLines, std::nullopt, "dialogue box lines");
		if (Functions.SubtitleScale)
			ApplyBranch(*Functions.SubtitleScale, std::nullopt, "subtitle scale");
	}

	void SetUpNouns(const Game::Resolved::DisplayLanguageNouns& nouns) {
		NounLanguage.emplace("Localize::ProcessNoun language", nouns.LanguageCall);
		for (const auto& handler : nouns.MacroHandlers) {
			MacroHandlers.emplace_back(std::make_unique<Misc::Hooks::PointerFunctionOf<Game::Resolved::TextModuleProcessNounFn>>("TextModule::ProcessNoun", handler.Function));
			MacroLocalizeCalls.emplace_back(std::make_unique<Misc::Hooks::CallSiteFunction<bool, void*, void*, void*>>("TextModule::ProcessNoun Localize", handler.LocalizeCall));
		}

		Cleanup += NounLanguage->SetHook([this](void* excelModuleInterface) -> const int32_t* {
			if (t_nounLanguage == NoOverride)
				return NounLanguage->bridge(excelModuleInterface);
			t_returnedLanguage = t_nounLanguage;
			return &t_returnedLanguage;
		});
		for (size_t i = 0; i < MacroHandlers.size(); i++) {
			Cleanup += MacroLocalizeCalls[i]->SetHook([this, i](void* localize, void* sheetName, void* output) {
				return MacroLocalizeDetour(i, localize, sheetName, output);
			});
			Cleanup += MacroHandlers[i]->SetHook([this, i](void* textModule, const uint8_t* macro, int32_t length, void* parameters, uint8_t hasCase, int32_t language, void* output) {
				return MacroHandlerDetour(i, textModule, macro, length, parameters, hasCase, language, output);
			});
		}
	}

	void SetUpWrap(const Game::Resolved::DisplayLanguageWrap& wrap) {
		Wrap.emplace("TextWrapper::Wrap", wrap.Wrap);
		WrapLanguage.emplace("TextWrapper::Wrap language", wrap.LanguageCall);

		Cleanup += WrapLanguage->SetHook([this](void* excelModuleInterface) -> const int32_t* {
			const auto display = Display.load();
			if (display == NoOverride)
				return WrapLanguage->bridge(excelModuleInterface);
			t_returnedLanguage = display;
			return &t_returnedLanguage;
		});
		Cleanup += Wrap->SetHook([this](void* wrapper, void* text, void* lines, void* style, uint8_t paging) {
			WrapDetour(wrapper);
			return Wrap->bridge(wrapper, text, lines, style, paging);
		});
	}

	int32_t GameLanguageOf(const void* owner, size_t interfaceOffset, Game::Resolved::ExcelLanguageGetterFn getter) const {
		return *getter(*reinterpret_cast<void* const*>(static_cast<const uint8_t*>(owner) + interfaceOffset));
	}

	void* ShadowOf(void* localize, int32_t language) const {
		const auto& nouns = *Functions.Nouns;
		std::lock_guard lock(s_shadowMutex);
		auto& shadow = Shadows()[{localize, language}];
		if (!shadow.Object) {
			auto head = std::make_unique<uint64_t[]>((nouns.NodeSize + 7) / 8);
			head[0] = head[1] = head[2] = reinterpret_cast<uint64_t>(head.get());  // left, parent, right: empty
			reinterpret_cast<uint8_t*>(head.get())[nouns.NodeIsNil - 1] = 1;  // black
			reinterpret_cast<uint8_t*>(head.get())[nouns.NodeIsNil] = 1;

			// The members before the map are the game's: the interface, and the sheets it loads.
			auto object = std::make_unique<uint64_t[]>((nouns.Map + 2 * sizeof(void*) + 7) / 8);
			const auto bytes = reinterpret_cast<uint8_t*>(object.get());
			memcpy(bytes, localize, nouns.Map);
			*reinterpret_cast<void**>(bytes + nouns.Map) = head.get();
			*reinterpret_cast<size_t*>(bytes + nouns.Map + sizeof(void*)) = 0;
			shadow.Head = std::move(head);
			shadow.Object = std::move(object);
		}
		return shadow.Object.get();
	}

	// A macro in the display language takes Localize, as one in the game's language does, rather than the plain cell; any other keeps what it does.
	int32_t MacroHandlerDetour(size_t index, void* textModule, const uint8_t* macro, int32_t length, void* parameters, uint8_t hasCase, int32_t language, void* output) {
		const auto& nouns = *Functions.Nouns;
		auto macroLanguage = NoOverride;
		if (const auto display = Display.load(); display != NoOverride && language == display) {
			if (const auto game = GameLanguageOf(textModule, nouns.MacroHandlers[index].Interface, nouns.GetLanguage); game != display) {
				macroLanguage = display;
				language = game;
			}
		}
		const auto previous = std::exchange(t_macroLanguage, macroLanguage);
		const auto result = MacroHandlers[index]->bridge(textModule, macro, length, parameters, hasCase, language, output);
		t_macroLanguage = previous;
		return result;
	}

	// The macro's parameters suit only the handler of its own language, which a shadow keeps for it.
	bool MacroLocalizeDetour(size_t index, void* localize, void* sheetName, void* output) {
		const auto language = std::exchange(t_macroLanguage, NoOverride);
		if (language == NoOverride)
			return MacroLocalizeCalls[index]->bridge(localize, sheetName, output);

		const auto shadow = ShadowOf(localize, language);
		const auto previous = std::exchange(t_nounLanguage, language);
		const auto result = MacroLocalizeCalls[index]->bridge(shadow, sheetName, output);
		t_nounLanguage = previous;
		return result;
	}

	// Only AtkModule's wrapper is set up for Korean breaks; it is the one holding what AtkModule's set-up stored.
	void WrapDetour(void* wrapper) {
		const auto& wrap = *Functions.Wrap;
		const auto p = static_cast<uint8_t*>(wrapper);
		if (*reinterpret_cast<uint8_t* const*>(p + wrap.WrapperFonts) != p - wrap.ModuleWrapper + wrap.ModuleFonts)
			return;
		ModuleWrapper = p;
		if (const auto display = Display.load(); display != NoOverride)
			p[wrap.KoreanFlag] = display == GameKorean ? 1 : 0;
	}

	void RestoreKoreanFlag() {
		if (!Functions.Wrap)
			return;
		const auto& wrap = *Functions.Wrap;
		if (const auto p = ModuleWrapper.load())
			p[wrap.KoreanFlag] = GameLanguageOf(p, wrap.Interface, wrap.GetLanguage) == GameKorean ? 1 : 0;
	}

	void ApplyBranch(const Game::Resolved::LanguageBranchPatch& patch, std::optional<bool> first, std::string_view what) {
		const auto wanted = !first ? patch.Original : *first ? patch.JumpToFirst : patch.JumpToSecond;
		const auto current = *reinterpret_cast<const volatile uint16_t*>(patch.Instruction);
		if (current == wanted)
			return;
		if (current != patch.Original && current != patch.JumpToFirst && current != patch.JumpToSecond) {
			Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "Display language: {} left alone, as something else changed it to {:04X}", what, current);
			return;
		}
		if ((reinterpret_cast<uintptr_t>(patch.Instruction) & 63) == 63) {
			Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "Display language: {} left alone, as it spans two cache lines", what);
			return;
		}
		try {
			Misc::Hooks::WriteAtomically(patch.Instruction, wanted);
		} catch (const std::exception& e) {
			Logger->Format<LogLevel::Warning>(LogCategory::GameResourceOverrider, "Display language: {} left alone: {}", what, e.what());
		}
	}

	void Update() {
		const auto s = ReadSettings();
		const auto display = s.Display == s.Game ? NoOverride : static_cast<int32_t>(s.Display) - 1;
		Display = display;

		if (Functions.Wrap) {
			if (display == NoOverride)
				RestoreKoreanFlag();
			else if (const auto p = ModuleWrapper.load())
				p[Functions.Wrap->KoreanFlag] = display == GameKorean ? 1 : 0;
		}

		if (display == GameJapanese && !JapaneseFuriganaLogged.test_and_set())
			Logger->Format(LogCategory::GameResourceOverrider, "Display language: furigana in dialogue boxes are laid out only when the game itself runs in Japanese");

		if (Functions.TalkLines) {
			auto lines = 0;
			switch (s.TalkLines) {
				case TalkLineCount::Three:
					lines = 3;
					break;
				case TalkLineCount::Four:
					lines = 4;
					break;
				case TalkLineCount::Automatic:
				default:
					if (display != NoOverride)
						lines = GameEnglish <= display && display <= GameFrench ? 4 : 3;
					break;
			}
			if (s.Game == GameLanguageValue::Unspecified)
				lines = 0;
			else if (lines == 4 && s.Game == GameLanguageValue::Japanese) {
				if (!JapaneseTalkLinesLogged.test_and_set())
					Logger->Format(LogCategory::GameResourceOverrider, "Display language: dialogue boxes stay at 3 lines, as the Japanese client lays furigana out 3 lines at a time");
				lines = 0;
			}
			ApplyBranch(*Functions.TalkLines, lines ? std::optional(lines == 4) : std::nullopt, "dialogue box lines");
		}

		if (Functions.SubtitleScale) {
			std::optional<bool> enlarged;
			if (display != NoOverride)
				enlarged = display == GameJapanese || display == GameChineseSimplified || display == GameChineseTraditional || display == GameTraditionalChinese;
			ApplyBranch(*Functions.SubtitleScale, enlarged, "subtitle scale");
		}
	}
};

XivAlexander::Apps::MainApp::Features::DisplayLanguage::DisplayLanguage()
	: m_pImpl(std::make_unique<Implementation>()) {
	m_pImpl->Update();
}

XivAlexander::Apps::MainApp::Features::DisplayLanguage::~DisplayLanguage() = default;

void XivAlexander::Apps::MainApp::Features::DisplayLanguage::Update() {
	m_pImpl->Update();
}

bool XivAlexander::Apps::MainApp::Features::DisplayLanguage::IsNeeded() {
	const auto s = ReadSettings();
	return s.Game != GameLanguageValue::Unspecified && (s.Display != s.Game || s.TalkLines != TalkLineCount::Automatic);
}
