#include "pch.h"

#include "XivRes/FontGenerator/WrappingFixedSizeFont.h"
#include "XivRes/FontGenerator/FontdataPacker.h"
#include "XivRes/FontGenerator/GameFontdataFixedSizeFont.h"
#include "XivRes/FontGenerator/MergedFixedSizeFont.h"
#include "XivRes/FontGenerator/TextMeasurer.h"
#include "XivRes/Internal/TrueTypeUtils.h"

#include "ExampleWindow.h"

void InspectFonts() {
	using namespace XivRes::Internal::TrueType;

	const auto testSfnt = [](const std::u8string& filename, int fontIndex, SfntFile::View sfnt) {
		const auto head = sfnt.TryGetTable<Head>();
		const auto name = sfnt.TryGetTable<Name>();
		const auto cmap = sfnt.TryGetTable<Cmap>();

		std::cout << std::format("{:<32} {:<24}\t", name.GetPreferredFamilyName(0), name.GetPreferredSubfamilyName(0));

		const auto cmapVector = cmap.GetGlyphToCharMap();
		size_t numc = 0;
		for (const auto& c : cmapVector)
			numc += c.size();
		std::cout << std::format("c({})", numc);

		std::map<std::pair<char32_t, char32_t>, int> kernPairs, gposPairs;
		if (auto kern = sfnt.TryGetTable<Kern>(); kern) {
			kernPairs = kern.Parse(cmapVector);
			std::cout << std::format(" k({})", kernPairs.size());
		}

		if (auto gpos = sfnt.TryGetTable<Gpos>(); gpos) {
			gposPairs = gpos.ExtractAdvanceX(cmapVector);
			if (!gposPairs.empty()) {
				std::cout << std::format(" g({})", gposPairs.size());

				std::set<std::pair<char32_t, char32_t>> kerned;
				for (const auto& p : kernPairs)
					kerned.insert(p.first);
				for (const auto& p : gposPairs)
					kerned.insert(p.first);

				std::cout << std::format(" t({})", kerned.size());
			}
		}

		std::cout << std::endl;
	};

	std::vector<uint8_t> buf;
	for (const auto& file : std::filesystem::directory_iterator(LR"(C:\Windows\Fonts)")) {
		if (file.is_directory())
			continue;

		if (_wcsicmp(file.path().extension().c_str(), L".ttc") == 0 || _wcsicmp(file.path().extension().c_str(), L".otc") == 0) {
			buf.resize(file.file_size());
			ReadStream(XivRes::FileStream(file), 0, std::span(buf));

			if (TtcFile::View ttc(&buf[0], buf.size()); ttc) {
				for (int i = 0; i < (int)ttc.GetFontCount(); i++) {
					testSfnt(file.path().filename().u8string(), i, ttc.GetFont(i));
				}
			}
		} else if (_wcsicmp(file.path().extension().c_str(), L".ttf") == 0 || _wcsicmp(file.path().extension().c_str(), L".otf") == 0) {
			buf.resize(file.file_size());
			ReadStream(XivRes::FileStream(file), 0, std::span(buf));

			if (SfntFile::View sfnt(&buf[0], buf.size()); sfnt)
				testSfnt(file.path().filename().u8string(), -1, sfnt);
		}
	}
}

void TestSourceHanSansK() {
	using namespace XivRes::Unicode;
	using namespace XivRes::Internal::TrueType;

	const auto buf = ReadStreamIntoVector<char>(XivRes::FileStream(LR"(C:\windows\fonts\SourceHanSansK-Regular.otf)"));
	SfntFile::View sfnt{ std::span(buf) };
	const auto head = sfnt.TryGetTable<Head>();
	const auto name = sfnt.TryGetTable<Name>();
	const auto cmap = sfnt.TryGetTable<Cmap>();
	const auto kern = sfnt.TryGetTable<Kern>();
	const auto gpos = sfnt.TryGetTable<Gpos>();

	const auto glyphToCharMap = cmap.GetGlyphToCharMap();
	std::map<const UnicodeBlocks::BlockDefinition*, std::set<char32_t>> visitedBlocks;
	for (int glyphId = 0; glyphId < glyphToCharMap.size(); glyphId++) {
		for (const auto charId : glyphToCharMap[glyphId]) {
			const auto& block = UnicodeBlocks::GetCorrespondingBlock(charId);
			if (block.First != 0x3130)
				continue;

			union {
				uint32_t u8u32v;
				char b[5]{};
			};
			u8u32v = XivRes::Unicode::CodePointToUtf8Uint32(charId);
			std::reverse(b, b + strlen(b));
			std::cout << b;

			visitedBlocks[&block].insert(charId);
		}
	}
	std::cout << std::endl;
}

int main() {
	system("chcp 65001");
	// InspectFonts();
	ShowExampleWindow();
	// TestSourceHanSansK();
	return 0;
}
