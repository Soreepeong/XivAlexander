#include "pch.h"
#include "MainApp/Modding/MergedText.h"

#include <algorithm>
#include <atomic>
#include <initializer_list>

#include <xivres/xivstring.h>

#include "Utils/StringUtils.h"

namespace XivAlexander::Apps::MainApp::Features::Modding {
	namespace {
		using xivres::xivstring;
		using PayloadType = xivstring::xivpayload_type;

		// Fixed macros of these groups are chat links; any other group is auto-translate.
		constexpr uint32_t LinkGroup = 200;
		constexpr uint32_t LinkGroupAlternative = 100;

		enum class LinkKind : uint32_t {
			Item = 4,
			ObjectName = 7,
			Status = 10,
			Quest = 12,
		};

		// Item ids in links carry the item's kind and quality.
		constexpr uint32_t CollectibleItemIdOffset = 500000;
		constexpr uint32_t HighQualityItemIdOffset = 1000000;
		constexpr uint32_t EventItemIdStart = 2000000;
		constexpr uint32_t QuestRowStart = 0x10000;

		// Prefix of object names the game builds for some objects of its own; not part of the sheet text.
		constexpr char ObjectNamePrefix = '\\';

		std::atomic<std::shared_ptr<const MergedText>> s_current;

		struct TextLink {
			xivstring::xivpayload Payload;
			LinkKind Kind{};

			[[nodiscard]] uint32_t Integer(size_t i) const {
				return static_cast<const xivstring::xivexpr_uint32&>(*Payload.params().at(i)).value();
			}

			[[nodiscard]] std::string Text(size_t i) const {
				return static_cast<const xivstring::xivexpr_string&>(*Payload.params().at(i)).value().escaped();
			}

			/// Arguments not replaced are written back byte for byte.
			[[nodiscard]] std::string Replaced(const std::map<size_t, std::string>& replacements) const {
				std::vector<std::unique_ptr<xivstring::xivexpr>> expressions;
				for (size_t i = 0; i < Payload.params().size(); i++) {
					if (const auto it = replacements.find(i); it != replacements.end())
						expressions.emplace_back(std::make_unique<xivstring::xivexpr_string>(xivstring(it->second)));
					else
						expressions.emplace_back(Payload.params()[i]->clone());
				}
				return xivstring::xivpayload(Payload.type(), std::move(expressions)).enclosed();
			}
		};

		/// A link of the kinds that carry text, with arguments shaped as the game writes them; nullopt for anything else.
		std::optional<TextLink> ParseTextLink(std::span<const uint8_t> bytes, const xivstring::macro_info& macro) {
			if (macro.type != PayloadType::Fixed)
				return std::nullopt;
			try {
				TextLink link{xivstring::xivpayload(std::string_view(reinterpret_cast<const char*>(bytes.data()) + 1, macro.size - 2))};
				const auto& e = link.Payload.params();
				const auto isText = [&e](size_t i) { return dynamic_cast<const xivstring::xivexpr_string*>(e[i].get()) != nullptr; };
				const auto isInteger = [&e](size_t i) { return dynamic_cast<const xivstring::xivexpr_uint32*>(e[i].get()) != nullptr; };
				const auto shaped = [&](std::initializer_list<bool> texts) {
					if (e.size() != texts.size() + 2)
						return false;
					size_t i = 2;
					for (const auto text : texts) {
						if (text ? !isText(i) : !isInteger(i))
							return false;
						i++;
					}
					return true;
				};
				if (e.size() < 2 || !isInteger(0) || !isInteger(1))
					return std::nullopt;
				if (link.Integer(0) != LinkGroup && link.Integer(0) != LinkGroupAlternative)
					return std::nullopt;

				link.Kind = static_cast<LinkKind>(link.Integer(1));
				switch (link.Kind) {
					case LinkKind::Item:
					case LinkKind::Quest:
						if (!shaped({false, false, false, false, true}))
							return std::nullopt;
						break;
					case LinkKind::ObjectName:
						if (!shaped({true}))
							return std::nullopt;
						break;
					case LinkKind::Status:
						// Only a status link with its name and description given has text.
						if (!shaped({false, false, true, true}) || link.Integer(3) != 1)
							return std::nullopt;
						break;
					default:
						return std::nullopt;
				}
				return link;
			} catch (const std::exception&) {
				return std::nullopt;
			}
		}

