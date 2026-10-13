#include "pch.h"
#include "MainApp/Modding/ExcelMerger.h"

#include <srell.hpp>
#include <xivres/excel.h>
#include <xivres/excel.type2gen.h>
#include <xivres/packed_stream.h>
#include <xivres/sqpack.reader.h>
#include <xivres/unpacked_stream.h>
#include <xivres/util.thread_pool.h>
#include <xivres/xivstring.h>

#include "MainApp/Modding/MergedText.h"
#include "Utils/StringUtils.h"

#pragma comment(lib, "Normaliz.lib")

namespace XivAlexander::Apps::MainApp::Features::Modding {
	namespace {
		using xivres::game_language;
		using xivres::excel::cell_type;

		constexpr const char* DeniedSheets[]{"CustomTalk"};

		// Sheets whose names reach chat through links, drag and drop, or pasting; columns by index, the one the game shows first.
		struct NameSheet {
			const char* Name;
			std::array<int, 3> Columns;
		};

		constexpr int NoColumn = -1;
		constexpr NameSheet NameSheets[]{
			{"Item", {9, 0, 2}},  // Name, Singular, Plural
			{"EventItem", {9, 0, 2}},
			{"Quest", {0, NoColumn, NoColumn}},
			{"Action", {0, NoColumn, NoColumn}},
			{"GeneralAction", {0, NoColumn, NoColumn}},
			{"CraftAction", {0, NoColumn, NoColumn}},
			{"PetAction", {0, NoColumn, NoColumn}},
			{"BuddyAction", {0, NoColumn, NoColumn}},
			{"MainCommand", {5, NoColumn, NoColumn}},
			{"ActionComboRoute", {0, NoColumn, NoColumn}},
			{"Emote", {0, NoColumn, NoColumn}},
			{"Status", {0, 1, NoColumn}},  // Name, Description
			{"BNpcName", {0, 2, NoColumn}},
			{"ENpcResident", {0, 2, NoColumn}},
			{"EObjName", {0, 2, NoColumn}},
			{"PlaceName", {0, 2, NoColumn}},
			{"Mount", {0, 2, NoColumn}},
			{"Companion", {0, 2, NoColumn}},
			{"Ornament", {8, 10, NoColumn}},
			{"Glasses", {13, 4, 6}},
			{"ContentFinderCondition", {43, NoColumn, NoColumn}},
		};

		// Item and event item links carry the Singular column instead of Name in these languages.
		bool ItemLinksUseSingular(game_language language) {
			return language == game_language::Japanese || language == game_language::ChineseSimplified || language == game_language::Korean || language == game_language::TraditionalChinese;
		}

		constexpr double RowIdAgreementThreshold = 0.9;
		constexpr double FingerprintCoverageThreshold = 0.9;
		constexpr double PluralSimilarityThreshold = 0.9;
		constexpr size_t SampleRowCount = 3000;

		uint32_t ReadBe32(const char* p) {
			return static_cast<uint32_t>(static_cast<uint8_t>(p[0])) << 24
				| static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 16
				| static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 8
				| static_cast<uint32_t>(static_cast<uint8_t>(p[3]));
		}

		uint16_t ReadBe16(const char* p) {
			return static_cast<uint16_t>(static_cast<uint8_t>(p[0]) << 8 | static_cast<uint8_t>(p[1]));
		}

		size_t CellSize(cell_type type) {
			switch (type) {
				case cell_type::String:
				case cell_type::Int32:
				case cell_type::UInt32:
				case cell_type::Float32:
					return 4;
				case cell_type::Int16:
				case cell_type::UInt16:
					return 2;
				case cell_type::Int64:
				case cell_type::UInt64:
					return 8;
				default:
					return 1;
			}
		}

		bool IsPackedBool(cell_type type) {
			return type >= cell_type::PackedBool0 && type <= cell_type::PackedBool7;
		}

		struct Column {
			cell_type Type{};
			uint16_t Offset{};
			size_t Index{};

			[[nodiscard]] bool IsString() const { return Type == cell_type::String; }
		};

		std::vector<Column> GetColumns(const xivres::excel::exh::reader& exh) {
			std::vector<Column> res;
			for (size_t i = 0; i < exh.get_columns().size(); ++i)
				res.push_back({*exh.get_column(i).Type, *exh.get_column(i).Offset, i});
			return res;
		}

		std::vector<Column> StringColumnsByOffset(const std::vector<Column>& columns) {
			std::vector<Column> res;
			for (const auto& c : columns) {
				if (c.IsString())
					res.push_back(c);
			}
			std::ranges::sort(res, {}, &Column::Offset);
			return res;
		}

		struct RowRef {
			const char* Data{};
			uint32_t Size{};
			uint16_t SubRows{};
		};

		struct ExdFile {
			std::vector<char> Bytes;
			std::vector<std::pair<uint32_t, RowRef>> Rows;
		};

		std::optional<std::vector<char>> ReadEntry(const xivres::sqpack::reader& reader, const xivres::path_spec& path) {
			const auto index = reader.find_entry_index(path);
			if (index == (std::numeric_limits<size_t>::max)())
				return std::nullopt;
			return reader.at(reader.Entries[index])->read_vector<char>();
		}

		bool EntryExists(const xivres::sqpack::reader& reader, const xivres::path_spec& path) {
			return reader.find_entry_index(path) != (std::numeric_limits<size_t>::max)();
		}

		ExdFile ParseExd(std::vector<char> bytes) {
			ExdFile file;
			file.Bytes = std::move(bytes);
			const auto& b = file.Bytes;
			if (b.size() < 0x20 || std::memcmp(b.data(), "EXDF", 4) != 0)
				throw std::runtime_error("not an EXD file");
			const auto indexSize = ReadBe32(&b[8]);
			if (0x20ULL + indexSize > b.size())
				throw std::runtime_error("EXD index out of range");
			for (size_t i = 0; i < indexSize / 8; ++i) {
				const auto id = ReadBe32(&b[0x20 + i * 8]);
				const auto offset = ReadBe32(&b[0x20 + i * 8 + 4]);
				if (offset + 6ULL > b.size())
					throw std::runtime_error("EXD row out of range");
				const auto size = ReadBe32(&b[offset]);
				const auto subRows = ReadBe16(&b[offset + 4]);
				if (offset + 6ULL + size > b.size())
					throw std::runtime_error("EXD row data out of range");
				file.Rows.emplace_back(id, RowRef{&b[offset + 6], size, subRows});
			}
			std::ranges::sort(file.Rows, {}, &std::pair<uint32_t, RowRef>::first);
			return file;
		}

		/// Every page of one language of a sheet, as present in one client.
		struct LanguageData {
			std::vector<std::shared_ptr<ExdFile>> Pages;  // Null where missing.
			std::map<uint32_t, RowRef> Rows;

			[[nodiscard]] bool Any() const { return std::ranges::any_of(Pages, [](const auto& p) { return !!p; }); }
		};

		LanguageData LoadLanguage(const xivres::sqpack::reader& reader, const xivres::excel::exh::reader& exh, game_language language) {
			LanguageData data;
			for (const auto& page : exh.get_pages()) {
				auto bytes = ReadEntry(reader, exh.get_exd_path(page, language));
				if (!bytes) {
					data.Pages.emplace_back();
					continue;
				}
				auto file = std::make_shared<ExdFile>(ParseExd(std::move(*bytes)));
				for (const auto& [id, row] : file->Rows)
					data.Rows.emplace(id, row);
				data.Pages.emplace_back(std::move(file));
			}
			return data;
		}

		std::string_view GetString(const RowRef& row, uint16_t fixedSize, uint16_t offset) {
			if (offset + 4ULL > row.Size || fixedSize > row.Size)
				return {};
			const auto start = fixedSize + static_cast<size_t>(ReadBe32(row.Data + offset));
			if (start >= row.Size)
				return {};
			const auto view = std::string_view(row.Data + start, row.Size - start);
			return view.substr(0, view.find('\0'));
		}

		uint64_t GetNumeric(const RowRef& row, const Column& column) {
			const auto size = CellSize(column.Type);
			if (column.Offset + size > row.Size)
				return 0;
			if (IsPackedBool(column.Type))
				return (static_cast<uint8_t>(row.Data[column.Offset]) >> (static_cast<int>(column.Type) - static_cast<int>(cell_type::PackedBool0))) & 1;
			uint64_t value = 0;
			for (size_t i = 0; i < size; ++i)
				value = value << 8 | static_cast<uint8_t>(row.Data[column.Offset + i]);
			return value;
		}

		std::string LanguageCode(game_language language) {
			const auto code = xivres::game_language_code(language);
			return code ? code : "";
		}

		// ---- String content features, for checking column mappings by content ----

		struct StringFeatures {
			bool Empty = true;
			std::string Signature;  // Payload bytes, each prefixed with its length.
			size_t VisibleLength = 0;
			std::string Digits;  // Digit runs separated by ','.
		};

		size_t ReadSeInt(std::string_view b, size_t& i) {
			const auto v = static_cast<uint8_t>(b.at(i));
			if (v < 0xF0) {
				i++;
				return v - 1u;
			}
			const auto mask = (v + 1) & 0xF;
			size_t value = 0;
			i++;
			for (int k = 3; k >= 0; --k) {
				if (mask & (1 << k))
					value |= static_cast<size_t>(static_cast<uint8_t>(b.at(i++))) << (8 * k);
			}
			return value;
		}

