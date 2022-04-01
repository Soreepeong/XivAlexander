#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <Windows.h>

#include "Sqex/ExcelReader.h"
#include "Sqex/FontCsvStream.h"
#include "Sqex/ScdReader.h"
#include "Sqex/SqpackReader.h"
#include "Sqex/SqpackEntryRawStream.h"
#include "Sqex/TextureStream.h"

void Test(const XivRes::SqpackReader& reader) {
	auto start = std::chrono::steady_clock::now();
	uint64_t accumulatedCompressedSize = 0;
	uint64_t accumulatedDecompressedSize = 0;
	for (const auto& info : reader.EntryInfo) {
		auto provider = reader.GetPackedFileStream(info);
		accumulatedCompressedSize += provider->StreamSize();

		auto stream = provider->GetUnpackedStream();
		auto data = stream.ReadStreamIntoVector<char>();
		accumulatedDecompressedSize += data.size();

		std::cout << std::format("{:>8.02f}M -> {:>8.02f}M\r", accumulatedCompressedSize / 1048576., accumulatedDecompressedSize / 1048576.);
	}
	auto end = std::chrono::steady_clock::now();
	std::cout << std::format("\nTook {}us.\n", std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

int main()
{
	system("chcp 65001");

	XivRes::GameReader gameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
	Test(gameReader.GetSqpackReader(0x0c0000));
	
	const auto axis14 = XivRes::FontCsvStream(*gameReader.GetFileStream("common/font/AXIS_14.fdt"));
	std::cout << std::format("AXIS_14: {}pt\n", axis14.Points());

	const auto font1 = XivRes::TextureStream(gameReader.GetFileStream("common/font/font1.tex"));
	std::cout << std::format("font1: {} x {} (0x{:0>4x})\n", font1.GetWidth(), font1.GetHeight(), static_cast<int>(font1.GetType()));

	const auto itemExcel = gameReader.GetExcelReader("Item");
	for (uint32_t i = 13498; i <= 13512; i++)
		std::cout << std::format("{}/{}/{}: {}\n", i, 0, 9, itemExcel[i][0][9].String.Parsed());

	XivRes::ScdReader(gameReader.GetFileStream("music/ffxiv/bgm_boss_07.scd")).GetSoundEntry(0).GetOggFile();
	XivRes::ScdReader(gameReader.GetFileStream("sound/zingle/Zingle_Sleep.scd")).GetSoundEntry(0).GetMsAdpcmWavFile();
}