		/// Where the quality marks the game appends to an item name start (private use characters, and a space before them).
		size_t ItemSuffixStart(std::string_view name) {
			auto end = name.size();
			while (end >= 3) {
				const auto b0 = static_cast<uint8_t>(name[end - 3]);
				const auto b1 = static_cast<uint8_t>(name[end - 2]);
				const auto b2 = static_cast<uint8_t>(name[end - 1]);
				const auto privateUse = b0 == 0xEE || (b0 == 0xEF && b1 <= 0xA3);
				if (!privateUse || (b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80)
					break;
				end -= 3;
			}
			if (end != name.size() && end && name[end - 1] == ' ')
				end--;
			return end;
		}

		const std::string* ItemName(const MergedText& names, uint32_t id) {
			const auto& map = id >= EventItemIdStart ? names.EventItemNames : names.ItemNames;
			if (id < EventItemIdStart && id >= HighQualityItemIdOffset)
				id -= HighQualityItemIdOffset;
			else if (id < HighQualityItemIdOffset && id >= CollectibleItemIdOffset)
				id -= CollectibleItemIdOffset;
			const auto it = map.find(id);
			return it == map.end() ? nullptr : &it->second;
		}

		std::optional<std::string> ToBase(const MergedText::LanguageTable& table, std::string_view text) {
			const auto it = table.ToBase.find(text);
			if (it == table.ToBase.end())
				return std::nullopt;
			return it->second;
		}

		std::optional<std::string> RewriteMacro(std::span<const uint8_t> bytes, const xivstring::macro_info& macro, const MergedText& names, const MergedText::LanguageTable& table) {
			const auto link = ParseTextLink(bytes, macro);
			if (!link)
				return std::nullopt;

			std::map<size_t, std::string> replacements;
			switch (link->Kind) {
				case LinkKind::Item:
					if (const auto base = ItemName(names, link->Integer(2))) {
						const auto original = link->Text(6);
						replacements.emplace(6, *base + original.substr(ItemSuffixStart(original)));
					}
					break;
				case LinkKind::Quest:
					if (const auto it = names.QuestNames.find(QuestRowStart + (link->Integer(2) & 0xFFFF)); it != names.QuestNames.end())
						replacements.emplace(6, it->second);
					break;
				case LinkKind::ObjectName: {
					const auto original = link->Text(2);
					auto text = std::string_view(original);
					const auto prefixed = text.starts_with(ObjectNamePrefix);
					if (prefixed)
						text.remove_prefix(1);
					if (auto base = ToBase(table, text))
						replacements.emplace(2, prefixed ? ObjectNamePrefix + *base : std::move(*base));
					break;
				}
				case LinkKind::Status:
					for (const auto i : {size_t{4}, size_t{5}}) {
						if (auto base = ToBase(table, link->Text(i)))
							replacements.emplace(i, std::move(*base));
					}
					break;
			}
			std::erase_if(replacements, [&link](const auto& r) { return r.second == link->Text(r.first); });
			if (replacements.empty())
				return std::nullopt;
			return link->Replaced(replacements);
		}

		struct ChatLayout {
			std::optional<uint16_t> Opcode;
			size_t DataSize;
			size_t TextOffset;
			OutgoingMessageKind Kind;
		};

		constexpr size_t ChatTextFieldSize = 0x400;
		constexpr size_t ZoneChatTypeOffset = 0x18;

		// The zone server's opcodes change with every patch, so its chat is known by its size alone.
		const ChatLayout ChatLayouts[]{
			{std::nullopt, 0x420, 0x1A, OutgoingMessageKind::ZoneChat},
			{uint16_t{0x64}, 0x438, 0x33, OutgoingMessageKind::Tell},
			{uint16_t{0x65}, 0x408, 0x08, OutgoingMessageKind::Channel},
			{uint16_t{0x6B}, 0x408, 0x05, OutgoingMessageKind::Alliance},
			{uint16_t{0x6D}, 0x410, 0x09, OutgoingMessageKind::CrossWorld},
		};
	}

