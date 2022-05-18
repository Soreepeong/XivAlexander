#include <iostream>
#include <Windows.h>
#include <windowsx.h>

#include "XivRes/FontdataStream.h"
#include "XivRes/GameReader.h"
#include "XivRes/MipmapStream.h"
#include "XivRes/PackedFileUnpackingStream.h"
#include "XivRes/PixelFormats.h"
#include "XivRes/TextureStream.h"
#include "XivRes/FontGenerator/CodepointLimitingFixedSizeFont.h"
#include "XivRes/FontGenerator/FontdataPacker.h"
#include "XivRes/FontGenerator/GameFontdataFixedSizeFont.h"
#include "XivRes/FontGenerator/TextMeasurer.h"
#include "XivRes/Internal/TexturePreview.Windows.h"

static const auto* const pszTestString = (
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
	u8"Chinese: 天地玄黄，宇宙洪荒。\n"
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
	);

int main() {
	std::vector<char> tmp;
	system("chcp 65001");

	XivRes::GameReader gameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
	XivRes::GameReader gameReaderCn(R"(C:\Program Files (x86)\SNDA\FFXIV\game)");
	XivRes::GameReader gameReaderKr(R"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game)");

	std::vector<std::shared_ptr<XivRes::MemoryMipmapStream>> font_tex;
	try {
		for (int i = 1; ; i++)
			font_tex.emplace_back(XivRes::MemoryMipmapStream::AsARGB8888(*XivRes::TextureStream(gameReader.GetFileStream(std::format("common/font/font{}.tex", i))).GetMipmap(0, 0)));
	} catch (const std::out_of_range&) {
		// do nothing
	}

	std::vector<std::shared_ptr<XivRes::MemoryMipmapStream>> font_krn_tex;
	try {
		for (int i = 1; ; i++)
			font_krn_tex.emplace_back(XivRes::MemoryMipmapStream::AsARGB8888(*XivRes::TextureStream(gameReaderKr.GetFileStream(std::format("common/font/font_krn_{}.tex", i))).GetMipmap(0, 0)));
	} catch (const std::out_of_range&) {
		// do nothing
	}

	XivRes::FontGenerator::FontdataPacker packer;
	std::set<char32_t> interestedCodepoints;
	for (char32_t i = 0x20; i < 0x80; i++)
		interestedCodepoints.insert(i);
	for (char32_t i = U'ㄱ'; i <= 'ㅡ'; i++)
		interestedCodepoints.insert(i);
	for (char32_t i = U'가'; i <= '힣'; i++)
		interestedCodepoints.insert(i);

	packer.AddFont(
		std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(
			std::make_shared<XivRes::FontGenerator::GameFontdataFixedSizeFont>(
				std::make_shared<XivRes::FontdataStream>(*gameReader.GetFileStream("common/font/AXIS_18.fdt")),
				font_tex),
			interestedCodepoints));

	packer.AddFont(
		std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(
			std::make_shared<XivRes::FontGenerator::GameFontdataFixedSizeFont>(
				std::make_shared<XivRes::FontdataStream>(*gameReaderKr.GetFileStream("common/font/KrnAXIS_180.fdt")),
				font_krn_tex),
			interestedCodepoints));

	//for (const char* pszPath : {
	//	"common/font/AXIS_96.fdt",
	//	"common/font/AXIS_12.fdt",
	//	"common/font/AXIS_14.fdt",
	//	"common/font/AXIS_18.fdt",
	//	"common/font/AXIS_36.fdt",
	//	"common/font/Jupiter_16.fdt",
	//	"common/font/Jupiter_20.fdt",
	//	"common/font/Jupiter_23.fdt",
	//	"common/font/Jupiter_45.fdt",
	//	"common/font/Jupiter_46.fdt",
	//	"common/font/Jupiter_90.fdt",
	//	"common/font/Meidinger_16.fdt",
	//	"common/font/Meidinger_20.fdt",
	//	"common/font/Meidinger_40.fdt",
	//	"common/font/MiedingerMid_10.fdt",
	//	"common/font/MiedingerMid_12.fdt",
	//	"common/font/MiedingerMid_14.fdt",
	//	"common/font/MiedingerMid_18.fdt",
	//	"common/font/MiedingerMid_36.fdt",
	//	"common/font/TrumpGothic_184.fdt",
	//	"common/font/TrumpGothic_23.fdt",
	//	"common/font/TrumpGothic_34.fdt",
	//	"common/font/TrumpGothic_68.fdt",
	//	}) {
	//	packer.AddFont(
	//		std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(
	//			std::make_shared<XivRes::FontGenerator::GameFontdataFixedSizeFont>(
	//				std::make_shared<XivRes::FontdataStream>(*gameReader.GetFileStream(pszPath)),
	//				streams),
	//			interestedCodepoints));
	//}

	{
		std::vector<std::thread> threads;

		auto [fdts, texs] = packer.Compile();
		auto res = std::make_shared<XivRes::TextureStream>(texs[0]->Type, texs[0]->Width, texs[0]->Height, 1, 1, texs.size());
		for (size_t i = 0; i < texs.size(); i++)
			res->SetMipmap(0, i, texs[i]);

		threads.emplace_back([&]() { XivRes::Internal::ShowTextureStream(*res); });

		auto face = std::make_shared<XivRes::FontGenerator::GameFontdataFixedSizeFont>(fdts[1], texs);
		auto tex = XivRes::FontGenerator::TextMeasurer(*face)
			.WithMaxWidth(1024)
			.Measure(pszTestString)
			.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200))
			->ToSingleTextureStream();
		threads.emplace_back([&]() { XivRes::Internal::ShowTextureStream(*tex); });

		for (auto& t : threads)
			t.join();
	}
	return 0;
}
