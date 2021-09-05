#include "pch.h"
#include <XivAlexanderCommon/Sqex_FontCsv_ModifiableFontCsvStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Virtual.h>

//int test_convert() {
//	const auto targetBasePath = LR"(Z:\scratch\t2)";
//	for (const auto& rootDir : {
//		std::filesystem::path(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)"),
//		// std::filesystem::path(LR"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game)"),
//		// std::filesystem::path(LR"(D:\Program Files (x86)\SNDA\FFXIV\game)"),
//		}) {
//
//		for (const auto& entry1 : std::filesystem::directory_iterator(rootDir / "sqpack")) {
//			if (!entry1.is_directory())
//				continue;
//			if (entry1.path().filename().wstring() != L"ffxiv")
//				continue;
//			for (const auto& entry2 : std::filesystem::directory_iterator(entry1)) {
//				const auto& path = entry2.path();
//				if (path.filename().wstring() != L"000000.win32.index")
//					continue;
//				if (path.extension() != ".index")
//					continue;
//
//				try {
//					std::cout << "Working on " << path << "..." << std::endl;
//					auto vpack = Sqex::Sqpack::VirtualSqPack("ffxiv", "040000");
//					{
//						const auto addResult = vpack.AddEntriesFromSqPack(path, true, true);
//						std::cout << std::format("Added {}, replaced {}, skipped {}\n",
//							addResult.Added.size(), addResult.Replaced.size(), addResult.SkippedExisting.size());
//					}
//
//					
//					if (const auto replBase = std::filesystem::path(path).replace_extension(""); 
//						is_directory(replBase)) {
//						for (const auto& entry3 : std::filesystem::recursive_directory_iterator(replBase)) {
//							const auto& currPath = entry3.path();
//							if (is_directory(currPath))
//								continue;
//
//							const auto p = vpack.AddEntryFromFile(proximate(currPath, replBase), currPath).AnyItem();
//							if (!p)
//								continue;
//							std::cout << std::format("Added {}\n", p->PathSpec().Original);
//						}
//					}
//
//					if (const auto ttmd = rootDir / "TexToolsMods";
//						is_directory(ttmd)) {
//						for (const auto& entry3 : std::filesystem::recursive_directory_iterator(ttmd)) {
//							if (entry3.path().filename() != "TTMPL.mpl") continue;
//							std::cout << std::format("Processing {}\n", entry3.path().filename());
//							for (const auto p : vpack.AddEntriesFromTTMP(entry3.path().parent_path()).AllEntries()) {
//								std::cout << std::format("=> Added {}\n", p->PathSpec().Original);
//							}
//						}
//					}
//
//					vpack.Freeze(false);
//
//					std::cout << "Testing..." << std::endl;
//					{
//						std::vector<char> buf(1048576);
//						// vpack.ReadData(0, 121296256, &buf[0], 123152);
//						vpack.ReadData(0, 121296256, &buf[0], 128);
//						vpack.ReadData(0, 121296256 + 128, &buf[128], 123152 - 128);
//						// vpack.ReadData(0, 121296256 + 120000, &buf[0], 123152 - 120000);
//						size_t pos = 0;
//						for (uint32_t i = 0; i < vpack.NumOfDataFiles(); ++i) {
//							pos = 0;
//							while (true) {
//								const auto w = vpack.ReadData(i, pos, &buf[0], buf.size());
//								if (!w)
//									break;
//								pos += w;
//							}
//						}
//					}
//
//					std::cout << "Writing index..." << std::endl;
//					const auto targetIndexPath = std::filesystem::path(std::format(LR"({}\{})", targetBasePath, path.filename().replace_extension(".index")));
//					{
//						const auto f1 = Utils::Win32::File::Create(
//							targetIndexPath,
//							GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0);
//						char buf[65536];
//						size_t pos = 0;
//						while (true) {
//							const auto w = vpack.ReadIndex1(pos, buf, sizeof buf);
//							if (!w)
//								break;
//							pos += f1.Write(pos, buf, w);
//						}
//					}
//					std::cout << "Writing index2..." << std::endl;
//					{
//						const auto f1 = Utils::Win32::File::Create(
//							std::format(R"({}\{})", targetBasePath, path.filename().replace_extension(".index2")),
//							GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0);
//						char buf[65536];
//						size_t pos = 0;
//						while (true) {
//							const auto w = vpack.ReadIndex2(pos, buf, sizeof buf);
//							if (!w)
//								break;
//							pos += f1.Write(pos, buf, w);
//						}
//					}
//					for (uint32_t i = 0; i < vpack.NumOfDataFiles(); ++i) {
//						std::cout << "Writing dat" << i << "..." << std::endl;
//						char buf[65536];
//						size_t pos = 0;
//						const auto f1 = Utils::Win32::File::Create(
//							std::format(R"({}\{})", targetBasePath, path.filename().replace_extension(std::format(".dat{}", i))),
//							GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0);
//						while (true) {
//							const auto w = vpack.ReadData(i, pos, buf, sizeof buf);
//							if (!w)
//								break;
//							pos += f1.Write(pos, buf, w);
//						}
//					}
//					std::cout << "OK" << std::endl;
//					return 0;
//				} catch (const std::invalid_argument& e) {
//					std::cout << e.what() << std::endl;
//					throw;
//				}
//
//			}
//		}
//	}
//
//	return 0;
//}