	const MergedText::LanguageTable* MergedText::Find(xivres::game_language language) const {
		const auto it = Languages.find(language);
		return it == Languages.end() ? nullptr : &it->second;
	}

	std::shared_ptr<const MergedText> MergedText::Current() {
		return s_current.load();
	}

	void MergedText::SetCurrent(std::shared_ptr<const MergedText> value) {
		s_current.store(std::move(value));
	}

	std::optional<std::string> MergedText::ToPlainText(std::string_view sheetText) {
		const auto bytes = std::span(reinterpret_cast<const uint8_t*>(sheetText.data()), sheetText.size());
		std::string result;
		for (size_t i = 0; i < sheetText.size();) {
			if (bytes[i] != static_cast<uint8_t>(xivstring::StartOfText)) {
				result.push_back(sheetText[i++]);
				continue;
			}
			const auto macro = xivstring::read_macro(bytes.subspan(i));
			if (!macro)
				return std::nullopt;
			switch (macro->type) {
				case PayloadType::Hyphen:
					result.push_back('-');
					break;
				case PayloadType::NewLine:
				case PayloadType::NonBreakingSpace:
					result.push_back(' ');
					break;
				case PayloadType::SoftHyphen:
				case PayloadType::Italic:
				case PayloadType::Bold:
					break;
				default:
					return std::nullopt;
			}
			i += macro->size;
		}
		return result;
	}

	MergedTextBuilder::MergedTextBuilder(xivres::game_language baseLanguage) {
		m_result->BaseLanguage = baseLanguage;
	}

	void MergedTextBuilder::AddBase(std::string_view text) {
		if (!text.empty())
			m_baseTexts.emplace(text);
	}

	void MergedTextBuilder::Add(xivres::game_language language, std::string_view merged, std::string_view base, uint8_t rank) {
		if (merged.empty() || base.empty() || merged == base)
			return;
		auto& candidates = m_candidates[language];
		const auto [it, inserted] = candidates.try_emplace(std::string(merged), Candidate{std::string(base), rank, false});
		if (inserted)
			return;
		auto& c = it->second;
		if (rank < c.Rank) {
			c = {std::string(base), rank, false};
		} else if (rank == c.Rank && c.Base != base) {
			// Sheets differ in capitalising the same name; the capitalised one reads as a name in chat.
			if (Utils::EqualsI(c.Base, base)) {
				if (base.front() >= 'A' && base.front() <= 'Z')
					c.Base = base;
			} else {
				c.Ambiguous = true;
			}
		}
	}

	std::shared_ptr<const MergedText> MergedTextBuilder::Finish() {
		for (auto& [language, candidates] : m_candidates) {
			MergedText::LanguageTable table;
			for (auto& [merged, c] : candidates) {
				if (m_baseTexts.contains(merged))
					continue;
				table.Merged.insert(merged);
				if (!c.Ambiguous)
					table.ToBase.emplace(merged, std::move(c.Base));
			}
			if (!table.Merged.empty())
				m_result->Languages.emplace(language, std::move(table));
		}
		m_candidates.clear();
		m_baseTexts.clear();
		if (m_result->Languages.empty())
			return nullptr;
		return std::move(m_result);
	}

	std::optional<std::string> RewriteOutgoingText(std::string_view text, const MergedText& names, const MergedText::LanguageTable& table, size_t maxBytes) {
		struct Piece {
			std::string_view Original;
			std::optional<std::string> Replacement;
		};
		std::vector<Piece> pieces;

		const auto addRun = [&](std::string_view run) {
			if (!run.empty())
				pieces.push_back({run});
		};

		const auto bytes = std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size());
		size_t runStart = 0;
		for (size_t i = 0; i < text.size();) {
			if (bytes[i] != static_cast<uint8_t>(xivstring::StartOfText)) {
				i++;
				continue;
			}
			addRun(text.substr(runStart, i - runStart));
			const auto macro = xivstring::read_macro(bytes.subspan(i));
			if (!macro) {
				// Whatever follows can't be told apart from macro bytes.
				pieces.push_back({text.substr(i)});
				runStart = i = text.size();
				break;
			}
			pieces.push_back({text.substr(i, macro->size), RewriteMacro(bytes.subspan(i), *macro, names, table)});
			i += macro->size;
			runStart = i;
		}
		if (runStart < text.size())
			addRun(text.substr(runStart));

