#ifndef _XIVRES_EXCELGENERATOR_H_
#define _XIVRES_EXCELGENERATOR_H_

#include <map>
#include <ranges>
#include <set>

#include "Excel.h"
#include "Sqpack.h"

namespace XivRes {
	class Depth2ExhExdCreator {
		static uint32_t CalculateFixedDataSize(const std::vector<ExhColHeader>& columns) {
			uint16_t size = 0;
			for (const auto& col : columns) {
				switch (col.Type) {
					case PackedBool0:
					case PackedBool1:
					case PackedBool2:
					case PackedBool3:
					case PackedBool4:
					case PackedBool5:
					case PackedBool6:
					case PackedBool7:
					case Bool:
					case Int8:
					case UInt8:
						size = (std::max<uint16_t>)(size, col.Offset + 1);
						break;

					case Int16:
					case UInt16:
						size = (std::max<uint16_t>)(size, col.Offset + 2);
						break;

					case String:
					case Int32:
					case UInt32:
					case Float32:
						size = (std::max<uint16_t>)(size, col.Offset + 4);
						break;

					case Int64:
					case UInt64:
						size = (std::max<uint16_t>)(size, col.Offset + 8);
						break;

					default:
						throw std::invalid_argument(std::format("Invald column type {}", static_cast<uint32_t>(col.Type)));
				}
			}
			return XivRes::Align<uint32_t>(size, 4).Alloc;
		}

	public:
		const std::string Name;
		const std::vector<ExhColHeader> Columns;
		const int SomeSortOfBufferSize;
		const size_t DivideUnit;
		const uint32_t FixedDataSize;
		std::map<uint32_t, std::map<Language, std::vector<ExcelCell>>> Data;
		std::set<uint32_t> DivideAtIds;
		std::vector<Language> Languages;
		std::vector<Language> FillMissingLanguageFrom;

		Depth2ExhExdCreator(std::string name, std::vector<ExhColHeader> columns, int someSortOfBufferSize, size_t divideUnit = SIZE_MAX)
			: Name(std::move(name))
			, Columns(std::move(columns))
			, SomeSortOfBufferSize(someSortOfBufferSize)
			, DivideUnit(divideUnit)
			, FixedDataSize(CalculateFixedDataSize(Columns)) {
		}

		void AddLanguage(Language language) {
			if (const auto it = std::ranges::lower_bound(Languages, language);
				it == Languages.end() || *it != language)
				Languages.insert(it, language);
		}

		const std::vector<ExcelCell>& GetRow(uint32_t id, Language language) const {
			return Data.at(id).at(language);
		}

		void SetRow(uint32_t id, Language language, std::vector<ExcelCell> row, bool replace = true) {
			if (!row.empty() && row.size() != Columns.size())
				throw std::invalid_argument(std::format("bad column data (expected {} columns, got {} columns)", Columns.size(), row.size()));
			auto& target = Data[id][language];
			if (target.empty() || replace)
				target = std::move(row);
		}

