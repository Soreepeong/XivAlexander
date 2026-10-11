#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <xivres/common.h>

namespace XivAlexander::Apps::MainApp::Features::Modding {
	/// Name text ExcelMerger put in place of the game's own, with the game's own text, for keeping it out of what the game sends.
	class MergedText {
	public:
		struct StringHash {
			using is_transparent = void;
			size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
		};

		struct LanguageTable {
			// Merged text that stands for one base text; text standing for several is left out.
			std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> ToBase;
			// Merged text that is no base text of any name cell.
			std::unordered_set<std::string, StringHash, std::equal_to<>> Merged;
		};

		xivres::game_language BaseLanguage = xivres::game_language::Unspecified;

		// By row id, in the base language, as the game words them in links.
		std::unordered_map<uint32_t, std::string> ItemNames;
		std::unordered_map<uint32_t, std::string> EventItemNames;
		std::unordered_map<uint32_t, std::string> QuestNames;

		std::map<xivres::game_language, LanguageTable> Languages;

		[[nodiscard]] const LanguageTable* Find(xivres::game_language language) const;

		/// What the merge of this session produced; null when it replaced nothing.
		[[nodiscard]] static std::shared_ptr<const MergedText> Current();
		static void SetCurrent(std::shared_ptr<const MergedText> value);

		/// Sheet text as it reads in chat: line breaks and hyphen macros as characters, emphasis dropped; nullopt for any other macro.
		[[nodiscard]] static std::optional<std::string> ToPlainText(std::string_view sheetText);

	};

	class MergedTextBuilder {
		struct Candidate {
			std::string Base;
			uint8_t Rank{};
			bool Ambiguous{};
		};

		std::shared_ptr<MergedText> m_result = std::make_shared<MergedText>();
		std::map<xivres::game_language, std::unordered_map<std::string, Candidate>> m_candidates;
		std::unordered_set<std::string> m_baseTexts;

	public:
		explicit MergedTextBuilder(xivres::game_language baseLanguage);

		MergedText& Result() { return *m_result; }

		/// A cell's text in the base language; merged text equal to any of these is not told apart from the base language.
		void AddBase(std::string_view text);

		/// A cell the merge replaced; for text that stands for several base texts, the lower rank wins, and a tie keeps none.
		void Add(xivres::game_language language, std::string_view merged, std::string_view base, uint8_t rank);

		/// Null if nothing was replaced.
		std::shared_ptr<const MergedText> Finish();
	};

	inline constexpr size_t MaxChatMessageBytes = 0x3FF;

	/// The chat text the game sends with merged names in links changed to the base language's; nullopt if nothing changed.
	/// Changes that would take the text over maxBytes are left out, each link whole.
	[[nodiscard]] std::optional<std::string> RewriteOutgoingText(std::string_view text, const MergedText& names, const MergedText::LanguageTable& table, size_t maxBytes = MaxChatMessageBytes);

	enum class OutgoingMessageKind {
		Other,
		ZoneChat,
		Tell,
		Channel,
		Alliance,
		CrossWorld,
	};

	struct OutgoingMessageFindings {
		OutgoingMessageKind Kind = OutgoingMessageKind::Other;
		uint16_t ChatType{};  // for ZoneChat
		std::vector<std::string> Texts;
	};

	/// Merged text left in an outgoing IPC message (the data after its IPC header): in well-formed chat links.
	[[nodiscard]] OutgoingMessageFindings FindMergedText(std::span<const uint8_t> ipcData, uint16_t opcode, const MergedText::LanguageTable& table);
}
