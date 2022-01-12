#include "pch.h"
#include "XivAlexanderCommon/Sqex/Excel/Generator.h"

Sqex::Excel::Depth2ExhExdCreator::Depth2ExhExdCreator(std::string name, std::vector<Exh::Column> columns, int someSortOfBufferSize, size_t divideUnit)
	: Name(std::move(name))
	, Columns(std::move(columns))
	, SomeSortOfBufferSize(someSortOfBufferSize)
	, DivideUnit(divideUnit)
	, FixedDataSize([&columns = Columns]() {
		uint16_t size = 0;
		for (const auto& col : columns) {
			switch (col.Type) {
				case Exh::PackedBool0:
				case Exh::PackedBool1:
				case Exh::PackedBool2:
				case Exh::PackedBool3:
				case Exh::PackedBool4:
				case Exh::PackedBool5:
				case Exh::PackedBool6:
				case Exh::PackedBool7:
				case Exh::Bool:
				case Exh::Int8:
				case Exh::UInt8:
					size = std::max<uint16_t>(size, col.Offset + 1);
					break;

				case Exh::Int16:
				case Exh::UInt16:
					size = std::max<uint16_t>(size, col.Offset + 2);
					break;

				case Exh::String:
				case Exh::Int32:
				case Exh::UInt32:
				case Exh::Float32:
					size = std::max<uint16_t>(size, col.Offset + 4);
					break;

				case Exh::Int64:
				case Exh::UInt64:
					size = std::max<uint16_t>(size, col.Offset + 8);
					break;

				default:
					throw std::invalid_argument(std::format("Invald column type {}", static_cast<uint32_t>(col.Type)));
			}
		}
		return Sqex::Align<uint32_t>(size, 4).Alloc;
	}()) {
}

void Sqex::Excel::Depth2ExhExdCreator::AddLanguage(Language language) {
	if (const auto it = std::ranges::lower_bound(Languages, language);
		it == Languages.end() || *it != language)
		Languages.insert(it, language);
}

const std::vector<Sqex::Excel::ExdColumn>& Sqex::Excel::Depth2ExhExdCreator::GetRow(uint32_t id, Language language) const {
	return Data.at(id).at(language);
}

void Sqex::Excel::Depth2ExhExdCreator::SetRow(uint32_t id, Language language, std::vector<ExdColumn> row, bool replace) {
	if (!row.empty() && row.size() != Columns.size())
		throw std::invalid_argument(std::format("bad column data (expected {} columns, got {} columns)", Columns.size(), row.size()));
	auto& target = Data[id][language];
	if (target.empty() || replace)
		target = std::move(row);
}

std::pair<Sqex::Sqpack::EntryPathSpec, std::vector<char>> Sqex::Excel::Depth2ExhExdCreator::Flush(uint32_t startId, std::map<uint32_t, std::vector<char>> rows, Language language) {
	Exd::Header exdHeader;
	const auto exdHeaderSpan = std::span(reinterpret_cast<char*>(&exdHeader), sizeof exdHeader);
	memcpy(exdHeader.Signature, Exd::Header::Signature_Value, 4);
	exdHeader.Version = Exd::Header::Version_Value;

	size_t dataSize = 0;
	for (const auto& row : rows | std::views::values)
		dataSize += row.size();
	exdHeader.DataSize = static_cast<uint32_t>(dataSize);

	std::vector<Exd::RowLocator> locators;
	auto offsetAccumulator = static_cast<uint32_t>(sizeof exdHeader + rows.size() * sizeof Exd::RowLocator);
	for (const auto& row : rows) {
		locators.emplace_back(row.first, offsetAccumulator);
		offsetAccumulator += static_cast<uint32_t>(row.second.size());
	}
	const auto locatorSpan = std::span(reinterpret_cast<char*>(&locators[0]), std::span(locators).size_bytes());
	exdHeader.IndexSize = static_cast<uint32_t>(locatorSpan.size_bytes());

	std::vector<char> exdFile;
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
		Sqpack::EntryPathSpec(std::format("exd/{}_{}{}.exd", Name, startId, languageCode)),
		std::move(exdFile)
	);
}