	private:
		template<typename T = char, typename = std::enable_if_t<sizeof T == 1>>
		std::pair<SqpackPathSpec, std::vector<T>> Flush(uint32_t startId, std::map<uint32_t, std::vector<char>> rows, Language language) {
			ExdHeader exdHeader;
			const auto exdHeaderSpan = span_cast<T>(1, &exdHeader);
			memcpy(exdHeader.Signature, ExdHeader::Signature_Value, 4);
			exdHeader.Version = ExdHeader::Version_Value;

			size_t dataSize = 0;
			for (const auto& row : rows | std::views::values)
				dataSize += row.size();
			exdHeader.DataSize = static_cast<uint32_t>(dataSize);

			std::vector<ExdRowLocator> locators;
			auto offsetAccumulator = static_cast<uint32_t>(sizeof exdHeader + rows.size() * sizeof ExdRowLocator);
			for (const auto& row : rows) {
				locators.emplace_back(row.first, offsetAccumulator);
				offsetAccumulator += static_cast<uint32_t>(row.second.size());
			}
			const auto locatorSpan = span_cast<T>(locators);
			exdHeader.IndexSize = static_cast<uint32_t>(locatorSpan.size_bytes());

			std::vector<T> exdFile;
			exdFile.reserve(offsetAccumulator);
			std::copy_n(&exdHeaderSpan[0], exdHeaderSpan.size_bytes(), std::back_inserter(exdFile));
			std::copy_n(&locatorSpan[0], locatorSpan.size_bytes(), std::back_inserter(exdFile));
			for (const auto& row : rows | std::views::values)
				std::copy_n(&row[0], row.size(), std::back_inserter(exdFile));

			const auto* languageCode = "";
			switch (language) {
				case Language::Unspecified:
					break;
				case Language::Japanese:
					languageCode = "_ja";
					break;
				case Language::English:
					languageCode = "_en";
					break;
				case Language::German:
					languageCode = "_de";
					break;
				case Language::French:
					languageCode = "_fr";
					break;
				case Language::ChineseSimplified:
					languageCode = "_chs";
					break;
				case Language::ChineseTraditional:
					languageCode = "_cht";
					break;
				case Language::Korean:
					languageCode = "_ko";
					break;
				default:
					throw std::invalid_argument("Invalid language");
			}
			return std::make_pair(
				SqpackPathSpec(std::format("exd/{}_{}{}.exd", Name, startId, languageCode)),
				std::move(exdFile)
			);
		}

