#include "pch.h"

#include <set>
#include <XivAlexanderCommon/Sqex_Excel.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>

class ExlReader {
	int m_version = 0;
	std::map<std::string, int> m_nameToIdMap;
	std::map<int, std::string> m_idToNameMap;

public:
	ExlReader(const Sqex::RandomAccessStream& stream) {
		std::string data(static_cast<size_t>(stream.StreamSize()), '\0');
		stream.ReadStream(0, std::span(data));
		std::istringstream in(std::move(data));

		std::string line;
		for (size_t i = 0; std::getline(in, line); ++i) {
			line = Utils::StringTrim(line);
			if (line.empty())
				continue;  // skip empty lines
			auto split = Utils::StringSplit<std::string>(line, ",", 1);
			if (split.size() < 2)
				throw Sqex::CorruptDataException("Could not find delimiter character ','");

			char* end;
			const auto id = std::strtol(split[1].data(), &end, 10);
			if (end != &split[1].back() + 1)
				throw Sqex::CorruptDataException("Bad numeric specification");
			if (!i) {
				if (split[0] != "EXLT")
					throw Sqex::CorruptDataException("Bad header");
				m_version = id;
			} else {
				if (id != -1)
					m_idToNameMap.emplace(id, split[0]);
				m_nameToIdMap.emplace(std::move(split[0]), id);
			}
		}
	}

	const std::string& operator[](int id) const {
		return m_idToNameMap.at(id);
	}

	int operator[](const std::string& s) const {
		return m_nameToIdMap.at(s);
	}

	[[nodiscard]] auto begin() const {
		return m_nameToIdMap.begin();
	}

	[[nodiscard]] auto end() const {
		return m_nameToIdMap.end();
	}

	[[nodiscard]] auto rbegin() const {
		return m_nameToIdMap.rbegin();
	}

	[[nodiscard]] auto rend() const {
		return m_nameToIdMap.rend();
	}
};

class ExhReader;

struct ExdColumn {
	Sqex::Excel::Exh::ColumnDataType Type;
	uint8_t ValidSize;

	union {
		uint8_t Buffer[8];
		bool boolean;
		int8_t int8;
		uint8_t uint8;
		int16_t int16;
		uint16_t uint16;
		int32_t int32;
		uint32_t uint32;
		float float32;
		int64_t int64;
		uint64_t uint64;
	};

	std::string String;
};

class ExhReader {
public:
	std::string Name;
	Sqex::Excel::Exh::Header Header;
	std::shared_ptr<std::vector<Sqex::Excel::Exh::Column>> Columns;
	std::vector<Sqex::Excel::Exh::Pagination> Pages;
	std::vector<Sqex::Language> Languages;

	ExhReader(std::string name, const Sqex::RandomAccessStream& stream, bool strict = false)
		: Name(std::move(name))
		, Header(stream.ReadStream<Sqex::Excel::Exh::Header>(0))
		, Columns(std::make_shared<std::vector<Sqex::Excel::Exh::Column>>(std::move(stream.ReadStreamIntoVector<Sqex::Excel::Exh::Column>(
			sizeof Header, Header.ColumnCount))))
		, Pages(stream.ReadStreamIntoVector<Sqex::Excel::Exh::Pagination>(
			sizeof Header + std::span(*Columns).size_bytes(),
			Header.PageCount))
		, Languages(stream.ReadStreamIntoVector<Sqex::Language>(
			sizeof Header + std::span(*Columns).size_bytes() + std::span(Pages).size_bytes(),
			Header.LanguageCount)) {
		if (strict) {
			const auto dataLength = stream.StreamSize();
			const auto expectedLength = sizeof Header + std::span(*Columns).size_bytes() + std::span(Pages).size_bytes() + std::span(Languages).size_bytes();
			if (dataLength > expectedLength)
				throw Sqex::CorruptDataException(std::format("Extra {} byte(s) found", dataLength - expectedLength));
		}
	}

