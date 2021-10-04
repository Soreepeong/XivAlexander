#include "pch.h"

#include <set>
#include <XivAlexanderCommon/Sqex_EscapedString.h>
#include <XivAlexanderCommon/Sqex_Excel.h>
#include <XivAlexanderCommon/Sqex_Excel_Reader.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>

const auto QuoteStartU8 = u8"¡°";
const auto QuoteEndU8 = u8"¡±";
const auto QuoteStart = reinterpret_cast<const char*>(QuoteStartU8);
const auto QuoteEnd = reinterpret_cast<const char*>(QuoteEndU8);

int main() {
	system("chcp 65001");
	const Sqex::Sqpack::Reader reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0a0000.win32.index)");
	const auto exl = Sqex::Excel::ExlReader(Sqex::Sqpack::EntryRawStream(reader.GetEntryProvider("exd/root.exl")));
	for (const auto& x : exl | std::views::keys) {
		if (x.find('/') == std::string::npos)
			continue;

		const auto exhProvider = reader.GetEntryProvider(std::format("exd/{}.exh", x));
		const auto exhStream = Sqex::Sqpack::EntryRawStream(exhProvider);
		const auto exh = Sqex::Excel::ExhReader(x, exhStream);
		if (exh.Header.Depth != Sqex::Excel::Exh::Depth::Level2)
			continue;

		if (std::ranges::find(exh.Languages, Sqex::Language::Unspecified) != exh.Languages.end())
			continue;
		
		// quest/019/HeaVnz905_01970: incantation is "Y'shtola"(88) and "Aloths'y"(89)

		for (const auto& page : exh.Pages) {
			const auto exd = std::make_unique<Sqex::Excel::ExdReader>(exh, std::make_shared<Sqex::BufferedRandomAccessStream>(reader[exh.GetDataPathSpec(page, Sqex::Language::English)]));
			std::set<std::string> candidates;
			std::set<std::string> says;
			for (const auto i : exd->GetIds()) {
				auto row = exd->ReadDepth2(i);

				const auto es = Sqex::EscapedString(row[1].String);
				const auto s = es.FilteredString();
				if (s.starts_with("With the chat mode")) {
					if (const auto i1 = s.find(QuoteStart), i2 = s.find(QuoteEnd, i1); i1 != std::string::npos && i2 != std::string::npos && i1 < i2) {
						auto say = s.substr(i1 + 3, i2 - i1 - 3);
						candidates.insert(say);
						std::cout << std::format("[{:03}] Found {}: {}\n", i, row[0].String, s);
					} else {
						// std::cout << std::format("[{:03}] Unknown {}: {}\n", i, row[0].String, s);
					}
				} else if (candidates.find(s) != candidates.end()) {
					says.insert(s);
					says.insert(s + "!");
					std::cout << std::format("{}:{:} = {}\n", x, i, row[0].String, row[1].String);
				}
			}
			std::set<std::string> missing;
			std::set_difference(candidates.begin(), candidates.end(), says.begin(), says.end(), std::inserter(missing, missing.end()));
			for (const auto& s : missing) {
				std::cout << "MISSING " << s << std::endl;
			}
		}
	}

	return 0;
}
