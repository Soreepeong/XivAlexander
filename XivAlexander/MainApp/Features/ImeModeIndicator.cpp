#include "pch.h"
#include "MainApp/Features/ImeModeIndicator.h"

#include <imm.h>

#include <xivres/util.on_dtor.h>

#include "Game/SignatureDefinitions.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"

namespace {
	// The indicator shows U+E01F + mode, or a blank for 0.
	constexpr uint32_t ImeModeClosed = 0;
	constexpr uint32_t ImeModeFullwidthLatin = 3;  // Ａ
	constexpr uint32_t ImeModeHangul = 6;  // 가
	constexpr uint32_t ImeModeChinese = 7;  // 中
	constexpr uint32_t ImeModeChineseLatin = 8;  // 英

	// LANG_KOREAN or LANG_CHINESE for the IMEs the game does not understand; LANG_NEUTRAL otherwise.
	WORD KoreanOrChineseKeyboardLanguage() {
		const auto language = PRIMARYLANGID(LOWORD(reinterpret_cast<uintptr_t>(GetKeyboardLayout(0))));
		return language == LANG_KOREAN || language == LANG_CHINESE ? language : static_cast<WORD>(LANG_NEUTRAL);
	}
}

struct XivAlexander::Apps::MainApp::Features::ImeModeIndicator::Implementation {
	const std::shared_ptr<Misc::Logger> Logger;

	Game::Resolved::ImeModeIndicatorFunctions Getter;
	std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::ImeModeGetterFn>> GetImeMode;
	Misc::Hooks::ImportedFunction<BOOL, HIMC, DWORD, DWORD> ImmSetConversionStatus{"imm32!ImmSetConversionStatus", "imm32.dll", "ImmSetConversionStatus"};

	xivres::util::on_dtor::multi Cleanup;

	Implementation()
		: Logger(Misc::Logger::Acquire()) {
		if (ImmSetConversionStatus)
			Cleanup += ImmSetConversionStatus.SetHook([this](HIMC inputContext, DWORD conversion, DWORD sentence) { return ImmSetConversionStatusDetour(inputContext, conversion, sentence); });
		else
			Logger->Log(LogCategory::General, "IME conversion modes the game sets are left as is: ImmSetConversionStatus is not imported");

		if (const auto status = Game::Resolved::ImeModeIndicator.Resolve(Getter); status != Game::Signatures::ResolveError::Ok) {
			Logger->Format<LogLevel::Warning>(LogCategory::General, "IME mode indicator is left as is: {}", status.Detail);
			return;
		}

		GetImeMode.emplace("TextService::GetImeMode", Getter.GetImeMode);
		Cleanup += GetImeMode->SetHook([this](void* textService) { return GetImeModeDetour(textService); });
	}

	~Implementation() {
		Cleanup.clear();
	}

	// The game picks Japanese modes, where hiragana is native and full-width; to a Korean or Chinese IME that turns on
	// full-width Latin, so these keep whatever width the IME has and take only the native or Latin choice.
	BOOL ImmSetConversionStatusDetour(HIMC inputContext, DWORD conversion, DWORD sentence) {
		if (KoreanOrChineseKeyboardLanguage() != LANG_NEUTRAL) {
			constexpr DWORD kept = IME_CMODE_FULLSHAPE | IME_CMODE_KATAKANA;
			if (DWORD current, currentSentence; ImmGetConversionStatus(inputContext, &current, &currentSentence))
				conversion = (conversion & ~kept) | (current & kept);
		}
		return ImmSetConversionStatus.bridge(inputContext, conversion, sentence);
	}

	uint32_t GetImeModeDetour(void* textService) {
		const auto language = KoreanOrChineseKeyboardLanguage();
		if (language == LANG_NEUTRAL)
			return GetImeMode->bridge(textService);

		const auto inputContext = static_cast<HIMC>(*Getter.InputContext);
		if (!inputContext)
			return ImeModeClosed;

		if (!ImmGetOpenStatus(inputContext))
			return language == LANG_KOREAN ? ImeModeFullwidthLatin : ImeModeClosed;

		DWORD conversion, sentence;
		if (!ImmGetConversionStatus(inputContext, &conversion, &sentence))
			return GetImeMode->bridge(textService);

		const auto native = !!(conversion & IME_CMODE_NATIVE);  // IME_CMODE_HANGUL for Korean
		if (language == LANG_KOREAN)
			return native ? ImeModeHangul : ImeModeFullwidthLatin;
		return native ? ImeModeChinese : ImeModeChineseLatin;
	}
};

XivAlexander::Apps::MainApp::Features::ImeModeIndicator::ImeModeIndicator()
	: m_pImpl(std::make_unique<Implementation>()) {
}

XivAlexander::Apps::MainApp::Features::ImeModeIndicator::~ImeModeIndicator() = default;
