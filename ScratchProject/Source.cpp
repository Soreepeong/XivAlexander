#include "pch.h"

#include <XivAlexanderCommon/Sqex_FontCsv_Creator.h>
#include <XivAlexanderCommon/Sqex_FontCsv_DirectWriteFont.h>
#include <XivAlexanderCommon/Sqex_FontCsv_GdiFont.h>
#include <XivAlexanderCommon/Sqex_FontCsv_ModifiableFontCsvStream.h>
#include <XivAlexanderCommon/Sqex_FontCsv_SeCompatibleDrawableFont.h>
#include <XivAlexanderCommon/Sqex_FontCsv_SeCompatibleFont.h>
#include <XivAlexanderCommon/Sqex_FontCsv_CreateConfig.h>
#include <XivAlexanderCommon/Sqex_Sqpack.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_Texture_Mipmap.h>

static const auto* const pszTestString = reinterpret_cast<const char*>(
	u8"Uppercase: ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
	u8"Lowercase: abcdefghijklmnopqrstuvwxyz\n"
	u8"Numbers: 0123456789 ０１２３４５６７８９\n"
	u8"SymbolsH: `~!@#$%^&*()_+-=[]{}\\|;':\",./<>?\n"
	u8"SymbolsF: ｀～！＠＃＄％＾＆＊（）＿＋－＝［］｛｝￦｜；＇：＂，．／＜＞？\n"
	u8"Hiragana: あかさたなはまやらわ\n"
	u8"KatakanaH: ｱｶｻﾀﾅﾊﾏﾔﾗﾜ\n"
	u8"KatakanaF: アカサタナハマヤラワ\n"
	u8"Hangul: 가나다라마바사ㅇㅈㅊㅋㅌㅍㅎ\n"
	u8"\n"
	u8"<<SupportedUnicode>>\n"
	u8"π™′＾¿¿‰øØ×∞∩£¥¢Ð€ªº†‡¤ ŒœŠšŸÅωψ↑↓→←⇔⇒♂♀♪¶§±＜＞≥≤≡÷½¼¾©®ª¹²³\n"
	u8"※⇔｢｣«»≪≫《》【】℉℃‡。·••‥…¨°º‰╲╳╱☁☀☃♭♯✓〃¹²³\n"
	u8"●◎○■□▲△▼▽∇♥♡★☆◆◇♦♦♣♠♤♧¶αß∇ΘΦΩδ∂∃∀∈∋∑√∝∞∠∟∥∪∩∨∧∫∮∬\n"
	u8"∴∵∽≒≠≦≤≥≧⊂⊃⊆⊇⊥⊿⌒─━│┃│¦┗┓└┏┐┌┘┛├┝┠┣┤┥┫┬┯┰┳┴┷┸┻╋┿╂┼￢￣，－．／：；＜＝＞［＼］＿｀｛｜｝～＠\n"
	u8"⑴⑵⑶⑷⑸⑹⑺⑻⑼⑽⑾⑿⒀⒁⒂⒃⒄⒅⒆⒇⓪①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳\n"
	u8"₀₁₂₃₄₅₆₇₈₉№ⅠⅡⅢⅣⅤⅥⅦⅧⅨⅩⅰⅱⅲⅳⅴⅵⅶⅷⅸⅹ０１２３４５６７８９！？＂＃＄％＆＇（）＊＋￠￤￥\n"
	u8"ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲＳＴＵＶＷＸＹＺａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒｓｔｕｖｗｘｙｚ\n"
	u8"\n"
	u8"<<GameSpecific>>\n"
	u8" \n"
	u8"\n"
	u8"\n"
	u8"\n"
	u8"\n"
	u8"<<Kerning>>\n"
	u8"AC AG AT AV AW AY LT LV LW LY TA Ta Tc Td Te Tg To VA Va Vc Vd Ve Vg Vm Vo Vp Vq Vu\n"
	u8"A\u200cC A\u200cG A\u200cT A\u200cV A\u200cW A\u200cY L\u200cT L\u200cV L\u200cW L\u200cY T\u200cA T\u200ca T\u200cc T\u200cd T\u200ce T\u200cg T\u200co V\u200cA V\u200ca V\u200cc V\u200cd V\u200ce V\u200cg V\u200cm V\u200co V\u200cp V\u200cq V\u200cu\n"
	u8"WA We Wq YA Ya Yc Yd Ye Yg Ym Yn Yo Yp Yq Yr Yu eT oT\n"
	u8"W\u200cA W\u200ce W\u200cq Y\u200cA Y\u200ca Y\u200cc Y\u200cd Y\u200ce Y\u200cg Y\u200cm Y\u200cn Y\u200co Y\u200cp Y\u200cq Y\u200cr Y\u200cu e\u200cT o\u200cT\n"
	u8"Az Fv Fw Fy TV TW TY Tv Tw Ty VT WT YT tv tw ty vt wt yt\n"
	u8"A\u200cz F\u200cv F\u200cw F\u200cy T\u200cV T\u200cW T\u200cY T\u200cv T\u200cw T\u200cy V\u200cT W\u200cT Y\u200cT t\u200cv t\u200cw t\u200cy v\u200ct w\u200ct y\u200ct\n"
	u8"\n"
	u8"finish"
	);

