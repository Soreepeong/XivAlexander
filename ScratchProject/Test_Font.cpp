#include "pch.h"

#include <XivAlexanderCommon/Sqex_FontCsv_CreateConfig.h>
#include <XivAlexanderCommon/Sqex_FontCsv_Creator.h>
#include <XivAlexanderCommon/Sqex_FontCsv_DirectWriteFont.h>
#include <XivAlexanderCommon/Sqex_FontCsv_FreeTypeFont.h>
#include <XivAlexanderCommon/Sqex_FontCsv_GdiFont.h>
#include <XivAlexanderCommon/Sqex_FontCsv_ModifiableFontCsvStream.h>
#include <XivAlexanderCommon/Sqex_FontCsv_SeCompatibleDrawableFont.h>
#include <XivAlexanderCommon/Sqex_FontCsv_SeCompatibleFont.h>
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
	u8"“elemental”"
);

template<bool Create>
void test_showcase(const char* testString = nullptr) {
	if (!testString)
		testString = pszTestString;

	const auto cw = 1024, ch = 1024;

	for (const auto fsize : {12}) {
		for (const auto fname : {L"Gulim"}) {
			auto lf = LOGFONTW{
				.lfHeight = -static_cast<int>(std::round(fsize)),
				.lfCharSet = DEFAULT_CHARSET,
			};
			wcsncpy_s(lf.lfFaceName, fname, 8);
			using T = std::conditional_t<Create, uint8_t, Sqex::Texture::RGBA8888>;

			std::shared_ptr<Sqex::Texture::MemoryBackedMipmap> mm32a = nullptr;

			for (const auto& [desc, fs, color] : std::vector<std::tuple<std::string, std::shared_ptr<Sqex::FontCsv::SeCompatibleDrawableFont<T>>, Sqex::Texture::RGBA8888>>{
					std::make_tuple("FreeType", std::make_shared<Sqex::FontCsv::FreeTypeDrawingFont<T>>(fname, fsize), Sqex::Texture::RGBA8888(255, 0, 0, 255)),
					std::make_tuple("GDI", std::make_shared<Sqex::FontCsv::GdiDrawingFont<T>>(lf), Sqex::Texture::RGBA8888(0, 255, 0, 255)),
					std::make_tuple("DirectWrite", std::make_shared<Sqex::FontCsv::DirectWriteDrawingFont<T>>(fname, fsize, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_RENDERING_MODE_NATURAL), Sqex::Texture::RGBA8888(0, 0, 255, 255)),
				}) {
				std::shared_ptr<Sqex::FontCsv::SeCompatibleDrawableFont<>> f;
				if constexpr (Create) {
					auto creator = Sqex::FontCsv::FontCsvCreator(Utils::Win32::Semaphore::Create(nullptr, 4, 4));
					creator.SizePoints = fs->Size();
					creator.AscentPixels = fs->Ascent();
					creator.LineHeightPixels = fs->LineHeight();
					creator.BorderOpacity = 0;
					creator.BorderThickness = 0;
					for (const auto c : Sqex::FontCsv::ToU32(testString))
						creator.AddCharacter(c, fs.get());
					Sqex::FontCsv::FontCsvCreator::RenderTarget target(4096, 4096, 1);
					creator.Step2_Layout(target);
					creator.Step3_Draw(target);
					const auto newFontCsv = creator.GetResult();
					target.Finalize();

					const auto newFontMipmaps = target.AsMipmapStreamVector();
					f = std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(newFontCsv, newFontMipmaps);
				} else
					f = fs;

				const auto size = f->Measure(0, 0, testString);
				/*const auto cw = static_cast<uint16_t>(size.Width() + 32);
				const auto ch = static_cast<uint16_t>(size.Height() + 32);*/
				const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(cw, ch, Sqex::Texture::Format::RGBA_1, std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * cw * ch));
				std::fill_n(mm32->View<uint32_t>().begin(), mm32->Width() * mm32->Height(), 0x80000000);
				for (auto i = 16; i < ch - 16; ++i)
					for (auto j = 16; j < cw - 16; ++j)
						mm32->View<uint32_t>()[i * cw + j] = 0xFF000000;
				if (!mm32a) {
					mm32a = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(cw, ch, Sqex::Texture::Format::RGBA_1, std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * cw * ch));
					std::fill_n(mm32a->View<uint32_t>().begin(), mm32a->Width() * mm32a->Height(), 0x80000000);
					for (auto i = 16; i < ch - 16; ++i)
						for (auto j = 16; j < cw - 16; ++j)
							mm32a->View<uint32_t>()[i * cw + j] = 0xFF000000;
				}

				void(f->Draw(mm32.get(), 16 - size.left, 16 - size.top, testString, 0xFFFFFFFF, 0));
				{
					const auto vt = mm32a->View<uint32_t>();
					const auto vs = mm32->View<uint32_t>();
					for (size_t i = 0; i < vt.size() && i < vs.size(); ++i) {
						vt[i] = (vt[i] & ~color.Value) | (vs[i] & color.Value);
					}
				}
				mm32->Show(std::format("{}, {}, {}", fname, fsize, desc));
			}
			mm32a->Show(std::format("{}, {}, All", fname, fsize));
		}
	}
}

