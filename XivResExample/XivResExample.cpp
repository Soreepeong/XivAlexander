#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <Windows.h>

#include "XivRes/GameReader.h"
#include "XivRes/FdtStream.h"
#include "XivRes/ScdReader.h"
#include "XivRes/PackedFileUnpackingStream.h"
#include "XivRes/SqpackGenerator.h"
#include "XivRes/TextureStream.h"

void TestDecompressAll(const XivRes::SqpackReader& reader) {
	const auto start = std::chrono::steady_clock::now();

	uint64_t accumulatedCompressedSize = 0;
	uint64_t accumulatedDecompressedSize = 0;
	int i = 0;

	std::vector<uint8_t> buf;
	for (const auto& info : reader.EntryInfo) {
		auto provider = reader.GetPackedFileStream(info);
		accumulatedCompressedSize += provider->StreamSize();

		auto stream = provider->GetUnpackedStream();
		buf.resize(static_cast<size_t>(stream.StreamSize()));
		stream.ReadStream(0, &buf[0], buf.size());
		accumulatedDecompressedSize += buf.size();

		if (++i == 128) {
			std::cout << std::format("\r{:06x}: {:>8.02f}M -> {:>8.02f}M: Took {}us.", reader.PackId(), accumulatedCompressedSize / 1048576., accumulatedDecompressedSize / 1048576., std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
			i = 0;
		}
	}
	std::cout << std::format("\r{:06x}: {:>8.02f}M -> {:>8.02f}M: Took {}us.\n", reader.PackId(), accumulatedCompressedSize / 1048576., accumulatedDecompressedSize / 1048576., std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
}

int main()
{
	system("chcp 65001");

	XivRes::GameReader gameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");

	TestDecompressAll(gameReader.GetSqpackReader(0x000000));
	TestDecompressAll(gameReader.GetSqpackReader(0x040000));
	TestDecompressAll(gameReader.GetSqpackReader(0x0a, 0x00, 0x00));
	TestDecompressAll(gameReader.GetSqpackReader(0x0c, 0x04, 0x00));

	std::cout << std::format("AXIS_96: {}pt\n", XivRes::FdtStream(*gameReader.GetFileStream("common/font/AXIS_96.fdt")).Points());
	std::cout << std::format("AXIS_12: {}pt\n", XivRes::FdtStream(*gameReader.GetFileStream("common/font/AXIS_12.fdt")).Points());
	std::cout << std::format("AXIS_14: {}pt\n", XivRes::FdtStream(*gameReader.GetFileStream("common/font/AXIS_14.fdt")).Points());
	std::cout << std::format("AXIS_18: {}pt\n", XivRes::FdtStream(*gameReader.GetFileStream("common/font/AXIS_18.fdt")).Points());
	std::cout << std::format("AXIS_36: {}pt\n", XivRes::FdtStream(*gameReader.GetFileStream("common/font/AXIS_36.fdt")).Points());

	for (size_t i = 1; i <= 7; i++) {
		const auto atlas = XivRes::TextureStream(gameReader.GetFileStream(std::format("common/font/font{}.tex", i)));
		std::cout << std::format("font{}: {} x {} (0x{:0>4x})\n", i, atlas.GetWidth(), atlas.GetHeight(), static_cast<int>(atlas.GetType()));
	}

	{
		const auto start = std::chrono::steady_clock::now();
		const auto exl = XivRes::ExlReader(*gameReader.GetFileStream("exd/root.exl"));
		size_t nonZeroFloats = 0, nonZeroInts = 0, nonZeroBools = 0, nonEmptyStrings = 0;
		for (const auto& l : exl) {
			auto excel = gameReader.GetExcelReader(l.first);
			if (excel.Exh().Languages()[0] != XivRes::Language::Unspecified)
				excel.WithLanguage(XivRes::Language::English);
			for (size_t i = 0, i_ = excel.Exh().Pages().size(); i < i_; i++) {
				for (const auto& rowSet : excel.Page(i)) {
					for (const auto& row : rowSet) {
						for (const auto& col : row) {
							switch (col.Type) {
								case XivRes::ExcelCellType::Float32:
									nonZeroFloats += col.float32 == 0 ? 0 : 1;
									break;
								case XivRes::ExcelCellType::Bool:
									nonZeroBools += col.boolean ? 1 : 0;
									break;
								case XivRes::ExcelCellType::String:
									nonEmptyStrings += col.String.Empty() ? 0 : 1;
									break;
								default:
									nonZeroInts += col.int64 == 0 ? 0 : 1;
									break;
							}
						}
					}
				}
			}
			std::cout << std::format("\r{:<64} {:>9}us", l.first, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
		}
		std::cout << std::endl;
	}

	const auto itemExcel = gameReader.GetExcelReader("Item").WithLanguage(XivRes::Language::English);
	for (uint32_t i = 13498; i <= 13502; i++)
		std::cout << std::format("{}/{}/{}: {}\n", i, 0, 9, itemExcel[i][0][9].String.Parsed());

	auto tmp = XivRes::ScdReader(gameReader.GetFileStream("music/ffxiv/bgm_boss_07.scd")).GetSoundEntry(0).GetOggFile<char>();
	std::ofstream("bgm_boss_07.scd", std::ios::binary).write(tmp.data(), tmp.size());
	
	tmp = XivRes::ScdReader(gameReader.GetFileStream("sound/zingle/Zingle_Sleep.scd")).GetSoundEntry(0).GetMsAdpcmWavFile<char>();
	std::ofstream("Zingle_Sleep.scd", std::ios::binary).write(tmp.data(), tmp.size());
}
