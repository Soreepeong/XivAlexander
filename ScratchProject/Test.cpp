#include "pch.h"

#include <string>

#include "XivAlexanderCommon/Sqex_EscapedString.h"
#include "XivAlexanderCommon/Sqex_Excel_Reader.h"
#include "XivAlexanderCommon/Sqex_Sqpack_Reader.h"

int main() {
	std::string nl = "\x02\x10\x01\x03";
	system("chcp 65001");
	Sqex::Sqpack::Reader r(LR"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\sqpack\ffxiv\0a0000.win32.index)");
	auto exlr = std::make_shared<Sqex::Excel::ExlReader>(*r["exd/root.exl"]);
	for (const auto& name : *exlr | std::views::keys) {
		auto exhr = std::make_shared<Sqex::Excel::ExhReader>(name, *r[std::format("exd/{}.exh", name)]);
		if (exhr->Header.Depth != Sqex::Excel::Exh::Depth::Level2)
			continue;

		bool useNewlineCharacter = false;
		bool useNewlineCode = false;
		for (const auto language : exhr->Languages) {
			for (const auto& page : exhr->Pages) {
				try {
					auto exdr = std::make_shared<Sqex::Excel::ExdReader>(*exhr, r[exhr->GetDataPathSpec(page, language)]);
					for (const auto id : exdr->GetIds()) {
						const auto row = exdr->ReadDepth2(id);
						for (const auto& col : row) {
							if (col.Type != Sqex::Excel::Exh::String)
								continue;

							Sqex::EscapedString es = std::string(col.String);
							std::string fs = es.FilteredString();
							if (fs.find('\n') != std::string::npos || fs.find('\r') != std::string::npos)
								useNewlineCharacter = true;
							for (const auto& e : es.EscapedItems())
								if (e.starts_with("\x02\x10")) {
									useNewlineCode = true;
									if (e != nl)
										__debugbreak();
								}
						}
					}
				} catch (const std::out_of_range& o) {
					// pass
				}
			}
		}
		if (useNewlineCharacter || useNewlineCode)
			std::cout << std::format("{}\t{}\t{}\n", useNewlineCharacter, useNewlineCode, name);
	}
	return 0;
}