		// Shrinking changes first, so that they make room for the others.
		std::vector<bool> applied(pieces.size());
		auto total = text.size();
		for (size_t i = 0; i < pieces.size(); i++) {
			if (pieces[i].Replacement && *pieces[i].Replacement != pieces[i].Original && pieces[i].Replacement->size() <= pieces[i].Original.size()) {
				applied[i] = true;
				total -= pieces[i].Original.size() - pieces[i].Replacement->size();
			}
		}
		for (size_t i = 0; i < pieces.size(); i++) {
			if (pieces[i].Replacement && pieces[i].Replacement->size() > pieces[i].Original.size()) {
				const auto grows = pieces[i].Replacement->size() - pieces[i].Original.size();
				if (total + grows <= maxBytes) {
					applied[i] = true;
					total += grows;
				}
			}
		}
		if (std::ranges::none_of(applied, [](bool b) { return b; }))
			return std::nullopt;

		std::string result;
		result.reserve(total);
		for (size_t i = 0; i < pieces.size(); i++) {
			if (applied[i])
				result += *pieces[i].Replacement;
			else
				result += pieces[i].Original;
		}
		return result;
	}

	OutgoingMessageFindings FindMergedText(std::span<const uint8_t> ipcData, uint16_t opcode, const MergedText::LanguageTable& table) {
		OutgoingMessageFindings result;
		const auto add = [&result](std::string_view text) {
			if (std::ranges::find(result.Texts, text) == result.Texts.end())
				result.Texts.emplace_back(text);
		};
		const auto isMerged = [&table](std::string_view text) {
			return !text.empty() && table.Merged.contains(text);
		};

		for (size_t i = 0; i + 1 < ipcData.size(); i++) {
			if (ipcData[i] != static_cast<uint8_t>(xivstring::StartOfText) || ipcData[i + 1] != static_cast<uint8_t>(PayloadType::Fixed))
				continue;
			const auto macro = xivstring::read_macro(ipcData.subspan(i));
			if (!macro)
				continue;
			if (const auto link = ParseTextLink(ipcData.subspan(i), *macro)) {
				switch (link->Kind) {
					case LinkKind::Item:
						if (const auto name = link->Text(6); isMerged(std::string_view(name).substr(0, ItemSuffixStart(name))) || isMerged(name))
							add(name);
						break;
					case LinkKind::Quest:
						if (const auto name = link->Text(6); isMerged(name))
							add(name);
						break;
					case LinkKind::ObjectName:
						if (const auto name = link->Text(2); isMerged(name) || (name.starts_with(ObjectNamePrefix) && isMerged(std::string_view(name).substr(1))))
							add(name);
						break;
					case LinkKind::Status:
						for (const auto k : {size_t{4}, size_t{5}}) {
							if (const auto text = link->Text(k); isMerged(text))
								add(text);
						}
						break;
				}
			}
			i += macro->size - 1;
		}

		for (const auto& layout : ChatLayouts) {
			if ((layout.Opcode && *layout.Opcode != opcode) || ipcData.size() != layout.DataSize)
				continue;
			const auto field = ipcData.subspan(layout.TextOffset, ChatTextFieldSize);
			const auto end = std::ranges::find(field, uint8_t{0});
			if (end == field.begin() || end == field.end() || std::any_of(end, field.end(), [](uint8_t b) { return b != 0; }))
				continue;
			result.Kind = layout.Kind;
			if (layout.Kind == OutgoingMessageKind::ZoneChat)
				result.ChatType = static_cast<uint16_t>(ipcData[ZoneChatTypeOffset] | ipcData[ZoneChatTypeOffset + 1] << 8);
			break;
		}
		return result;
	}
}
