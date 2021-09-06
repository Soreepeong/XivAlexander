#include "pch.h"
#include <XivAlexanderCommon/Sqex_FontCsv_ModifiableFontCsvStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_Texture_Mipmap.h>

#include "XivAlexanderCommon/Sqex_FontCsv_Render.h"

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
	//for (const auto& it : std::filesystem::recursive_directory_iterator(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack)")) {
	//	if (it.path().extension() == ".index") {
	//		std::cout << std::format("Verifying {}...\n", it.path());
	//		Sqex::Sqpack::Reader(it, true, true);
	//	}
	//}
	const auto common = Sqex::Sqpack::Reader(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\000000.win32.index)", true, true);
	
	std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> texs;
	for (int i = 1; i <= 7; ++i) {
		auto s = common.GetEntryProvider(std::format("common/font/font{}.tex", i));
		if (!s)
			break;
		texs.emplace_back(Sqex::Texture::MipmapStream::FromTexture(std::make_shared<Sqex::Sqpack::EntryRawStream>(std::move(s)), 0));
	}

	const auto axis36 = std::make_shared<Sqex::FontCsv::SeFont>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(
		Sqex::Sqpack::EntryRawStream(common.GetEntryProvider("common/font/AXIS_36.fdt")),
		true), texs);
	const auto trump68 = std::make_shared<Sqex::FontCsv::SeFont>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(
		Sqex::Sqpack::EntryRawStream(common.GetEntryProvider("common/font/TrumpGothic_68.fdt")),
		true), texs);
	const auto jupiter90 = std::make_shared<Sqex::FontCsv::SeFont>(std::make_shared<Sqex::FontCsv::ModifiableFontCsvStream>(
		Sqex::Sqpack::EntryRawStream(common.GetEntryProvider("common/font/Jupiter_90.fdt")),
		true), texs);
	const auto font = std::make_shared<Sqex::FontCsv::CascadingFont>(std::vector<std::shared_ptr<Sqex::FontCsv::SeCompatibleFont>>{ jupiter90, trump68, axis36 });
	// const auto font = trump68;

	// const auto pszTestString = reinterpret_cast<const char*>(u8"WA\nW\u200cA");
	const auto color = Sqex::Texture::RGBA8888(255, 200, 150);
	const auto bbox = font->MeasureAndDraw(nullptr, 0, 0, pszTestString, color);
	const auto targetMipmap = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(
		static_cast<uint16_t>(bbox.right - bbox.left),
		static_cast<uint16_t>(bbox.bottom - bbox.top),
		Sqex::Texture::CompressionType::ARGB_1,
		std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * (size_t() + bbox.right - bbox.left) * (size_t() + bbox.bottom - bbox.top))
		);
	font->MeasureAndDraw(targetMipmap.get(), 0, 0, pszTestString, color);

	targetMipmap->Show();
	return 0;
}
