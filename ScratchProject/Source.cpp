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

#include "XivAlexanderCommon/Sqex_FontCsv_FreeTypeFont.h"

static const auto* const pszTestString = reinterpret_cast<const char*>(
	u8"Uppercase: ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
	u8"Lowercase: abcdefghijklmnopqrstuvwxyz\n"
	u8"Numbers: 0123456789 ０１２３４５６７８９\n"
	u8"SymbolsH: `~!@#$%^&*()_+-=[]{}\\|;':\",./<>?\n"
	u8"SymbolsF: ｀～！＠＃＄％＾＆＊（）＿＋－＝［］｛｝￦｜；＇：＂，．／＜＞？\n"
	u8"Hiragana: あかさたなはまやらわ\n"
	u8"KatakanaH: ｱｶｻﾀﾅﾊﾏﾔﾗﾜ\n"
	u8"KatakanaF: アカサタナハマヤラワ\n"
	u8"Hangul: 가나다라마바사아자차카타파하\n"
	u8"Hangul: ㄱㄴㄷㄹㅁㅂㅅㅇㅈㅊㅋㅌㅍㅎ\n"
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
	u8"테스트 finish\n"
	u8"ㅌㅅㅌ nj\n"
	);

const auto testString = U"Hello world!";
const auto testWeight = 400;

auto test(const Sqex::Sqpack::Reader& common, const std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>>& texs, float size, const char* sizestr) {
	const auto sourcek = std::make_shared<Sqex::FontCsv::DirectWriteDrawingFont<uint8_t>>(
		L"Source Han Sans K", size, static_cast<DWRITE_FONT_WEIGHT>(testWeight)
		);
	const auto axis = std::make_shared<Sqex::FontCsv::SeDrawableFont<Sqex::Texture::RGBA4444, uint8_t>>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(
		Sqex::Sqpack::EntryRawStream(common.GetEntryProvider(std::format("common/font/{}.fdt", sizestr))),
		true), texs);

	auto creator = Sqex::FontCsv::FontCsvCreator();
	creator.SizePoints = axis->Size();
	creator.AscentPixels = axis->Ascent();
	creator.DescentPixels = axis->Descent();
	for (const auto c : std::u32string(testString)) {
		creator.AddCharacter(c, axis.get());
		creator.AddCharacter(c, sourcek.get());
	}
	creator.AddKerning(axis.get());
	creator.AddKerning(sourcek.get());

	Sqex::FontCsv::FontCsvCreator::RenderTarget target(4096, 4096, 1);
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

	auto creator = Sqex::FontCsv::FontCsvCreator();
	creator.SizePoints = axis->Size();
	creator.AscentPixels = axis->Ascent();
	creator.DescentPixels = axis->Descent();
	for (const auto c : std::u32string(testString)) {
		creator.AddCharacter(c, axis.get());
		creator.AddCharacter(c, sourcek.get());
	}
	creator.AddKerning(axis.get());
	creator.AddKerning(sourcek.get());

	Sqex::FontCsv::FontCsvCreator::RenderTarget target(4096, 4096, 1);
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

	auto creator = Sqex::FontCsv::FontCsvCreator();
	creator.SizePoints = axis->Size();
	creator.AscentPixels = axis->Ascent();
	creator.DescentPixels = axis->Descent();
	for (const auto c : std::u32string(testString)) {
		creator.AddCharacter(c, axis.get());
		creator.AddCharacter(c, sourcek.get());
	}
	creator.AddKerning(axis.get());
	creator.AddKerning(sourcek.get());

	Sqex::FontCsv::FontCsvCreator::RenderTarget target(4096, 4096, 1);
	const auto newFont = creator.Compile(target);
	target.Finalize();

	const auto newFontMipmaps = target.AsMipmapStreamVector();

	return std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(newFont, newFontMipmaps);
}