		StringFeatures GetFeatures(std::string_view b) {
			StringFeatures f;
			if (b.empty())
				return f;
			f.Empty = false;
			std::string visible;
			size_t i = 0;
			try {
				while (i < b.size()) {
					if (b[i] == 2 && i + 2 < b.size()) {
						auto j = i + 2;
						const auto len = ReadSeInt(b, j);
						const auto end = (std::min)(j + len, b.size());
						f.Signature += std::format("{}:", end - (i + 1));
						f.Signature.append(b.substr(i + 1, end - (i + 1)));
						i = end + 1;
					} else {
						visible.push_back(b[i++]);
					}
				}
			} catch (const std::out_of_range&) {
				// Truncated payload; keep what was read.
			}
			for (const auto c : visible) {
				if ((static_cast<uint8_t>(c) & 0xC0) != 0x80)
					f.VisibleLength++;
			}
			for (size_t k = 0; k < visible.size();) {
				if (visible[k] >= '0' && visible[k] <= '9') {
					const auto start = k;
					while (k < visible.size() && visible[k] >= '0' && visible[k] <= '9')
						k++;
					f.Digits.append(visible, start, k - start);
					f.Digits.push_back(',');
				} else {
					k++;
				}
			}
			return f;
		}

		std::optional<double> Spearman(const std::vector<double>& xs, const std::vector<double>& ys) {
			const auto n = xs.size();
			if (n < 8)
				return std::nullopt;
			const auto rank = [n](const std::vector<double>& v) {
				std::vector<size_t> order(n);
				std::iota(order.begin(), order.end(), 0);
				std::ranges::sort(order, [&v](size_t a, size_t b) { return v[a] < v[b]; });
				std::vector<double> r(n);
				for (size_t i = 0; i < n;) {
					auto j = i;
					while (j + 1 < n && v[order[j + 1]] == v[order[i]])
						j++;
					for (auto k = i; k <= j; ++k)
						r[order[k]] = (i + j) / 2.0;
					i = j + 1;
				}
				return r;
			};
			const auto rx = rank(xs), ry = rank(ys);
			const auto mx = std::accumulate(rx.begin(), rx.end(), 0.0) / n;
			const auto my = std::accumulate(ry.begin(), ry.end(), 0.0) / n;
			double sxy = 0, sxx = 0, syy = 0;
			for (size_t i = 0; i < n; ++i) {
				sxy += (rx[i] - mx) * (ry[i] - my);
				sxx += (rx[i] - mx) * (rx[i] - mx);
				syy += (ry[i] - my) * (ry[i] - my);
			}
			if (sxx == 0 || syy == 0)
				return std::nullopt;
			return sxy / std::sqrt(sxx * syy);
		}

		struct StringColumnSample {
			std::vector<std::string_view> Values;
			std::vector<StringFeatures> Features;
			bool NonEmpty = false;
		};

		/// How much two columns of the same rows look like the same text in two languages; second is whether there was enough to tell.
		std::pair<double, bool> StringScore(const StringColumnSample& t, const StringColumnSample& s) {
			const auto n = t.Values.size();
			if (!n)
				return {0.0, false};
			std::vector<std::pair<double, double>> signals;

			size_t both = 0, equal = 0;
			for (size_t i = 0; i < n; ++i) {
				if (!t.Values[i].empty() && !s.Values[i].empty()) {
					both++;
					equal += t.Values[i] == s.Values[i];
				}
			}
			if (both && equal * 2 > both)
				signals.emplace_back(static_cast<double>(equal) / both, 3.0);

			double a = 0, d = 0, b = 0, c = 0;
			for (size_t i = 0; i < n; ++i) {
				const auto et = t.Features[i].Empty, es = s.Features[i].Empty;
				if (!et && !es)
					a++;
				else if (et && es)
					d++;
				else if (!et && es)
					b++;
				else
					c++;
			}
			if (const auto den = std::sqrt((a + b) * (c + d) * (a + c) * (b + d)); den > 0)
				signals.emplace_back((std::max)((a * d - b * c) / den, 0.0), 2.0);
			else
				signals.emplace_back(a + d == static_cast<double>(n) ? 1.0 : 0.0, 0.3);

			size_t sigTotal = 0, sigEqual = 0, digTotal = 0, digEqual = 0;
			std::vector<double> lx, ly;
			for (size_t i = 0; i < n; ++i) {
				const auto& x = t.Features[i];
				const auto& y = s.Features[i];
				if (!x.Signature.empty() || !y.Signature.empty()) {
					sigTotal++;
					sigEqual += x.Signature == y.Signature;
				}
				if (!x.Digits.empty() || !y.Digits.empty()) {
					digTotal++;
					digEqual += x.Digits == y.Digits;
				}
				if (!x.Empty && !y.Empty) {
					lx.push_back(static_cast<double>(x.VisibleLength));
					ly.push_back(static_cast<double>(y.VisibleLength));
				}
			}
			if (sigTotal >= 3)
				signals.emplace_back(static_cast<double>(sigEqual) / sigTotal, 2.0);
			if (digTotal >= 3)
				signals.emplace_back(static_cast<double>(digEqual) / digTotal, 2.0);
			if (lx.size() >= 8) {
				if (const auto r = Spearman(lx, ly))
					signals.emplace_back((std::max)(*r, 0.0), 1.5);
			}

			double wsum = 0, sum = 0;
			for (const auto& [score, weight] : signals) {
				wsum += weight;
				sum += score * weight;
			}
			return {sum / wsum, wsum > 1.0};
		}

		/// Order-preserving alignment maximizing the sum of (score - tau) over matched pairs.
		std::map<size_t, size_t> AlignOrdered(size_t m, size_t n, const std::vector<std::vector<double>>& score, double tau) {
			std::vector dp(m + 1, std::vector(n + 1, 0.0));
			std::vector bt(m + 1, std::vector(n + 1, 0));
			for (size_t i = 1; i <= m; ++i) {
				for (size_t j = 1; j <= n; ++j) {
					auto best = dp[i - 1][j];
					auto how = 1;
					if (dp[i][j - 1] > best) {
						best = dp[i][j - 1];
						how = 2;
					}
					if (const auto v = dp[i - 1][j - 1] + score[i - 1][j - 1] - tau; v > best) {
						best = v;
						how = 3;
					}
					dp[i][j] = best;
					bt[i][j] = how;
				}
			}
			std::map<size_t, size_t> res;
			for (auto i = m, j = n; i > 0 && j > 0;) {
				if (bt[i][j] == 3) {
					res[i - 1] = j - 1;
					i--;
					j--;
				} else if (bt[i][j] == 1) {
					i--;
				} else {
					j--;
				}
			}
			return res;
		}

		std::map<size_t, size_t> AlignGreedy(size_t m, size_t n, const std::vector<std::vector<double>>& score, double tau) {
			std::vector<std::tuple<double, size_t, size_t>> pairs;
			for (size_t i = 0; i < m; ++i)
				for (size_t j = 0; j < n; ++j)
					pairs.emplace_back(score[i][j], i, j);
			std::ranges::sort(pairs, std::greater());
			std::set<size_t> usedT, usedS;
			std::map<size_t, size_t> res;
			for (const auto& [s, i, j] : pairs) {
				if (s < tau)
					break;
				if (usedT.contains(i) || usedS.contains(j))
					continue;
				res[i] = j;
				usedT.insert(i);
				usedS.insert(j);
			}
			return res;
		}

		/// Between consecutive anchors, maps positionally where both sides have gaps of the same size.
		std::map<size_t, size_t> FillGaps(std::map<size_t, size_t> al, size_t m, size_t n) {
			std::vector<std::pair<ptrdiff_t, ptrdiff_t>> anchors{{-1, -1}};
			for (const auto& [i, j] : al)
				anchors.emplace_back(i, j);
			anchors.emplace_back(static_cast<ptrdiff_t>(m), static_cast<ptrdiff_t>(n));
			for (size_t k = 0; k + 1 < anchors.size(); ++k) {
				const auto [i0, j0] = anchors[k];
				const auto [i1, j1] = anchors[k + 1];
				if (i1 - i0 == j1 - j0 && i1 - i0 > 1) {
					for (ptrdiff_t d = 1; d < i1 - i0; ++d)
						al[static_cast<size_t>(i0 + d)] = static_cast<size_t>(j0 + d);
				}
			}
			return al;
		}

		/// Like difflib's ratio, from the longest common subsequence of the lowercased bytes.
		double Similarity(std::string_view a, std::string_view b) {
			a = a.substr(0, 200);
			b = b.substr(0, 200);
			if (a.empty() && b.empty())
				return 1.0;
			const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
			std::vector<uint16_t> prev(b.size() + 1), cur(b.size() + 1);
			for (size_t i = 1; i <= a.size(); ++i) {
				for (size_t j = 1; j <= b.size(); ++j)
					cur[j] = lower(a[i - 1]) == lower(b[j - 1]) ? static_cast<uint16_t>(prev[j - 1] + 1) : (std::max)(prev[j], cur[j - 1]);
				std::swap(prev, cur);
			}
			return 2.0 * prev[b.size()] / static_cast<double>(a.size() + b.size());
		}

		// ---- Phrases typed in /say for quests ----

		std::vector<std::string_view> Split(std::string_view s, char delimiter) {
			std::vector<std::string_view> res;
			for (size_t start = 0;;) {
				const auto pos = s.find(delimiter, start);
				res.push_back(s.substr(start, pos == std::string_view::npos ? std::string_view::npos : pos - start));
				if (pos == std::string_view::npos)
					return res;
				start = pos + 1;
			}
		}

		bool IsQuotedIn(std::string_view phrase, std::string_view text) {
			static constexpr std::pair<std::string_view, std::string_view> Quotes[]{
				{"\xe2\x80\x9c", "\xe2\x80\x9d"}, {"\xe3\x80\x8c", "\xe3\x80\x8d"}, {"\xe3\x80\x8e", "\xe3\x80\x8f"},
				{"\xc2\xbb", "\xc2\xab"}, {"\xc2\xab", "\xc2\xbb"}, {"\xe2\x80\x9e", "\xe2\x80\x9c"}, {"\"", "\""},
			};
			constexpr std::string_view whitespace = " \t\n\r\v\f";
			const auto first = phrase.find_first_not_of(whitespace);
			if (first == std::string_view::npos)
				return false;
			phrase = phrase.substr(first, phrase.find_last_not_of(whitespace) - first + 1);

			// Either side may end the phrase with its own punctuation.
			static constexpr std::string_view Punctuations[]{"", ".", "!", "?", "\xef\xbc\x81", "\xef\xbc\x9f", "\xe3\x80\x82"};
			for (const auto& p : Punctuations | std::views::drop(1)) {
				if (phrase.ends_with(p))
					phrase.remove_suffix(p.size());
			}
			for (const auto& [open, close] : Quotes) {
				for (const auto& p : Punctuations) {
					if (text.find(std::format("{}{}{}{}", open, phrase, p, close)) != std::string_view::npos)
						return true;
				}
			}
			return false;
		}

