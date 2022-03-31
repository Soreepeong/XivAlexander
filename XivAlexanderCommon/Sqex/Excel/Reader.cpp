#include <ranges>

#include "XivAlexanderCommon/Sqex/Excel/Reader.h"

Sqex::Excel::ExlReader::ExlReader(const RandomAccessStream& stream) {
	std::string data(static_cast<size_t>(stream.StreamSize()), '\0');
	stream.ReadStream(0, std::span(data));
	std::istringstream in(std::move(data));

	std::string line;
	for (size_t i = 0; std::getline(in, line); ++i) {
		line = StringTrim(line);
		if (line.empty())
			continue;  // skip empty lines
		auto split = Utils::StringSplit<std::string>(line, ",", 1);
		if (split.size() < 2)
			throw CorruptDataException("Could not find delimiter character ','");

		char* end;
		const auto id = std::strtol(split[1].data(), &end, 10);
		if (end != &split[1].back() + 1)
			throw CorruptDataException("Bad numeric specification");
		if (!i) {
			if (split[0] != "EXLT")
				throw CorruptDataException("Bad header");
			m_version = id;
		} else {
			if (id != -1)
				m_idToNameMap.emplace(id, split[0]);
			m_nameToIdMap.emplace(std::move(split[0]), id);
		}
	}
}

Sqex::Excel::ExhReader::ExhReader(std::string name, const RandomAccessStream& stream, bool strict)
	: Name(std::move(name))
	, Header(stream.ReadStream<Exh::Header>(0))
	, Columns(std::make_shared<std::vector<Exh::Column>>(std::move(stream.ReadStreamIntoVector<Exh::Column>(
		sizeof Header, Header.ColumnCount))))
	, Pages(stream.ReadStreamIntoVector<Exh::Pagination>(
		sizeof Header + std::span(*Columns).size_bytes(),
		Header.PageCount))
	, Languages(stream.ReadStreamIntoVector<Language>(
		sizeof Header + std::span(*Columns).size_bytes() + std::span(Pages).size_bytes(),
		Header.LanguageCount)) {
	if (strict) {
		const auto dataLength = stream.StreamSize();
		const auto expectedLength = sizeof Header + std::span(*Columns).size_bytes() + std::span(Pages).size_bytes() + std::span(Languages).size_bytes();
		if (dataLength > expectedLength)
			throw CorruptDataException(std::format("Extra {} byte(s) found", dataLength - expectedLength));
		if (Header.Version != Exh::Header::Version_Value)
			throw CorruptDataException(std::format("Unsupported version {}", Header.Version.Value()));
	}
}

Sqex::Sqpack::EntryPathSpec Sqex::Excel::ExhReader::GetDataPathSpec(const Exh::Pagination& page, Language language) const {
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
	return std::format("exd/{}_{}{}.exd", Name, *page.StartId, languageCode);
}

Sqex::Excel::ExdReader::ExdReader(const ExhReader& exh, std::shared_ptr<const RandomAccessStream> stream, bool strict): m_stream(std::move(stream))
	, m_fixedDataSize(exh.Header.FixedDataSize)
	, m_depth(exh.Header.Depth)
	, Header(m_stream->ReadStream<Exd::Header>(0))
	, ColumnDefinitions(exh.Columns) {
	const auto count = Header.IndexSize / sizeof Exd::RowLocator;
	m_rowLocators.reserve(count);
	for (const auto& locator : m_stream->ReadStreamIntoVector<Exd::RowLocator>(sizeof Header, count)) {
		m_rowLocators.emplace_back(std::make_pair(*locator.RowId, *locator.Offset));
	}
	std::ranges::sort(m_rowLocators);
}

Sqex::Excel::ExdColumn Sqex::Excel::ExdReader::TranslateColumn(const Exh::Column& columnDefinition, std::span<const char> fixedData, std::span<const char> fullData) const {
	ExdColumn column{ .Type = columnDefinition.Type };
	switch (column.Type) {
		case Exh::String:
		{
			BE<uint32_t> stringOffset;
			std::copy_n(&fixedData[columnDefinition.Offset], 4, reinterpret_cast<char*>(&stringOffset));
			column.String.SetEscaped(&fullData[m_fixedDataSize + stringOffset]);
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
			column.boolean = fixedData[columnDefinition.Offset] & (1 << (static_cast<int>(column.Type) - static_cast<int>(Exh::PackedBool0)));
			break;

		default:
			throw CorruptDataException(std::format("Invald column type {}", static_cast<uint32_t>(column.Type)));
	}
	if (column.ValidSize) {
		std::copy_n(&fixedData[columnDefinition.Offset], column.ValidSize, &column.Buffer[0]);
		std::reverse(&column.Buffer[0], &column.Buffer[column.ValidSize]);
	}
	return column;
}

std::pair<Sqex::Excel::Exd::RowHeader, std::vector<char>> Sqex::Excel::ExdReader::ReadRowRaw(uint32_t index) const {
	const auto it = std::ranges::lower_bound(m_rowLocators, std::make_pair(index, 0U), [](const auto& l, const auto& r) {
		return l.first < r.first;
	});
	if (it == m_rowLocators.end() || it->first != index)
		throw std::out_of_range("index out of range");

	const auto rowHeader = m_stream->ReadStream<Exd::RowHeader>(it->second);
	return std::make_pair(rowHeader, m_stream->ReadStreamIntoVector<char>(it->second + sizeof rowHeader, rowHeader.DataSize));
}

std::vector<Sqex::Excel::ExdColumn> Sqex::Excel::ExdReader::ReadDepth2(uint32_t index) const {
	if (m_depth != Exh::Level2)
		throw std::invalid_argument("Not a 2nd depth sheet");

	std::vector<ExdColumn> result;
	const auto [rowHeader, buffer] = ReadRowRaw(index);
	const auto fixedData = std::span(buffer).subspan(0, m_fixedDataSize);

	if (rowHeader.SubRowCount != 1)
		throw CorruptDataException("SubRowCount > 1 on 2nd depth sheet");

	for (const auto& columnDefinition : *ColumnDefinitions)
		result.emplace_back(TranslateColumn(columnDefinition, fixedData, std::span(buffer)));

	return result;
}

std::vector<std::vector<Sqex::Excel::ExdColumn>> Sqex::Excel::ExdReader::ReadDepth3(uint32_t index) const {
	if (m_depth != Exh::Level3)
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

std::vector<uint32_t> Sqex::Excel::ExdReader::GetIds() const {
	std::vector<uint32_t> ids;
	for (const auto& id : m_rowLocators | std::views::keys)
		ids.emplace_back(id);
	return ids;
}