void test_direct() {
	for (const auto fsize : { 12, 36, 72 }) {
		for (const auto fname : { L"Gulim" /*L"Papyrus", L"Comic Sans MS"*/}) {
			auto lf = LOGFONTW{
				.lfHeight = -static_cast<int>(std::round(fsize)),
				.lfWeight = 400,
				.lfCharSet = DEFAULT_CHARSET,
				.lfQuality = CLEARTYPE_NATURAL_QUALITY,
			};
			wcsncpy_s(lf.lfFaceName, fname, 8);

			using T = Sqex::Texture::RGBA8888;

			for (const auto& fs : std::vector<std::shared_ptr<Sqex::FontCsv::SeCompatibleDrawableFont<T>>>{
				// std::make_shared<Sqex::FontCsv::GdiDrawingFont<T>>(lf),
				// std::make_shared<Sqex::FontCsv::DirectWriteDrawingFont<T>>(fname, fsize, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_RENDERING_MODE_NATURAL),
				std::make_shared<Sqex::FontCsv::FreeTypeDrawingFont<T>>(LR"(C:\Windows\Fonts\gulim.ttc)", 0, fsize)
				}) {
				// const auto testStr = pszTestString;
				const auto testStr = "j";

				const auto size = fs->Measure(0, 0, testStr);
				const auto cw = static_cast<uint16_t>(size.Width() + 10);
				const auto ch = static_cast<uint16_t>(size.Height() + 10);
				const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(cw, ch, Sqex::Texture::CompressionType::ARGB_1, std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * cw * ch));
				const auto yptr = 5 - size.top;
				std::fill_n(mm32->View<uint32_t>().begin(), mm32->Width() * mm32->Height(), 0xFF000000);

				for (int i = 0; i < cw; ++i) {
					mm32->View<Sqex::Texture::RGBA8888>()[cw * yptr + i].SetFrom(255, 0, 0, 255);
					mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + fs->Ascent()) + i].SetFrom(0, 255, 0, 255);
					mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + fs->Ascent() + fs->Descent()) + i].SetFrom(0, 255, 0, 255);
				}
				for (int i = 0; i < cw; ++i)
					mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + fs->Height()) + i].SetFrom(0, 0, 255, 255);
				void(fs->Draw(mm32.get(), 5, yptr, testStr, 0xFFFFFFFF, 0));
				mm32->Show();
			}
		}
	}
}

void test_create() {
	for (const auto fsize : { 12, 36 }) {
		for (const auto fname : { L"Gulim", L"Papyrus", L"Comic Sans MS" }) {
			auto lf = LOGFONTW{
				.lfHeight = -static_cast<int>(std::round(fsize)),
				.lfWeight = 400,
				.lfCharSet = DEFAULT_CHARSET,
				.lfQuality = CLEARTYPE_NATURAL_QUALITY,
			};
			wcsncpy_s(lf.lfFaceName, fname, 8);

			using T = uint8_t;

			for (const auto& fs : std::vector<std::shared_ptr<Sqex::FontCsv::SeCompatibleDrawableFont<T>>>{
				std::make_shared<Sqex::FontCsv::GdiDrawingFont<T>>(lf),
				std::make_shared<Sqex::FontCsv::DirectWriteDrawingFont<T>>(fname, fsize, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_RENDERING_MODE_NATURAL),
				}) {
				auto creator = Sqex::FontCsv::FontCsvCreator();
				creator.SizePoints = fs->Size();
				creator.AscentPixels = fs->Ascent();
				creator.DescentPixels = fs->Descent();
				for (const auto c : Sqex::FontCsv::ToU32(pszTestString))
					creator.AddCharacter(c, fs.get());
				Sqex::FontCsv::FontCsvCreator::RenderTarget target(4096, 4096, 1);
				const auto newFontCsv = creator.Compile(target);
				target.Finalize();

				const auto newFontMipmaps = target.AsMipmapStreamVector();
				const auto f = std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(newFontCsv, newFontMipmaps);

				const auto size = fs->Measure(0, 0, pszTestString);
				const auto cw = static_cast<uint16_t>(size.Width() + 10);
				const auto ch = static_cast<uint16_t>(size.Height() + 10);
				const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(cw, ch, Sqex::Texture::CompressionType::ARGB_1, std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * cw * ch));
				const auto yptr = std::max<SSIZE_T>(0, 5 - size.top);
				std::fill_n(mm32->View<uint32_t>().begin(), mm32->Width() * mm32->Height(), 0xFF000000);

				for (int i = 0; i < cw; ++i) {
					mm32->View<Sqex::Texture::RGBA8888>()[cw * yptr + i].SetFrom(255, 0, 0, 255);
					mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + f->Ascent()) + i].SetFrom(0, 255, 0, 255);
					mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + f->Ascent() + f->Descent()) + i].SetFrom(0, 255, 0, 255);
				}
				for (int i = 0; i < cw; ++i)
					mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + f->Height()) + i].SetFrom(0, 0, 255, 255);
				void(f->Draw(mm32.get(), 5, yptr, pszTestString, 0xFFFFFFFF, 0));
				mm32->Show();
			}
		}
	}
}

