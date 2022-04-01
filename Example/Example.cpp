#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <Windows.h>

#include "Sqex/GameReader.h"
#include "Sqex/FdtStream.h"
#include "Sqex/ScdReader.h"
#include "Sqex/PackedFileUnpackingStream.h"
#include "Sqex/SqpackGenerator.h"
#include "Sqex/TextureStream.h"

void TestDecompressAll(const XivRes::SqpackReader& reader) {
	const auto start = std::chrono::steady_clock::now();
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
	std::cout << std::format("\nTook {}us.\n", std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
}

int main()
{
	system("chcp 65001");

	XivRes::GameReader gameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
	TestDecompressAll(gameReader.GetSqpackReader(0x000000));
	TestDecompressAll(gameReader.GetSqpackReader(0x0a0000));
	// TestDecompressAll(gameReader.GetSqpackReader(0x0c0000));

	std::cout << std::format("AXIS_96: {}pt\n", XivRes::FdtStream(*gameReader.GetFileStream("common/font/AXIS_96.fdt")).Points());
	std::cout << std::format("AXIS_12: {}pt\n", XivRes::FdtStream(*gameReader.GetFileStream("common/font/AXIS_12.fdt")).Points());
	std::cout << std::format("AXIS_14: {}pt\n", XivRes::FdtStream(*gameReader.GetFileStream("common/font/AXIS_14.fdt")).Points());
	std::cout << std::format("AXIS_18: {}pt\n", XivRes::FdtStream(*gameReader.GetFileStream("common/font/AXIS_18.fdt")).Points());
	std::cout << std::format("AXIS_36: {}pt\n", XivRes::FdtStream(*gameReader.GetFileStream("common/font/AXIS_36.fdt")).Points());

	for (size_t i = 1; i <= 7; i++) {
		const auto atlas = XivRes::TextureStream(gameReader.GetFileStream(std::format("common/font/font{}.tex", i)));
		std::cout << std::format("font{}: {} x {} (0x{:0>4x})\n", i, atlas.GetWidth(), atlas.GetHeight(), static_cast<int>(atlas.GetType()));
	}

	//const auto exl = XivRes::ExlReader(*gameReader.GetFileStream("exd/root.exl"));
	//for (const auto& l : exl) {
	//	std::cout << std::format("{:<56} ... ", l.first);
	//	const auto start = std::chrono::steady_clock::now();
	//	auto excel = gameReader.GetExcelReader(l.first);
	//	if (excel.Exh().Languages()[0] != XivRes::Language::Unspecified)
	//		excel.WithLanguage(XivRes::Language::English);
	//	for (size_t i = 0, i_ = excel.Exh().Pages().size(); i < i_; i++) {
	//		for (const auto& rowSet : excel.Page(i)) {
	//			for (const auto& row : rowSet) {
	//				for (const auto& col : row) {
	//					// do nothing
	//					GetTickCount();
	//				}
	//			}
	//		}
	//	}
	//	std::cout << std::format("{:>9}us\n", std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
	//}

	const auto itemExcel = gameReader.GetExcelReader("Item").WithLanguage(XivRes::Language::English);
	for (uint32_t i = 13498; i <= 13512; i++)
		std::cout << std::format("{}/{}/{}: {}\n", i, 0, 9, itemExcel[i][0][9].String.Parsed());

	XivRes::ScdReader(gameReader.GetFileStream("music/ffxiv/bgm_boss_07.scd")).GetSoundEntry(0).GetOggFile();
	XivRes::ScdReader(gameReader.GetFileStream("sound/zingle/Zingle_Sleep.scd")).GetSoundEntry(0).GetMsAdpcmWavFile();
}