std::map<Sqex::Sqpack::EntryPathSpec, std::vector<char>, Sqex::Sqpack::EntryPathSpec::FullPathComparator> Sqex::Excel::Depth2ExhExdCreator::Compile() {
	std::map<Sqpack::EntryPathSpec, std::vector<char>, Sqpack::EntryPathSpec::FullPathComparator> result;

	std::vector<std::pair<Exh::Pagination, std::vector<uint32_t>>> pages;
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
				std::vector<char> row(sizeof Exd::RowHeader + FixedDataSize);

				const auto fixedDataOffset = sizeof Exd::RowHeader;
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
						case Exh::String:
						{
							const auto stringOffset = BE(static_cast<uint32_t>(row.size() - variableDataOffset));
							std::copy_n(reinterpret_cast<const char*>(&stringOffset), 4, &row[fixedDataOffset + columnDefinition.Offset]);
							row.insert(row.end(), column.String.Escaped().begin(), column.String.Escaped().end());
							row.push_back(0);
							column.ValidSize = 0;
							break;
						}

						case Exh::Bool:
						case Exh::Int8:
						case Exh::UInt8:
							column.ValidSize = 1;
							break;

						case Exh::Int16:
						case Exh::UInt16:
							column.ValidSize = 2;
							break;

						case Exh::Int32:
						case Exh::UInt32:
						case Exh::Float32:
							column.ValidSize = 4;
							break;

						case Exh::Int64:
						case Exh::UInt64:
							column.ValidSize = 8;
							break;

						case Exh::PackedBool0:
						case Exh::PackedBool1:
						case Exh::PackedBool2:
						case Exh::PackedBool3:
						case Exh::PackedBool4:
						case Exh::PackedBool5:
						case Exh::PackedBool6:
						case Exh::PackedBool7:
							column.ValidSize = 0;
							if (column.boolean)
								row[fixedDataOffset + columnDefinition.Offset] |= (1 << (static_cast<int>(column.Type) - static_cast<int>(Exh::PackedBool0)));
							else
								row[fixedDataOffset + columnDefinition.Offset] &= ~((1 << (static_cast<int>(column.Type) - static_cast<int>(Exh::PackedBool0))));
							break;
					}
					if (column.ValidSize) {
						const auto target = std::span(row).subspan(fixedDataOffset + columnDefinition.Offset, column.ValidSize);
						std::copy_n(&column.Buffer[0], column.ValidSize, &target[0]);
						// ReSharper disable once CppUseRangeAlgorithm
						std::reverse(target.begin(), target.end());
					}
				}
				row.resize(Sqex::Align<size_t>(row.size(), 4));

				auto& rowHeader = *reinterpret_cast<Exd::RowHeader*>(&row[0]);
				rowHeader.DataSize = static_cast<uint32_t>(row.size() - sizeof rowHeader);
				rowHeader.SubRowCount = 1;

				rows.emplace(id, std::move(row));
			}
			if (rows.empty())
				continue;
			result.emplace(Flush(page.first.StartId, std::move(rows), language));
		}
	}

	{
		Exh::Header exhHeader;
		const auto exhHeaderSpan = std::span(reinterpret_cast<char*>(&exhHeader), sizeof exhHeader);
		memcpy(exhHeader.Signature, Exh::Header::Signature_Value, 4);
		exhHeader.Version = Exh::Header::Version_Value;
		exhHeader.FixedDataSize = static_cast<uint16_t>(FixedDataSize);
		exhHeader.ColumnCount = static_cast<uint16_t>(Columns.size());
		exhHeader.PageCount = static_cast<uint16_t>(pages.size());
		exhHeader.LanguageCount = static_cast<uint16_t>(Languages.size());
		exhHeader.SomeSortOfBufferSize = SomeSortOfBufferSize;
		exhHeader.Depth = Exh::Level2;
		exhHeader.RowCountWithoutSkip = static_cast<uint32_t>(Data.size());

		const auto columnSpan = std::span(reinterpret_cast<const char*>(&Columns[0]), std::span(Columns).size_bytes());
		std::vector<Exh::Pagination> paginations;
		for (const auto& pagination : pages | std::views::keys)
			paginations.emplace_back(pagination);
		const auto paginationSpan = std::span(reinterpret_cast<const char*>(&paginations[0]), std::span(paginations).size_bytes());
		const auto languageSpan = std::span(reinterpret_cast<const char*>(&Languages[0]), std::span(Languages).size_bytes());

		std::vector<char> exhFile;
		exhFile.reserve(exhHeaderSpan.size_bytes() + columnSpan.size_bytes() + paginationSpan.size_bytes() + languageSpan.size_bytes());
		std::copy_n(&exhHeaderSpan[0], exhHeaderSpan.size_bytes(), std::back_inserter(exhFile));
		std::copy_n(&columnSpan[0], columnSpan.size_bytes(), std::back_inserter(exhFile));
		std::copy_n(&paginationSpan[0], paginationSpan.size_bytes(), std::back_inserter(exhFile));
		std::copy_n(&languageSpan[0], languageSpan.size_bytes(), std::back_inserter(exhFile));
		result.emplace(std::format("exd/{}.exh", Name), std::move(exhFile));
	}

	return result;
}