	[[nodiscard]] Sqex::Sqpack::EntryPathSpec GetDataPathSpec(const Sqex::Excel::Exh::Pagination& page, Sqex::Language language) const {
		const auto* languageCode = "";
		switch (language) {
			case Sqex::Language::Unspecified:
				break;
			case Sqex::Language::Japanese:
				languageCode = "_ja";
				break;
			case Sqex::Language::English:
				languageCode = "_en";
				break;
			case Sqex::Language::German:
				languageCode = "_de";
				break;
			case Sqex::Language::French:
				languageCode = "_fr";
				break;
			case Sqex::Language::ChineseSimplified:
				languageCode = "_chs";
				break;
			case Sqex::Language::ChineseTraditional:
				languageCode = "_cht";
				break;
			case Sqex::Language::Korean:
				languageCode = "_ko";
				break;
			default:
				throw std::invalid_argument("Invalid language");
		}
		return std::format("exd/{}_{}{}.exd", Name, page.StartId.Value(), languageCode);
	}

	void Dump() const {
		std::cout << std::format("Signature: {}\n", std::string(Header.Signature, 4));
		std::cout << std::format("Version: {}\n", Header.Version.Value());
		std::cout << std::format("FixedDataSize: {}\n", Header.FixedDataSize.Value());
		std::cout << std::format("ColumnCount: {}\n", Header.ColumnCount.Value());
		std::cout << std::format("PageCount: {}\n", Header.PageCount.Value());
		std::cout << std::format("LanguageCount: {}\n", Header.LanguageCount.Value());
		std::cout << std::format("Unknown2: {}\n", Header.Unknown2.Value());
		std::cout << std::format("Padding_0x010: {}\n", Header.Padding_0x010.Value());
		std::cout << std::format("Depth: {}\n", static_cast<uint8_t>(Header.Depth.Value()));
		std::cout << std::format("Padding_0x012: {}\n", Header.Padding_0x012.Value());
		std::cout << std::format("SubRowCount: {}\n", Header.RowCount.Value());
		std::cout << std::format("Padding_0x018: {}\n", Header.Padding_0x018.Value());

		for (const auto& column : *Columns)
			std::cout << std::format("Column: Type {}, Offset {}\n", static_cast<uint16_t>(column.Type.Value()), column.Offset.Value());
		for (const auto& page : Pages)
			std::cout << std::format("Page: StartId {}, SubRowCount {}\n", page.StartId.Value(), page.RowCount.Value());
		for (const auto& lang : Languages)
			std::cout << std::format("Language: {}\n", static_cast<uint16_t>(lang));
		for (const auto& page : Pages)
			for (const auto& lang : Languages)
				std::cout << std::format("Example Entry: {}\n", GetDataPathSpec(page, lang));
	}
};

class ExdReader {
	const std::shared_ptr<const Sqex::RandomAccessStream> m_stream;
	const size_t m_fixedDataSize;
	const Sqex::Excel::Exh::Depth m_depth;
	std::vector<std::pair<uint32_t, uint32_t>> m_rowLocators;

public:
	const Sqex::Excel::Exd::Header Header;
	const std::shared_ptr<std::vector<Sqex::Excel::Exh::Column>> ColumnDefinitions;