const auto testString = U"Testing 테스트 テスト\n\n0123456789\n!@#$%^&*()_+-=[]{}";
const auto testWeight = 400;

auto test(const Sqex::Sqpack::Reader& common, const std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>>& texs, float size, const char* sizestr) {
	const auto sourcek = std::make_shared<Sqex::FontCsv::DirectWriteDrawingFont<uint8_t>>(
		L"Source Han Sans K", size, static_cast<DWRITE_FONT_WEIGHT>(testWeight)
		);
	const auto axis = std::make_shared<Sqex::FontCsv::SeDrawableFont<Sqex::Texture::RGBA4444, uint8_t>>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(
		Sqex::Sqpack::EntryRawStream(common.GetEntryProvider(std::format("common/font/{}.fdt", sizestr))),
		true), texs);

	auto creator = Sqex::FontCsv::Creator();
	creator.SizePoints = axis->Size();
	creator.AscentPixels = axis->Ascent();
	creator.DescentPixels = axis->Descent();
	for (const auto c : std::u32string(testString)) {
		creator.AddCharacter(c, axis);
		creator.AddCharacter(c, sourcek);
	}
	creator.AddKerning(axis);
	creator.AddKerning(sourcek);

	Sqex::FontCsv::Creator::RenderTarget target(4096, 4096, 1);
	const auto newFont = creator.Compile(target);
	target.Finalize();

	const auto newFontMipmaps = target.AsMipmapStreamVector();

	return std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(newFont, newFontMipmaps);
}

auto test2(float size) {
	const auto sourcek = std::make_shared<Sqex::FontCsv::GdiDrawingFont<uint8_t>>(LOGFONTW{
		.lfHeight = -static_cast<int>(std::round(size)),
		.lfWeight = testWeight,
		.lfCharSet = DEFAULT_CHARSET,
		.lfQuality = CLEARTYPE_NATURAL_QUALITY,
		.lfFaceName = L"Source Han Sans K",
		});
	const auto axis = std::make_shared<Sqex::FontCsv::GdiDrawingFont<uint8_t>>(LOGFONTW{
		.lfHeight = -static_cast<int>(std::round(size)),
		.lfWeight = testWeight,
		.lfCharSet = DEFAULT_CHARSET,
		.lfQuality = CLEARTYPE_NATURAL_QUALITY,
		.lfFaceName = L"AXIS Basic ProN R",
		});

	auto creator = Sqex::FontCsv::Creator();
	creator.SizePoints = axis->Size();
	creator.AscentPixels = axis->Ascent();
	creator.DescentPixels = axis->Descent();
	for (const auto c : std::u32string(testString)) {
		creator.AddCharacter(c, axis);
		creator.AddCharacter(c, sourcek);
	}
	creator.AddKerning(axis);
	creator.AddKerning(sourcek);

	Sqex::FontCsv::Creator::RenderTarget target(4096, 4096, 1);
	const auto newFont = creator.Compile(target);
	target.Finalize();

	const auto newFontMipmaps = target.AsMipmapStreamVector();

	return std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(newFont, newFontMipmaps);
}

