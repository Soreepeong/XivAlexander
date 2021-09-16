#include "pch.h"

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

class ExdReader {
public:
	const Sqex::Excel::Exd::Header Header;
	const std::vector<Sqex::Excel::Exd::RowLocator> RowLocators;

	ExdReader(const ExhReader& exh, const Sqex::RandomAccessStream& stream, bool strict = false)
		: Header(stream.ReadStream<Sqex::Excel::Exd::Header>(0))
		, RowLocators(stream.ReadStreamIntoVector<Sqex::Excel::Exd::RowLocator>(sizeof Header, Header.IndexSize / sizeof Sqex::Excel::Exd::RowLocator)) {
		
	}
};

class ExhReader {
public:
	const std::string Name;
	const Sqex::Excel::Exh::Header Header;
	const std::vector<Sqex::Excel::Exh::Column> Columns;
	const std::vector<Sqex::Excel::Exh::Pagination> Pages;
	const std::vector<Sqex::Language> Languages;
	
	ExhReader(std::string name, const Sqex::RandomAccessStream& stream, bool strict = false)
		: Name(std::move(name))
		, Header(stream.ReadStream<Sqex::Excel::Exh::Header>(0))
		, Columns(stream.ReadStreamIntoVector<Sqex::Excel::Exh::Column>(
			sizeof Header, Header.ColumnCount,
			1024))
		, Pages(stream.ReadStreamIntoVector<Sqex::Excel::Exh::Pagination>(
			sizeof Header + std::span(Columns).size_bytes(),
			Header.PageCount, 1024))
		, Languages(stream.ReadStreamIntoVector<Sqex::Language>(
			sizeof Header + std::span(Columns).size_bytes() + std::span(Pages).size_bytes(),
			Header.LanguageCount, 1024)) {
		if (strict) {
			const auto dataLength = stream.StreamSize();
			const auto expectedLength = sizeof Header + std::span(Columns).size_bytes() + std::span(Pages).size_bytes() + std::span(Languages).size_bytes();
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
};

int main() {
	const Sqex::Sqpack::Reader reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0a0000.win32.index)");
	const auto exl = ExlReader(Sqex::Sqpack::EntryRawStream(reader.GetEntryProvider("exd/root.exl")));
	const auto exh = ExhReader(exl[10], Sqex::Sqpack::EntryRawStream(reader.GetEntryProvider(std::format("exd/{}.exh", exl[10]))));

	std::cout << std::format("Signature: {}\n", std::string(exh.Header.Signature, 4));
	std::cout << std::format("Unknown1: {}\n", exh.Header.Unknown1.Value());
	std::cout << std::format("DataOffset: {}\n", exh.Header.DataOffset.Value());
	std::cout << std::format("ColumnCount: {}\n", exh.Header.ColumnCount.Value());
	std::cout << std::format("PageCount: {}\n", exh.Header.PageCount.Value());
	std::cout << std::format("LanguageCount: {}\n", exh.Header.LanguageCount.Value());
	std::cout << std::format("Unknown2: {}\n", exh.Header.Unknown2.Value());
	std::cout << std::format("Unknown3: {}\n", exh.Header.Unknown3.Value());
	std::cout << std::format("Variant: {}\n", static_cast<uint8_t>(exh.Header.Variant.Value()));
	std::cout << std::format("Unknown4: {}\n", exh.Header.Unknown4.Value());
	std::cout << std::format("RowCount: {}\n", exh.Header.RowCount.Value());
	std::cout << std::format("Unknown5: {}\n", exh.Header.Unknown5.Value());
	std::cout << std::format("Unknown6: {}\n", exh.Header.Unknown6.Value());

	for (const auto& column : exh.Columns)
		std::cout << std::format("Column: Type {}, Offset {}\n", static_cast<uint16_t>(column.Type.Value()), column.Offset.Value());
	for (const auto& page : exh.Pages)
		std::cout << std::format("Page: StartId {}, RowCount {}\n", page.StartId.Value(), page.RowCount.Value());
	for (const auto& lang : exh.Languages)
		std::cout << std::format("Language: {}\n", static_cast<uint16_t>(lang));
	for (const auto& page : exh.Pages)
		for (const auto& lang : exh.Languages)
			std::cout << std::format("Example Entry: {}\n", exh.GetDataPathSpec(page, lang));

	const auto exd = ExdReader(exh, Sqex::Sqpack::EntryRawStream(reader.GetEntryProvider(exh.GetDataPathSpec(exh.Pages[0], Sqex::Language::English))));
	return 0;
}