	ExdReader(const ExhReader& exh, std::shared_ptr<const Sqex::RandomAccessStream> stream, bool strict = false)
		: m_stream(std::move(stream))
		, m_fixedDataSize(exh.Header.FixedDataSize)
		, m_depth(exh.Header.Depth)
		, Header(m_stream->ReadStream<Sqex::Excel::Exd::Header>(0))
		, ColumnDefinitions(exh.Columns) {
		const auto count = Header.IndexSize / sizeof Sqex::Excel::Exd::RowLocator;
		m_rowLocators.reserve(count);
		for (const auto& locator : m_stream->ReadStreamIntoVector<Sqex::Excel::Exd::RowLocator>(sizeof Header, count)) {
			m_rowLocators.emplace_back(std::make_pair(locator.RowId.Value(), locator.Offset.Value()));
		}
		std::ranges::sort(m_rowLocators);
	}

private:
	ExdColumn TranslateColumn(const Sqex::Excel::Exh::Column& columnDefinition, std::span<const char> fixedData, std::span<const char> fullData) const {
		ExdColumn column{.Type = columnDefinition.Type};
		switch (column.Type) {
			case Sqex::Excel::Exh::String: {
				Sqex::BE<uint32_t> stringOffset;
				std::copy_n(&fixedData[columnDefinition.Offset], 4, reinterpret_cast<char*>(&stringOffset));
				column.String = std::string(&fullData[m_fixedDataSize + stringOffset]);
				break;
			}

			case Sqex::Excel::Exh::Bool:
			case Sqex::Excel::Exh::Int8:
			case Sqex::Excel::Exh::UInt8:
				column.ValidSize = 1;
				break;

			case Sqex::Excel::Exh::Int16:
			case Sqex::Excel::Exh::UInt16:
				column.ValidSize = 2;
				break;

			case Sqex::Excel::Exh::Int32:
			case Sqex::Excel::Exh::UInt32:
			case Sqex::Excel::Exh::Float32:
				column.ValidSize = 4;
				break;

			case Sqex::Excel::Exh::Int64:
			case Sqex::Excel::Exh::UInt64:
				column.ValidSize = 8;
				break;

			case Sqex::Excel::Exh::PackedBool0:
			case Sqex::Excel::Exh::PackedBool1:
			case Sqex::Excel::Exh::PackedBool2:
			case Sqex::Excel::Exh::PackedBool3:
			case Sqex::Excel::Exh::PackedBool4:
			case Sqex::Excel::Exh::PackedBool5:
			case Sqex::Excel::Exh::PackedBool6:
			case Sqex::Excel::Exh::PackedBool7:
				column.boolean = fixedData[columnDefinition.Offset] & (1 << (static_cast<int>(column.Type) - static_cast<int>(Sqex::Excel::Exh::PackedBool0)));
				break;

			default:
				throw Sqex::CorruptDataException(std::format("Invald column type {}", static_cast<uint32_t>(column.Type)));
		}
		if (column.ValidSize) {
			std::copy_n(&fixedData[columnDefinition.Offset], column.ValidSize, &column.Buffer[0]);
			std::reverse(&column.Buffer[0], &column.Buffer[column.ValidSize]);
		}
		return column;
	}

	[[nodiscard]] std::pair<Sqex::Excel::Exd::RowHeader, std::vector<char>> ReadRowRaw(uint32_t index) const {
		const auto it = std::ranges::lower_bound(m_rowLocators, std::make_pair(index, 0U), [](const auto& l, const auto& r) {
			return l.first < r.first;
		});
		if (it == m_rowLocators.end() || it->first != index)
			throw std::out_of_range("index out of range");

		const auto rowHeader = m_stream->ReadStream<Sqex::Excel::Exd::RowHeader>(it->second);
		return std::make_pair(rowHeader, m_stream->ReadStreamIntoVector<char>(it->second + sizeof rowHeader, rowHeader.DataSize));
	}

public:
	[[nodiscard]] std::vector<ExdColumn> ReadDepth2(uint32_t index) const {
		if (m_depth != Sqex::Excel::Exh::Level2)
			throw std::invalid_argument("Not a 2nd depth sheet");

		std::vector<ExdColumn> result;
		const auto [rowHeader, buffer] = ReadRowRaw(index);
		const auto fixedData = std::span(buffer).subspan(0, m_fixedDataSize);

		if (rowHeader.SubRowCount != 1)
			throw Sqex::CorruptDataException("SubRowCount > 1 on 2nd depth sheet");

		for (const auto& columnDefinition : *ColumnDefinitions)
			result.emplace_back(TranslateColumn(columnDefinition, fixedData, std::span(buffer)));

		return result;
	}