auto test3(float size) {
	const auto sourcek = std::make_shared<Sqex::FontCsv::DirectWriteDrawingFont<uint8_t>>(
		L"Source Han Sans K", size, static_cast<DWRITE_FONT_WEIGHT>(testWeight)
		);
	const auto axis = std::make_shared<Sqex::FontCsv::DirectWriteDrawingFont<uint8_t>>(
		L"AXIS Basic ProN", size, static_cast<DWRITE_FONT_WEIGHT>(testWeight)
		);

	auto creator = Sqex::FontCsv::Creator();
	creator.SizePoints = axis->Size();
	creator.AscentPixels = axis->Ascent();
	creator.DescentPixels = axis->Descent();
	for (const auto c : std::u32string(testString)) {
		creator.AddCharacter(c, axis);
		creator.AddCharacter(c, sourcek);
	}
	creator.AddKerning(axis);
	creator.AddKerning(sourcek);

	Sqex::FontCsv::Creator::RenderTarget target(4096, 4096, 1);
	const auto newFont = creator.Compile(target);
	target.Finalize();

	const auto newFontMipmaps = target.AsMipmapStreamVector();

	return std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(newFont, newFontMipmaps);
}

void singletest() {
	const auto cw = 800;
	const auto ch = 800;

	const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(cw, ch, Sqex::Texture::CompressionType::ARGB_1, std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * cw * ch));
	std::fill_n(mm32->View<uint32_t>().begin(), mm32->Width() * mm32->Height(), 0xFF000000);

	SSIZE_T yptr = 5;
	for (const auto fname : { L"Source Han Sans K", L"AXIS Basic ProN", L"Comic Sans MS", L"Segoe UI", L"Gulim" }) {
		const auto f = std::make_shared<Sqex::FontCsv::DirectWriteDrawingFont<Sqex::Texture::RGBA8888>>(fname, 18);
		for (int i = 0; i < cw; ++i) {
			mm32->View<Sqex::Texture::RGBA8888>()[cw * yptr + i].SetFrom(255, 0, 0, 255);
			mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + f->Ascent()) + i].SetFrom(0, 255, 0, 255);
			mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + f->Ascent() + f->Descent()) + i].SetFrom(0, 255, 0, 255);
		}
		yptr += f->Draw(mm32.get(), 5, yptr, testString, 0xFFFFFFFF, 0).Height();
		for (int i = 0; i < cw; ++i)
			mm32->View<Sqex::Texture::RGBA8888>()[cw * yptr + i].SetFrom(0, 0, 255, 255);
		yptr += 10;
	}
	mm32->Show();
}