void compile() {
	Sqex::FontCsv::FontSetsCreator::ResultFontSets result;
	bool isArgb32;
	try {
		// std::ifstream fin(R"(..\StaticData\FontConfig\International.Original.json)");
		std::ifstream fin(R"(..\StaticData\FontConfig\International.Gulim.dwrite.json)");
		// std::ifstream fin(R"(..\StaticData\FontConfig\International.Gulim.gdi.json)");
		// std::ifstream fin(R"(..\StaticData\FontConfig\International.Gulimche.dwrite_file.json)");
		// std::ifstream fin(R"(..\StaticData\FontConfig\International.ComicGulim.json)");
		// std::ifstream fin(R"(..\StaticData\FontConfig\International.ComicSans.freetype.border.json)");
		// std::ifstream fin(R"(..\StaticData\FontConfig\International.ComicSans.json)");
		// std::ifstream fin(R"(..\StaticData\FontConfig\International.PapyrusGungsuh.json)");
		// std::ifstream fin(R"(..\StaticData\FontConfig\International.WithMinimalHangul.json)");
		// std::ifstream fin(R"(..\StaticData\FontConfig\International.WithMinimalHangul.Border.json)");
		// std::ifstream fin(R"(..\StaticData\FontConfig\Korean.Original.json)");
		// std::ifstream fin(R"(..\StaticData\FontConfig\Korean.Border36.json)");
		nlohmann::json j;
		fin >> j;
		auto cfg = j.get<Sqex::FontCsv::CreateConfig::FontCreateConfig>();
		isArgb32 = cfg.textureFormat != Sqex::Texture::Format::RGBA4444;

		Sqex::FontCsv::FontSetsCreator creator(cfg, R"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\)");
		// Sqex::FontCsv::FontSetsCreator creator(cfg, R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
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
		return;
	}

	for (const auto& fontSet : result.Result | std::views::values) {
		std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> newMipmaps;
		for (const auto& texture : fontSet.Textures) {
			auto mipmap = Sqex::Texture::MipmapStream::FromTexture(texture, 0);
			mipmap->Show();
			newMipmaps.emplace_back(std::move(mipmap));
		}

		if (isArgb32) {
			for (const auto& [fontName, newFontCsv] : fontSet.Fonts) {
				const auto newFont = std::make_shared<Sqex::FontCsv::SeDrawableFont<Sqex::Texture::RGBA8888>>(newFontCsv, newMipmaps);
				{
					const auto lines = Utils::StringSplit<std::string>(pszTestString, "\n");
					const auto cw = static_cast<uint16_t>(newFont->Measure(5, 5, pszTestString).Width() + 10);
					const auto ch = static_cast<uint16_t>(5 * (lines.size() + 1) + newFont->LineHeight() * lines.size());
					const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(cw, ch, Sqex::Texture::Format::RGBA_1, std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * cw * ch));
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
							mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + newFont->LineHeight()) + i].SetFrom(0, 255, 0, 255);
						}
						newFont->Draw(mm32.get(), 5, yptr, line, 0xFFFFFFFF, 0);
						yptr += newFont->LineHeight();
						for (SSIZE_T i = 0; i < cw; ++i)
							mm32->View<Sqex::Texture::RGBA8888>()[cw * yptr + i].SetFrom(0, 0, 255, 255);
						yptr += 5;
					}

					mm32->Show(fontName);
				}
			}
		} else {
			for (const auto& [fontName, newFontCsv] : fontSet.Fonts) {
				const auto newFont = std::make_shared<Sqex::FontCsv::SeDrawableFont<>>(newFontCsv, newMipmaps);
				{
					const auto lines = Utils::StringSplit<std::string>(pszTestString, "\n");
					const auto cw = static_cast<uint16_t>(newFont->Measure(5, 5, pszTestString).Width() + 10);
					const auto ch = static_cast<uint16_t>(5 * (lines.size() + 1) + newFont->LineHeight() * lines.size());
					const auto mm32 = std::make_shared<Sqex::Texture::MemoryBackedMipmap>(cw, ch, Sqex::Texture::Format::RGBA_1, std::vector<uint8_t>(sizeof Sqex::Texture::RGBA8888 * cw * ch));
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
							mm32->View<Sqex::Texture::RGBA8888>()[cw * (yptr + newFont->LineHeight()) + i].SetFrom(0, 255, 0, 255);
						}
						newFont->Draw(mm32.get(), 5, yptr, line, 0xFFFFFFFF, 0);
						yptr += newFont->LineHeight();
						for (SSIZE_T i = 0; i < cw; ++i)
							mm32->View<Sqex::Texture::RGBA8888>()[cw * yptr + i].SetFrom(0, 0, 255, 255);
						yptr += 5;
					}

					mm32->Show(fontName);
				}
			}
		}
	}
}

int main() {
	system("chcp 65001");
	// test_showcase<true>();
	// test_direct();
	compile();
	return 0;
}