	[[nodiscard]] std::vector<std::vector<ExdColumn>> ReadDepth3(uint32_t index) const {
		if (m_depth != Sqex::Excel::Exh::Level3)
			throw std::invalid_argument("Not a 3rd depth sheet");

		std::vector<std::vector<ExdColumn>> result;
		const auto [rowHeader, buffer] = ReadRowRaw(index);

		for (size_t i = 0, i_ = rowHeader.SubRowCount; i < i_; ++i) {
			const auto baseOffset = i * (2 + m_fixedDataSize);
			const auto fixedData = std::span(buffer).subspan(2 + baseOffset, m_fixedDataSize);

			std::vector<ExdColumn> row;
			for (const auto& columnDefinition : *ColumnDefinitions)
				row.emplace_back(TranslateColumn(columnDefinition, fixedData, std::span(buffer)));
			result.emplace_back(std::move(row));
		}
		return result;
	}

	[[nodiscard]] std::vector<uint32_t> GetIds() const {
		std::vector<uint32_t> ids;
		for (const auto& id : m_rowLocators | std::views::keys)
			ids.emplace_back(id);
		return ids;
	}
};

class Depth2ExhExdCreator {
public:
	const std::string Name;
	const std::vector<Sqex::Excel::Exh::Column> Columns;
	const int Unknown2;
	const size_t DivideUnit;
	const uint32_t FixedDataSize;
	std::map<uint32_t, std::map<Sqex::Language, std::vector<ExdColumn>>> Data;
	std::vector<Sqex::Language> Languages;

	Depth2ExhExdCreator(std::string name, std::vector<Sqex::Excel::Exh::Column> columns, int unknown2, size_t divideUnit = SIZE_MAX)
		: Name(std::move(name))
		, Columns(std::move(columns))
		, Unknown2(unknown2)
		, DivideUnit(divideUnit)
		, FixedDataSize([&columns = Columns]() {
			uint16_t size = 0;
			for (const auto& col : columns) {
				switch (col.Type) {
					case Sqex::Excel::Exh::PackedBool0:
					case Sqex::Excel::Exh::PackedBool1:
					case Sqex::Excel::Exh::PackedBool2:
					case Sqex::Excel::Exh::PackedBool3:
					case Sqex::Excel::Exh::PackedBool4:
					case Sqex::Excel::Exh::PackedBool5:
					case Sqex::Excel::Exh::PackedBool6:
					case Sqex::Excel::Exh::PackedBool7:
					case Sqex::Excel::Exh::Bool:
					case Sqex::Excel::Exh::Int8:
					case Sqex::Excel::Exh::UInt8:
						size = std::max<uint16_t>(size, col.Offset + 1);
						break;

					case Sqex::Excel::Exh::Int16:
					case Sqex::Excel::Exh::UInt16:
						size = std::max<uint16_t>(size, col.Offset + 2);
						break;

					case Sqex::Excel::Exh::String:
					case Sqex::Excel::Exh::Int32:
					case Sqex::Excel::Exh::UInt32:
					case Sqex::Excel::Exh::Float32:
						size = std::max<uint16_t>(size, col.Offset + 4);
						break;

					case Sqex::Excel::Exh::Int64:
					case Sqex::Excel::Exh::UInt64:
						size = std::max<uint16_t>(size, col.Offset + 8);
						break;

					default:
						throw std::invalid_argument(std::format("Invald column type {}", static_cast<uint32_t>(col.Type)));
				}
			}
			return Sqex::Align<uint32_t>(size, 4).Alloc;
		}()) {
	}

	void AddLanguage(Sqex::Language language) {
		if (const auto it = std::ranges::lower_bound(Languages, language);
			it == Languages.end() || *it != language)
			Languages.insert(it, language);
	}

	void SetRow(uint32_t id, Sqex::Language language, std::vector<ExdColumn> row, bool replace = true) {
		if (!row.empty() && row.size() != Columns.size())
			throw std::invalid_argument("bad column data");
		auto& target = Data[id][language];
		if (target.empty() || replace)
			target = std::move(row);
	}

