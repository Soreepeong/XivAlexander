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
		ExdColumn column{ .Type = columnDefinition.Type };
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

	std::pair<Sqex::Excel::Exd::RowHeader, std::vector<char>> ReadRowRaw(uint32_t index) const {
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
	const uint32_t FixedDataSize;
	std::map<uint32_t, std::map<Sqex::Language, std::vector<ExdColumn>>> Data;

	Depth2ExhExdCreator(std::string name, std::vector<Sqex::Excel::Exh::Column> columns)
		: Name(std::move(name))
		, Columns(std::move(columns))
		, FixedDataSize([&columns = Columns]() -> uint32_t {
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
			return size;
		}()) {
	}

	void AddRow(uint32_t id, Sqex::Language language, std::vector<ExdColumn> row) {
		Data[id][language] = std::move(row);
	}

	std::map<Sqex::Sqpack::EntryPathSpec, std::vector<char>> Compile() {
		std::map<Sqex::Sqpack::EntryPathSpec, std::vector<char>> result;

		std::set<Sqex::Language> languages;
		for (const auto& v1 : Data | std::views::values)
			for (const auto& v2 : v1 | std::views::keys)
				languages.insert(v2);

		Sqex::Excel::Exh::Header exhHeader;
		const auto exhHeaderSpan = std::span(reinterpret_cast<char*>(&exhHeader), sizeof exhHeader);
		memcpy(exhHeader.Signature, Sqex::Excel::Exh::Header::Signature_Value, 4);
		exhHeader.Version = Sqex::Excel::Exh::Header::Version_Value;
		exhHeader.FixedDataSize = FixedDataSize;
		exhHeader.ColumnCount = static_cast<uint16_t>(Columns.size());
		exhHeader.PageCount = 1;
		exhHeader.LanguageCount = static_cast<uint16_t>(languages.size());
		exhHeader.Unknown2 = 0;  // TODO: ?
		exhHeader.Depth = Sqex::Excel::Exh::Level2;

		// TODO: which of the following is right?
		exhHeader.RowCount = static_cast<uint32_t>(Data.size());
		exhHeader.RowCount = Data.empty() ? 0U : Data.rbegin()->first;

		Sqex::Excel::Exd::Header exdHeader;
		const auto exdHeaderSpan = std::span(reinterpret_cast<char*>(&exdHeader), sizeof exdHeader);
		memcpy(exdHeader.Signature, Sqex::Excel::Exd::Header::Signature_Value, 4);
		exdHeader.Version = Sqex::Excel::Exd::Header::Version_Value;

		for (const auto language : languages) {
			std::map<uint32_t, std::vector<char>> rows;
			size_t dataSize = 0;
			for (auto& [id, languageData] : Data) {
				std::vector<char> row(FixedDataSize);
				auto& columns = languageData.at(language);
				for (size_t i = 0; i < columns.size(); ++i) {
					auto& column = columns[i];
					const auto& columnDefinition = Columns[i];
					switch (columnDefinition.Type) {
						case Sqex::Excel::Exh::String: {
							const auto stringOffset = Sqex::BE(static_cast<uint32_t>(row.size()));
							std::copy_n(reinterpret_cast<const char*>(&stringOffset), 4, &row[columnDefinition.Offset]);
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
								row[columnDefinition.Offset] |= (1 << (static_cast<int>(column.Type) - static_cast<int>(Sqex::Excel::Exh::PackedBool0)));
							else
								row[columnDefinition.Offset] &= ~((1 << (static_cast<int>(column.Type) - static_cast<int>(Sqex::Excel::Exh::PackedBool0))));
							break;
					}
					if (column.ValidSize) {
						std::copy_n(&column.Buffer[0] , column.ValidSize, &row[columnDefinition.Offset]);
						std::reverse(&row[columnDefinition.Offset], &row[columnDefinition.Offset + column.ValidSize]);
					}
				}
				dataSize += row.size();
				rows.emplace(id, std::move(row));
			}
			exdHeader.DataSize = static_cast<uint32_t>(dataSize);

			std::vector<Sqex::Excel::Exd::RowLocator> locators;
			auto offsetAccumulator = static_cast<uint32_t>(sizeof exdHeader + rows.size() * sizeof Sqex::Excel::Exd::RowLocator);
			for (const auto& [id, row] : rows) {
				locators.emplace_back(id, offsetAccumulator);
				offsetAccumulator += static_cast<uint32_t>(row.size());
			}
			const auto locatorSpan = std::span(reinterpret_cast<char*>(&locators[0]), std::span(locators).size_bytes());
			exdHeader.IndexSize = static_cast<uint32_t>(locatorSpan.size_bytes());

			std::vector<char> exdFile(offsetAccumulator);
			auto ptr = &exdFile[0];
			ptr = std::copy_n(&exdHeaderSpan[0], exdHeaderSpan.size_bytes(), ptr);
			ptr = std::copy_n(&locatorSpan[0], locatorSpan.size_bytes(), ptr);
			for (const auto& row : rows | std::views::values)
				ptr = std::copy_n(&row[0], row.size(), ptr);

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
			result.emplace(std::format("exd/{}_0{}.exd", Name, languageCode), std::move(exdFile));
			// TODO: do something with "result"
		}
		return result;
	}
};

int main() {
	system("chcp 65001");
	const Sqex::Sqpack::Reader reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0a0000.win32.index)");
	const auto exl = ExlReader(Sqex::Sqpack::EntryRawStream(reader.GetEntryProvider("exd/root.exl")));
	for (const auto& x : exl | std::views::keys) {
		const auto exhProvider = reader.GetEntryProvider(std::format("exd/{}.exh", x));
		const auto exhStream = Sqex::Sqpack::EntryRawStream(exhProvider);
		const auto exh = ExhReader(x, exhStream);
		if (exh.Header.Depth != Sqex::Excel::Exh::Depth::Level2)
			continue;

		Depth2ExhExdCreator creator(x, *exh.Columns);

		for (const auto language : exh.Languages) {
			if (language != Sqex::Language::English && language != Sqex::Language::Japanese)
				continue;
			for (const auto& page : exh.Pages) {
				const auto pathSpec = exh.GetDataPathSpec(page, language);
				try {
					const auto entryStream = std::make_shared<Sqex::Sqpack::EntryRawStream>(reader.GetEntryProvider(pathSpec));
					const auto bufferedStream = std::make_shared<Sqex::BufferedRandomAccessStream>(entryStream);
					const auto exd = ExdReader(exh, bufferedStream);
					for (const auto i : exd.GetIds())
						creator.AddRow(i, language, exd.ReadDepth2(i));
				} catch (const Sqex::Sqpack::Reader::EntryNotFoundError&) {
					std::cout << std::format("Entry {} not found\n", pathSpec);
				}
			}
		}

		if (creator.Data.empty())
			continue;

		creator.Compile();
	}

	return 0;
}