void singletest2() {
	const auto commonG = Sqex::Sqpack::Reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\000000.win32.index)", true, true);
	std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> texG;
	for (int i = 1; i <= 7; ++i) {
		texG.emplace_back(Sqex::Texture::MipmapStream::FromTexture(std::make_shared<Sqex::Sqpack::EntryRawStream>(commonG.GetEntryProvider(std::format("common/font/font{}.tex", i))), 0));
		// preload
		texG[i - 1]->ReadStreamIntoVector<char>(0);
	}

	const auto jupiter = std::make_shared<Sqex::FontCsv::SeDrawableFont<Sqex::Texture::RGBA4444, uint8_t>>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(Sqex::Sqpack::EntryRawStream(commonG.GetEntryProvider("common/font/Jupiter_16.fdt")), true), texG);
	const auto axis = std::make_shared<Sqex::FontCsv::SeDrawableFont<Sqex::Texture::RGBA4444, uint8_t>>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(Sqex::Sqpack::EntryRawStream(commonG.GetEntryProvider("common/font/AXIS_18.fdt")), true), texG);
	const auto sourceK = std::make_shared<Sqex::FontCsv::DirectWriteDrawingFont<uint8_t>>(L"Source Han Sans K", 18, DWRITE_FONT_WEIGHT_MEDIUM);
	// const auto sourceK = std::make_shared<Sqex::FontCsv::DirectWriteDrawingFont<uint8_t>>(L"Gulim", 18);

	auto creator = Sqex::FontCsv::Creator();
	creator.SizePoints = axis->Size();
	creator.AscentPixels = axis->Ascent();
	creator.DescentPixels = axis->Descent();
	creator.AddFont(jupiter);
	creator.AddFont(axis);
	creator.AddFont(sourceK);

	Sqex::FontCsv::Creator::RenderTarget target(4096, 4096, 1);
	const auto newFontCsv = creator.Compile(target);
	target.Finalize();

	const auto newFontMipmaps = target.AsMipmapStreamVector();
	const auto newFont = std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(newFontCsv, newFontMipmaps);

	const auto lines = Utils::StringSplit<std::string>(pszTestString, "\n");
	const auto measurement = newFont->Measure(5, 5, pszTestString);
	const auto cw = static_cast<uint16_t>(measurement.Width() + 10);
	const auto ch = static_cast<uint16_t>(measurement.Height() + 5 * (lines.size() + 1));
	const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(cw, ch, Sqex::Texture::CompressionType::ARGB_1, std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * cw * ch));
	std::fill_n(mm32->View<uint32_t>().begin(), mm32->Width() * mm32->Height(), 0xFF000000);

	SSIZE_T yptr = 5;
	for (auto line : lines) {
		if (line.empty())
			line = " ";
		for (SSIZE_T i = 0; i < cw; ++i) {
			mm32->View<Sqex::Texture::RGBA8888>()[cw * yptr + i].SetFrom(255, 0, 0, 255);
			mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + newFont->Ascent()) + i].SetFrom(0, 255, 0, 255);
			mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + newFont->Ascent() + newFont->Descent()) + i].SetFrom(0, 255, 0, 255);
		}
		newFont->Draw(mm32.get(), 5, yptr, line, 0xFFFFFFFF, 0);
		yptr += newFont->Height();
		for (SSIZE_T i = 0; i < cw; ++i)
			mm32->View<Sqex::Texture::RGBA8888>()[cw * yptr + i].SetFrom(0, 0, 255, 255);
		yptr += 5;
	}

	mm32->Show();
}

void multitest() {
	const auto commonG = Sqex::Sqpack::Reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\000000.win32.index)", true, true);
	const auto commonK = Sqex::Sqpack::Reader(LR"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\sqpack\ffxiv\000000.win32.index)", true, true);
	std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> texG, texK;
	for (int i = 1; i <= 7; ++i)
		texG.emplace_back(Sqex::Texture::MipmapStream::FromTexture(std::make_shared<Sqex::Sqpack::EntryRawStream>(commonG.GetEntryProvider(std::format("common/font/font{}.tex", i))), 0));
	for (int i = 1; i <= 3; ++i)
		texK.emplace_back(Sqex::Texture::MipmapStream::FromTexture(std::make_shared<Sqex::Sqpack::EntryRawStream>(commonK.GetEntryProvider(std::format("common/font/font_krn_{}.tex", i))), 0));

	const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(800, 800, Sqex::Texture::CompressionType::ARGB_1, std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * 800 * 800));
	std::fill_n(mm32->View<uint32_t>().begin(), mm32->Width() * mm32->Height(), 0xFF000000);

	SSIZE_T yptr = 0;
	yptr += test(commonG, texG, 9.6f, "AXIS_96")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	// yptr += test2(9.6)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test3(9.6f)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();

	yptr += test(commonK, texK, 12, "KrnAXIS_120")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test(commonG, texG, 12, "AXIS_12")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	// yptr += test2(12)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test3(12)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();

	yptr += test(commonK, texK, 14, "KrnAXIS_140")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test(commonG, texG, 14, "AXIS_14")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	// yptr += test2(14)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test3(14)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();

	yptr += test(commonK, texK, 18, "KrnAXIS_180")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test(commonG, texG, 18, "AXIS_18")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	// yptr += test2(18)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test3(18)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();

	yptr += test(commonG, texG, 36, "AXIS_36")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	// yptr += test2(36)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test3(36)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	mm32->Show();
}