	std::pair<Sqex::Sqpack::EntryPathSpec, std::vector<char>> Flush(uint32_t startId, std::map<uint32_t, std::vector<char>> rows, Sqex::Language language) {
		Sqex::Excel::Exd::Header exdHeader;
		const auto exdHeaderSpan = std::span(reinterpret_cast<char*>(&exdHeader), sizeof exdHeader);
		memcpy(exdHeader.Signature, Sqex::Excel::Exd::Header::Signature_Value, 4);
		exdHeader.Version = Sqex::Excel::Exd::Header::Version_Value;

		size_t dataSize = 0;
		for (const auto& row : rows | std::views::values)
			dataSize += row.size();
		exdHeader.DataSize = static_cast<uint32_t>(dataSize);

		std::vector<Sqex::Excel::Exd::RowLocator> locators;
		auto offsetAccumulator = static_cast<uint32_t>(sizeof exdHeader + rows.size() * sizeof Sqex::Excel::Exd::RowLocator);
		for (const auto& [id, row] : rows) {
			locators.emplace_back(id, offsetAccumulator);
			offsetAccumulator += static_cast<uint32_t>(row.size());
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
			case Sqex::Language::Unspecified:
				break;
			case Sqex::Language::Japanese:
				languageCode = "_ja";
				break;
			case Sqex::Language::English:
				languageCode = "_en";
				break;
			case Sqex::Language::German:
				languageCode = "_de";
				break;
			case Sqex::Language::French:
				languageCode = "_fr";
				break;
			case Sqex::Language::ChineseSimplified:
				languageCode = "_chs";
				break;
			case Sqex::Language::ChineseTraditional:
				languageCode = "_cht";
				break;
			case Sqex::Language::Korean:
				languageCode = "_ko";
				break;
			default:
				throw std::invalid_argument("Invalid language");
		}
		return std::make_pair(
			Sqex::Sqpack::EntryPathSpec(std::format("exd/{}_{}{}.exd", Name, startId, languageCode)),
			std::move(exdFile)
		);
	}

