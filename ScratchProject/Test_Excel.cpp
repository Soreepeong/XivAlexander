#include "pch.h"

#include <set>
#include <XivAlexanderCommon/Sqex_Excel.h>
#include <XivAlexanderCommon/Sqex_Excel_Generator.h>
#include <XivAlexanderCommon/Sqex_Excel_Reader.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>

int main() {
	system("chcp 65001");
	const Sqex::Sqpack::Reader reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0a0000.win32.index)");
	const Sqex::Sqpack::Reader readerK(LR"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\sqpack\ffxiv\0a0000.win32.index)");
	const auto exl = Sqex::Excel::ExlReader(Sqex::Sqpack::EntryRawStream(reader.GetEntryProvider("exd/root.exl")));
	for (const auto& x : exl | std::views::keys) {
		// if (x != "Action") continue;
		// if (x != "Lobby") continue;
		// if (x != "Item") continue;
		// if (x != "Lobby" && x != "Item") continue;
		// if (x.find('/') != std::string::npos) continue;

		const auto exhProvider = reader.GetEntryProvider(std::format("exd/{}.exh", x));
		const auto exhStream = Sqex::Sqpack::EntryRawStream(exhProvider);
		const auto exh = Sqex::Excel::ExhReader(x, exhStream);
		if (exh.Header.Depth != Sqex::Excel::Exh::Depth::Level2)
			continue;

		// std::cout << std::format("{:04x}\t{:x}\t{:x}\t{:x}\t{}\n", exh.Header.SomeSortOfBufferSize.Value(), exh.Header.PageCount.Value(), exh.Header.RowCount.Value(), exl[x], x); continue;

		if (std::ranges::find(exh.Languages, Sqex::Language::Unspecified) != exh.Languages.end())
			continue;

		std::cout << std::format("Processing {} (id {}, unk2 0x{:04x})...\n", x, exl[x], exh.Header.SomeSortOfBufferSize.Value());

		Sqex::Excel::Depth2ExhExdCreator creator(x, *exh.Columns, exh.Header.SomeSortOfBufferSize);
		for (const auto language : exh.Languages)
			creator.AddLanguage(language);

		for (const auto& page : exh.Pages) {
			const auto englishPathSpec = exh.GetDataPathSpec(page, Sqex::Language::English);
			const auto germanPathSpec = exh.GetDataPathSpec(page, Sqex::Language::German);
			const auto japanesePathSpec = exh.GetDataPathSpec(page, Sqex::Language::Japanese);
			const auto koreanPathSpec = exh.GetDataPathSpec(page, Sqex::Language::Korean);
			try {
				const auto englishExd = std::make_unique<Sqex::Excel::ExdReader>(exh, std::make_shared<Sqex::BufferedRandomAccessStream>(std::make_shared<Sqex::Sqpack::EntryRawStream>(reader.GetEntryProvider(englishPathSpec))));
				const auto germanExd = std::make_unique<Sqex::Excel::ExdReader>(exh, std::make_shared<Sqex::BufferedRandomAccessStream>(std::make_shared<Sqex::Sqpack::EntryRawStream>(reader.GetEntryProvider(germanPathSpec))));
				const auto japaneseExd = std::make_unique<Sqex::Excel::ExdReader>(exh, std::make_shared<Sqex::BufferedRandomAccessStream>(std::make_shared<Sqex::Sqpack::EntryRawStream>(reader.GetEntryProvider(japanesePathSpec))));
				std::unique_ptr<Sqex::Excel::ExdReader> koreanExd;
				try {
					koreanExd = std::make_unique<Sqex::Excel::ExdReader>(exh, std::make_shared<Sqex::BufferedRandomAccessStream>(std::make_shared<Sqex::Sqpack::EntryRawStream>(readerK.GetEntryProvider(koreanPathSpec))));
				} catch (const std::out_of_range&) {
					std::cout << std::format("Entry {} not found\n", koreanPathSpec);
				}
				for (const auto i : englishExd->GetIds()) {
					auto englishRow = englishExd->ReadDepth2(i);
					auto germanRow = germanExd->ReadDepth2(i);
					auto japaneseRow = japaneseExd->ReadDepth2(i);
					std::vector<Sqex::Excel::ExdColumn> koreanRow;
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
			} catch (const std::out_of_range&) {
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