	public:
		template<typename T = char, typename = std::enable_if_t<sizeof T == 1>>
		std::map<SqpackPathSpec, std::vector<T>, SqpackPathSpec::FullPathComparator> Compile() {
			std::map<SqpackPathSpec, std::vector<T>, SqpackPathSpec::FullPathComparator> result;

			std::vector<std::pair<ExhPagination, std::vector<uint32_t>>> pages;
			for (const auto id : Data | std::views::keys) {
				if (pages.empty()) {
					pages.emplace_back();
				} else if (pages.back().second.size() == DivideUnit || DivideAtIds.find(id) != DivideAtIds.end()) {
					pages.back().first.RowCountWithSkip = pages.back().second.back() - pages.back().second.front() + 1;
					pages.emplace_back();
				}

				if (pages.back().second.empty())
					pages.back().first.StartId = id;
				pages.back().second.push_back(id);
			}
			if (pages.empty())
				return {};

			pages.back().first.RowCountWithSkip = pages.back().second.back() - pages.back().second.front() + 1;

			for (const auto& page : pages) {
				for (const auto language : Languages) {
					std::map<uint32_t, std::vector<char>> rows;
					for (const auto id : page.second) {
						std::vector<char> row(sizeof ExdRowHeader + FixedDataSize);

						const auto fixedDataOffset = sizeof ExdRowHeader;
						const auto variableDataOffset = fixedDataOffset + FixedDataSize;

						auto sourceLanguage = language;
						auto& rowSet = Data[id];
						if (rowSet.find(sourceLanguage) == rowSet.end()) {
							sourceLanguage = Language::Unspecified;
							for (auto lang : FillMissingLanguageFrom) {
								if (rowSet.find(lang) == rowSet.end()) {
									sourceLanguage = lang;
									break;
								}
							}
							if (sourceLanguage == Language::Unspecified)
								continue;
						}

						auto& columns = rowSet[sourceLanguage];
						if (columns.empty())
							continue;

						for (size_t i = 0; i < columns.size(); ++i) {
							auto& column = columns[i];
							const auto& columnDefinition = Columns[i];
							switch (columnDefinition.Type) {
								case String:
								{
									const auto stringOffset = BE<uint32_t>(static_cast<uint32_t>(row.size() - variableDataOffset));
									std::copy_n(reinterpret_cast<const char*>(&stringOffset), 4, &row[fixedDataOffset + columnDefinition.Offset]);
									row.insert(row.end(), column.String.Escaped().begin(), column.String.Escaped().end());
									row.push_back(0);
									column.ValidSize = 0;
									break;
								}

								case Bool:
								case Int8:
								case UInt8:
									column.ValidSize = 1;
									break;

								case Int16:
								case UInt16:
									column.ValidSize = 2;
									break;

								case Int32:
								case UInt32:
								case Float32:
									column.ValidSize = 4;
									break;

								case Int64:
								case UInt64:
									column.ValidSize = 8;
									break;

								case PackedBool0:
								case PackedBool1:
								case PackedBool2:
								case PackedBool3:
								case PackedBool4:
								case PackedBool5:
								case PackedBool6:
								case PackedBool7:
									column.ValidSize = 0;
									if (column.boolean)
										row[fixedDataOffset + columnDefinition.Offset] |= (1 << (static_cast<int>(column.Type) - static_cast<int>(PackedBool0)));
									else
										row[fixedDataOffset + columnDefinition.Offset] &= ~((1 << (static_cast<int>(column.Type) - static_cast<int>(PackedBool0))));
									break;
							}
							if (column.ValidSize) {
								const auto target = std::span(row).subspan(fixedDataOffset + columnDefinition.Offset, column.ValidSize);
								std::copy_n(&column.Buffer[0], column.ValidSize, &target[0]);
								// ReSharper disable once CppUseRangeAlgorithm
								std::reverse(target.begin(), target.end());
							}
						}
						row.resize(XivRes::Align<size_t>(row.size(), 4));

						auto& rowHeader = *reinterpret_cast<ExdRowHeader*>(&row[0]);
						rowHeader.DataSize = static_cast<uint32_t>(row.size() - sizeof rowHeader);
						rowHeader.SubRowCount = 1;

						rows.emplace(id, std::move(row));
					}
					if (rows.empty())
						continue;
					result.emplace(Flush<T>(page.first.StartId, std::move(rows), language));
				}
			}

			{
				ExhHeader exhHeader;
				const auto exhHeaderSpan = span_cast<T>(1, &exhHeader);
				memcpy(exhHeader.Signature, ExhHeader::Signature_Value, 4);
				exhHeader.Version = ExhHeader::Version_Value;
				exhHeader.FixedDataSize = static_cast<uint16_t>(FixedDataSize);
				exhHeader.ColumnCount = static_cast<uint16_t>(Columns.size());
				exhHeader.PageCount = static_cast<uint16_t>(pages.size());
				exhHeader.LanguageCount = static_cast<uint16_t>(Languages.size());
				exhHeader.SomeSortOfBufferSize = SomeSortOfBufferSize;
				exhHeader.Depth = Level2;
				exhHeader.RowCountWithoutSkip = static_cast<uint32_t>(Data.size());

				const auto columnSpan = span_cast<T>(Columns);
				std::vector<ExhPagination> paginations;
				for (const auto& pagination : pages | std::views::keys)
					paginations.emplace_back(pagination);
				const auto paginationSpan = span_cast<T>(paginations);
				const auto languageSpan = span_cast<T>(Languages);

				std::vector<T> exhFile;
				exhFile.reserve(exhHeaderSpan.size_bytes() + columnSpan.size_bytes() + paginationSpan.size_bytes() + languageSpan.size_bytes());
				std::copy_n(&exhHeaderSpan[0], exhHeaderSpan.size_bytes(), std::back_inserter(exhFile));
				std::copy_n(&columnSpan[0], columnSpan.size_bytes(), std::back_inserter(exhFile));
				std::copy_n(&paginationSpan[0], paginationSpan.size_bytes(), std::back_inserter(exhFile));
				std::copy_n(&languageSpan[0], languageSpan.size_bytes(), std::back_inserter(exhFile));
				result.emplace(std::format("exd/{}.exh", Name), std::move(exhFile));
			}

			return result;
		}
	};
}

#endif