	std::map<Sqex::Sqpack::EntryPathSpec, std::vector<char>> Compile() {
		std::map<Sqex::Sqpack::EntryPathSpec, std::vector<char>> result;

		std::vector<std::pair<Sqex::Excel::Exh::Pagination, std::vector<uint32_t>>> pages;
		for (const auto id : Data | std::views::keys) {
			if (pages.empty()) {
				pages.emplace_back();
			} else if (pages.back().second.size() == DivideUnit) {
				pages.back().first.RowCount = pages.back().second.back() - pages.back().second.front() + 1;
				pages.emplace_back();
			}

			if (pages.back().second.empty())
				pages.back().first.StartId = id;
			pages.back().second.push_back(id);
		}
		pages.back().first.RowCount = pages.back().second.back() - pages.back().second.front() + 1;

		for (const auto& [page, ids] : pages) {
			for (const auto language : Languages) {
				std::map<uint32_t, std::vector<char>> rows;
				for (const auto id : ids) {
					std::vector<char> row(sizeof Sqex::Excel::Exd::RowHeader + FixedDataSize);

					const auto fixedDataOffset = sizeof Sqex::Excel::Exd::RowHeader;
					const auto variableDataOffset = fixedDataOffset + FixedDataSize;

					auto& rowSet = Data[id];
					if (rowSet.find(language) == rowSet.end())
						continue;

					auto& columns = rowSet[language];
					if (columns.empty())
						continue;

					for (size_t i = 0; i < columns.size(); ++i) {
						auto& column = columns[i];
						const auto& columnDefinition = Columns[i];
						switch (columnDefinition.Type) {
							case Sqex::Excel::Exh::String: {
								const auto stringOffset = Sqex::BE(static_cast<uint32_t>(row.size() - variableDataOffset));
								std::copy_n(reinterpret_cast<const char*>(&stringOffset), 4, &row[fixedDataOffset + columnDefinition.Offset]);
								row.insert(row.end(), column.String.begin(), column.String.end());
								row.push_back(0);
								column.ValidSize = 0;
								break;
							}

							case Sqex::Excel::Exh::Bool:
							case Sqex::Excel::Exh::Int8:
							case Sqex::Excel::Exh::UInt8:
								column.ValidSize = 1;
								break;

							case Sqex::Excel::Exh::Int16:
							case Sqex::Excel::Exh::UInt16:
								column.ValidSize = 2;
								break;

							case Sqex::Excel::Exh::Int32:
							case Sqex::Excel::Exh::UInt32:
							case Sqex::Excel::Exh::Float32:
								column.ValidSize = 4;
								break;

							case Sqex::Excel::Exh::Int64:
							case Sqex::Excel::Exh::UInt64:
								column.ValidSize = 8;
								break;

							case Sqex::Excel::Exh::PackedBool0:
							case Sqex::Excel::Exh::PackedBool1:
							case Sqex::Excel::Exh::PackedBool2:
							case Sqex::Excel::Exh::PackedBool3:
							case Sqex::Excel::Exh::PackedBool4:
							case Sqex::Excel::Exh::PackedBool5:
							case Sqex::Excel::Exh::PackedBool6:
							case Sqex::Excel::Exh::PackedBool7:
								column.ValidSize = 0;
								if (column.boolean)
									row[fixedDataOffset + columnDefinition.Offset] |= (1 << (static_cast<int>(column.Type) - static_cast<int>(Sqex::Excel::Exh::PackedBool0)));
								else
									row[fixedDataOffset + columnDefinition.Offset] &= ~((1 << (static_cast<int>(column.Type) - static_cast<int>(Sqex::Excel::Exh::PackedBool0))));
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

					auto& rowHeader = *reinterpret_cast<Sqex::Excel::Exd::RowHeader*>(&row[0]);
					rowHeader.DataSize = static_cast<uint32_t>(row.size() - sizeof rowHeader);
					rowHeader.SubRowCount = 1;

					rows.emplace(id, std::move(row));
				}
				if (rows.empty())
					continue;
				result.emplace(Flush(page.StartId, std::move(rows), language));
			}
		}

		{
			Sqex::Excel::Exh::Header exhHeader;
			const auto exhHeaderSpan = std::span(reinterpret_cast<char*>(&exhHeader), sizeof exhHeader);
			memcpy(exhHeader.Signature, Sqex::Excel::Exh::Header::Signature_Value, 4);
			exhHeader.Version = Sqex::Excel::Exh::Header::Version_Value;
			exhHeader.FixedDataSize = static_cast<uint16_t>(FixedDataSize);
			exhHeader.ColumnCount = static_cast<uint16_t>(Columns.size());
			exhHeader.PageCount = static_cast<uint16_t>(pages.size());
			exhHeader.LanguageCount = static_cast<uint16_t>(Languages.size());
			exhHeader.Unknown2 = Unknown2;
			exhHeader.Depth = Sqex::Excel::Exh::Level2;
			exhHeader.RowCount = Data.empty() ? 0U : Data.rbegin()->first - Data.begin()->first + 1;  // some ids skip over

			const auto columnSpan = std::span(reinterpret_cast<const char*>(&Columns[0]), std::span(Columns).size_bytes());
			std::vector<Sqex::Excel::Exh::Pagination> paginations;
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
};

int main() {
	system("chcp 65001");
	const Sqex::Sqpack::Reader reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0a0000.win32.index)");
	const Sqex::Sqpack::Reader readerK(LR"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\sqpack\ffxiv\0a0000.win32.index)");
	const auto exl = ExlReader(Sqex::Sqpack::EntryRawStream(reader.GetEntryProvider("exd/root.exl")));
	for (const auto& x : exl | std::views::keys) {
		// if (x != "Action") continue;
		// if (x != "Lobby") continue;
		// if (x != "Item") continue;
		// if (x != "Lobby" && x != "Item") continue;
		// if (x.find('/') != std::string::npos) continue;

		const auto exhProvider = reader.GetEntryProvider(std::format("exd/{}.exh", x));
		const auto exhStream = Sqex::Sqpack::EntryRawStream(exhProvider);
		const auto exh = ExhReader(x, exhStream);
		if (exh.Header.Depth != Sqex::Excel::Exh::Depth::Level2)
			continue;

		// std::cout << std::format("{:04x}\t{:x}\t{:x}\t{:x}\t{}\n", exh.Header.Unknown2.Value(), exh.Header.PageCount.Value(), exh.Header.RowCount.Value(), exl[x], x); continue;

		if (std::ranges::find(exh.Languages, Sqex::Language::Unspecified) != exh.Languages.end())
			continue;

		std::cout << std::format("Processing {} (id {}, unk2 0x{:04x})...\n", x, exl[x], exh.Header.Unknown2.Value());

		Depth2ExhExdCreator creator(x, *exh.Columns, exh.Header.Unknown2);
		for (const auto language : exh.Languages)
			creator.AddLanguage(language);

		for (const auto& page : exh.Pages) {
			const auto englishPathSpec = exh.GetDataPathSpec(page, Sqex::Language::English);
			const auto germanPathSpec = exh.GetDataPathSpec(page, Sqex::Language::German);
			const auto japanesePathSpec = exh.GetDataPathSpec(page, Sqex::Language::Japanese);
			const auto koreanPathSpec = exh.GetDataPathSpec(page, Sqex::Language::Korean);
			try {
				const auto englishExd = std::make_unique<ExdReader>(exh, std::make_shared<Sqex::BufferedRandomAccessStream>(std::make_shared<Sqex::Sqpack::EntryRawStream>(reader.GetEntryProvider(englishPathSpec))));
				const auto germanExd = std::make_unique<ExdReader>(exh, std::make_shared<Sqex::BufferedRandomAccessStream>(std::make_shared<Sqex::Sqpack::EntryRawStream>(reader.GetEntryProvider(germanPathSpec))));
				const auto japaneseExd = std::make_unique<ExdReader>(exh, std::make_shared<Sqex::BufferedRandomAccessStream>(std::make_shared<Sqex::Sqpack::EntryRawStream>(reader.GetEntryProvider(japanesePathSpec))));
				std::unique_ptr<ExdReader> koreanExd;
				try {
					koreanExd = std::make_unique<ExdReader>(exh, std::make_shared<Sqex::BufferedRandomAccessStream>(std::make_shared<Sqex::Sqpack::EntryRawStream>(readerK.GetEntryProvider(koreanPathSpec))));
				} catch (const Sqex::Sqpack::Reader::EntryNotFoundError&) {
					std::cout << std::format("Entry {} not found\n", koreanPathSpec);
				}
				for (const auto i : englishExd->GetIds()) {
					auto englishRow = englishExd->ReadDepth2(i);
					auto germanRow = germanExd->ReadDepth2(i);
					auto japaneseRow = japaneseExd->ReadDepth2(i);
					std::vector<ExdColumn> koreanRow;
					if (koreanExd) {
						try {
							koreanRow = koreanExd->ReadDepth2(i);
						} catch(const std::out_of_range&) {
							// pass
						}
					}

					for (size_t i = 0; i < japaneseRow.size(); ++i) {
						if (japaneseRow[i].Type != Sqex::Excel::Exh::String)
							continue;
						if (germanRow[i].String == englishRow[i].String && japaneseRow[i].String == englishRow[i].String)
							continue;

						if (!koreanRow.empty() && !koreanRow[i].String.empty())
							japaneseRow[i].String = koreanRow[i].String;
						else
							japaneseRow[i].String = englishRow[i].String;
					}

					creator.SetRow(i, Sqex::Language::Japanese, std::move(japaneseRow));
				}
			} catch (const Sqex::Sqpack::Reader::EntryNotFoundError&) {
				std::cout << std::format("Entry {} not found\n", englishPathSpec);
			}
		}

		if (creator.Data.empty())
			continue;

		for (const auto& [path, contents] : creator.Compile()) {
			const auto targetPath = std::filesystem::path(LR"(Z:\scratch\exd)") / path.Original;
			create_directories(targetPath.parent_path());

			Utils::Win32::File::Create(targetPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0)
				.Write(0, std::span(contents));
		}
	}

	return 0;
}