void compile() {
	Sqex::FontCsv::FontSetsCreator::ResultFontSets result;
	try {
		// std::ifstream fin(R"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\FontConfig\International.Original.json)");
		// std::ifstream fin(R"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\FontConfig\International.Gulim.dwrite.json)");
		// std::ifstream fin(R"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\FontConfig\International.Gulim.gdi.json)");
		std::ifstream fin(R"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\FontConfig\International.Gulimche.dwrite_file.json)");
		// std::ifstream fin(R"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\FontConfig\International.ComicGulim.json)");
		// std::ifstream fin(R"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\FontConfig\International.PapyrusGungsuh.json)");
		// std::ifstream fin(R"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\FontConfig\International.WithMinimalHangul.json)");
		// std::ifstream fin(R"(Z:\GitWorks\Soreepeong\XivAlexander\StaticData\FontConfig\Korean.18to36.json)");
		nlohmann::json j;
		fin >> j;
		auto cfg = j.get<Sqex::FontCsv::CreateConfig::FontCreateConfig>();

		// Sqex::FontCsv::FontSetsCreator creator(cfg, R"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\)");
		Sqex::FontCsv::FontSetsCreator creator(cfg, R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
		while (!creator.Wait(100)) {
			const auto progress = creator.GetProgress();
			std::cout << progress.Indeterminate << " " << progress.Scale(100.) << "%     \r";
		}
		result = creator.GetResult();

		std::cout << "Done!                 \n";
		for (const auto& [t, f] : result.Result) {
			std::cout << std::format("\t=> {}: {} files\n", t, f.Textures.size());
		}
	} catch (const std::exception& e) {
		std::cout << e.what() << std::endl;
	}

	for (const auto& [fileName, stream] : result.GetAllStreams()) {
		auto buf = stream->ReadStreamIntoVector<uint8_t>(0);
		Utils::Win32::File::Create(
			std::format(LR"(Z:\scratch\cfonts\{})", fileName.Original.filename()),
			GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0
		).Write(0, std::span(buf));
	}

	for (const auto& [textureGroupFilenamePattern, fontSet] : result.Result) {
		std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> newMipmaps;
		for (const auto& texture : fontSet.Textures)
			newMipmaps.emplace_back(Sqex::Texture::MipmapStream::FromTexture(texture, 0));

		for (const auto& [fontName, newFontCsv] : fontSet.Fonts) {
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

void freetype_test() {
	FT_Library library;
	if (const auto err = FT_Init_FreeType(&library))
		return;
	
	static FT_Face face;
	if (const auto err = FT_New_Face(library, R"(C:\windows\fonts\gulim.ttc)", 0, &face))
		return;

	if (const auto err = FT_Set_Char_Size(face, 0, 12 * 64, 96, 96))
		return;

	const auto glyphIndex = FT_Get_Char_Index(face, U'w');

	if (const auto err = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT))
		return;

	const auto w = face->glyph->bitmap.width;
	const auto h = face->glyph->bitmap.rows;
	const auto mm8 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(
		w,
		h,
		Sqex::Texture::CompressionType::L8_1,
		std::vector<uint8_t>(w * h));

	// FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);

	if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
		FT_Outline outline;
		FT_Outline_New(library, 0xFFFF, 0xFFFF, &outline);

		FT_Raster_Params rasterParams{
			.flags = FT_RASTER_FLAG_DIRECT | FT_RASTER_FLAG_AA,
			.gray_spans = [](int y, int count, const FT_Span* spans, void* user) {
				const auto mm8 = static_cast<Sqex::Texture::MemoryBackedMipmap*>(user);
				const auto buf = mm8->View<uint8_t>();
				for (const auto& span : std::span(spans, spans + count)) {
					std::fill_n(&buf[mm8->Width() * (face->glyph->bitmap_top - y - 1) + span.x - face->glyph->bitmap_left], span.len, span.coverage);
				}
			},
			.user = mm8.get(),
		};
		if (const auto err = FT_Outline_Render(library, &face->glyph->outline, &rasterParams))
			return;
	} else if (face->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
		FT_Bitmap target;
		if (face->glyph->bitmap.pitch != 8) {
			FT_Bitmap_Init(&target);
			if (const auto err = FT_Bitmap_Convert(library, &face->glyph->bitmap, &target, 1))
				return;
		} else
			target = face->glyph->bitmap;

		const auto buf = mm8->View<uint8_t>();
		for (size_t y = 0; y < target.rows; y++) {
			for (size_t x = 0; x < target.width; ++x) {
				buf[mm8->Width() * y + x] = target.buffer[target.width * y + x] * 255 / (face->glyph->bitmap.num_grays - 1);
			}
		}
	}
	
	mm8->Show();
}

int main() {
	system("chcp 65001");
	// freetype_test();
	// test_create();
	// test_direct();
	compile();
	return 0;
}