		bool HasSayMarker(std::string_view text) {
			static constexpr std::string_view Markers[]{
				{"\x02\x48\x04\xF2\x01\xF4\x03\x02\x49\x04\xF2\x01\xF5\x03", 14},
				"Say", "Sagen", "Dire", "\xeb\xa7\x90\xed\x95\x98\xea\xb8\xb0",
			};
			return std::ranges::any_of(Markers, [text](std::string_view m) { return text.find(m) != std::string_view::npos; });
		}

		/// Row ids whose text is a phrase the player types in /say, which must stay in the client's language.
		std::set<uint32_t> DetectSayPhraseRows(const LanguageData& data, uint16_t fixedSize, uint16_t keyOffset, uint16_t textOffset) {
			struct Item {
				uint32_t Id;
				std::string_view Key;
				std::string_view Text;
			};
			std::vector<Item> items;
			for (const auto& [id, row] : data.Rows)
				items.push_back({id, GetString(row, fixedSize, keyOffset), GetString(row, fixedSize, textOffset)});

			std::set<uint32_t> res;
			for (const auto& item : items) {
				if (item.Text.empty() || item.Text.size() > 80 || item.Text.find('\x02') != std::string_view::npos)
					continue;
				const auto parts = Split(item.Key, '_');
				if (std::ranges::any_of(parts, [](std::string_view p) { return p == "SAY" || p == "SAYTODO"; })) {
					res.insert(item.Id);
					continue;
				}
				if (parts.size() <= 4 || parts[3] != "SYSTEM")
					continue;
				for (const auto& other : items) {
					if (other.Id != item.Id && other.Text.size() > item.Text.size() + 10 && HasSayMarker(other.Text) && IsQuotedIn(item.Text, other.Text)) {
						res.insert(item.Id);
						break;
					}
				}
			}
			return res;
		}

		// ---- Text steps from the override files ----

		// Supplementary Private Use Area-A; the game's icons are in the BMP private use area.
		constexpr char32_t PlaceholderBase = 0xF0000;
		constexpr size_t MaxPlaceholders = 0xFFFE;
		constexpr char StepSeparator = '\x1F';
		constexpr std::string_view NewLineMacro{"\x02\x10\x01\x03", 4};

		struct TextStep {
			enum class Kind { Replace, ToNfd, ToNfc };
			Kind Kind{};
			std::shared_ptr<const srell::u8cregex> Pattern;
			std::string Replacement;
		};

		struct StepList {
			std::string Label;
			std::vector<TextStep> Steps;
		};

		void AppendUtf8(std::string& s, char32_t c) {
			if (c < 0x80) {
				s.push_back(static_cast<char>(c));
			} else if (c < 0x800) {
				s.push_back(static_cast<char>(0xC0 | c >> 6));
				s.push_back(static_cast<char>(0x80 | (c & 0x3F)));
			} else if (c < 0x10000) {
				s.push_back(static_cast<char>(0xE0 | c >> 12));
				s.push_back(static_cast<char>(0x80 | (c >> 6 & 0x3F)));
				s.push_back(static_cast<char>(0x80 | (c & 0x3F)));
			} else {
				s.push_back(static_cast<char>(0xF0 | c >> 18));
				s.push_back(static_cast<char>(0x80 | (c >> 12 & 0x3F)));
				s.push_back(static_cast<char>(0x80 | (c >> 6 & 0x3F)));
				s.push_back(static_cast<char>(0x80 | (c & 0x3F)));
			}
		}

		/// \n, \t, \\, \uXXXX and \u{X...} as in JavaScript strings; any other backslash stays as written.
		std::string UnescapeReplacement(std::string_view s) {
			std::string res;
			for (size_t i = 0; i < s.size(); ++i) {
				if (s[i] != '\\' || i + 1 >= s.size()) {
					res.push_back(s[i]);
					continue;
				}
				const auto c = s[i + 1];
				if (c == 'n' || c == 't' || c == '\\') {
					res.push_back(c == 'n' ? '\n' : c == 't' ? '\t' : '\\');
					i++;
					continue;
				}
				if (c == 'u') {
					auto start = i + 2;
					size_t end, next;
					if (start < s.size() && s[start] == '{') {
						start++;
						end = s.find('}', start);
						next = end + 1;
					} else {
						end = start + 4 <= s.size() ? start + 4 : std::string_view::npos;
						next = end;
					}
					uint32_t value = 0;
					if (end != std::string_view::npos && end > start) {
						const auto [ptr, ec] = std::from_chars(s.data() + start, s.data() + end, value, 16);
						if (ec == std::errc() && ptr == s.data() + end && value <= 0x10FFFF && (value < 0xD800 || value > 0xDFFF)) {
							AppendUtf8(res, value);
							i = next - 1;
							continue;
						}
					}
				}
				res.push_back(s[i]);
			}
			return res;
		}

		StepList ParseStepList(const nlohmann::ordered_json& value, std::string label) {
			StepList list{std::move(label)};
			for (const auto& step : value) {
				if (step.is_string()) {
					const auto name = step.get<std::string>();
					if (name == "to-nfd")
						list.Steps.push_back({TextStep::Kind::ToNfd});
					else if (name == "to-nfc")
						list.Steps.push_back({TextStep::Kind::ToNfc});
					else
						throw std::runtime_error(std::format("unknown step \"{}\"", name));
				} else if (step.is_array() && step.size() == 2 && step[0].is_string() && step[1].is_string()) {
					TextStep s{TextStep::Kind::Replace};
					try {
						s.Pattern = std::make_shared<srell::u8cregex>(step[0].get<std::string>(), srell::regex_constants::ECMAScript);
					} catch (const std::exception& e) {
						throw std::runtime_error(std::format("invalid pattern {}: {}", step[0].dump(), e.what()));
					}
					s.Replacement = UnescapeReplacement(step[1].get<std::string>());
					list.Steps.push_back(std::move(s));
				} else {
					throw std::runtime_error(std::format("invalid step {}", step.dump()));
				}
			}
			return list;
		}

		bool IsPlaceholderAt(std::string_view s, size_t i) {
			return i + 4 <= s.size() && static_cast<uint8_t>(s[i]) == 0xF3 && (static_cast<uint8_t>(s[i + 1]) & 0xF0) == 0xB0;
		}

		/// Text as steps see it: macros and characters they must not touch as one placeholder character each, line break macros as \n.
		struct StepText {
			std::string Text;
			std::vector<std::string_view> Hidden;
		};

		bool HideForSteps(std::string_view s, StepText& out) {
			const auto bytes = std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size());
			for (size_t i = 0; i < s.size();) {
				size_t length;
				if (s[i] == xivres::xivstring::StartOfText) {
					const auto macro = xivres::xivstring::read_macro(bytes.subspan(i));
					if (!macro)
						return false;
					if (s.substr(i, macro->size) == NewLineMacro) {
						out.Text.push_back('\n');
						i += macro->size;
						continue;
					}
					length = macro->size;
				} else if (s[i] == '\n' || s[i] == StepSeparator) {
					length = 1;
				} else if (IsPlaceholderAt(s, i)) {
					length = 4;
				} else {
					out.Text.push_back(s[i++]);
					continue;
				}
				if (out.Hidden.size() >= MaxPlaceholders)
					return false;
				AppendUtf8(out.Text, PlaceholderBase + static_cast<char32_t>(out.Hidden.size()));
				out.Hidden.push_back(s.substr(i, length));
				i += length;
			}
			return true;
		}

		std::optional<std::string> RestoreHidden(std::string_view s, const StepText& t, size_t mergedHidden) {
			std::string res;
			std::vector<uint8_t> seen(t.Hidden.size());
			for (size_t i = 0; i < s.size();) {
				if (IsPlaceholderAt(s, i)) {
					const auto c = static_cast<char32_t>(static_cast<uint8_t>(s[i]) & 0x07) << 18
						| static_cast<char32_t>(static_cast<uint8_t>(s[i + 1]) & 0x3F) << 12
						| static_cast<char32_t>(static_cast<uint8_t>(s[i + 2]) & 0x3F) << 6
						| static_cast<char32_t>(static_cast<uint8_t>(s[i + 3]) & 0x3F);
					const auto index = static_cast<size_t>(c - PlaceholderBase);
					if (index >= t.Hidden.size() || seen[index]++)
						return std::nullopt;
					res += t.Hidden[index];
					i += 4;
				} else if (s[i] == '\n') {
					res += NewLineMacro;
					i++;
				} else {
					res.push_back(s[i++]);
				}
			}
			// Each side's macros are kept whole or dropped whole.
			const auto wholeOrNone = [&seen](size_t from, size_t to) {
				const auto n = static_cast<size_t>(std::count(seen.begin() + from, seen.begin() + to, uint8_t{1}));
				return n == 0 || n == to - from;
			};
			if (!wholeOrNone(0, mergedHidden) || !wholeOrNone(mergedHidden, seen.size()))
				return std::nullopt;
			return res;
		}

