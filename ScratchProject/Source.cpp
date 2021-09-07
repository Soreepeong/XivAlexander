#include "pch.h"
#include <XivAlexanderCommon/Sqex_FontCsv_ModifiableFontCsvStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_Texture_Mipmap.h>

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

int main() {
	// const auto pszTestString = reinterpret_cast<const char*>(u8"breh\n\n\n\n\n\n\nnTTTTT");
	//
	//for (const auto& it : std::filesystem::recursive_directory_iterator(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack)")) {
	//	if (it.path().extension() == ".index") {
	//		std::cout << std::format("Verifying {}...\n", it.path());
	//		Sqex::Sqpack::Reader(it, true, true);
	//	}
	//}
	const auto comic36 = std::make_shared<Sqex::FontCsv::GdiDrawingFont<>>(LOGFONTW{
		.lfHeight = -MulDiv(36, GetDeviceCaps(GetDC(0), LOGPIXELSY), 72),
		.lfCharSet = DEFAULT_CHARSET,
		.lfQuality = CLEARTYPE_NATURAL_QUALITY,
		.lfFaceName = L"Comic Sans MS",
		});
	const auto comic36l = std::make_shared<Sqex::FontCsv::GdiDrawingFont<uint8_t>>(LOGFONTW{
		.lfHeight = -MulDiv(36, GetDeviceCaps(GetDC(0), LOGPIXELSY), 72),
		.lfCharSet = DEFAULT_CHARSET,
		.lfQuality = CLEARTYPE_NATURAL_QUALITY,
		.lfFaceName = L"Comic Sans MS",
		});
	//{

	//	const auto bbox = comic36->Measure(0, 0, pszTestString);

	//	auto w = static_cast<uint16_t>(bbox.right - bbox.left);
	//	auto h = static_cast<uint16_t>(bbox.bottom - bbox.top);
	//	const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(
	//		w,
	//		h,
	//		Sqex::Texture::CompressionType::ARGB_1,
	//		std::vector<uint8_t>((size_t() + w) * (size_t() + h) * 4)
	//		);
	//	comic36->Draw(mm32.get(), 0, 0, pszTestString, 0xffffffff, 0xff000000);
	//	mm32->Show();
	//	//return 0;
	//}

	auto t = GetTickCount64();
	const auto common = Sqex::Sqpack::Reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\000000.win32.index)", true, true);
	std::cout << std::format("Sqpack::Reader {}\n", GetTickCount64() - t);
	t = GetTickCount64();

	std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> texs;
	for (int i = 1; i <= 7; ++i) {
		auto s = common.GetEntryProvider(std::format("common/font/font{}.tex", i));
		if (!s)
			break;
		texs.emplace_back(Sqex::Texture::MipmapStream::FromTexture(std::make_shared<Sqex::Sqpack::EntryRawStream>(std::move(s)), 0));
	}
	std::cout << std::format("Read 7 textures {}\n", GetTickCount64() - t);
	t = GetTickCount64();

	const auto axis36 = std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(
		Sqex::Sqpack::EntryRawStream(common.GetEntryProvider("common/font/AXIS_36.fdt")),
		true), texs);
	const auto trump68 = std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(
		Sqex::Sqpack::EntryRawStream(common.GetEntryProvider("common/font/TrumpGothic_68.fdt")),
		true), texs);
	const auto jupiter90 = std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(
		Sqex::Sqpack::EntryRawStream(common.GetEntryProvider("common/font/Jupiter_90.fdt")),
		true), texs);
	const auto font = std::make_shared<Sqex::FontCsv::CascadingDrawableFont<>>(
		std::vector<std::shared_ptr<Sqex::FontCsv::SeCompatibleDrawableFont<>>>{ jupiter90, comic36, axis36, }
	, 36.f, axis36->Ascent(), axis36->Descent()
		);

	const auto axis36l = std::make_shared<Sqex::FontCsv::SeDrawableFont<Sqex::Texture::RGBA4444, uint8_t>>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(
		Sqex::Sqpack::EntryRawStream(common.GetEntryProvider("common/font/AXIS_36.fdt")),
		true), texs);
	const auto trump68l = std::make_shared<Sqex::FontCsv::SeDrawableFont<Sqex::Texture::RGBA4444, uint8_t>>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(
		Sqex::Sqpack::EntryRawStream(common.GetEntryProvider("common/font/TrumpGothic_68.fdt")),
		true), texs);
	const auto jupiter90l = std::make_shared<Sqex::FontCsv::SeDrawableFont<Sqex::Texture::RGBA4444, uint8_t>>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(
		Sqex::Sqpack::EntryRawStream(common.GetEntryProvider("common/font/Jupiter_90.fdt")),
		true), texs);
	const auto fontl = std::make_shared<Sqex::FontCsv::CascadingDrawableFont<uint8_t>>(
		std::vector<std::shared_ptr<Sqex::FontCsv::SeCompatibleDrawableFont<uint8_t>>>{ jupiter90l, comic36l, axis36l, }
	, 36.f, axis36->Ascent(), axis36->Descent()
		);

	std::cout << std::format("Read 3 fdt files {}\n", GetTickCount64() - t);

	t = GetTickCount64();
	const auto bbox = font->Measure(0, 0, pszTestString);
	std::cout << std::format("Measure: {}\n", GetTickCount64() - t);

	t = GetTickCount64();
	const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(
		static_cast<uint16_t>(bbox.right - bbox.left),
		static_cast<uint16_t>(bbox.bottom - bbox.top),
		Sqex::Texture::CompressionType::ARGB_1,
		std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * (size_t() + bbox.right - bbox.left) * (size_t() + bbox.bottom - bbox.top))
		);
	std::fill(mm32->View<uint32_t>().begin(), mm32->View<uint32_t>().end(), 0xFF000000);
	font->Draw(mm32.get(), 0, 0, pszTestString,
		Sqex::Texture::RGBA8888(200, 200, 255), Sqex::Texture::RGBA8888(0, 0, 0, 0));
	std::cout << std::format("Draw32: {}\n", GetTickCount64() - t);

	t = GetTickCount64();
	const auto mm8 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(
		static_cast<uint16_t>(bbox.right - bbox.left),
		static_cast<uint16_t>(bbox.bottom - bbox.top),
		Sqex::Texture::CompressionType::L8_1,
		std::vector<uint8_t>((size_t() + bbox.right - bbox.left) * (size_t() + bbox.bottom - bbox.top))
		);
	fontl->Draw(mm8.get(), 0, 0, pszTestString, 255, 0, 255, 0);
	std::cout << std::format("Draw8: {}\n", GetTickCount64() - t);

	mm32->Show();
	mm8->Show();
	return 0;
}