void compile() {
	std::ifstream fin(R"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\FontConfig\International.Gulim.json)");
	// std::ifstream fin(R"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\FontConfig\International.ComicGulim.json)");
	// std::ifstream fin(R"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\FontConfig\International.PapyrusGungsuh.json)");
	nlohmann::json j;
	fin >> j;
	auto cfg = j.get<Sqex::FontCsv::CreateConfig::FontCreateConfig>();

	std::map<std::filesystem::path, std::unique_ptr<Sqex::Sqpack::Reader>> readers;
	std::map<std::tuple<std::filesystem::path, std::filesystem::path>, std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>>> gameTextures;
	std::map<std::string, std::shared_ptr<const Sqex::FontCsv::SeCompatibleDrawableFont<uint8_t>>> sourceFonts;
	for (const auto& [name, inputFontSource] : cfg.sources) {
		if (const auto& source = inputFontSource.gameSource; inputFontSource.isGameSource) {
			auto indexFilePath = source.indexFile.empty() ? std::filesystem::path(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\000000.win32.index)") : std::filesystem::path(source.indexFile);
			indexFilePath = canonical(indexFilePath);

			auto reader = readers.find(indexFilePath);
			if (reader == readers.end())
				reader = readers.emplace(indexFilePath, std::make_unique<Sqex::Sqpack::Reader>(indexFilePath)).first;

			auto textureKey = std::make_tuple(indexFilePath, source.texturePath);
			auto textures = gameTextures.find(textureKey);
			if (textures == gameTextures.end())
				textures = gameTextures.emplace(textureKey, std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>>()).first;

			for (size_t i = textures->second.size(); i < source.textureCount; ++i) {
				auto mipmap = Sqex::Texture::MipmapStream::FromTexture(std::make_shared<Sqex::Sqpack::EntryRawStream>(reader->second->GetEntryProvider(std::format(source.texturePath.string(), i + 1))), 0);

				// preload sqex entry layout so that we can process stuff multithreaded later
				void(mipmap->ReadStreamIntoVector<char>(0));

				textures->second.emplace_back(std::move(mipmap));
			}

			sourceFonts.emplace(name, std::make_shared<Sqex::FontCsv::SeDrawableFont<Sqex::Texture::RGBA4444, uint8_t>>(
				std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(Sqex::Sqpack::EntryRawStream(reader->second->GetEntryProvider(source.fdtPath))), textures->second));
		}
		if (const auto& source = inputFontSource.gdiSource; inputFontSource.isGdiSource) {
			sourceFonts.emplace(name, std::make_shared<Sqex::FontCsv::GdiDrawingFont<uint8_t>>(source));
		}
		if (const auto& source = inputFontSource.directWriteSource; inputFontSource.isDirectWriteSource) {
			sourceFonts.emplace(name, std::make_shared<Sqex::FontCsv::DirectWriteDrawingFont<uint8_t>>(
				Utils::FromUtf8(source.familyName).c_str(), source.height, static_cast<DWRITE_FONT_WEIGHT>(source.weight), source.stretch, source.style, source.renderMode
				));
		}
	}

	for (const auto& [textureGroupFilenamePattern, fonts] : cfg.targets) {
		std::map<std::string, std::shared_ptr<Sqex::FontCsv::ModifiableFontCsvStream>> newFonts;
		std::vector<std::shared_ptr<Sqex::Texture::ModifiableTextureStream>> newTextures;
		{
			Sqex::FontCsv::Creator::RenderTarget target(cfg.textureWidth, cfg.textureHeight, cfg.glyphGap);
			for (const auto& [fontName, plan] : fonts.fontTargets) {
				auto creator = Sqex::FontCsv::Creator();
				creator.SizePoints = static_cast<float>(plan.height);
				if (plan.autoAscent)
					creator.AscentPixels = Sqex::FontCsv::Creator::AutoAscentDescent;
				else if (!plan.ascentFrom.empty())
					creator.AscentPixels = sourceFonts.at(plan.ascentFrom)->Ascent();
				else
					creator.AscentPixels = 0;
				if (plan.autoDescent)
					creator.DescentPixels = Sqex::FontCsv::Creator::AutoAscentDescent;
				else if (!plan.ascentFrom.empty())
					creator.DescentPixels = sourceFonts.at(plan.ascentFrom)->Descent();
				else
					creator.DescentPixels = 0;
				creator.MinGlobalOffsetX = plan.minGlobalOffsetX;
				creator.MaxGlobalOffsetX = plan.maxGlobalOffsetX;
				creator.GlobalOffsetYModifier = plan.globalOffsetY;
				creator.AlwaysApplyKerningCharacters.insert(plan.charactersToKernAcrossFonts.begin(), plan.charactersToKernAcrossFonts.end());
				creator.AlignToBaseline = plan.alignToBaseline;
				for (const auto& source : plan.sources) {
					const auto& sourceFont = sourceFonts.at(source.name);
					if (source.ranges.empty())
						creator.AddFont(sourceFont, source.replace, source.extendRange);
					else {
						for (const auto& rangeName : source.ranges) {
							for (const auto& range : cfg.ranges.at(rangeName).ranges | std::views::values) {
								for (auto i = range.from; i < range.to; ++i)
									creator.AddCharacter(i, sourceFont, source.replace, source.extendRange);
								if (range.from != range.to)  // separate line to prevent overflow
									creator.AddCharacter(range.to, sourceFont, source.replace, source.extendRange);
							}
						}
					}
					creator.AddKerning(sourceFont, source.replace);
				}

				newFonts.emplace(fontName, creator.Compile(target));
			}
			target.Finalize();
			newTextures = target.AsTextureStreamVector();
		}

		std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> newMipmaps;
		for (size_t i = 0; i < newTextures.size(); ++i) {
			auto buf = newTextures[i]->ReadStreamIntoVector<uint8_t>(0);
			Utils::Win32::File::Create(
				std::format(LR"(Z:\scratch\cfonts\{})", std::format(textureGroupFilenamePattern, i + 1)),
				GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0
			).Write(0, std::span(buf));
		}
		for (const auto& [fontName, newFontCsv] : newFonts) {
			auto buf = newFontCsv->ReadStreamIntoVector<uint8_t>(0);
			Utils::Win32::File::Create(
				std::format(LR"(Z:\scratch\cfonts\{})", fontName),
				GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0
			).Write(0, std::span(buf));
		}

		for (const auto& texture : newTextures)
			newMipmaps.emplace_back(Sqex::Texture::MipmapStream::FromTexture(texture, 0));

		for (const auto& [fontName, newFontCsv] : newFonts) {
			const auto newFont = std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(newFontCsv, newMipmaps);
			{
				const auto lines = Utils::StringSplit<std::string>(pszTestString, "\n");
				const auto cw = static_cast<uint16_t>(newFont->Measure(5, 5, pszTestString).Width() + 10);
				const auto ch = static_cast<uint16_t>(5 * (lines.size() + 1) + newFont->Height() * lines.size());
				const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(cw, ch, Sqex::Texture::CompressionType::ARGB_1, std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * cw * ch));
				std::fill_n(mm32->View<uint32_t>().begin(), mm32->Width() * mm32->Height(), 0xFF000000);

				SSIZE_T yptr = 5;
				auto j = 0;
				for (auto line : lines) {
					++j;
					if (line.empty())
						line = " ";
					for (SSIZE_T i = 0; i < cw; ++i) {
						mm32->View<Sqex::Texture::RGBA8888>()[cw * yptr + i].SetFrom(255, 0, 0, 255);
						mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + newFont->Ascent()) + i].SetFrom(0, 255, 0, 255);
						mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + newFont->Ascent() + newFont->Descent()) + i].SetFrom(0, 255, 0, 255);
					}
					newFont->Draw(mm32.get(), 5, yptr, line, 0xFFFFFFFF, 0);
					yptr += newFont->Height();
					for (SSIZE_T i = 0; i < cw; ++i)
						mm32->View<Sqex::Texture::RGBA8888>()[cw * yptr + i].SetFrom(0, 0, 255, 255);
					yptr += 5;
				}

				mm32->Show(fontName);
			}
		}
	}
}

int main() {
	// singletest();
	// singletest2();
	// multitest();
	compile();
	return 0;
}
