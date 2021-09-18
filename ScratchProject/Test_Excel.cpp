#include "pch.h"

#include <set>
#include <XivAlexanderCommon/Sqex_Excel.h>
#include <XivAlexanderCommon/Sqex_Excel_Generator.h>
#include <XivAlexanderCommon/Sqex_Excel_Reader.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>

int main() {
	system("chcp 65001");
	const Sqex::Sqpack::Reader reader(LR"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\sqpack\ffxiv\0a0000.win32.index)");
	const Sqex::Sqpack::Reader readerG(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0a0000.win32.index)");
	const auto exl = Sqex::Excel::ExlReader(Sqex::Sqpack::EntryRawStream(reader.GetEntryProvider("exd/root.exl")));
	for (const auto& x : exl | std::views::keys) {
		if (x != "Addon") continue;
		// if (x.find('/') != std::string::npos) continue;

		const auto exhProvider = reader.GetEntryProvider(std::format("exd/{}.exh", x));
		const auto exhStream = Sqex::Sqpack::EntryRawStream(exhProvider);
		const auto exh = Sqex::Excel::ExhReader(x, exhStream);
		if (exh.Header.Depth != Sqex::Excel::Exh::Depth::Level2)
			continue;

		if (std::ranges::find(exh.Languages, Sqex::Language::Unspecified) != exh.Languages.end())
			continue;

		std::cout << std::format("Processing {} (id {}, unk2 0x{:04x})...\n", x, exl[x], exh.Header.SomeSortOfBufferSize.Value());

		Sqex::Excel::Depth2ExhExdCreator creator(x, *exh.Columns, exh.Header.SomeSortOfBufferSize);
		creator.AddLanguage(Sqex::Language::English);
		creator.AddLanguage(Sqex::Language::Japanese);
		creator.AddLanguage(Sqex::Language::Korean);

		for (const auto& page : exh.Pages) {
			const auto koreanExd = std::make_unique<Sqex::Excel::ExdReader>(exh, std::make_shared<Sqex::BufferedRandomAccessStream>(reader[exh.GetDataPathSpec(page, Sqex::Language::Korean)]));
			const auto englishExd = std::make_unique<Sqex::Excel::ExdReader>(exh, std::make_shared<Sqex::BufferedRandomAccessStream>(readerG[exh.GetDataPathSpec(page, Sqex::Language::English)]));
			for (const auto i : koreanExd->GetIds()) {
				auto koreanRow = koreanExd->ReadDepth2(i);
				creator.SetRow(i, Sqex::Language::Korean, koreanRow);
				creator.SetRow(i, Sqex::Language::Japanese, koreanRow);
				try {
					auto englishRow = englishExd->ReadDepth2(i);
					for (size_t i = 0; i < koreanRow.size() && i < englishRow.size(); ++i) {
						auto& col = koreanRow[i];
						if (col.Type == Sqex::Excel::Exh::String && !col.String.empty()) {
							col.String = englishRow[i].String;
						}
					}
				} catch (const std::out_of_range&) {
				}
				creator.SetRow(i, Sqex::Language::English, koreanRow);
			}
		}

		if (creator.Data.empty())
			continue;

		for (const auto& res : creator.Compile()) {
			const auto& path = res.first;
			const auto& contents = res.second;
			const auto targetPath = std::filesystem::path(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\ffxiv\0a0000)") / path.Original;
			create_directories(targetPath.parent_path());

			Utils::Win32::File::Create(targetPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0)
				.Write(0, std::span(contents));
		}
	}

	return 0;
}