		std::optional<std::string> Normalize(const std::string& s, NORM_FORM form) {
			if (std::ranges::all_of(s, [](char c) { return static_cast<uint8_t>(c) < 0x80; }))
				return s;
			const auto wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
			if (wideLength <= 0)
				return std::nullopt;
			std::wstring wide(wideLength, L'\0');
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), wide.data(), wideLength);

			std::wstring normalized;
			auto size = NormalizeString(form, wide.data(), wideLength, nullptr, 0);
			for (auto attempt = 0;; attempt++) {
				if (size <= 0 || attempt == 4)
					return std::nullopt;
				normalized.resize(size);
				const auto written = NormalizeString(form, wide.data(), wideLength, normalized.data(), size);
				if (written > 0) {
					normalized.resize(written);
					break;
				}
				if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
					return std::nullopt;
				size = -written;
			}

			const auto length = WideCharToMultiByte(CP_UTF8, 0, normalized.data(), static_cast<int>(normalized.size()), nullptr, 0, nullptr, nullptr);
			std::string res(length, '\0');
			WideCharToMultiByte(CP_UTF8, 0, normalized.data(), static_cast<int>(normalized.size()), res.data(), length, nullptr, nullptr);
			return res;
		}

		struct StepOutcome {
			enum class Kind { Unchanged, Changed, Failed };
			Kind Kind = Kind::Unchanged;
			std::string Text;  // The new text, or why it failed.
		};

		StepOutcome RunSteps(const std::vector<const StepList*>& lists, std::string_view merged, std::string_view base, std::map<const StepList*, size_t>* changedCells) {
			// Steps see text identical to the base as empty: each side's macros would become different placeholders.
			StepText t;
			if (!HideForSteps(merged == base ? std::string_view() : merged, t))
				return {StepOutcome::Kind::Failed, "the merged text has a broken macro"};
			const auto mergedHidden = t.Hidden.size();
			const auto mergedLength = t.Text.size();
			t.Text.push_back(StepSeparator);
			if (!HideForSteps(base, t))
				return {StepOutcome::Kind::Failed, "the base text has a broken macro"};

			auto work = t.Text;
			try {
				for (const auto* list : lists) {
					const auto before = changedCells ? work : std::string();
					for (const auto& step : list->Steps) {
						if (step.Kind == TextStep::Kind::Replace) {
							work = srell::regex_replace(work, *step.Pattern, step.Replacement);
						} else {
							auto normalized = Normalize(work, step.Kind == TextStep::Kind::ToNfd ? NormalizationD : NormalizationC);
							if (!normalized)
								return {StepOutcome::Kind::Failed, "the text is not valid UTF-8"};
							work = std::move(*normalized);
						}
					}
					if (changedCells && work != before)
						(*changedCells)[list]++;
				}
			} catch (const std::exception& e) {
				return {StepOutcome::Kind::Failed, e.what()};
			}

			if (const auto separator = work.find(StepSeparator); separator != std::string::npos)
				work.resize(separator);
			if (std::string_view(work) == std::string_view(t.Text).substr(0, mergedLength))
				return {};
			auto restored = RestoreHidden(work, t, mergedHidden);
			if (!restored)
				return {StepOutcome::Kind::Failed, "a macro was dropped in part or repeated"};
			if (*restored == merged)
				return {};
			return {StepOutcome::Kind::Changed, std::move(*restored)};
		}

		struct SheetRule {
			std::string Pattern;
			std::shared_ptr<const srell::regex> Sheets;
			const nlohmann::ordered_json* Rules{};
			// By "*" or string index, in file order.
			std::vector<std::pair<std::string, std::shared_ptr<const StepList>>> StepLists;
		};

		bool IsIndexKey(std::string_view key) {
			return !key.empty() && key.size() <= 5 && std::ranges::all_of(key, [](char c) { return c >= '0' && c <= '9'; });
		}

		std::vector<SheetRule> PrepareOverrides(const std::vector<nlohmann::ordered_json>& files, const ExcelMerger::Callbacks& callbacks) {
			const auto warn = [&callbacks](const std::string& message) {
				if (callbacks.Log)
					callbacks.Log(true, message);
			};
			std::vector<SheetRule> res;
			for (const auto& file : files) {
				if (!file.is_object())
					continue;
				for (const auto& [pattern, rules] : file.items()) {
					// A sheet rule is an object, so a sheet pattern "name" or "description" still works.
					if (pattern.starts_with('_') || ((pattern == "name" || pattern == "description") && rules.is_string()))
						continue;
					if (!rules.is_object()) {
						warn(std::format("Override {} is not an object", pattern));
						continue;
					}
					SheetRule rule{pattern};
					try {
						rule.Sheets = std::make_shared<srell::regex>(pattern, srell::regex_constants::ECMAScript | srell::regex_constants::icase);
					} catch (const std::exception& e) {
						warn(std::format("Invalid sheet pattern {}: {}", pattern, e.what()));
						continue;
					}
					rule.Rules = &rules;
					for (const auto& [key, value] : rules.items()) {
						if (key.starts_with('_') || key == "keep" || key == "unkeep")
							continue;
						if (key != "*" && !IsIndexKey(key)) {
							warn(std::format("Override {}: unknown key \"{}\"", pattern, key));
							continue;
						}
						if (value.is_array()) {
							try {
								rule.StepLists.emplace_back(key, std::make_shared<StepList>(ParseStepList(value, std::format("{} / {}", pattern, key))));
							} catch (const std::exception& e) {
								warn(std::format("Override {} / {}: ignoring the steps: {}", pattern, key, e.what()));
							}
						} else if (key == "*" || !(value.is_null() || value.is_number_unsigned())) {
							warn(std::format("Override {} / {}: expected a string column index, null, or a list of steps", pattern, key));
						}
					}
					res.emplace_back(std::move(rule));
				}
			}
			return res;
		}

		/// For the report: macros as <type>, at most a few dozen characters.
		std::string SampleText(std::string_view s) {
			constexpr size_t MaxCharacters = 40;
			const auto bytes = std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size());
			std::string res;
			size_t characters = 0, i = 0;
			for (; i < s.size() && characters < MaxCharacters; characters++) {
				const auto b = bytes[i];
				if (b == static_cast<uint8_t>(xivres::xivstring::StartOfText)) {
					const auto macro = xivres::xivstring::read_macro(bytes.subspan(i));
					if (!macro)
						break;
					res += macro->type == xivres::xivstring::xivpayload_type::NewLine ? std::string("\n") : std::format("<{:02X}>", static_cast<int>(macro->type));
					i += macro->size;
					continue;
				}
				const size_t length = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
				auto valid = length && i + length <= s.size();
				for (size_t k = 1; valid && k < length; k++)
					valid = (bytes[i + k] & 0xC0) == 0x80;
				if (!valid || (length == 1 && b < 0x20 && b != '\n' && b != '\t')) {
					res.push_back('?');
					i++;
				} else {
					res.append(s.substr(i, length));
					i += length;
				}
			}
			if (i < s.size())
				res += "...";
			return res;
		}

		// ---- Per sheet ----

		struct SheetResult {
			std::string Name;
			std::vector<ExcelMerger::PassThrough> PassThroughs;
			std::vector<ExcelMerger::Generated> Generated;
			nlohmann::json Report = nlohmann::json::object();
			size_t FastPath{}, Mapped{}, Skipped{};
		};

		class SheetJob {
			const xivres::sqpack::reader& m_target;
			const std::vector<ExcelMerger::Source>& m_sources;
			const ExcelMerger::Options& m_options;
			const ExcelMerger::Callbacks& m_callbacks;
			const std::vector<SheetRule>& m_allRules;
			const std::string m_name;

			std::vector<const SheetRule*> m_rules;
			std::optional<xivres::excel::exh::reader> m_exh;
			std::vector<char> m_exhRaw;
			std::vector<Column> m_columns;
			std::vector<Column> m_strings;
			std::map<game_language, std::shared_ptr<LanguageData>> m_targetLanguages;

			std::optional<LanguageData> m_base;
			std::vector<game_language> m_basePageLanguages;

			std::optional<std::set<std::pair<uint32_t, uint16_t>>> m_keep;
			nlohmann::json m_keepReport = nlohmann::json::array();

			SheetResult m_result;

		public:
			SheetJob(const xivres::sqpack::reader& target, const std::vector<ExcelMerger::Source>& sources, const ExcelMerger::Options& options, const ExcelMerger::Callbacks& callbacks, const std::vector<SheetRule>& rules, std::string name)
				: m_target(target)
				, m_sources(sources)
				, m_options(options)
				, m_callbacks(callbacks)
				, m_allRules(rules)
				, m_name(std::move(name)) {
				m_result.Name = m_name;
			}

			SheetResult Run() {
				try {
					RunInner();
				} catch (const std::exception& e) {
					Log(true, std::format("[{}] Skipped: {}", m_name, e.what()));
					m_result.Report["error"] = e.what();
					m_result.PassThroughs.clear();
					m_result.Generated.clear();
					m_result.FastPath = m_result.Mapped = 0;
					m_result.Skipped = 1;
				}
				return std::move(m_result);
			}

		private:
			void Log(bool warning, const std::string& message) const {
				if (m_callbacks.Log)
					m_callbacks.Log(warning, message);
			}

			void RunInner() {
				if (std::ranges::find(DeniedSheets, m_name) != std::end(DeniedSheets))
					return Decide("*", "skipped", "built-in deny list");

				auto exhRaw = ReadEntry(m_target, std::format("exd/{}.exh", m_name));
				if (!exhRaw)
					return;
				m_exhRaw = std::move(*exhRaw);
				m_exh.emplace(m_name, xivres::memory_stream(std::span<const char>(m_exhRaw)));
				m_columns = GetColumns(*m_exh);
				m_strings = StringColumnsByOffset(m_columns);
				for (const auto& rule : m_allRules) {
					if (srell::regex_search(m_name, *rule.Sheets))
						m_rules.push_back(&rule);
				}

				const auto& declared = m_exh->get_languages();
				if (declared.empty() || (declared.size() == 1 && declared[0] == game_language::Unspecified))
					return;
				if (std::ranges::none_of(m_columns, &Column::IsString))
					return;

				std::vector<game_language> wanted;
				for (const auto lang : declared) {
					if (lang != game_language::Unspecified)
						wanted.push_back(lang);
				}
				for (const auto lang : m_options.Languages) {
					if (std::ranges::find(wanted, lang) == wanted.end())
						wanted.push_back(lang);
				}

				std::vector<game_language> addToExh;
				for (const auto lang : wanted) {
					if (lang == game_language::Unspecified || TargetLanguage(lang).Any())
						continue;
					if (ProcessLanguage(lang) && std::ranges::find(declared, lang) == declared.end())
						addToExh.push_back(lang);
				}

				if (!addToExh.empty())
					PatchExhLanguages(addToExh);
				if (!m_keepReport.empty())
					m_result.Report["keptBaseCells"] = m_keepReport;
			}

			void Decide(const std::string& language, const char* decision, const std::string& reason, nlohmann::json extra = nlohmann::json::object()) {
				extra["decision"] = decision;
				if (!reason.empty())
					extra["reason"] = reason;
				m_result.Report[language] = std::move(extra);
				if (std::string_view(decision) == "fast path")
					m_result.FastPath = 1;
				else if (std::string_view(decision) == "mapped")
					m_result.Mapped = 1;
				else
					m_result.Skipped = 1;
			}

			const LanguageData& TargetLanguage(game_language language) {
				auto& slot = m_targetLanguages[language];
				if (!slot)
					slot = std::make_shared<LanguageData>(LoadLanguage(m_target, *m_exh, language));
				return *slot;
			}

			/// Rows of the base language, page by page falling back to the others.
			const LanguageData& Base() {
				if (m_base)
					return *m_base;
				std::vector<game_language> order{m_options.BaseLanguage};
				for (const auto lang : m_options.FallbackLanguages) {
					if (std::ranges::find(order, lang) == order.end())
						order.push_back(lang);
				}
				for (const auto lang : m_exh->get_languages()) {
					if (std::ranges::find(order, lang) == order.end())
						order.push_back(lang);
				}

				auto& base = m_base.emplace();
				for (size_t i = 0; i < m_exh->get_pages().size(); ++i) {
					base.Pages.emplace_back();
					m_basePageLanguages.push_back(game_language::Unspecified);
					for (const auto lang : order) {
						if (lang == game_language::Unspecified || std::ranges::find(m_exh->get_languages(), lang) == m_exh->get_languages().end())
							continue;
						const auto& data = TargetLanguage(lang);
						if (!data.Pages[i])
							continue;
						base.Pages.back() = data.Pages[i];
						m_basePageLanguages.back() = lang;
						for (const auto& [id, row] : data.Pages[i]->Rows)
							base.Rows.emplace(id, row);
						break;
					}
				}
				return base;
			}

			size_t StringIndex(uint16_t offset) const {
				return static_cast<size_t>(std::ranges::find(m_strings, offset, &Column::Offset) - m_strings.begin());
			}

			/// Cells (row id, target column offset) to keep in the base language.
			const std::set<std::pair<uint32_t, uint16_t>>& KeptCells() {
				if (m_keep)
					return *m_keep;
				auto& keep = m_keep.emplace();
				const auto fixedSize = *m_exh->header().FixedDataSize;
				const auto addReport = [this](uint32_t row, uint16_t offset, const char* why) {
					m_keepReport.push_back({{"row", row}, {"column", StringIndex(offset)}, {"why", why}});
				};

				if (m_name.starts_with("quest/") && m_columns.size() >= 2 && m_columns[0].IsString() && m_columns[1].IsString()) {
					for (const auto lang : m_exh->get_languages()) {
						if (lang == game_language::Unspecified)
							continue;
						const auto& data = TargetLanguage(lang);
						if (!data.Any())
							continue;
						for (const auto id : DetectSayPhraseRows(data, fixedSize, m_columns[0].Offset, m_columns[1].Offset)) {
							if (keep.emplace(id, m_columns[1].Offset).second)
								addReport(id, m_columns[1].Offset, "say phrase");
						}
					}
				}

				for (const auto* rule : m_rules) {
					for (const auto* action : {"keep", "unkeep"}) {
						if (!rule->Rules->contains(action))
							continue;
						for (const auto& entry : rule->Rules->at(action)) {
							const auto cell = ResolveOverrideCell(entry);
							if (!cell) {
								Log(true, std::format("[{}] {} entry {} of {} matches no cell", m_name, action, entry.dump(), rule->Pattern));
								continue;
							}
							if (std::string_view(action) == "keep") {
								if (keep.insert(*cell).second)
									addReport(cell->first, cell->second, "override");
							} else if (keep.erase(*cell)) {
								addReport(cell->first, cell->second, "override removed");
							}
						}
					}
				}
				return keep;
			}

			/// Whether string column 0 holds text keys (A-Z, 0-9, _), as in quest and dialogue sheets.
			bool IsKeyedSheet() {
				if (m_strings.size() < 2 || m_columns.empty() || !m_columns[0].IsString() || m_strings[0].Offset != m_columns[0].Offset)
					return false;
				auto any = false;
				for (const auto& row : Base().Rows | std::views::values) {
					const auto key = GetString(row, *m_exh->header().FixedDataSize, m_strings[0].Offset);
					if (key.empty())
						continue;
					if (!std::ranges::all_of(key, [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }))
						return false;
					any = true;
				}
				return any;
			}

			std::optional<std::pair<uint32_t, uint16_t>> ResolveOverrideCell(const nlohmann::ordered_json& entry) {
				if (!entry.is_object())
					return std::nullopt;
				try {
					const auto index = entry.contains("column") ? entry.at("column").get<size_t>() : IsKeyedSheet() ? size_t{1} : size_t{0};
					if (index >= m_strings.size())
						return std::nullopt;
					const auto offset = m_strings[index].Offset;
					if (entry.contains("row"))
						return std::make_pair(entry.at("row").get<uint32_t>(), offset);
					if (entry.contains("key") && !m_strings.empty()) {
						const auto key = entry.at("key").get<std::string>();
						for (const auto& [id, row] : Base().Rows) {
							if (GetString(row, *m_exh->header().FixedDataSize, m_strings[0].Offset) == key)
								return std::make_pair(id, offset);
						}
					}
				} catch (const nlohmann::json::exception&) {
					// Wrong value types; reported as matching nothing.
				}
				return std::nullopt;
			}

			/// Step lists of the matching override rules for each target string column, in file order.
			std::vector<std::vector<const StepList*>> ColumnSteps() {
				std::vector<std::vector<const StepList*>> res(m_strings.size());
				for (const auto* rule : m_rules) {
					for (const auto& [key, list] : rule->StepLists) {
						if (key == "*") {
							for (auto& r : res)
								r.push_back(list.get());
						} else if (const auto index = std::stoul(key); index < res.size()) {
							res[index].push_back(list.get());
						} else {
							Log(true, std::format("[{}] Override {}: no string column {} (there are {})", m_name, rule->Pattern, key, m_strings.size()));
						}
					}
				}
				return res;
			}

			bool HasColumnRemaps() const {
				for (const auto* rule : m_rules) {
					for (const auto& [key, value] : rule->Rules->items()) {
						if (IsIndexKey(key) && (value.is_null() || value.is_number_unsigned()))
							return true;
					}
				}
				return false;
			}

			nlohmann::json DescribeStringColumns(const std::vector<Column>& strings, const std::map<uint32_t, RowRef>& rows, uint16_t fixedSize) const {
				auto res = nlohmann::json::array();
				for (size_t i = 0; i < strings.size(); ++i) {
					std::string sample;
					for (const auto& row : rows | std::views::values) {
						if (const auto text = GetString(row, fixedSize, strings[i].Offset); !text.empty()) {
							sample = SampleText(text);
							break;
						}
					}
					res.push_back({{"index", i}, {"offset", strings[i].Offset}, {"sample", std::move(sample)}});
				}
				return res;
			}

			void PatchExhLanguages(const std::vector<game_language>& languages) {
				auto patched = m_exhRaw;
				const auto& h = m_exh->header();
				const auto languageOffset = sizeof h + m_exh->get_columns().size() * 4 + m_exh->get_pages().size() * 8;
				patched.resize(languageOffset + m_exh->get_languages().size() * 2);
				for (const auto lang : languages) {
					patched.push_back(static_cast<char>(lang));
					patched.push_back(0);
				}
				const auto count = static_cast<uint16_t>(m_exh->get_languages().size() + languages.size());
				patched[0x0C] = static_cast<char>(count >> 8);
				patched[0x0D] = static_cast<char>(count & 0xFF);
				m_result.Generated.push_back({std::format("exd/{}.exh", m_name), std::move(patched)});
				m_result.Report["exhLanguagesAdded"] = [&] {
					auto arr = nlohmann::json::array();
					for (const auto lang : languages)
						arr.push_back(LanguageCode(lang));
					return arr;
				}();
			}

			/// \returns Whether files for the language were provided.
			bool ProcessLanguage(game_language language) {
				const auto code = LanguageCode(language);

				const ExcelMerger::Source* source = nullptr;
				size_t sourceIndex = 0;
				std::optional<xivres::excel::exh::reader> sourceExh;
				for (size_t i = 0; i < m_sources.size(); ++i) {
					const auto raw = ReadEntry(*m_sources[i].Reader, std::format("exd/{}.exh", m_name));
					if (!raw)
						continue;
					auto exh = xivres::excel::exh::reader(m_name, xivres::memory_stream(std::span<const char>(*raw)));
					if (exh.get_pages().empty() || !EntryExists(*m_sources[i].Reader, exh.get_exd_path(exh.get_page(0), language)))
						continue;
					source = &m_sources[i];
					sourceIndex = i;
					sourceExh.emplace(std::move(exh));
					break;
				}
				if (!source)
					return false;

				nlohmann::json info = nlohmann::json::object();
				info["source"] = Utils::ToUtf8(source->GameRoot);

				const auto& base = Base();
				if (base.Rows.empty()) {
					Decide(code, "skipped", "no base language rows", info);
					return false;
				}
				info["base"] = [&] {
					auto arr = nlohmann::json::array();
					for (const auto lang : m_basePageLanguages)
						arr.push_back(LanguageCode(lang));
					return arr;
				}();

				const auto sourceData = LoadLanguage(*source->Reader, *sourceExh, language);
				const auto& keep = KeptCells();

				info["targetStringColumns"] = DescribeStringColumns(m_strings, base.Rows, *m_exh->header().FixedDataSize);
				info["sourceStringColumns"] = DescribeStringColumns(StringColumnsByOffset(GetColumns(*sourceExh)), sourceData.Rows, *sourceExh->header().FixedDataSize);

				std::string fastPathMiss;
				if (!keep.empty())
					fastPathMiss = std::format("{} cell(s) kept in the base language", keep.size());
				else if (HasColumnRemaps())
					fastPathMiss = "string columns mapped by an override";
				else
					fastPathMiss = CheckFastPath(*sourceExh, sourceData);
				if (fastPathMiss.empty()) {
					for (const auto& page : m_exh->get_pages())
						m_result.PassThroughs.push_back({sourceIndex, m_exh->get_exd_path(page, language).text()});
					Decide(code, "fast path", {}, info);
					Log(false, std::format("[{}] {}: fast path", m_name, code));
					return true;
				}
				info["notFastPath"] = fastPathMiss;
				return Generate(language, *sourceExh, sourceData, info);
			}

			/// \returns Empty if the source's files can be used as they are, or why not.
			std::string CheckFastPath(const xivres::excel::exh::reader& sourceExh, const LanguageData& sourceData) {
				if (auto reason = CheckSameLayout(sourceExh, sourceData); !reason.empty())
					return reason;

				const auto& th = m_exh->header();
				const auto& base = Base();
				const auto fixedSize = *th.FixedDataSize;
				std::vector<bool> isString(fixedSize);
				for (const auto& c : m_columns) {
					if (c.IsString()) {
						for (size_t i = c.Offset; i < c.Offset + 4u && i < fixedSize; ++i)
							isString[i] = true;
					}
				}
				const auto subRowStride = *th.Variant == xivres::excel::variant::Level2 ? 0 : 2 + fixedSize;
				size_t differingRows = 0;
				for (auto it = base.Rows.begin(), it2 = sourceData.Rows.begin(); it != base.Rows.end(); ++it, ++it2) {
					const auto& a = it->second;
					const auto& b = it2->second;
					if (a.SubRows != b.SubRows)
						return std::format("subrow count of row {} differs", it->first);
					const auto blocks = subRowStride ? a.SubRows : 1u;
					const auto need = subRowStride ? static_cast<size_t>(subRowStride) * blocks : fixedSize;
					if (a.Size < need || b.Size < need)
						return std::format("row {} is too short", it->first);
					auto same = true;
					for (size_t s = 0; s < blocks && same; ++s) {
						const auto start = subRowStride ? s * subRowStride : 0;
						if (subRowStride && std::memcmp(a.Data + start, b.Data + start, 2) != 0)
							same = false;
						const auto fixedStart = start + (subRowStride ? 2 : 0);
						for (size_t i = 0; i < fixedSize && same; ++i)
							same = isString[i] || a.Data[fixedStart + i] == b.Data[fixedStart + i];
					}
					differingRows += !same;
				}
				if (differingRows)
					return std::format("non-string cells differ in {} row(s)", differingRows);

				// Sheets with subrows can't be generated; theirs pass through as they are.
				if (subRowStride)
					return {};
				size_t emptyInSource = 0, reservedInSource = 0;
				for (auto it = base.Rows.begin(), it2 = sourceData.Rows.begin(); it != base.Rows.end(); ++it, ++it2) {
					for (const auto& c : m_strings) {
						const auto text = GetString(it2->second, fixedSize, c.Offset);
						const auto baseText = GetString(it->second, fixedSize, c.Offset);
						emptyInSource += text.empty() && !baseText.empty();
						// The key names the source's language, but the server sends the text under the base language's.
						reservedInSource += text.starts_with("_rsv_") && text != baseText;
					}
				}
				if (emptyInSource)
					return std::format("{} cell(s) empty in the source but not in the base language", emptyInSource);
				if (reservedInSource)
					return std::format("{} reserved cell(s) keyed differently from the base language", reservedInSource);

				const auto steps = ColumnSteps();
				if (std::ranges::all_of(steps, &std::vector<const StepList*>::empty))
					return {};
				for (auto it = base.Rows.begin(), it2 = sourceData.Rows.begin(); it != base.Rows.end(); ++it, ++it2) {
					for (size_t i = 0; i < m_strings.size(); ++i) {
						if (steps[i].empty())
							continue;
						const auto text = GetString(it2->second, fixedSize, m_strings[i].Offset);
						if (text.starts_with("_rsv_"))
							continue;
						if (RunSteps(steps[i], text, GetString(it->second, fixedSize, m_strings[i].Offset), nullptr).Kind != StepOutcome::Kind::Unchanged)
							return std::format("steps change row {} string column {}", it->first, i);
					}
				}
				return {};
			}

			/// \returns Empty if the source has the same columns, pages, and row ids, or why not.
			std::string CheckSameLayout(const xivres::excel::exh::reader& sourceExh, const LanguageData& sourceData) {
				const auto& th = m_exh->header();
				const auto& sh = sourceExh.header();
				if (*th.FixedDataSize != *sh.FixedDataSize)
					return "fixed row size differs";
				if (*th.Variant != *sh.Variant)
					return "variant differs";
				auto tc = GetColumns(*m_exh), sc = GetColumns(sourceExh);
				const auto key = [](const Column& c) { return std::make_pair(c.Offset, static_cast<uint16_t>(c.Type)); };
				std::ranges::sort(tc, {}, key);
				std::ranges::sort(sc, {}, key);
				if (tc.size() != sc.size() || !std::ranges::equal(tc, sc, {}, key, key))
					return "columns differ";
				const auto& tp = m_exh->get_pages();
				const auto& sp = sourceExh.get_pages();
				if (tp.size() != sp.size() || !std::ranges::equal(tp, sp, [](const auto& a, const auto& b) { return *a.StartId == *b.StartId && *a.RowCountWithSkip == *b.RowCountWithSkip; }))
					return "pages differ";
				if (std::ranges::any_of(sourceData.Pages, [](const auto& p) { return !p; }))
					return "source lacks a page";

				const auto& base = Base();
				if (base.Rows.size() != sourceData.Rows.size() || !std::ranges::equal(base.Rows | std::views::keys, sourceData.Rows | std::views::keys))
					return "row ids differ";
				return {};
			}

			bool Generate(game_language language, const xivres::excel::exh::reader& sourceExh, const LanguageData& sourceData, nlohmann::json& info) {
				const auto code = LanguageCode(language);
				if (*m_exh->header().Variant != xivres::excel::variant::Level2 || *sourceExh.header().Variant != xivres::excel::variant::Level2) {
					Decide(code, "skipped", "sheets with subrows are only passed through", info);
					return false;
				}

				const auto& base = Base();
				const auto tFixed = *m_exh->header().FixedDataSize;
				const auto sFixed = *sourceExh.header().FixedDataSize;
				const auto sColumns = GetColumns(sourceExh);
				const auto& tStrings = m_strings;
				const auto sStrings = StringColumnsByOffset(sColumns);

				std::map<uint32_t, RowRef> matched;
				std::string rowMethod;
				{
					std::vector<std::pair<Column, Column>> numericPairs;
					for (const auto& t : m_columns) {
						if (t.IsString())
							continue;
						for (const auto& s : sColumns) {
							if (s.Type == t.Type && s.Offset == t.Offset) {
								numericPairs.emplace_back(t, s);
								break;
							}
						}
					}

					const auto isDialogue = [](const std::vector<Column>& c) { return c.size() == 2 && c[0].IsString() && c[1].IsString(); };
					if (isDialogue(m_columns) && isDialogue(sColumns)) {
						rowMethod = "key";
						std::map<std::string_view, RowRef> byKey;
						for (const auto& [id, row] : sourceData.Rows)
							byKey.try_emplace(GetString(row, sFixed, sStrings[0].Offset), row);
						for (const auto& [id, row] : base.Rows) {
							if (const auto it = byKey.find(GetString(row, tFixed, tStrings[0].Offset)); it != byKey.end())
								matched.emplace(id, it->second);
						}
					} else {
						// Language-dependent columns (grammar attributes, icons) disagree anyway; a column that agrees confirms the ids.
						size_t total = 0, equal = 0, common = 0;
						std::vector<size_t> columnEqual(numericPairs.size());
						std::vector<std::optional<uint64_t>> firstValue(numericPairs.size());
						std::vector<bool> informativeColumn(numericPairs.size());
						for (const auto& [id, row] : base.Rows) {
							const auto it = sourceData.Rows.find(id);
							if (it == sourceData.Rows.end())
								continue;
							common++;
							for (size_t i = 0; i < numericPairs.size(); ++i) {
								const auto& [t, s] = numericPairs[i];
								const auto value = GetNumeric(row, t);
								const auto same = value == GetNumeric(it->second, s);
								total++;
								equal += same;
								columnEqual[i] += same;
								if (!firstValue[i])
									firstValue[i] = value;
								else if (*firstValue[i] != value)
									informativeColumn[i] = true;
							}
						}
						const auto agreement = total ? static_cast<double>(equal) / total : 1.0;
						double bestColumn = 0;
						for (size_t i = 0; i < numericPairs.size(); ++i) {
							if (informativeColumn[i] && common)
								bestColumn = (std::max)(bestColumn, static_cast<double>(columnEqual[i]) / common);
						}
						const auto sameLayout = CheckSameLayout(sourceExh, sourceData).empty();
						info["rowIdAgreement"] = agreement;
						info["rowIdBestColumnAgreement"] = bestColumn;
						if (agreement >= RowIdAgreementThreshold || bestColumn >= RowIdAgreementThreshold || sameLayout) {
							if (agreement < RowIdAgreementThreshold && bestColumn < RowIdAgreementThreshold)
								info["rowIdTrustedBy"] = "identical layout and row ids";
							rowMethod = "id";
							for (const auto& [id, row] : base.Rows) {
								if (const auto it = sourceData.Rows.find(id); it != sourceData.Rows.end())
									matched.emplace(id, it->second);
							}
						} else {
							rowMethod = "fingerprint";
							const auto fingerprint = [&numericPairs](const RowRef& row, bool target) {
								std::vector<uint64_t> v;
								for (const auto& [t, s] : numericPairs)
									v.push_back(GetNumeric(row, target ? t : s));
								return v;
							};
							std::map<std::vector<uint64_t>, std::pair<size_t, RowRef>> sourceFp;
							for (const auto& row : sourceData.Rows | std::views::values) {
								auto& slot = sourceFp[fingerprint(row, false)];
								slot.first++;
								slot.second = row;
							}
							std::map<std::vector<uint64_t>, size_t> targetFp;
							for (const auto& row : base.Rows | std::views::values)
								targetFp[fingerprint(row, true)]++;
							for (const auto& [id, row] : base.Rows) {
								auto fp = fingerprint(row, true);
								if (targetFp[fp] != 1)
									continue;
								if (const auto it = sourceFp.find(fp); it != sourceFp.end() && it->second.first == 1)
									matched.emplace(id, it->second.second);
							}
							const auto coverage = static_cast<double>(matched.size()) / base.Rows.size();
							info["fingerprintCoverage"] = coverage;
							if (numericPairs.empty() || coverage < FingerprintCoverageThreshold) {
								Decide(code, "skipped", "rows could not be matched", info);
								Log(true, std::format("[{}] {}: skipped; rows could not be matched (agreement by row id {:.3f})", m_name, code, agreement));
								return false;
							}
						}
					}
				}
				info["rowMatch"] = rowMethod;
				info["rowsMatched"] = matched.size();
				info["rows"] = base.Rows.size();

				std::vector<std::pair<const RowRef*, const RowRef*>> sample;
				{
					std::vector<std::pair<const RowRef*, const RowRef*>> all;
					for (const auto& [id, row] : matched)
						all.emplace_back(&base.Rows.at(id), &row);
					const auto step = (std::max<size_t>)(1, all.size() / SampleRowCount);
					for (size_t i = 0; i < all.size(); i += step)
						sample.push_back(all[i]);
				}
				const auto sampleColumn = [&sample](const Column& c, uint16_t fixedSize, bool target) {
					StringColumnSample res;
					for (const auto& [t, s] : sample) {
						res.Values.push_back(GetString(target ? *t : *s, fixedSize, c.Offset));
						res.Features.push_back(GetFeatures(res.Values.back()));
						res.NonEmpty |= !res.Values.back().empty();
					}
					return res;
				};
				std::vector<StringColumnSample> tSamples, sSamples;
				for (const auto& c : tStrings)
					tSamples.push_back(sampleColumn(c, tFixed, true));
				for (const auto& c : sStrings)
					sSamples.push_back(sampleColumn(c, sFixed, false));
				std::vector scores(tStrings.size(), std::vector(sStrings.size(), 0.0));
				std::vector informative(tStrings.size(), std::vector<char>(sStrings.size(), 0));
				const auto computeScores = [&] {
					for (size_t i = 0; i < tStrings.size(); ++i) {
						for (size_t j = 0; j < sStrings.size(); ++j) {
							const auto [score, enough] = StringScore(tSamples[i], sSamples[j]);
							scores[i][j] = score;
							informative[i][j] = enough;
						}
					}
				};

				std::map<uint16_t, std::optional<uint16_t>> columnMap;
				std::string columnMethod;
				std::set<uint16_t> overridden;
				for (const auto* rule : m_rules) {
					for (const auto& [key, value] : rule->Rules->items()) {
						if (!IsIndexKey(key) || !(value.is_null() || value.is_number_unsigned()))
							continue;
						const auto index = std::stoul(key);
						if (index >= tStrings.size()) {
							Log(true, std::format("[{}] Override {}: no string column {} (there are {})", m_name, rule->Pattern, index, tStrings.size()));
							continue;
						}
						const auto offset = tStrings[index].Offset;
						if (value.is_null()) {
							columnMap[offset] = std::nullopt;
						} else {
							const auto sourceIndex = value.get<size_t>();
							if (sourceIndex >= sStrings.size()) {
								Log(true, std::format("[{}] Override {}: no source string column {} (there are {})", m_name, rule->Pattern, sourceIndex, sStrings.size()));
								continue;
							}
							columnMap[offset] = sStrings[sourceIndex].Offset;
						}
						overridden.insert(offset);
					}
				}
				const auto sourceIndexOf = [&sStrings](uint16_t offset) {
					return static_cast<size_t>(std::ranges::find(sStrings, offset, &Column::Offset) - sStrings.begin());
				};

				const auto offsetsOf = [](const std::vector<Column>& c) {
					std::vector<uint16_t> v;
					for (const auto& x : c)
						v.push_back(x.Offset);
					return v;
				};
				if (overridden.size() == tStrings.size()) {
					columnMethod = "override";
				} else if (offsetsOf(tStrings) == offsetsOf(sStrings) || tStrings.size() == sStrings.size()) {
					columnMethod = offsetsOf(tStrings) == offsetsOf(sStrings) ? "offset" : "offset rank";
					computeScores();
					const auto m = tStrings.size();
					for (size_t i = 0; i < m; ++i) {
						if (!overridden.contains(tStrings[i].Offset))
							columnMap[tStrings[i].Offset] = sStrings[i].Offset;
					}
					// Identical offsets make the mapping certain; only a mapping by rank can be shifted.
					std::vector<std::string> contradictions;
					for (size_t i = 0; i < m && offsetsOf(tStrings) != offsetsOf(sStrings); ++i) {
						if (!sSamples[i].NonEmpty || !informative[i][i])
							continue;
						for (size_t j = 0; j < m; ++j) {
							if (j != i && scores[i][j] > scores[i][i] + 0.15 && scores[i][j] > scores[j][j] + 0.05)
								contradictions.push_back(std::format("{}<-{} looks like {}", i, i, j));
						}
					}
					if (!contradictions.empty()) {
						info["contradictions"] = contradictions;
						Log(true, std::format("[{}] {}: content contradicts the column mapping: {}", m_name, code, nlohmann::json(contradictions).dump()));
					}
				} else if (m_options.AllowContentMatching) {
					columnMethod = "content";
					computeScores();
					const auto m = tStrings.size(), n = sStrings.size();
					const auto a1 = AlignOrdered(m, n, scores, 0.3);
					const auto a2 = AlignGreedy(m, n, scores, 0.3);
					std::map<size_t, size_t> agreed;
					for (const auto& [i, j] : a1) {
						if (const auto it = a2.find(i); (it != a2.end() && it->second == j) || !informative[i][j])
							agreed.emplace(i, j);
					}
					for (const auto& [i, j] : FillGaps(agreed, m, n)) {
						if (!overridden.contains(tStrings[i].Offset))
							columnMap[tStrings[i].Offset] = sStrings[j].Offset;
					}
				} else {
					Decide(code, "skipped", std::format("string column counts differ ({} here, {} in source)", tStrings.size(), sStrings.size()), info);
					Log(true, std::format("[{}] {}: skipped; string column counts differ ({} vs {})", m_name, code, tStrings.size(), sStrings.size()));
					return false;
				}
				info["columnMap"] = [&] {
					auto obj = nlohmann::json::object();
					for (size_t i = 0; i < tStrings.size(); ++i) {
						const auto it = columnMap.find(tStrings[i].Offset);
						obj[std::to_string(i)] = it == columnMap.end() || !it->second ? nlohmann::json() : nlohmann::json(sourceIndexOf(*it->second));
					}
					return obj;
				}();
				info["columnMapMethod"] = columnMethod;

				// Step. Cells the source leaves empty but the base fills, filled from the most alike sibling column.
				std::map<uint16_t, uint16_t> fillFrom;
				if (m_options.FillPluralColumns) {
					const auto sourceOf = [&](uint16_t offset) -> std::optional<uint16_t> {
						const auto it = columnMap.find(offset);
						return it == columnMap.end() ? std::nullopt : it->second;
					};
					for (const auto& c : tStrings) {
						const auto src = sourceOf(c.Offset);
						if (!src)
							continue;
						size_t baseFilled = 0;
						for (const auto& id : matched | std::views::keys)
							baseFilled += !GetString(base.Rows.at(id), tFixed, c.Offset).empty();
						if (baseFilled < 10)
							continue;

						std::optional<std::pair<double, uint16_t>> best;
						for (const auto& d : tStrings) {
							const auto dsrc = sourceOf(d.Offset);
							if (d.Offset == c.Offset || !dsrc)
								continue;
							double sum = 0;
							size_t pairs = 0, dFilled = 0;
							for (const auto& [id, row] : matched) {
								dFilled += !GetString(row, sFixed, *dsrc).empty();
								if (pairs >= 200)
									continue;
								const auto a = GetString(base.Rows.at(id), tFixed, c.Offset);
								const auto b = GetString(base.Rows.at(id), tFixed, d.Offset);
								if (a.empty() || b.empty())
									continue;
								sum += Similarity(a, b);
								pairs++;
							}
							if (pairs < 5 || dFilled * 2 < matched.size())
								continue;
							const auto sim = sum / pairs;
							if (sim >= PluralSimilarityThreshold && (!best || sim > best->first))
								best.emplace(sim, *dsrc);
						}
						if (best)
							fillFrom.emplace(c.Offset, best->second);
					}
					if (!fillFrom.empty()) {
						auto obj = nlohmann::json::object();
						for (const auto& [t, s] : fillFrom)
							obj[std::to_string(StringIndex(t))] = sourceIndexOf(s);
						info["filledFrom"] = obj;
					}
				}

				const auto& keep = KeptCells();
				xivres::excel::type2gen gen(m_name, m_exh->get_columns(), *m_exh->header().ReadStrategy, SIZE_MAX, tFixed);
				for (const auto& page : m_exh->get_pages())
					gen.DivideAtIds.insert(*page.StartId);
				gen.add_language(language);
				gen.AlignRowData = std::ranges::all_of(base.Rows | std::views::values, [](const RowRef& r) { return r.Size % 4 == 0; });
				const auto steps = ColumnSteps();
				std::map<const StepList*, size_t> stepChanges;
				size_t replaced = 0, reserved = 0, emptyKept = 0, stepFailures = 0;
				for (const auto& [id, row] : base.Rows) {
					const auto data = std::span(row.Data, row.Size);
					auto cells = xivres::excel::exd::row::reader(data.subspan(0, tFixed), data, m_exh->get_columns()).get_cells_vector();
					const auto src = matched.find(id);
					for (size_t i = 0; i < tStrings.size(); ++i) {
						const auto& c = tStrings[i];
						if (keep.contains({id, c.Offset}))
							continue;
						const auto baseText = std::string_view(cells[c.Index].String.escaped());
						auto text = baseText;
						auto fromSource = false;
						if (const auto mapped = columnMap.find(c.Offset); src != matched.end() && mapped != columnMap.end() && mapped->second) {
							text = GetString(src->second, sFixed, *mapped->second);
							if (const auto fill = fillFrom.find(c.Offset); fill != fillFrom.end() && text.empty() && !baseText.empty())
								text = GetString(src->second, sFixed, fill->second);
							if (text.starts_with("_rsv_")) {
								reserved++;
								continue;
							}
							if (text.empty() && !baseText.empty()) {
								text = baseText;
								emptyKept++;
							} else {
								fromSource = true;
							}
						}

						std::string stepped;
						if (!steps[i].empty()) {
							auto outcome = RunSteps(steps[i], text, baseText, &stepChanges);
							if (outcome.Kind == StepOutcome::Kind::Changed) {
								stepped = std::move(outcome.Text);
								text = stepped;
							} else if (outcome.Kind == StepOutcome::Kind::Failed && stepFailures++ < 3) {
								Log(true, std::format("[{}] {}: steps not applied to row {} string column {}: {}", m_name, code, id, i, outcome.Text));
							}
						}
						if (text != baseText)
							cells[c.Index].String.escaped(std::string(text));
						replaced += fromSource;
					}
					gen.set_row(id, language, std::move(cells));
				}
				info["cellsReplaced"] = replaced;
				if (reserved)
					info["reservedSkipped"] = reserved;
				if (emptyKept)
					info["emptyInSourceKept"] = emptyKept;
				if (std::ranges::any_of(steps, [](const auto& s) { return !s.empty(); })) {
					auto obj = nlohmann::json::object();
					for (const auto& columnSteps : steps) {
						for (const auto* list : columnSteps)
							obj[list->Label] = stepChanges[list];
					}
					info["stepsChangedCells"] = obj;
					if (stepFailures) {
						info["stepFailures"] = stepFailures;
						Log(true, std::format("[{}] {}: steps not applied to {} cell(s) in total", m_name, code, stepFailures));
					}
				}

				std::set<std::string> expected;
				for (const auto& page : m_exh->get_pages())
					expected.insert(m_exh->get_exd_path(page, language).text());
				size_t files = 0;
				std::vector<ExcelMerger::Generated> generated;
				for (auto& [path, bytes] : gen.compile()) {
					if (path.text().ends_with(".exh"))
						continue;
					if (!expected.contains(path.text())) {
						Decide(code, "skipped", std::format("generated an unexpected page {}", path.text()), info);
						Log(true, std::format("[{}] {}: skipped; generated an unexpected page {}", m_name, code, path.text()));
						return false;
					}
					generated.push_back({path.text(), std::move(bytes)});
					files++;
				}
				for (auto& g : generated)
					m_result.Generated.push_back(std::move(g));
				Decide(code, "mapped", {}, info);
				Log(false, std::format("[{}] {}: generated {} file(s); rows by {}, columns by {}, {} cell(s) replaced", m_name, code, files, rowMethod, columnMethod, replaced));
				return true;
			}
		};
	}

	ExcelMerger::Output ExcelMerger::Run(const xivres::sqpack::reader& target, const std::vector<Source>& sources, const Options& options, const Callbacks& callbacks) {
		Output output;
		std::vector<std::string> names;
		for (const auto& name : xivres::excel::exl::reader(*target.at(xivres::path_spec("exd/root.exl"))) | std::views::keys)
			names.push_back(name);

		const auto rules = PrepareOverrides(options.Overrides, callbacks);
		std::mutex mtx;
		std::vector<SheetResult> results;
		results.reserve(names.size());
		{
			xivres::util::thread_pool::task_waiter waiter;
			for (const auto& name : names) {
				waiter.submit([&, name](auto&) {
					if (callbacks.Cancelled && callbacks.Cancelled())
						return;
					auto result = SheetJob(target, sources, options, callbacks, rules, name).Run();
					const auto lock = std::lock_guard(mtx);
					results.emplace_back(std::move(result));
				});
			}
			size_t done = 0;
			while (waiter.get()) {
				done++;
				if (callbacks.Progress)
					callbacks.Progress(done, names.size());
			}
		}

		if (callbacks.Cancelled && callbacks.Cancelled()) {
			output.Cancelled = true;
			return output;
		}

		std::ranges::sort(results, {}, &SheetResult::Name);
		for (auto& r : results) {
			if (r.Report.empty())
				continue;
			if (r.Mapped)
				output.GeneratedSheets++;
			else if (r.FastPath)
				output.FastPathSheets++;
			else if (r.Skipped)
				output.SkippedSheets++;
			output.Report[r.Name] = std::move(r.Report);
			for (auto& p : r.PassThroughs)
				output.PassThroughs.emplace_back(std::move(p));
			for (auto& g : r.Generated)
				output.Generated.emplace_back(std::move(g));
		}
		return output;
	}

	std::shared_ptr<xivres::packed_stream> ExcelMerger::MakePassThroughStream(const xivres::sqpack::reader& reader, const std::string& path) {
		const auto& info = reader.Entries[reader.get_entry_index(xivres::path_spec(path))];
		return std::make_shared<xivres::stream_as_packed_stream>(
			xivres::path_spec(path),
			std::make_shared<xivres::partial_view_stream>(reader.Data.at(info.Locator.DatFileIndex).Stream, info.Locator.offset(), info.Allocation));
	}

	std::shared_ptr<const MergedText> ExcelMerger::CollectMergedText(const xivres::sqpack::reader& target, game_language baseLanguage, const std::vector<std::shared_ptr<xivres::packed_stream>>& files) {
		static constexpr game_language AllLanguages[]{
			game_language::Japanese, game_language::English, game_language::German, game_language::French,
			game_language::ChineseSimplified, game_language::ChineseTraditional, game_language::Korean, game_language::TraditionalChinese,
		};
		const auto lower = [](std::string s) {
			std::ranges::transform(s, s.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
			return s;
		};

		std::map<std::string, const xivres::packed_stream*> byPath;
		for (const auto& file : files)
			byPath.emplace(lower(file->path_spec().text()), file.get());

		MergedTextBuilder builder(baseLanguage);
		for (const auto& sheet : NameSheets) {
			const auto exhRaw = ReadEntry(target, std::format("exd/{}.exh", sheet.Name));
			if (!exhRaw)
				continue;
			const auto exh = xivres::excel::exh::reader(sheet.Name, xivres::memory_stream(std::span<const char>(*exhRaw)));
			const auto columns = GetColumns(exh);
			const auto fixedSize = *exh.header().FixedDataSize;

			auto ranked = std::vector(sheet.Columns.begin(), sheet.Columns.end());
			const auto isItem = std::string_view(sheet.Name) == "Item" || std::string_view(sheet.Name) == "EventItem";
			if (isItem && ItemLinksUseSingular(baseLanguage))
				std::ranges::stable_partition(ranked, [](int c) { return c == 0; });
			std::vector<uint16_t> offsets;
			for (const auto c : ranked) {
				if (c != NoColumn && static_cast<size_t>(c) < columns.size() && columns[c].IsString())
					offsets.push_back(columns[c].Offset);
			}
			if (offsets.empty())
				continue;

			const auto plain = [fixedSize](const RowRef& row, uint16_t offset) { return MergedText::ToPlainText(GetString(row, fixedSize, offset)); };
			const auto base = LoadLanguage(target, exh, baseLanguage);
			auto* linkNames = std::string_view(sheet.Name) == "Item" ? &builder.Result().ItemNames
				: std::string_view(sheet.Name) == "EventItem" ? &builder.Result().EventItemNames
				: std::string_view(sheet.Name) == "Quest" ? &builder.Result().QuestNames
				: nullptr;
			for (const auto& [id, row] : base.Rows) {
				for (const auto offset : offsets) {
					if (const auto text = plain(row, offset))
						builder.AddBase(*text);
				}
				if (linkNames) {
					if (auto text = plain(row, offsets.front()); text && !text->empty())
						linkNames->emplace(id, std::move(*text));
				}
			}

			for (const auto language : AllLanguages) {
				if (language == baseLanguage)
					continue;
				std::vector<std::shared_ptr<ExdFile>> pages;
				std::map<uint32_t, RowRef> rows;
				for (const auto& page : exh.get_pages()) {
					const auto it = byPath.find(lower(exh.get_exd_path(page, language).text()));
					if (it == byPath.end())
						continue;
					pages.emplace_back(std::make_shared<ExdFile>(ParseExd(it->second->make_unpacked_ptr()->read_vector<char>())));
					for (const auto& [id, row] : pages.back()->Rows)
						rows.emplace(id, row);
				}
				for (const auto& [id, row] : rows) {
					const auto baseRow = base.Rows.find(id);
					if (baseRow == base.Rows.end())
						continue;
					for (size_t rank = 0; rank < offsets.size(); rank++) {
						const auto merged = plain(row, offsets[rank]);
						const auto original = plain(baseRow->second, offsets[rank]);
						if (merged && original)
							builder.Add(language, *merged, *original, static_cast<uint8_t>(rank));
					}
				}
			}
		}
		return builder.Finish();
	}
}
