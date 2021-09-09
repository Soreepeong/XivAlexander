#include "pch.h"
#include <XivAlexanderCommon/Sqex_FontCsv_ModifiableFontCsvStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_Texture_Mipmap.h>

#include "XivAlexanderCommon/Sqex_FontCsv_Creator.h"
#include "XivAlexanderCommon/Sqex_FontCsv_SeCompatibleDrawableFont.h"
#include "XivAlexanderCommon/Sqex_FontCsv_SeCompatibleFont.h"

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

const auto testString = U"Test 테스트 テスト 12345 !@#$%^&*()_+-=[]{}";
const auto testWeight = 400;

auto test(const Sqex::Sqpack::Reader& common, const std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>>& texs, double size, const char* sizestr) {
	const auto sourcek = std::make_shared<Sqex::FontCsv::GdiDrawingFont<uint8_t>>(LOGFONTW{
		.lfHeight = -static_cast<int>(std::round(size)),
		.lfWeight = testWeight,
		.lfCharSet = DEFAULT_CHARSET,
		.lfQuality = CLEARTYPE_NATURAL_QUALITY,
		.lfFaceName = L"Source Han Sans K",
		});
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

auto test2(double size) {
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

int main() {
	const auto commonG = Sqex::Sqpack::Reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\000000.win32.index)", true, true);
	const auto commonK = Sqex::Sqpack::Reader(LR"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\sqpack\ffxiv\000000.win32.index)", true, true);
	std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> texG, texK;
	for (int i = 1; i <= 7; ++i)
		texG.emplace_back(Sqex::Texture::MipmapStream::FromTexture(std::make_shared<Sqex::Sqpack::EntryRawStream>(commonG.GetEntryProvider(std::format("common/font/font{}.tex", i))), 0));
	for (int i = 1; i <= 3; ++i)
		texK.emplace_back(Sqex::Texture::MipmapStream::FromTexture(std::make_shared<Sqex::Sqpack::EntryRawStream>(commonK.GetEntryProvider(std::format("common/font/font_krn_{}.tex", i))), 0));

	const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(800, 400, Sqex::Texture::CompressionType::ARGB_1, std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * 800 * 400));
	std::fill_n(mm32->View<uint32_t>().begin(), mm32->Width() * mm32->Height(), 0xFF000000);

	SSIZE_T yptr = 0;
	yptr += test(commonG, texG, 9.6, "AXIS_96")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test2(9.6)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test(commonK, texK, 12, "KrnAXIS_120")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test(commonG, texG, 12, "AXIS_12")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test2(12)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test(commonK, texK, 14, "KrnAXIS_140")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test(commonG, texG, 14, "AXIS_14")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test2(14)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test(commonK, texK, 18, "KrnAXIS_180")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test(commonG, texG, 18, "AXIS_18")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test2(18)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test(commonG, texG, 36, "AXIS_36")->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	yptr += test2(36)->Draw(mm32.get(), 0, yptr, testString, 0xFFFFFFFF, 0x0).Height();
	mm32->Show();
	return 0;
}