void verifyTex(const Sqex::Sqpack::Reader& reader, int offset = 0) {
	const auto tex1 = std::make_shared<Sqex::Sqpack::EntryRawStream>(reader.GetEntryProvider("common/font/font1.tex"));
	const auto tex1e = std::make_shared<Sqex::Sqpack::OnTheFlyTextureEntryProvider>("test", tex1);
	const auto tex2 = std::make_shared<Sqex::Sqpack::EntryRawStream>(reader.GetEntryProvider("common/font/font1.tex"));
	const auto tex3 = std::make_shared<Sqex::Sqpack::EntryRawStream>(tex1e);
	const auto tex1d = tex1->ReadStreamIntoVector<uint8_t>(0, tex1->StreamSize());
	const auto tex2d = tex2->ReadStreamIntoVector<uint8_t>(offset, tex2->StreamSize() - offset);
	const auto tex3d = tex2->ReadStreamIntoVector<uint8_t>(offset, tex3->StreamSize() - offset);
	if (memcmp(&tex1d[offset], &tex2d[0], tex2d.size()) != 0)
		throw std::runtime_error("tex1d != tex2d");
	if (memcmp(&tex1d[offset], &tex3d[0], tex3d.size()) != 0)
		throw std::runtime_error("tex1d != tex3d");
}

int verifyMdl(const Sqex::Sqpack::Reader& reader, int offset = 0) {
	const auto mdl1 = std::make_shared<Sqex::Sqpack::EntryRawStream>(reader.GetEntryProvider("chara/equipment/e0100/model/c0101e0100_met.mdl"));
	const auto mdl1e = std::make_shared<Sqex::Sqpack::OnTheFlyModelEntryProvider>("test", mdl1);
	const auto mdl2 = std::make_shared<Sqex::Sqpack::EntryRawStream>(reader.GetEntryProvider("chara/equipment/e0100/model/c0101e0100_met.mdl"));
	const auto mdl3 = std::make_shared<Sqex::Sqpack::EntryRawStream>(mdl1e);
	const auto mdl1d = mdl1->ReadStreamIntoVector<char>(0, mdl1->StreamSize());
	const auto mdl2d = mdl2->ReadStreamIntoVector<char>(offset, mdl2->StreamSize() - offset);
	const auto mdl3d = mdl3->ReadStreamIntoVector<char>(offset, mdl3->StreamSize() - offset);
	// Utils::Win32::File::Create(R"(Z:\mdl2d.mdl)", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0).Write(0, std::span(mdl2d));
	// Utils::Win32::File::Create(R"(Z:\mdl3d.mdl)", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0).Write(0, std::span(mdl3d));

	if (memcmp(&mdl1d[offset], &mdl2d[0], mdl2d.size()) != 0)
		throw std::runtime_error("mdl1d != mdl2d");
	if (memcmp(&mdl1d[offset], &mdl3d[0], mdl3d.size()) != 0)
		throw std::runtime_error("mdl1d != mdl3d");
	return 0;
}

int main() {
	const auto reader000000 = Sqex::Sqpack::Reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\000000.win32.index)", true, true);
	const auto reader040000 = Sqex::Sqpack::Reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\040000.win32.index)", true, true);
	for (int i = 0; i < 60000; i += 1527) {
		verifyTex(reader000000, i);
	}
	for (int i = 0; i < 60000; i += 1527) {
		verifyMdl(reader040000, i);
	}

	const auto fcsv = std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(Sqex::Sqpack::EntryRawStream(reader000000.GetEntryProvider("common/font/AXIS_36.fdt")), true);
	// test_convert();
	return 0;
}
