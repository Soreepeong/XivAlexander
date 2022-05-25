#include "pch.h"
#include "resource.h"

#include "XivRes/BinaryPackedFileStream.h"
#include "XivRes/TexturePackedFileStream.h"
#include "XivRes/FontGenerator/WrappingFixedSizeFont.h"
#include "XivRes/FontGenerator/DirectWriteFixedSizeFont.h"
#include "XivRes/FontGenerator/FontdataPacker.h"
#include "XivRes/FontGenerator/FreeTypeFixedSizeFont.h"
#include "XivRes/FontGenerator/GameFontdataFixedSizeFont.h"
#include "XivRes/FontGenerator/MergedFixedSizeFont.h"
#include "XivRes/FontGenerator/TextMeasurer.h"
#include "XivRes/FontGenerator/WrappingFixedSizeFont.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

HINSTANCE g_hInstance;

_COM_SMARTPTR_TYPEDEF(IFileSaveDialog, __uuidof(IFileSaveDialog));
_COM_SMARTPTR_TYPEDEF(IFileOpenDialog, __uuidof(IFileOpenDialog));
_COM_SMARTPTR_TYPEDEF(IShellItem, __uuidof(IShellItem));
_COM_SMARTPTR_TYPEDEF(IShellItemArray, __uuidof(IShellItemArray));

struct ListViewCols {
	enum : int {
		FamilyName,
		SubfamilyName,
		Size,
		LineHeight,
		Ascent,
		HorizontalOffset,
		LetterSpacing,
		Gamma,
		Codepoints,
		GlyphCount,
		Overwrite,
		Renderer,
		Lookup,
	};
};

static std::wstring GetWindowString(HWND hwnd) {
	std::wstring buf(GetWindowTextLengthW(hwnd) + static_cast<size_t>(1), L'\0');
	buf.resize(GetWindowTextW(hwnd, &buf[0], static_cast<int>(buf.size())));
	return buf;
}

static float GetWindowFloat(HWND hwnd) {
	return std::wcstof(GetWindowString(hwnd).c_str(), nullptr);
}

static void SetWindowFloat(HWND hwnd, float v) {
	SetWindowTextW(hwnd, std::format(L"{:g}", v).c_str());
}

static int GetWindowInt(HWND hwnd) {
	return std::wcstol(GetWindowString(hwnd).c_str(), nullptr, 0);
}

static void SetWindowInt(HWND hwnd, int v) {
	SetWindowTextW(hwnd, std::format(L"{}", v).c_str());
}

static std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont> GetGameFont(XivRes::FontGenerator::GameFontFamily family, float size) {
	static std::map<XivRes::GameFontType, XivRes::FontGenerator::GameFontdataSet> s_fontSet;
	static std::mutex s_mtx;

	const auto lock = std::lock_guard(s_mtx);

	std::shared_ptr<XivRes::FontGenerator::GameFontdataSet> strong;

	auto pathconf = nlohmann::json::object();
	pathconf["global"] = nlohmann::json::array({ R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)" });
	pathconf["chinese"] = nlohmann::json::array({
		R"(C:\Program Files (x86)\SNDA\FFXIV\game)",
		reinterpret_cast<const char*>(u8R"(C:\Program Files (x86)\上海数龙科技有限公司\最终幻想XIV\)"),
		});
	pathconf["korean"] = nlohmann::json::array({ R"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game)" });

	try {
		if (!exists(std::filesystem::path("config.json"))) {
			std::ofstream out("config.json");
			out << pathconf;
		} else {
			std::ifstream in("config.json");
			pathconf = nlohmann::json::parse(in);
		}
	} catch (...) {

	}

	try {
		switch (family) {
			case XivRes::FontGenerator::GameFontFamily::AXIS:
			case XivRes::FontGenerator::GameFontFamily::Jupiter:
			case XivRes::FontGenerator::GameFontFamily::JupiterN:
			case XivRes::FontGenerator::GameFontFamily::MiedingerMid:
			case XivRes::FontGenerator::GameFontFamily::Meidinger:
			case XivRes::FontGenerator::GameFontFamily::TrumpGothic:
			{
				auto& font = s_fontSet[XivRes::GameFontType::font];
				if (!font) {
					for (const auto& path : pathconf["global"]) {
						try {
							font = XivRes::GameReader(path.get<std::string>()).GetFonts(XivRes::GameFontType::font);
						} catch (...) {
						}
					}
					if (!font)
						throw std::runtime_error("Font not found in given path");
				}
				return font.GetFont(family, size);
			}

			case XivRes::FontGenerator::GameFontFamily::ChnAXIS:
			{
				auto& font = s_fontSet[XivRes::GameFontType::chn_axis];
				if (!font) {
					for (const auto& path : pathconf["chinese"]) {
						try {
							font = XivRes::GameReader(path.get<std::string>()).GetFonts(XivRes::GameFontType::chn_axis);
						} catch (...) {
						}
					}
					if (!font)
						throw std::runtime_error("Font not found in given path");
				}
				return font.GetFont(family, size);
			}

			case XivRes::FontGenerator::GameFontFamily::KrnAXIS:
			{
				auto& font = s_fontSet[XivRes::GameFontType::krn_axis];
				if (!font) {
					for (const auto& path : pathconf["korean"]) {
						try {
							font = XivRes::GameReader(path.get<std::string>()).GetFonts(XivRes::GameFontType::krn_axis);
						} catch (...) {
						}
					}
					if (!font)
						throw std::runtime_error("Font not found in given path");
				}
				return font.GetFont(family, size);
			}
		}
	} catch (const std::exception& e) {
		static bool showed = false;
		if (!showed) {
			showed = true;
			MessageBoxW(nullptr, std::format(
				L"Failed to find corresponding game installation ({}). Specify it in config.json. Delete config.json and run this program again to start anew. Suppressing this message from now on.",
				XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), L"Error", MB_OK);
		}
	}

	return std::make_shared<XivRes::FontGenerator::EmptyFixedSizeFont>(size, XivRes::FontGenerator::EmptyFixedSizeFont::CreateStruct{});
}

struct FontSet {
	struct Face {
		struct Element {
			enum class RendererEnum : uint8_t {
				Empty,
				PrerenderedGameInstallation,
				DirectWrite,
				FreeType,
			};

			struct EmptyFontDef {
				int Ascent = 0;
				int LineHeight = 0;
			};

			struct FontLookupStruct {
				std::string Name;
				DWRITE_FONT_WEIGHT Weight = DWRITE_FONT_WEIGHT_REGULAR;
				DWRITE_FONT_STRETCH Stretch = DWRITE_FONT_STRETCH_NORMAL;
				DWRITE_FONT_STYLE Style = DWRITE_FONT_STYLE_NORMAL;

				std::wstring GetWeightString() const {
					switch (Weight) {
						case DWRITE_FONT_WEIGHT_THIN: return L"Thin";
						case DWRITE_FONT_WEIGHT_EXTRA_LIGHT: return L"Extra Light";
						case DWRITE_FONT_WEIGHT_LIGHT: return L"Light";
						case DWRITE_FONT_WEIGHT_SEMI_LIGHT: return L"Semi Light";
						case DWRITE_FONT_WEIGHT_NORMAL: return L"Normal";
						case DWRITE_FONT_WEIGHT_MEDIUM: return L"Medium";
						case DWRITE_FONT_WEIGHT_SEMI_BOLD: return L"Semi Bold";
						case DWRITE_FONT_WEIGHT_BOLD: return L"Bold";
						case DWRITE_FONT_WEIGHT_EXTRA_BOLD:return L"Extra Bold";
						case DWRITE_FONT_WEIGHT_BLACK: return L"Black";
						case DWRITE_FONT_WEIGHT_EXTRA_BLACK: return L"Extra Black";
						default: return std::format(L"{}", static_cast<int>(Weight));
					}
				}

				std::wstring GetStretchString() const {
					switch (Stretch) {
						case DWRITE_FONT_STRETCH_UNDEFINED: return L"Undefined";
						case DWRITE_FONT_STRETCH_ULTRA_CONDENSED: return L"Ultra Condensed";
						case DWRITE_FONT_STRETCH_EXTRA_CONDENSED: return L"Extra Condensed";
						case DWRITE_FONT_STRETCH_CONDENSED: return L"Condensed";
						case DWRITE_FONT_STRETCH_SEMI_CONDENSED: return L"Semi Condensed";
						case DWRITE_FONT_STRETCH_NORMAL: return L"Normal";
						case DWRITE_FONT_STRETCH_SEMI_EXPANDED: return L"Semi Expanded";
						case DWRITE_FONT_STRETCH_EXPANDED: return L"Expanded";
						case DWRITE_FONT_STRETCH_EXTRA_EXPANDED: return L"Extra Expanded";
						case DWRITE_FONT_STRETCH_ULTRA_EXPANDED: return L"Ultra Expanded";
						default: return L"Invalid";
					}
				}

				std::wstring GetStyleString() const {
					switch (Style) {
						case DWRITE_FONT_STYLE_NORMAL: return L"Normal";
						case DWRITE_FONT_STYLE_OBLIQUE: return L"Oblique";
						case DWRITE_FONT_STYLE_ITALIC: return L"Italic";
						default: return L"Invalid";
					}
				}

				std::pair<IDWriteFactoryPtr, IDWriteFontPtr> ResolveFont() const {
					using namespace XivRes::FontGenerator;

					IDWriteFactoryPtr factory;
					SuccessOrThrow(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)));

					IDWriteFontCollectionPtr coll;
					SuccessOrThrow(factory->GetSystemFontCollection(&coll));

					uint32_t index;
					BOOL exists;
					SuccessOrThrow(coll->FindFamilyName(XivRes::Unicode::Convert<std::wstring>(Name).c_str(), &index, &exists));
					if (!exists)
						throw std::invalid_argument("Font not found");

					IDWriteFontFamilyPtr family;
					SuccessOrThrow(coll->GetFontFamily(index, &family));

					IDWriteFontPtr font;
					SuccessOrThrow(family->GetFirstMatchingFont(Weight, Stretch, Style, &font));

					return std::make_pair(std::move(factory), std::move(font));
				}

				std::pair<std::shared_ptr<XivRes::IStream>, int> ResolveStream() const {
					using namespace XivRes::FontGenerator;

					auto [factory, font] = ResolveFont();

					IDWriteFontFacePtr face;
					SuccessOrThrow(font->CreateFontFace(&face));

					IDWriteFontFile* pFontFileTmp;
					uint32_t nFiles = 1;
					SuccessOrThrow(face->GetFiles(&nFiles, &pFontFileTmp));
					IDWriteFontFilePtr file(pFontFileTmp, false);

					IDWriteFontFileLoaderPtr loader;
					SuccessOrThrow(file->GetLoader(&loader));

					void const* refKey;
					UINT32 refKeySize;
					SuccessOrThrow(file->GetReferenceKey(&refKey, &refKeySize));

					IDWriteFontFileStreamPtr stream;
					SuccessOrThrow(loader->CreateStreamFromKey(refKey, refKeySize, &stream));

					auto res = std::make_shared<XivRes::MemoryStream>();
					uint64_t fileSize;
					SuccessOrThrow(stream->GetFileSize(&fileSize));
					const void* pFragmentStart;
					void* pFragmentContext;
					SuccessOrThrow(stream->ReadFileFragment(&pFragmentStart, 0, fileSize, &pFragmentContext));
					std::vector<uint8_t> buf(static_cast<size_t>(fileSize));
					memcpy(&buf[0], pFragmentStart, buf.size());
					stream->ReleaseFileFragment(pFragmentContext);

					return { std::make_shared<XivRes::MemoryStream>(std::move(buf)), face->GetIndex() };
				}
			};

			struct RendererSpecificStruct {
				XivRes::FontGenerator::EmptyFixedSizeFont::CreateStruct Empty;
				XivRes::FontGenerator::FreeTypeFixedSizeFont::CreateStruct FreeType;
				XivRes::FontGenerator::DirectWriteFixedSizeFont::CreateStruct DirectWrite;
			};

			std::shared_ptr<void*> RuntimeTag;
			std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont> BaseFont;
			std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont> Font;

			float Size = 0.f;
			float Gamma = 1.f;
			bool Overwrite = false;
			XivRes::FontGenerator::WrapModifiers WrapModifiers;
			RendererEnum Renderer = RendererEnum::Empty;

			FontLookupStruct Lookup;
			RendererSpecificStruct RendererSpecific;

			const std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>& EnsureFont() {
				if (!Font)
					RefreshFont();

				return Font;
			}

			void RefreshFont() {
				if (!BaseFont)
					return RefreshBaseFont();

				Font = std::make_shared<XivRes::FontGenerator::WrappingFixedSizeFont>(BaseFont, WrapModifiers);
			}

			void RefreshBaseFont() {
				try {
					switch (Renderer) {
						case RendererEnum::Empty:
							BaseFont = std::make_shared<XivRes::FontGenerator::EmptyFixedSizeFont>(Size, RendererSpecific.Empty);
							break;

						case RendererEnum::PrerenderedGameInstallation:
							if (Lookup.Name == "AXIS")
								BaseFont = GetGameFont(XivRes::FontGenerator::GameFontFamily::AXIS, Size);
							else if (Lookup.Name == "Jupiter")
								BaseFont = GetGameFont(XivRes::FontGenerator::GameFontFamily::Jupiter, Size);
							else if (Lookup.Name == "JupiterN")
								BaseFont = GetGameFont(XivRes::FontGenerator::GameFontFamily::JupiterN, Size);
							else if (Lookup.Name == "Meidinger")
								BaseFont = GetGameFont(XivRes::FontGenerator::GameFontFamily::Meidinger, Size);
							else if (Lookup.Name == "MiedingerMid")
								BaseFont = GetGameFont(XivRes::FontGenerator::GameFontFamily::MiedingerMid, Size);
							else if (Lookup.Name == "TrumpGothic")
								BaseFont = GetGameFont(XivRes::FontGenerator::GameFontFamily::TrumpGothic, Size);
							else if (Lookup.Name == "ChnAXIS")
								BaseFont = GetGameFont(XivRes::FontGenerator::GameFontFamily::ChnAXIS, Size);
							else if (Lookup.Name == "KrnAXIS")
								BaseFont = GetGameFont(XivRes::FontGenerator::GameFontFamily::KrnAXIS, Size);
							else
								throw std::runtime_error("Invalid name");
							break;

						case RendererEnum::DirectWrite:
						{
							auto [factory, font] = Lookup.ResolveFont();
							BaseFont = std::make_shared<XivRes::FontGenerator::DirectWriteFixedSizeFont>(std::move(factory), std::move(font), Size, Gamma, RendererSpecific.DirectWrite);
							break;
						}

						case RendererEnum::FreeType:
						{
							auto [pStream, index] = Lookup.ResolveStream();
							BaseFont = std::make_shared<XivRes::FontGenerator::FreeTypeFixedSizeFont>(*pStream, index, Size, Gamma, RendererSpecific.FreeType);
							break;
						}

						default:
							BaseFont = std::make_shared<XivRes::FontGenerator::EmptyFixedSizeFont>();
							break;
					}

				} catch (...) {
					BaseFont = std::make_shared<XivRes::FontGenerator::EmptyFixedSizeFont>(Size, RendererSpecific.Empty);
				}
				RefreshFont();
			}

			void UpdateText(HWND hListView, int nListIndex) {
				if (!Font)
					RefreshFont();

				std::wstring buf;
				ListView_SetItemText(hListView, nListIndex, ListViewCols::FamilyName, &(buf = XivRes::Unicode::Convert<std::wstring>(Font->GetFamilyName()))[0]);
				ListView_SetItemText(hListView, nListIndex, ListViewCols::SubfamilyName, &(buf = XivRes::Unicode::Convert<std::wstring>(Font->GetSubfamilyName()))[0]);
				if (std::fabsf(Font->GetSize() - Size) >= 0.01f) {
					ListView_SetItemText(hListView, nListIndex, ListViewCols::Size, &(buf = std::format(L"{:g}px (req. {:g}px)", Font->GetSize(), Size))[0]);
				} else {
					ListView_SetItemText(hListView, nListIndex, ListViewCols::Size, &(buf = std::format(L"{:g}px", Font->GetSize()))[0]);
				}
				ListView_SetItemText(hListView, nListIndex, ListViewCols::LineHeight, &(buf = std::format(L"{}px", Font->GetLineHeight()))[0]);
				if (WrapModifiers.BaselineShift && Renderer != RendererEnum::Empty) {
					ListView_SetItemText(hListView, nListIndex, ListViewCols::Ascent, &(buf = std::format(L"{}px({:+}px)", BaseFont->GetAscent(), WrapModifiers.BaselineShift))[0]);
				} else {
					ListView_SetItemText(hListView, nListIndex, ListViewCols::Ascent, &(buf = std::format(L"{}px", BaseFont->GetAscent()))[0]);
				}
				ListView_SetItemText(hListView, nListIndex, ListViewCols::HorizontalOffset, &(buf = std::format(L"{}px", Renderer == RendererEnum::Empty ? 0 : WrapModifiers.HorizontalOffset))[0]);
				ListView_SetItemText(hListView, nListIndex, ListViewCols::LetterSpacing, &(buf = std::format(L"{}px", Renderer == RendererEnum::Empty ? 0 : WrapModifiers.LetterSpacing))[0]);
				ListView_SetItemText(hListView, nListIndex, ListViewCols::Codepoints, &(buf = GetRangeRepresentation())[0]);
				ListView_SetItemText(hListView, nListIndex, ListViewCols::GlyphCount, &(buf = std::format(L"{}", Font->GetAllCodepoints().size()))[0]);
				ListView_SetItemText(hListView, nListIndex, ListViewCols::Overwrite, &(buf = Overwrite ? L"Yes" : L"No")[0]);
				ListView_SetItemText(hListView, nListIndex, ListViewCols::Gamma, &(buf = std::format(L"{:g}", Gamma))[0]);
				ListView_SetItemText(hListView, nListIndex, ListViewCols::Renderer, &(buf = GetRendererRepresentation())[0]);
				ListView_SetItemText(hListView, nListIndex, ListViewCols::Lookup, &(buf = GetLookupRepresentation())[0]);
			}

			std::wstring GetRangeRepresentation() const {
				if (WrapModifiers.Codepoints.empty())
					return L"(None)";

				std::wstring res;
				std::vector<char32_t> charVec(BaseFont->GetAllCodepoints().begin(), BaseFont->GetAllCodepoints().end());
				for (const auto& [c1, c2] : WrapModifiers.Codepoints) {
					if (!res.empty())
						res += L", ";

					const auto left = std::ranges::lower_bound(charVec, c1);
					const auto right = std::ranges::upper_bound(charVec, c2);
					const auto count = right - left;

					const auto blk = std::lower_bound(XivRes::Unicode::UnicodeBlocks::Blocks.begin(), XivRes::Unicode::UnicodeBlocks::Blocks.end(), c1, [](const auto& l, const auto& r) { return l.First < r; });
					if (blk != XivRes::Unicode::UnicodeBlocks::Blocks.end() && blk->First == c1 && blk->Last == c2) {
						res += std::format(L"{}({})", XivRes::Unicode::Convert<std::wstring>(blk->Name), count);
					} else if (c1 == c2) {
						res += std::format(
							L"U+{:04X} [{}]",
							static_cast<uint32_t>(c1),
							XivRes::Unicode::RepresentChar<std::wstring>(c1)
						);
					} else {
						res += std::format(
							L"U+{:04X}~{:04X} ({}) {} ~ {}",
							static_cast<uint32_t>(c1),
							static_cast<uint32_t>(c2),
							count,
							XivRes::Unicode::RepresentChar<std::wstring>(c1),
							XivRes::Unicode::RepresentChar<std::wstring>(c2)
						);
					}
				}

				return res;
			}

			std::wstring GetRendererRepresentation() const {
				switch (Renderer) {
					case RendererEnum::Empty:
						return L"Empty";

					case RendererEnum::PrerenderedGameInstallation:
						return L"Prerendered (Game)";

					case RendererEnum::DirectWrite:
						return std::format(L"DirectWrite (Render={}, Measure={}, GridFit={})",
							RendererSpecific.DirectWrite.GetRenderModeString(),
							RendererSpecific.DirectWrite.GetMeasuringModeString(),
							RendererSpecific.DirectWrite.GetGridFitModeString()
						);

					case RendererEnum::FreeType:
						return std::format(L"FreeType ({})", RendererSpecific.FreeType.GetLoadFlagsString());

					default:
						return L"INVALID";
				}
			}

			std::wstring GetLookupRepresentation() const {
				switch (Renderer) {
					case RendererEnum::DirectWrite:
					case RendererEnum::FreeType:
						return std::format(L"{} ({}, {}, {})",
							XivRes::Unicode::Convert<std::wstring>(Lookup.Name),
							Lookup.GetWeightString(),
							Lookup.GetStyleString(),
							Lookup.GetStretchString()
						);

					default:
						return L"-";
				}
			}

			class EditorDialog;
		};

		std::shared_ptr<void*> RuntimeTag;
		std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont> MergedFont;

		std::string Name;
		std::vector<Element> Elements;
		std::string PreviewText;

		const std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>& EnsureFont() {
			if (!MergedFont)
				RefreshFont();

			return MergedFont;
		}

		void RefreshFont() {
			std::vector<std::pair<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>, bool>> mergeFontList;

			for (auto& e : Elements)
				mergeFontList.emplace_back(e.EnsureFont(), e.Overwrite);

			MergedFont = std::make_shared<XivRes::FontGenerator::MergedFixedSizeFont>(std::move(mergeFontList));
		}
	};

	std::string TexFilenameFormat;
	std::vector<Face> Faces;

	void ConsolidateFonts();
};

void to_json(nlohmann::json& json, const XivRes::FontGenerator::WrapModifiers& value) {
	json = nlohmann::json::object();
	auto& codepoints = json["codepoints"] = nlohmann::json::array();
	for (const auto& c : value.Codepoints)
		codepoints.emplace_back(nlohmann::json::array({ static_cast<uint32_t>(c.first), static_cast<uint32_t>(c.second) }));
	json["letterSpacing"] = value.LetterSpacing;
	json["horizontalOffset"] = value.HorizontalOffset;
	json["baselineShift"] = value.BaselineShift;
}

void from_json(const nlohmann::json& json, XivRes::FontGenerator::WrapModifiers& value) {
	if (!json.is_object()) {
		value = {};
		return;
	}

	value.Codepoints.clear();
	if (const auto it = json.find("codepoints"); it != json.end() && it->is_array()) {
		for (const auto& v : *it) {
			if (!v.is_array())
				continue;
			switch (v.size()) {
				case 0:
					break;
				case 1:
					value.Codepoints.emplace_back(static_cast<char32_t>(v[0].get<uint32_t>()), static_cast<char32_t>(v[0].get<uint32_t>()));
					break;
				default:
					value.Codepoints.emplace_back(static_cast<char32_t>(v[0].get<uint32_t>()), static_cast<char32_t>(v[1].get<uint32_t>()));
					break;
			}
		}
	}

	value.LetterSpacing = json.value<int>("letterSpacing", 0);
	value.HorizontalOffset = json.value<int>("horizontalOffset", 0);
	value.BaselineShift = json.value<int>("baselineShift", 0);
}

void to_json(nlohmann::json& json, const FontSet::Face::Element::FontLookupStruct& value) {
	json = nlohmann::json::object();
	json["name"] = value.Name;
	json["weight"] = static_cast<int>(value.Weight);
	json["stretch"] = static_cast<int>(value.Stretch);
	json["style"] = static_cast<int>(value.Style);
}

void from_json(const nlohmann::json& json, FontSet::Face::Element::FontLookupStruct& value) {
	if (!json.is_object()) {
		value = {};
		return;
	}

	value.Name = json.value<std::string>("name", "");
	value.Weight = static_cast<DWRITE_FONT_WEIGHT>(json.value<int>("weight", DWRITE_FONT_WEIGHT_NORMAL));
	value.Stretch = static_cast<DWRITE_FONT_STRETCH>(json.value<int>("stretch", DWRITE_FONT_STRETCH_NORMAL));
	value.Style = static_cast<DWRITE_FONT_STYLE>(json.value<int>("style", DWRITE_FONT_STYLE_NORMAL));
}

void to_json(nlohmann::json& json, const FontSet::Face::Element::RendererSpecificStruct& value) {
	json = nlohmann::json::object();
	json["empty"] = nlohmann::json::object({
		{ "ascent", value.Empty.Ascent },
		{ "lineHeight", value.Empty.LineHeight },
		});
	json["freetype"] = nlohmann::json::object({
		{ "noHinting", !!(value.FreeType.LoadFlags & FT_LOAD_NO_HINTING) },
		{ "noBitmap", !!(value.FreeType.LoadFlags & FT_LOAD_NO_BITMAP) },
		{ "forceAutohint", !!(value.FreeType.LoadFlags & FT_LOAD_FORCE_AUTOHINT) },
		{ "noAutohint", !!(value.FreeType.LoadFlags & FT_LOAD_NO_AUTOHINT) },
		});
	json["directwrite"] = nlohmann::json::object({
		{ "renderMode", static_cast<int>(value.DirectWrite.RenderMode) },
		{ "measureMode", static_cast<int>(value.DirectWrite.MeasureMode) },
		{ "gridFitMode", static_cast<int>(value.DirectWrite.GridFitMode) },
		});
}

void from_json(const nlohmann::json& json, FontSet::Face::Element::RendererSpecificStruct& value) {
	if (!json.is_object()) {
		value = {};
		return;
	}

	if (const auto obj = json.find("empty"); obj != json.end() && obj->is_object()) {
		value.Empty.Ascent = obj->value<int>("ascent", 0);
		value.Empty.LineHeight = obj->value<int>("lineHeight", 0);
	} else
		value.Empty = {};
	if (const auto obj = json.find("freetype"); obj != json.end() && obj->is_object()) {
		value.FreeType.LoadFlags = 0;
		value.FreeType.LoadFlags |= obj->value<bool>("noHinting", false) ? FT_LOAD_NO_HINTING : 0;
		value.FreeType.LoadFlags |= obj->value<bool>("noBitmap", false) ? FT_LOAD_NO_BITMAP : 0;
		value.FreeType.LoadFlags |= obj->value<bool>("forceAutohint", false) ? FT_LOAD_FORCE_AUTOHINT : 0;
		value.FreeType.LoadFlags |= obj->value<bool>("noAutohint", false) ? FT_LOAD_NO_AUTOHINT : 0;
	} else
		value.FreeType = {};
	if (const auto obj = json.find("directwrite"); obj != json.end() && obj->is_object()) {
		value.DirectWrite.RenderMode = static_cast<DWRITE_RENDERING_MODE>(obj->value<int>("renderMode", DWRITE_RENDERING_MODE_DEFAULT));
		value.DirectWrite.MeasureMode = static_cast<DWRITE_MEASURING_MODE>(obj->value<int>("measureMode", DWRITE_MEASURING_MODE_GDI_NATURAL));
		value.DirectWrite.GridFitMode = static_cast<DWRITE_GRID_FIT_MODE>(obj->value<int>("gridFitMode", DWRITE_GRID_FIT_MODE_DEFAULT));
	} else
		value.DirectWrite = {};
}

void to_json(nlohmann::json& json, const FontSet::Face::Element& value) {
	json = nlohmann::json::object();
	json["size"] = value.Size;
	json["gamma"] = value.Gamma;
	json["overwrite"] = value.Overwrite;
	to_json(json["wrapModifiers"], value.WrapModifiers);
	json["renderer"] = static_cast<int>(value.Renderer);
	to_json(json["lookup"], value.Lookup);
	to_json(json["renderSpecific"], value.RendererSpecific);
}

void from_json(const nlohmann::json& json, FontSet::Face::Element& value) {
	if (!json.is_object()) {
		value = {};
		return;
	}

	value.Size = json.value<float>("size", 0.f);
	value.Gamma = json.value<float>("gamma", 1.f);
	value.Overwrite = json.value<bool>("overwrite", false);
	if (const auto it = json.find("wrapModifiers"); it != json.end())
		from_json(*it, value.WrapModifiers);
	else
		value.WrapModifiers = {};
	value.Renderer = static_cast<FontSet::Face::Element::RendererEnum>(json.value<int>("renderer", static_cast<int>(FontSet::Face::Element::RendererEnum::Empty)));
	if (const auto it = json.find("lookup"); it != json.end())
		from_json(*it, value.Lookup);
	else
		value.Lookup = {};
	if (const auto it = json.find("renderSpecific"); it != json.end())
		from_json(*it, value.RendererSpecific);
	else
		value.RendererSpecific = {};
}

void to_json(nlohmann::json& json, const FontSet::Face& value) {
	json = nlohmann::json::object();
	json["name"] = value.Name;
	auto& elements = json["elements"] = nlohmann::json::array();
	for (const auto& e : value.Elements) {
		elements.emplace_back();
		to_json(elements.back(), e);
	}
	json["previewText"] = value.PreviewText;
}

void from_json(const nlohmann::json& json, FontSet::Face& value) {
	if (!json.is_object()) {
		value = {};
		return;
	}

	value.Name = json.value<std::string>("name", "");
	value.Elements.clear();
	if (const auto it = json.find("elements"); it != json.end() && it->is_array()) {
		for (const auto& v : *it) {
			if (!v.is_object())
				continue;

			value.Elements.emplace_back();
			from_json(v, value.Elements.back());
		}
	}
	value.PreviewText = json.value<std::string>("previewText", "");
}

void to_json(nlohmann::json& json, const FontSet& value) {
	json = nlohmann::json::object();
	auto& faces = json["faces"] = nlohmann::json::array();
	for (const auto& e : value.Faces) {
		faces.emplace_back();
		to_json(faces.back(), e);
	}
	json["texFilenameFormat"] = value.TexFilenameFormat;
}

void from_json(const nlohmann::json& json, FontSet& value) {
	if (!json.is_object()) {
		value = {};
		return;
	}

	value.Faces.clear();
	if (const auto it = json.find("faces"); it != json.end() && it->is_array()) {
		for (const auto& v : *it) {
			if (!v.is_object())
				continue;

			value.Faces.emplace_back();
			from_json(v, value.Faces.back());
		}
	}
	value.TexFilenameFormat = json.value<std::string>("texFilenameFormat", "");
}

class FontSet::Face::Element::EditorDialog {
	Face::Element& m_element;
	Face::Element m_elementOriginal;
	HWND m_hParentWnd;
	bool m_bOpened = false;
	std::function<void()> m_onFontChanged;
	bool m_bBaseFontChanged = false;
	bool m_bWrappedFontChanged = false;

	enum : UINT {
		WmApp = WM_APP,
		WmFontSizeTextChanged,
	};

	struct ControlStruct {
		HWND Window;
		HWND OkButton = GetDlgItem(Window, IDOK);
		HWND CancelButton = GetDlgItem(Window, IDCANCEL);
		HWND FontRendererCombo = GetDlgItem(Window, IDC_COMBO_FONT_RENDERER);
		HWND FontCombo = GetDlgItem(Window, IDC_COMBO_FONT);
		HWND FontSizeCombo = GetDlgItem(Window, IDC_COMBO_FONT_SIZE);
		HWND FontWeightCombo = GetDlgItem(Window, IDC_COMBO_FONT_WEIGHT);
		HWND FontStyleCombo = GetDlgItem(Window, IDC_COMBO_FONT_STYLE);
		HWND FontStretchCombo = GetDlgItem(Window, IDC_COMBO_FONT_STRETCH);
		HWND EmptyAscentEdit = GetDlgItem(Window, IDC_EDIT_EMPTY_ASCENT);
		HWND EmptyLineHeightEdit = GetDlgItem(Window, IDC_EDIT_EMPTY_LINEHEIGHT);
		HWND FreeTypeNoHintingCheck = GetDlgItem(Window, IDC_CHECK_FREETYPE_NOHINTING);
		HWND FreeTypeNoBitmapCheck = GetDlgItem(Window, IDC_CHECK_FREETYPE_NOBITMAP);
		HWND FreeTypeForceAutohintCheck = GetDlgItem(Window, IDC_CHECK_FREETYPE_FORCEAUTOHINT);
		HWND FreeTypeNoAutohintCheck = GetDlgItem(Window, IDC_CHECK_FREETYPE_NOAUTOHINT);
		HWND DirectWriteRenderModeCombo = GetDlgItem(Window, IDC_COMBO_DIRECTWRITE_RENDERMODE);
		HWND DirectWriteMeasureModeCombo = GetDlgItem(Window, IDC_COMBO_DIRECTWRITE_MEASUREMODE);
		HWND DirectWriteGridFitModeCombo = GetDlgItem(Window, IDC_COMBO_DIRECTWRITE_GRIDFITMODE);
		HWND AdjustmentBaselineShiftEdit = GetDlgItem(Window, IDC_EDIT_ADJUSTMENT_BASELINESHIFT);
		HWND AdjustmentLetterSpacingEdit = GetDlgItem(Window, IDC_EDIT_ADJUSTMENT_LETTERSPACING);
		HWND AdjustmentHorizontalOffsetEdit = GetDlgItem(Window, IDC_EDIT_ADJUSTMENT_HORIZONTALOFFSET);
		HWND AdjustmentGammaEdit = GetDlgItem(Window, IDC_EDIT_ADJUSTMENT_GAMMA);
		HWND CodepointsList = GetDlgItem(Window, IDC_LIST_CODEPOINTS);
		HWND CodepointsDeleteButton = GetDlgItem(Window, IDC_BUTTON_CODEPOINTS_DELETE);
		HWND CodepointsOverwriteCheck = GetDlgItem(Window, IDC_CHECK_CODEPOINTS_OVERWRITE);
		HWND UnicodeBlockSearchNameEdit = GetDlgItem(Window, IDC_EDIT_UNICODEBLOCKS_SEARCH);
		HWND UnicodeBlockSearchShowBlocksWithAnyOfCharactersInput = GetDlgItem(Window, IDC_CHECK_UNICODEBLOCKS_SHOWBLOCKSWITHANYOFCHARACTERSINPUT);
		HWND UnicodeBlockSearchResultList = GetDlgItem(Window, IDC_LIST_UNICODEBLOCKS_SEARCHRESULTS);
		HWND UnicodeBlockSearchSelectedPreviewEdit = GetDlgItem(Window, IDC_EDIT_UNICODEBLOCKS_RANGEPREVIEW);
		HWND UnicodeBlockSearchAddAll = GetDlgItem(Window, IDC_BUTTON_UNICODEBLOCKS_ADDALL);
		HWND UnicodeBlockSearchAdd = GetDlgItem(Window, IDC_BUTTON_UNICODEBLOCKS_ADD);
		HWND CustomRangeEdit = GetDlgItem(Window, IDC_EDIT_ADDCUSTOMRANGE_INPUT);
		HWND CustomRangePreview = GetDlgItem(Window, IDC_EDIT_ADDCUSTOMRANGE_PREVIEW);
		HWND CustomRangeAdd = GetDlgItem(Window, IDC_BUTTON_ADDCUSTOMRANGE_ADD);
	} *m_controls = nullptr;

public:
	EditorDialog(HWND hParentWnd, Face::Element& element, std::function<void()> onFontChanged)
		: m_hParentWnd(hParentWnd)
		, m_element(element)
		, m_elementOriginal(element)
		, m_onFontChanged(onFontChanged) {

		std::unique_ptr<std::remove_pointer<HGLOBAL>::type, decltype(FreeResource)*> hglob(LoadResource(g_hInstance, FindResourceW(g_hInstance, MAKEINTRESOURCE(IDD_FACEELEMENTEDITOR), RT_DIALOG)), FreeResource);
		CreateDialogIndirectParamW(
			g_hInstance,
			reinterpret_cast<DLGTEMPLATE*>(LockResource(hglob.get())),
			m_hParentWnd,
			DlgProcStatic,
			reinterpret_cast<LPARAM>(this));
	}

	~EditorDialog() {
		delete m_controls;
	}

	bool IsOpened() const {
		return m_bOpened;
	}

	void Activate() const {
		BringWindowToTop(m_controls->Window);
	}

	bool ConsumeDialogMessage(MSG& msg) {
		return m_controls && IsDialogMessage(m_controls->Window, &msg);
	}

private:
	INT_PTR OkButton_OnCommand(uint16_t notiCode) {
		EndDialog(m_controls->Window, 0);
		m_bOpened = false;
		return 0;
	}

	INT_PTR CancelButton_OnCommand(uint16_t notiCode) {
		m_element = std::move(m_elementOriginal);
		EndDialog(m_controls->Window, 0);
		m_bOpened = false;

		if (m_bBaseFontChanged)
			OnBaseFontChanged();
		else if (m_bWrappedFontChanged)
			OnWrappedFontChanged();
		return 0;
	}

	INT_PTR FontRendererCombo_OnCommand(uint16_t notiCode) {
		if (notiCode != CBN_SELCHANGE)
			return 0;

		m_element.Renderer = static_cast<RendererEnum>(ComboBox_GetCurSel(m_controls->FontRendererCombo));
		SetControlsEnabledOrDisabled();
		RepopulateFontCombobox();
		OnBaseFontChanged();
		RefreshUnicodeBlockSearchResults();
		return 0;
	}

	INT_PTR FontCombo_OnCommand(uint16_t notiCode) {
		if (notiCode != CBN_SELCHANGE)
			return 0;

		RepopulateFontSubComboBox();
		m_element.Lookup.Name = XivRes::Unicode::Convert<std::string>(GetWindowString(m_controls->FontCombo));
		OnBaseFontChanged();
		RefreshUnicodeBlockSearchResults();
		return 0;
	}

	INT_PTR FontSizeCombo_OnCommand(uint16_t notiCode) {
		if (notiCode != CBN_SELCHANGE && notiCode != CBN_EDITUPDATE)
			return 0;

		PostMessageW(m_controls->Window, WmFontSizeTextChanged, 0, 0);
		return 0;
	}

	INT_PTR FontWeightCombo_OnCommand(uint16_t notiCode) {
		if (notiCode != CBN_SELCHANGE)
			return 0;

		if (const auto w = static_cast<DWRITE_FONT_WEIGHT>(GetWindowInt(m_controls->FontWeightCombo)); w != m_element.Lookup.Weight) {
			m_element.Lookup.Weight = w;
			OnBaseFontChanged();
		}
		return 0;
	}

	INT_PTR FontStyleCombo_OnCommand(uint16_t notiCode) {
		if (notiCode != CBN_SELCHANGE)
			return 0;

		DWRITE_FONT_STYLE newStyle;
		if (const auto prevText = GetWindowString(m_controls->FontStyleCombo); prevText == L"Normal")
			newStyle = DWRITE_FONT_STYLE_NORMAL;
		else if (prevText == L"Oblique")
			newStyle = DWRITE_FONT_STYLE_OBLIQUE;
		else if (prevText == L"Italic")
			newStyle = DWRITE_FONT_STYLE_ITALIC;
		else
			newStyle = DWRITE_FONT_STYLE_NORMAL;
		if (newStyle != m_element.Lookup.Style) {
			m_element.Lookup.Style = newStyle;
			OnBaseFontChanged();
		}
		return 0;
	}

	INT_PTR FontStretchCombo_OnCommand(uint16_t notiCode) {
		if (notiCode != CBN_SELCHANGE)
			return 0;

		DWRITE_FONT_STRETCH newStretch;
		if (const auto prevText = GetWindowString(m_controls->FontStretchCombo); prevText == L"Ultra Condensed")
			newStretch = DWRITE_FONT_STRETCH_ULTRA_CONDENSED;
		else if (prevText == L"Extra Condensed")
			newStretch = DWRITE_FONT_STRETCH_EXTRA_CONDENSED;
		else if (prevText == L"Condensed")
			newStretch = DWRITE_FONT_STRETCH_CONDENSED;
		else if (prevText == L"Semi Condensed")
			newStretch = DWRITE_FONT_STRETCH_SEMI_CONDENSED;
		else if (prevText == L"Normal")
			newStretch = DWRITE_FONT_STRETCH_NORMAL;
		else if (prevText == L"Medium")
			newStretch = DWRITE_FONT_STRETCH_MEDIUM;
		else if (prevText == L"Semi Expanded")
			newStretch = DWRITE_FONT_STRETCH_SEMI_EXPANDED;
		else if (prevText == L"Expanded")
			newStretch = DWRITE_FONT_STRETCH_EXPANDED;
		else if (prevText == L"Extra Expanded")
			newStretch = DWRITE_FONT_STRETCH_EXTRA_EXPANDED;
		else if (prevText == L"Ultra Expanded")
			newStretch = DWRITE_FONT_STRETCH_ULTRA_EXPANDED;
		else
			newStretch = DWRITE_FONT_STRETCH_NORMAL;
		if (newStretch != newStretch) {
			newStretch = newStretch;
			OnBaseFontChanged();
		}
		return 0;
	}

	INT_PTR EmptyAscentEdit_OnChange(uint16_t notiCode) {
		if (notiCode != EN_CHANGE)
			return 0;

		if (const auto n = GetWindowInt(m_controls->EmptyAscentEdit); n != m_element.RendererSpecific.Empty.Ascent) {
			m_element.RendererSpecific.Empty.Ascent = n;
			OnBaseFontChanged();
		}
		return 0;
	}

	INT_PTR EmptyLineHeightEdit_OnChange(uint16_t notiCode) {
		if (notiCode != EN_CHANGE)
			return 0;

		if (const auto n = GetWindowInt(m_controls->EmptyLineHeightEdit); n != m_element.RendererSpecific.Empty.LineHeight) {
			m_element.RendererSpecific.Empty.LineHeight = n;
			OnBaseFontChanged();
		}
		return 0;
	}

	INT_PTR FreeTypeCheck_OnClick(uint16_t notiCode, uint16_t id, HWND hWnd) {
		if (notiCode != BN_CLICKED)
			return 0;

		const auto newFlags = 0
			| (Button_GetCheck(m_controls->FreeTypeNoHintingCheck) ? FT_LOAD_NO_HINTING : 0)
			| (Button_GetCheck(m_controls->FreeTypeNoBitmapCheck) ? FT_LOAD_NO_BITMAP : 0)
			| (Button_GetCheck(m_controls->FreeTypeForceAutohintCheck) ? FT_LOAD_FORCE_AUTOHINT : 0)
			| (Button_GetCheck(m_controls->FreeTypeNoAutohintCheck) ? FT_LOAD_NO_AUTOHINT : 0);

		if (newFlags != m_element.RendererSpecific.FreeType.LoadFlags) {
			m_element.RendererSpecific.FreeType.LoadFlags = newFlags;
			OnBaseFontChanged();
		}
		return 0;
	}

	INT_PTR DirectWriteRenderModeCombo_OnCommand(uint16_t notiCode) {
		if (notiCode != CBN_SELCHANGE)
			return 0;

		if (const auto v = static_cast<DWRITE_RENDERING_MODE>(ComboBox_GetCurSel(m_controls->DirectWriteRenderModeCombo));
			v != m_element.RendererSpecific.DirectWrite.RenderMode) {
			m_element.RendererSpecific.DirectWrite.RenderMode = v;
			OnBaseFontChanged();
		}
		return 0;
	}

	INT_PTR DirectWriteMeasureModeCombo_OnCommand(uint16_t notiCode) {
		if (notiCode != CBN_SELCHANGE)
			return 0;

		if (const auto v = static_cast<DWRITE_MEASURING_MODE>(ComboBox_GetCurSel(m_controls->DirectWriteMeasureModeCombo));
			v != m_element.RendererSpecific.DirectWrite.MeasureMode) {
			m_element.RendererSpecific.DirectWrite.MeasureMode = v;
			OnBaseFontChanged();
		}
		return 0;
	}

	INT_PTR DirectWriteGridFitModeCombo_OnCommand(uint16_t notiCode) {
		if (notiCode != CBN_SELCHANGE)
			return 0;

		if (const auto v = static_cast<DWRITE_GRID_FIT_MODE>(ComboBox_GetCurSel(m_controls->DirectWriteGridFitModeCombo));
			v != m_element.RendererSpecific.DirectWrite.GridFitMode) {
			m_element.RendererSpecific.DirectWrite.GridFitMode = v;
			OnBaseFontChanged();
		}
		return 0;
	}

	INT_PTR AdjustmentBaselineShiftEdit_OnChange(uint16_t notiCode) {
		if (notiCode != EN_CHANGE)
			return 0;

		if (const auto n = GetWindowInt(m_controls->AdjustmentBaselineShiftEdit); n != m_element.WrapModifiers.BaselineShift) {
			m_element.WrapModifiers.BaselineShift = n;
			OnWrappedFontChanged();
		}
		return 0;
	}

	INT_PTR AdjustmentLetterSpacingEdit_OnChange(uint16_t notiCode) {
		if (notiCode != EN_CHANGE)
			return 0;

		if (const auto n = GetWindowInt(m_controls->AdjustmentLetterSpacingEdit); n != m_element.WrapModifiers.LetterSpacing) {
			m_element.WrapModifiers.LetterSpacing = n;
			OnWrappedFontChanged();
		}
		return 0;
	}

	INT_PTR AdjustmentHorizontalOffsetEdit_OnChange(uint16_t notiCode) {
		if (notiCode != EN_CHANGE)
			return 0;

		if (const auto n = GetWindowInt(m_controls->AdjustmentHorizontalOffsetEdit); n != m_element.WrapModifiers.HorizontalOffset) {
			m_element.WrapModifiers.HorizontalOffset = n;
			OnWrappedFontChanged();
		}
		return 0;
	}

	INT_PTR AdjustmentGammaEdit_OnChange(uint16_t notiCode) {
		if (notiCode != EN_CHANGE)
			return 0;

		if (const auto n = GetWindowFloat(m_controls->AdjustmentGammaEdit); n != m_element.Gamma) {
			m_element.Gamma = n;
			OnWrappedFontChanged();
		}
		return 0;
	}

	INT_PTR CodepointsList_OnCommand(uint16_t notiCode) {
		if (notiCode == LBN_DBLCLK)
			return CodepointsDeleteButton_OnCommand(BN_CLICKED);

		return 0;
	}

	INT_PTR CodepointsDeleteButton_OnCommand(uint16_t notiCode) {
		std::vector<int> selItems(ListBox_GetSelCount(m_controls->CodepointsList));
		if (selItems.empty())
			return 0;

		ListBox_GetSelItems(m_controls->CodepointsList, static_cast<int>(selItems.size()), &selItems[0]);
		std::ranges::sort(selItems, std::greater<>());
		for (const auto itemIndex : selItems) {
			m_element.WrapModifiers.Codepoints.erase(m_element.WrapModifiers.Codepoints.begin() + itemIndex);
			ListBox_DeleteString(m_controls->CodepointsList, itemIndex);
		}

		OnWrappedFontChanged();
		return 0;
	}

	INT_PTR CodepointsOverwriteCheck_OnCommand(uint16_t notiCode) {
		m_element.Overwrite = Button_GetCheck(m_controls->CodepointsOverwriteCheck);
		OnWrappedFontChanged();
		return 0;
	}

	INT_PTR UnicodeBlockSearchNameEdit_OnCommand(uint16_t notiCode) {
		if (notiCode != EN_CHANGE)
			return 0;

		RefreshUnicodeBlockSearchResults();
		return 0;
	}

	INT_PTR UnicodeBlockSearchShowBlocksWithAnyOfCharactersInput_OnCommand(uint16_t notiCode) {
		RefreshUnicodeBlockSearchResults();
		return 0;
	}

	INT_PTR UnicodeBlockSearchResultList_OnCommand(uint16_t notiCode) {
		if (notiCode == LBN_SELCHANGE || notiCode == LBN_SELCANCEL) {
			std::vector<int> selItems(ListBox_GetSelCount(m_controls->UnicodeBlockSearchResultList));
			if (selItems.empty())
				return 0;

			ListBox_GetSelItems(m_controls->UnicodeBlockSearchResultList, static_cast<int>(selItems.size()), &selItems[0]);

			std::vector<char32_t> charVec(m_element.BaseFont->GetAllCodepoints().begin(), m_element.BaseFont->GetAllCodepoints().end());
			std::wstring containingChars;

			containingChars.reserve(8192);

			for (const auto itemIndex : selItems) {
				const auto& block = *reinterpret_cast<XivRes::Unicode::UnicodeBlocks::BlockDefinition*>(ListBox_GetItemData(m_controls->UnicodeBlockSearchResultList, itemIndex));

				for (auto it = std::ranges::lower_bound(charVec, block.First), it_ = std::ranges::upper_bound(charVec, block.Last); it != it_; ++it) {
					XivRes::Unicode::RepresentChar(containingChars, *it);
					if (containingChars.size() >= 8192)
						break;
				}

				if (containingChars.size() >= 8192)
					break;
			}

			Static_SetText(m_controls->UnicodeBlockSearchSelectedPreviewEdit, containingChars.c_str());
		} else if (notiCode == LBN_DBLCLK) {
			return UnicodeBlockSearchAdd_OnCommand(BN_CLICKED);
		}
		return 0;
	}

	INT_PTR UnicodeBlockSearchAddAll_OnCommand(uint16_t notiCode) {
		auto changed = false;
		std::vector<char32_t> charVec(m_element.BaseFont->GetAllCodepoints().begin(), m_element.BaseFont->GetAllCodepoints().end());
		for (int i = 0, i_ = ListBox_GetCount(m_controls->UnicodeBlockSearchResultList); i < i_; i++) {
			const auto& block = *reinterpret_cast<const XivRes::Unicode::UnicodeBlocks::BlockDefinition*>(ListBox_GetItemData(m_controls->UnicodeBlockSearchResultList, i));
			changed |= AddNewCodepointRange(block.First, block.Last, charVec);
		}

		if (changed)
			OnWrappedFontChanged();

		return 0;
	}

	INT_PTR UnicodeBlockSearchAdd_OnCommand(uint16_t notiCode) {
		std::vector<int> selItems(ListBox_GetSelCount(m_controls->UnicodeBlockSearchResultList));
		if (selItems.empty())
			return 0;

		ListBox_GetSelItems(m_controls->UnicodeBlockSearchResultList, static_cast<int>(selItems.size()), &selItems[0]);

		auto changed = false;
		std::vector<char32_t> charVec(m_element.BaseFont->GetAllCodepoints().begin(), m_element.BaseFont->GetAllCodepoints().end());
		for (const auto itemIndex : selItems) {
			const auto& block = *reinterpret_cast<const XivRes::Unicode::UnicodeBlocks::BlockDefinition*>(ListBox_GetItemData(m_controls->UnicodeBlockSearchResultList, itemIndex));
			changed |= AddNewCodepointRange(block.First, block.Last, charVec);
		}

		if (changed)
			OnWrappedFontChanged();

		return 0;
	}

	INT_PTR CustomRangeEdit_OnCommand(uint16_t notiCode) {
		if (notiCode != EN_CHANGE)
			return 0;

		std::wstring description;
		for (const auto& [c1, c2] : ParseCustomRangeString()) {
			if (!description.empty())
				description += L", ";
			if (c1 == c2) {
				description += std::format(
					L"U+{:04X} {}",
					static_cast<uint32_t>(c1),
					XivRes::Unicode::RepresentChar<std::wstring>(c1)
				);
			} else {
				description += std::format(
					L"U+{:04X}~{:04X} {}~{}",
					static_cast<uint32_t>(c1),
					static_cast<uint32_t>(c2),
					XivRes::Unicode::RepresentChar<std::wstring>(c1),
					XivRes::Unicode::RepresentChar<std::wstring>(c2)
				);
			}
		}

		Edit_SetText(m_controls->CustomRangePreview, &description[0]);

		return 0;
	}

	INT_PTR CustomRangeAdd_OnCommand(uint16_t notiCode) {
		std::vector<char32_t> charVec(m_element.BaseFont->GetAllCodepoints().begin(), m_element.BaseFont->GetAllCodepoints().end());

		auto changed = false;
		for (const auto& [c1, c2] : ParseCustomRangeString())
			changed |= AddNewCodepointRange(c1, c2, charVec);

		if (changed)
			OnWrappedFontChanged();

		return 0;
	}

	LRESULT Dialog_OnInitDialog() {
		m_bOpened = true;
		SetControlsEnabledOrDisabled();
		RepopulateFontCombobox();
		RefreshUnicodeBlockSearchResults();
		ComboBox_AddString(m_controls->FontRendererCombo, L"Empty");
		ComboBox_AddString(m_controls->FontRendererCombo, L"Prerendered (Game)");
		ComboBox_AddString(m_controls->FontRendererCombo, L"DirectWrite");
		ComboBox_AddString(m_controls->FontRendererCombo, L"FreeType");
		ComboBox_SetCurSel(m_controls->FontRendererCombo, static_cast<int>(m_element.Renderer));
		ComboBox_SetText(m_controls->FontCombo, XivRes::Unicode::Convert<std::wstring>(m_element.Lookup.Name).c_str());
		Edit_SetText(m_controls->EmptyAscentEdit, std::format(L"{}", m_element.RendererSpecific.Empty.Ascent).c_str());
		Edit_SetText(m_controls->EmptyLineHeightEdit, std::format(L"{}", m_element.RendererSpecific.Empty.LineHeight).c_str());
		Button_SetCheck(m_controls->FreeTypeNoHintingCheck, (m_element.RendererSpecific.FreeType.LoadFlags & FT_LOAD_NO_HINTING) ? TRUE : FALSE);
		Button_SetCheck(m_controls->FreeTypeNoBitmapCheck, (m_element.RendererSpecific.FreeType.LoadFlags & FT_LOAD_NO_BITMAP) ? TRUE : FALSE);
		Button_SetCheck(m_controls->FreeTypeForceAutohintCheck, (m_element.RendererSpecific.FreeType.LoadFlags & FT_LOAD_FORCE_AUTOHINT) ? TRUE : FALSE);
		Button_SetCheck(m_controls->FreeTypeNoAutohintCheck, (m_element.RendererSpecific.FreeType.LoadFlags & FT_LOAD_NO_AUTOHINT) ? TRUE : FALSE);
		ComboBox_AddString(m_controls->DirectWriteRenderModeCombo, L"Default");
		ComboBox_AddString(m_controls->DirectWriteRenderModeCombo, L"Aliased");
		ComboBox_AddString(m_controls->DirectWriteRenderModeCombo, L"GDI Classic");
		ComboBox_AddString(m_controls->DirectWriteRenderModeCombo, L"GDI Natural");
		ComboBox_AddString(m_controls->DirectWriteRenderModeCombo, L"Natural");
		ComboBox_AddString(m_controls->DirectWriteRenderModeCombo, L"Natural Symmetric");
		ComboBox_SetCurSel(m_controls->DirectWriteRenderModeCombo, static_cast<int>(m_element.RendererSpecific.DirectWrite.RenderMode));
		ComboBox_AddString(m_controls->DirectWriteMeasureModeCombo, L"Natural");
		ComboBox_AddString(m_controls->DirectWriteMeasureModeCombo, L"GDI Classic");
		ComboBox_AddString(m_controls->DirectWriteMeasureModeCombo, L"GDI Natural");
		ComboBox_SetCurSel(m_controls->DirectWriteMeasureModeCombo, static_cast<int>(m_element.RendererSpecific.DirectWrite.MeasureMode));
		ComboBox_AddString(m_controls->DirectWriteGridFitModeCombo, L"Default");
		ComboBox_AddString(m_controls->DirectWriteGridFitModeCombo, L"Disabled");
		ComboBox_AddString(m_controls->DirectWriteGridFitModeCombo, L"Enabled");
		ComboBox_SetCurSel(m_controls->DirectWriteGridFitModeCombo, static_cast<int>(m_element.RendererSpecific.DirectWrite.GridFitMode));

		Edit_SetText(m_controls->AdjustmentBaselineShiftEdit, std::format(L"{}", m_element.WrapModifiers.BaselineShift).c_str());
		Edit_SetText(m_controls->AdjustmentLetterSpacingEdit, std::format(L"{}", m_element.WrapModifiers.LetterSpacing).c_str());
		Edit_SetText(m_controls->AdjustmentHorizontalOffsetEdit, std::format(L"{}", m_element.WrapModifiers.HorizontalOffset).c_str());
		Edit_SetText(m_controls->AdjustmentGammaEdit, std::format(L"{:g}", m_element.Gamma).c_str());

		for (const auto& controlHwnd : {
			m_controls->EmptyAscentEdit,
			m_controls->EmptyLineHeightEdit,
			m_controls->AdjustmentBaselineShiftEdit,
			m_controls->AdjustmentLetterSpacingEdit,
			m_controls->AdjustmentHorizontalOffsetEdit
			}) {
			SetWindowSubclass(controlHwnd, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
				if (msg == WM_KEYDOWN && wParam == VK_DOWN)
					SetWindowInt(hWnd, (std::max)(-128, GetWindowInt(hWnd) + 1));
				else if (msg == WM_KEYDOWN && wParam == VK_UP)
					SetWindowInt(hWnd, (std::min)(127, GetWindowInt(hWnd) - 1));
				else
					return DefSubclassProc(hWnd, msg, wParam, lParam);
				return 0;
			}, 1, 0);
		}
		SetWindowSubclass(m_controls->AdjustmentGammaEdit, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
			if (msg == WM_KEYDOWN && wParam == VK_DOWN)
				SetWindowFloat(hWnd, (std::max)(0.1f, GetWindowFloat(hWnd) + 0.1f));
			else if (msg == WM_KEYDOWN && wParam == VK_UP)
				SetWindowFloat(hWnd, (std::min)(3.0f, GetWindowFloat(hWnd) - 0.1f));
			else
				return DefSubclassProc(hWnd, msg, wParam, lParam);
			return 0;
		}, 1, 0);

		std::vector<char32_t> charVec(m_element.BaseFont->GetAllCodepoints().begin(), m_element.BaseFont->GetAllCodepoints().end());
		for (int i = 0, i_ = static_cast<int>(m_element.WrapModifiers.Codepoints.size()); i < i_; i++)
			AddCodepointRangeToListBox(i, m_element.WrapModifiers.Codepoints[i].first, m_element.WrapModifiers.Codepoints[i].second, charVec);

		Button_SetCheck(m_controls->CodepointsOverwriteCheck, m_element.Overwrite ? TRUE : FALSE);

		RECT rc, rcParent;
		GetWindowRect(m_controls->Window, &rc);
		GetWindowRect(m_hParentWnd, &rcParent);
		rc.right -= rc.left;
		rc.bottom -= rc.top;
		rc.left = (rcParent.left + rcParent.right - rc.right) / 2;
		rc.top = (rcParent.top + rcParent.bottom - rc.bottom) / 2;
		rc.right += rc.left;
		rc.bottom += rc.top;
		SetWindowPos(m_controls->Window, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE | SWP_NOZORDER);

		ShowWindow(m_controls->Window, SW_SHOW);

		return 0;
	}

	std::vector<std::pair<char32_t, char32_t>> ParseCustomRangeString() {
		const auto input = XivRes::Unicode::Convert<std::u32string>(GetWindowString(m_controls->CustomRangeEdit));
		size_t next = 0;
		std::vector<std::pair<char32_t, char32_t>> ranges;
		for (size_t i = 0; i < input.size(); i = next + 1) {
			next = input.find_first_of(U",;", i);
			std::u32string_view part;
			if (next == std::u32string::npos) {
				next = input.size() - 1;
				part = std::u32string_view(input).substr(i);
			} else
				part = std::u32string_view(input).substr(i, next - i);

			while (!part.empty() && part.front() < 128 && std::isspace(part.front()))
				part = part.substr(1);
			while (!part.empty() && part.front() < 128 && std::isspace(part.back()))
				part = part.substr(0, part.size() - 1);

			if (part.empty())
				continue;

			if (part.starts_with(U"0x") || part.starts_with(U"0X") || part.starts_with(U"U+") || part.starts_with(U"u+") || part.starts_with(U"\\x") || part.starts_with(U"\\X")) {
				if (const auto sep = part.find_first_of(U"-~:"); sep != std::u32string::npos) {
					auto c1 = std::strtol(XivRes::Unicode::Convert<std::string>(part.substr(2, sep - 2)).c_str(), nullptr, 16);
					auto part2 = part.substr(sep + 1);
					while (!part2.empty() && part2.front() < 128 && std::isspace(part2.front()))
						part2 = part2.substr(1);
					if (part2.starts_with(U"0x") || part2.starts_with(U"0X") || part2.starts_with(U"U+") || part2.starts_with(U"u+") || part2.starts_with(U"\\x") || part2.starts_with(U"\\X"))
						part2 = part2.substr(2);
					auto c2 = std::strtol(XivRes::Unicode::Convert<std::string>(part2).c_str(), nullptr, 16);
					if (c1 < c2)
						ranges.emplace_back(c1, c2);
					else
						ranges.emplace_back(c2, c1);
				} else {
					const auto c = std::strtol(XivRes::Unicode::Convert<std::string>(part.substr(2)).c_str(), nullptr, 16);
					ranges.emplace_back(c, c);
				}
			} else {
				for (const auto c : part)
					ranges.emplace_back(c, c);
			}
		}
		std::sort(ranges.begin(), ranges.end());
		for (size_t i = 1; i < ranges.size();) {
			if (ranges[i - 1].second + 1 >= ranges[i].first) {
				ranges[i - 1].second = (std::max)(ranges[i - 1].second, ranges[i].second);
				ranges.erase(ranges.begin() + i);
			} else
				++i;
		}
		return ranges;
	}

	bool AddNewCodepointRange(char32_t c1, char32_t c2, const std::vector<char32_t>& charVec) {
		const auto newItem = std::make_pair(c1, c2);
		const auto it = std::ranges::lower_bound(m_element.WrapModifiers.Codepoints, newItem);
		if (it != m_element.WrapModifiers.Codepoints.end() && *it == newItem)
			return false;

		const auto newIndex = static_cast<int>(it - m_element.WrapModifiers.Codepoints.begin());
		m_element.WrapModifiers.Codepoints.insert(it, newItem);
		AddCodepointRangeToListBox(newIndex, c1, c2, charVec);
		return true;
	}

	void AddCodepointRangeToListBox(int index, char32_t c1, char32_t c2, const std::vector<char32_t>& charVec) {
		const auto left = std::ranges::lower_bound(charVec, c1);
		const auto right = std::ranges::upper_bound(charVec, c2);
		const auto count = right - left;

		const auto block = std::lower_bound(XivRes::Unicode::UnicodeBlocks::Blocks.begin(), XivRes::Unicode::UnicodeBlocks::Blocks.end(), c1, [](const auto& l, const auto& r) { return l.First < r; });
		if (block != XivRes::Unicode::UnicodeBlocks::Blocks.end() && block->First == c1 && block->Last == c2) {
			if (c1 == c2) {
				ListBox_AddString(m_controls->CodepointsList, std::format(
					L"U+{:04X} {} [{}]",
					static_cast<uint32_t>(c1),
					XivRes::Unicode::Convert<std::wstring>(block->Name),
					XivRes::Unicode::RepresentChar<std::wstring>(c1)
				).c_str());
			} else {
				ListBox_AddString(m_controls->CodepointsList, std::format(
					L"U+{:04X}~{:04X} {} ({}) {} ~ {}",
					static_cast<uint32_t>(c1),
					static_cast<uint32_t>(c2),
					XivRes::Unicode::Convert<std::wstring>(block->Name),
					count,
					XivRes::Unicode::RepresentChar<std::wstring>(c1),
					XivRes::Unicode::RepresentChar<std::wstring>(c2)
				).c_str());
			}
		} else if (c1 == c2) {
			ListBox_AddString(m_controls->CodepointsList, std::format(
				L"U+{:04X} [{}]",
				static_cast<int>(c1),
				XivRes::Unicode::RepresentChar<std::wstring>(c1)
			).c_str());
		} else {
			ListBox_AddString(m_controls->CodepointsList, std::format(
				L"U+{:04X}~{:04X} ({}) {} ~ {}",
				static_cast<uint32_t>(c1),
				static_cast<uint32_t>(c2),
				count,
				XivRes::Unicode::RepresentChar<std::wstring>(c1),
				XivRes::Unicode::RepresentChar<std::wstring>(c2)
			).c_str());
		}
	}

	void RefreshUnicodeBlockSearchResults() {
		const auto input = XivRes::Unicode::Convert<std::string>(GetWindowString(m_controls->UnicodeBlockSearchNameEdit));
		const auto input32 = XivRes::Unicode::Convert<std::u32string>(input);
		ListBox_ResetContent(m_controls->UnicodeBlockSearchResultList);

		const auto searchByChar = Button_GetCheck(m_controls->UnicodeBlockSearchShowBlocksWithAnyOfCharactersInput);

		std::vector<char32_t> charVec(m_element.BaseFont->GetAllCodepoints().begin(), m_element.BaseFont->GetAllCodepoints().end());
		for (const auto& block : XivRes::Unicode::UnicodeBlocks::Blocks) {
			const auto nameView = std::string_view(block.Name);
			const auto it = std::search(nameView.begin(), nameView.end(), input.begin(), input.end(), [](char ch1, char ch2) {
				return std::toupper(ch1) == std::toupper(ch2);
			});
			if (it == nameView.end()) {
				if (searchByChar) {
					auto contains = false;
					for (const auto& c : input32) {
						if (block.First <= c && block.Last >= c) {
							contains = true;
							break;
						}
					}
					if (!contains)
						continue;
				} else
					continue;
			}

			const auto left = std::ranges::lower_bound(charVec, block.First);
			const auto right = std::ranges::upper_bound(charVec, block.Last);
			if (left == right)
				continue;

			ListBox_AddString(m_controls->UnicodeBlockSearchResultList, std::format(
				L"U+{:04X}~{:04X} {} ({}) {} ~ {}",
				static_cast<uint32_t>(block.First),
				static_cast<uint32_t>(block.Last),
				XivRes::Unicode::Convert<std::wstring>(nameView),
				right - left,
				XivRes::Unicode::RepresentChar<std::wstring>(block.First),
				XivRes::Unicode::RepresentChar<std::wstring>(block.Last)
			).c_str());
			ListBox_SetItemData(m_controls->UnicodeBlockSearchResultList, ListBox_GetCount(m_controls->UnicodeBlockSearchResultList) - 1, &block);
		}
	}

	void SetControlsEnabledOrDisabled() {
		switch (m_element.Renderer) {
			case RendererEnum::Empty:
				EnableWindow(m_controls->FontCombo, FALSE);
				EnableWindow(m_controls->FontSizeCombo, TRUE);
				EnableWindow(m_controls->FontWeightCombo, FALSE);
				EnableWindow(m_controls->FontStyleCombo, FALSE);
				EnableWindow(m_controls->FontStretchCombo, FALSE);
				EnableWindow(m_controls->EmptyAscentEdit, TRUE);
				EnableWindow(m_controls->EmptyLineHeightEdit, TRUE);
				EnableWindow(m_controls->FreeTypeNoHintingCheck, FALSE);
				EnableWindow(m_controls->FreeTypeNoBitmapCheck, FALSE);
				EnableWindow(m_controls->FreeTypeForceAutohintCheck, FALSE);
				EnableWindow(m_controls->FreeTypeNoAutohintCheck, FALSE);
				EnableWindow(m_controls->DirectWriteRenderModeCombo, FALSE);
				EnableWindow(m_controls->DirectWriteMeasureModeCombo, FALSE);
				EnableWindow(m_controls->DirectWriteGridFitModeCombo, FALSE);
				EnableWindow(m_controls->AdjustmentBaselineShiftEdit, FALSE);
				EnableWindow(m_controls->AdjustmentLetterSpacingEdit, FALSE);
				EnableWindow(m_controls->AdjustmentHorizontalOffsetEdit, FALSE);
				EnableWindow(m_controls->AdjustmentGammaEdit, FALSE);
				EnableWindow(m_controls->CodepointsList, FALSE);
				EnableWindow(m_controls->CodepointsDeleteButton, FALSE);
				EnableWindow(m_controls->CodepointsOverwriteCheck, FALSE);
				EnableWindow(m_controls->UnicodeBlockSearchNameEdit, FALSE);
				EnableWindow(m_controls->UnicodeBlockSearchResultList, FALSE);
				EnableWindow(m_controls->UnicodeBlockSearchAddAll, FALSE);
				EnableWindow(m_controls->UnicodeBlockSearchAdd, FALSE);
				EnableWindow(m_controls->CustomRangeEdit, FALSE);
				EnableWindow(m_controls->CustomRangeAdd, FALSE);
				break;

			case RendererEnum::PrerenderedGameInstallation:
				EnableWindow(m_controls->FontCombo, TRUE);
				EnableWindow(m_controls->FontSizeCombo, TRUE);
				EnableWindow(m_controls->FontWeightCombo, FALSE);
				EnableWindow(m_controls->FontStyleCombo, FALSE);
				EnableWindow(m_controls->FontStretchCombo, FALSE);
				EnableWindow(m_controls->EmptyAscentEdit, FALSE);
				EnableWindow(m_controls->EmptyLineHeightEdit, FALSE);
				EnableWindow(m_controls->FreeTypeNoHintingCheck, FALSE);
				EnableWindow(m_controls->FreeTypeNoBitmapCheck, FALSE);
				EnableWindow(m_controls->FreeTypeForceAutohintCheck, FALSE);
				EnableWindow(m_controls->FreeTypeNoAutohintCheck, FALSE);
				EnableWindow(m_controls->DirectWriteRenderModeCombo, FALSE);
				EnableWindow(m_controls->DirectWriteMeasureModeCombo, FALSE);
				EnableWindow(m_controls->DirectWriteGridFitModeCombo, FALSE);
				EnableWindow(m_controls->AdjustmentBaselineShiftEdit, TRUE);
				EnableWindow(m_controls->AdjustmentLetterSpacingEdit, TRUE);
				EnableWindow(m_controls->AdjustmentHorizontalOffsetEdit, TRUE);
				EnableWindow(m_controls->AdjustmentGammaEdit, FALSE);
				EnableWindow(m_controls->CodepointsList, TRUE);
				EnableWindow(m_controls->CodepointsDeleteButton, TRUE);
				EnableWindow(m_controls->CodepointsOverwriteCheck, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchNameEdit, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchResultList, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchAddAll, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchAdd, TRUE);
				EnableWindow(m_controls->CustomRangeEdit, TRUE);
				EnableWindow(m_controls->CustomRangeAdd, TRUE);
				break;

			case RendererEnum::DirectWrite:
				EnableWindow(m_controls->FontCombo, TRUE);
				EnableWindow(m_controls->FontSizeCombo, TRUE);
				EnableWindow(m_controls->FontWeightCombo, TRUE);
				EnableWindow(m_controls->FontStyleCombo, TRUE);
				EnableWindow(m_controls->FontStretchCombo, TRUE);
				EnableWindow(m_controls->EmptyAscentEdit, FALSE);
				EnableWindow(m_controls->EmptyLineHeightEdit, FALSE);
				EnableWindow(m_controls->FreeTypeNoHintingCheck, FALSE);
				EnableWindow(m_controls->FreeTypeNoBitmapCheck, FALSE);
				EnableWindow(m_controls->FreeTypeForceAutohintCheck, FALSE);
				EnableWindow(m_controls->FreeTypeNoAutohintCheck, FALSE);
				EnableWindow(m_controls->DirectWriteRenderModeCombo, TRUE);
				EnableWindow(m_controls->DirectWriteMeasureModeCombo, TRUE);
				EnableWindow(m_controls->DirectWriteGridFitModeCombo, TRUE);
				EnableWindow(m_controls->AdjustmentBaselineShiftEdit, TRUE);
				EnableWindow(m_controls->AdjustmentLetterSpacingEdit, TRUE);
				EnableWindow(m_controls->AdjustmentHorizontalOffsetEdit, TRUE);
				EnableWindow(m_controls->AdjustmentGammaEdit, TRUE);
				EnableWindow(m_controls->CodepointsList, TRUE);
				EnableWindow(m_controls->CodepointsDeleteButton, TRUE);
				EnableWindow(m_controls->CodepointsOverwriteCheck, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchNameEdit, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchResultList, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchAddAll, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchAdd, TRUE);
				EnableWindow(m_controls->CustomRangeEdit, TRUE);
				EnableWindow(m_controls->CustomRangeAdd, TRUE);
				break;

			case RendererEnum::FreeType:
				EnableWindow(m_controls->FontCombo, TRUE);
				EnableWindow(m_controls->FontSizeCombo, TRUE);
				EnableWindow(m_controls->FontWeightCombo, TRUE);
				EnableWindow(m_controls->FontStyleCombo, TRUE);
				EnableWindow(m_controls->FontStretchCombo, TRUE);
				EnableWindow(m_controls->EmptyAscentEdit, FALSE);
				EnableWindow(m_controls->EmptyLineHeightEdit, FALSE);
				EnableWindow(m_controls->FreeTypeNoHintingCheck, TRUE);
				EnableWindow(m_controls->FreeTypeNoBitmapCheck, TRUE);
				EnableWindow(m_controls->FreeTypeForceAutohintCheck, TRUE);
				EnableWindow(m_controls->FreeTypeNoAutohintCheck, TRUE);
				EnableWindow(m_controls->DirectWriteRenderModeCombo, FALSE);
				EnableWindow(m_controls->DirectWriteMeasureModeCombo, FALSE);
				EnableWindow(m_controls->DirectWriteGridFitModeCombo, FALSE);
				EnableWindow(m_controls->AdjustmentBaselineShiftEdit, TRUE);
				EnableWindow(m_controls->AdjustmentLetterSpacingEdit, TRUE);
				EnableWindow(m_controls->AdjustmentHorizontalOffsetEdit, TRUE);
				EnableWindow(m_controls->AdjustmentGammaEdit, TRUE);
				EnableWindow(m_controls->CodepointsList, TRUE);
				EnableWindow(m_controls->CodepointsDeleteButton, TRUE);
				EnableWindow(m_controls->CodepointsOverwriteCheck, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchNameEdit, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchResultList, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchAddAll, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchAdd, TRUE);
				EnableWindow(m_controls->CustomRangeEdit, TRUE);
				EnableWindow(m_controls->CustomRangeAdd, TRUE);
				break;
		}
	}

	std::vector<IDWriteFontFamilyPtr> m_fontFamilies;

	void RepopulateFontCombobox() {
		ComboBox_ResetContent(m_controls->FontCombo);
		switch (m_element.Renderer) {
			case RendererEnum::Empty:
				break;

			case RendererEnum::PrerenderedGameInstallation:
			{
				std::array<std::wstring, 8> ValidFonts{ {
					L"AXIS",
					L"Jupiter",
					L"JupiterN",
					L"Meidinger",
					L"MiedingerMid",
					L"TrumpGothic",
					L"ChnAXIS",
					L"KrnAXIS",
				} };

				auto anySel = false;
				for (auto& name : ValidFonts) {
					ComboBox_AddString(m_controls->FontCombo, name.c_str());

					auto curNameLower = XivRes::Unicode::Convert<std::wstring>(m_element.Lookup.Name);
					for (auto& c : curNameLower)
						c = std::tolower(c);
					for (auto& c : name)
						c = std::tolower(c);
					if (curNameLower != name)
						continue;
					anySel = true;
					ComboBox_SetCurSel(m_controls->FontCombo, ComboBox_GetCount(m_controls->FontCombo) - 1);
				}
				if (!anySel)
					ComboBox_SetCurSel(m_controls->FontCombo, 0);
				break;
			}

			case RendererEnum::DirectWrite:
			case RendererEnum::FreeType:
			{
				using namespace XivRes::FontGenerator;

				IDWriteFactory3Ptr factory;
				SuccessOrThrow(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(&factory)));

				IDWriteFontCollectionPtr coll;
				SuccessOrThrow(factory->GetSystemFontCollection(&coll));

				m_fontFamilies.clear();
				std::vector<std::wstring> names;
				for (uint32_t i = 0, i_ = coll->GetFontFamilyCount(); i < i_; i++) {
					IDWriteFontFamilyPtr family;
					IDWriteLocalizedStringsPtr strings;

					if (FAILED(coll->GetFontFamily(i, &family)))
						continue;

					if (FAILED(family->GetFamilyNames(&strings)))
						continue;

					uint32_t index;
					BOOL exists;
					if (FAILED(strings->FindLocaleName(L"en-us", &index, &exists)))
						continue;
					if (exists)
						index = 0;

					uint32_t length;
					if (FAILED(strings->GetStringLength(index, &length)))
						continue;

					std::wstring res(length + 1, L'\0');
					if (FAILED(strings->GetString(index, &res[0], length + 1)))
						continue;
					res.resize(length);

					std::wstring resLower = res;
					for (auto& c : resLower)
						c = std::tolower(c);

					const auto insertAt = std::ranges::lower_bound(names, resLower) - names.begin();

					ComboBox_InsertString(m_controls->FontCombo, insertAt, res.c_str());
					m_fontFamilies.insert(m_fontFamilies.begin() + insertAt, std::move(family));
					names.insert(names.begin() + insertAt, std::move(resLower));
				}

				auto curNameLower = XivRes::Unicode::Convert<std::wstring>(m_element.Lookup.Name);
				for (auto& c : curNameLower)
					c = std::tolower(c);
				if (const auto it = std::ranges::lower_bound(names, curNameLower); it != names.end() && *it == curNameLower)
					ComboBox_SetCurSel(m_controls->FontCombo, it - names.begin());
				else
					ComboBox_SetCurSel(m_controls->FontCombo, -1);
				break;
			}
			default:
				break;
		}

		RepopulateFontSubComboBox();
	}

	void RepopulateFontSubComboBox() {
		ComboBox_ResetContent(m_controls->FontWeightCombo);
		ComboBox_ResetContent(m_controls->FontStyleCombo);
		ComboBox_ResetContent(m_controls->FontStretchCombo);
		ComboBox_ResetContent(m_controls->FontSizeCombo);

		auto restoreFontSizeInCommonWay = true;
		switch (m_element.Renderer) {
			case RendererEnum::PrerenderedGameInstallation:
			{
				std::vector<float> sizes;
				switch (ComboBox_GetCurSel(m_controls->FontCombo)) {
					case 0: // AXIS
						sizes = { 9.6f, 12.f, 14.f, 18.f, 36.f };
						break;

					case 1: // Jupiter
						sizes = { 16.f, 20.f, 23.f, 46.f };
						break;

					case 2: // JupiterN
						sizes = { 45.f, 90.f };
						break;

					case 3: // Meidinger
						sizes = { 16.f, 20.f, 40.f };
						break;

					case 4: // MiedingerMid
						sizes = { 10.f, 12.f, 14.f, 18.f, 36.f };
						break;

					case 5: // TrumpGothic
						sizes = { 18.4f, 23.f, 34.f, 68.f };
						break;

					case 6: // ChnAXIS
						sizes = { 12.f, 14.f, 18.f, 36.f };
						break;

					case 7: // KrnAXIS
						sizes = { 12.f, 14.f, 18.f, 36.f };
						break;
				}

				auto closestIndex = std::ranges::min_element(sizes, [prevSize = m_element.Size](const auto& n1, const auto& n2) { return std::fabs(n1 - prevSize) < std::fabs(n2 - prevSize); }) - sizes.begin();

				for (size_t i = 0; i < sizes.size(); i++) {
					ComboBox_AddString(m_controls->FontSizeCombo, std::format(L"{:g}", sizes[i]).c_str());
					if (i == closestIndex) {
						ComboBox_SetCurSel(m_controls->FontSizeCombo, i);
						m_element.Size = sizes[i];
					}
				}

				restoreFontSizeInCommonWay = false;

				ComboBox_AddString(m_controls->FontWeightCombo, L"400 (Normal/Regular)");
				ComboBox_AddString(m_controls->FontStyleCombo, L"Normal");
				ComboBox_AddString(m_controls->FontStretchCombo, L"Normal");
				ComboBox_SetCurSel(m_controls->FontWeightCombo, 0);
				ComboBox_SetCurSel(m_controls->FontStyleCombo, 0);
				ComboBox_SetCurSel(m_controls->FontStretchCombo, 0);
				m_element.Lookup.Weight = DWRITE_FONT_WEIGHT_NORMAL;
				m_element.Lookup.Style = DWRITE_FONT_STYLE_NORMAL;
				m_element.Lookup.Stretch = DWRITE_FONT_STRETCH_NORMAL;
				break;
			}

			case RendererEnum::DirectWrite:
			case RendererEnum::FreeType:
			{
				const auto curSel = (std::max)(0, (std::min)(static_cast<int>(m_fontFamilies.size() - 1), ComboBox_GetCurSel(m_controls->FontCombo)));
				const auto& family = m_fontFamilies[curSel];
				std::set<DWRITE_FONT_WEIGHT> weights;
				std::set<DWRITE_FONT_STYLE> styles;
				std::set<DWRITE_FONT_STRETCH> stretches;
				for (uint32_t i = 0, i_ = family->GetFontCount(); i < i_; i++) {
					IDWriteFontPtr font;
					if (FAILED(family->GetFont(i, &font)))
						continue;

					weights.insert(font->GetWeight());
					styles.insert(font->GetStyle());
					stretches.insert(font->GetStretch());
				}

				auto closestWeight = *weights.begin();
				auto closestStyle = *styles.begin();
				auto closestStretch = *stretches.begin();
				for (const auto v : weights) {
					if (std::abs(static_cast<int>(m_element.Lookup.Weight) - static_cast<int>(v)) < std::abs(static_cast<int>(m_element.Lookup.Weight) - static_cast<int>(closestWeight)))
						closestWeight = v;
				}
				for (const auto v : styles) {
					if (std::abs(static_cast<int>(m_element.Lookup.Style) - static_cast<int>(v)) < std::abs(static_cast<int>(m_element.Lookup.Style) - static_cast<int>(closestStyle)))
						closestStyle = v;
				}
				for (const auto v : stretches) {
					if (std::abs(static_cast<int>(m_element.Lookup.Stretch) - static_cast<int>(v)) < std::abs(static_cast<int>(m_element.Lookup.Stretch) - static_cast<int>(closestStretch)))
						closestStretch = v;
				}

				for (const auto v : weights) {
					switch (v) {
						case 100: ComboBox_AddString(m_controls->FontWeightCombo, L"100 (Thin)"); break;
						case 200: ComboBox_AddString(m_controls->FontWeightCombo, L"200 (Extra Light/Ultra Light)"); break;
						case 300: ComboBox_AddString(m_controls->FontWeightCombo, L"300 (Light)"); break;
						case 350: ComboBox_AddString(m_controls->FontWeightCombo, L"350 (Semi Light)"); break;
						case 400: ComboBox_AddString(m_controls->FontWeightCombo, L"400 (Normal/Regular)"); break;
						case 500: ComboBox_AddString(m_controls->FontWeightCombo, L"500 (Medium)"); break;
						case 600: ComboBox_AddString(m_controls->FontWeightCombo, L"600 (Semi Bold/Demibold)"); break;
						case 700: ComboBox_AddString(m_controls->FontWeightCombo, L"700 (Bold)"); break;
						case 800: ComboBox_AddString(m_controls->FontWeightCombo, L"800 (Extra Bold/Ultra Bold)"); break;
						case 900: ComboBox_AddString(m_controls->FontWeightCombo, L"900 (Black/Heavy)"); break;
						case 950: ComboBox_AddString(m_controls->FontWeightCombo, L"950 (Extra Black/Ultra Black)"); break;
						default: ComboBox_AddString(m_controls->FontWeightCombo, std::format(L"{}", static_cast<int>(v)).c_str());
					}

					if (v == closestWeight) {
						ComboBox_SetCurSel(m_controls->FontWeightCombo, ComboBox_GetCount(m_controls->FontWeightCombo) - 1);
						m_element.Lookup.Weight = v;
					}
				}

				for (const auto v : styles) {
					switch (v) {
						case DWRITE_FONT_STYLE_NORMAL: ComboBox_AddString(m_controls->FontStyleCombo, L"Normal"); break;
						case DWRITE_FONT_STYLE_OBLIQUE: ComboBox_AddString(m_controls->FontStyleCombo, L"Oblique"); break;
						case DWRITE_FONT_STYLE_ITALIC: ComboBox_AddString(m_controls->FontStyleCombo, L"Italic"); break;
						default: continue;
					}

					if (v == closestStyle) {
						ComboBox_SetCurSel(m_controls->FontStyleCombo, ComboBox_GetCount(m_controls->FontStyleCombo) - 1);
						m_element.Lookup.Style = v;
					}
				}

				for (const auto v : stretches) {
					switch (v) {
						case DWRITE_FONT_STRETCH_ULTRA_CONDENSED: ComboBox_AddString(m_controls->FontStretchCombo, L"Ultra Condensed"); break;
						case DWRITE_FONT_STRETCH_EXTRA_CONDENSED: ComboBox_AddString(m_controls->FontStretchCombo, L"Extra Condensed"); break;
						case DWRITE_FONT_STRETCH_CONDENSED: ComboBox_AddString(m_controls->FontStretchCombo, L"Condensed"); break;
						case DWRITE_FONT_STRETCH_SEMI_CONDENSED: ComboBox_AddString(m_controls->FontStretchCombo, L"Semi Condensed"); break;
						case DWRITE_FONT_STRETCH_NORMAL: ComboBox_AddString(m_controls->FontStretchCombo, L"Normal"); break;
						case DWRITE_FONT_STRETCH_SEMI_EXPANDED: ComboBox_AddString(m_controls->FontStretchCombo, L"Semi Expanded"); break;
						case DWRITE_FONT_STRETCH_EXPANDED: ComboBox_AddString(m_controls->FontStretchCombo, L"Expanded"); break;
						case DWRITE_FONT_STRETCH_EXTRA_EXPANDED: ComboBox_AddString(m_controls->FontStretchCombo, L"Extra Expanded"); break;
						case DWRITE_FONT_STRETCH_ULTRA_EXPANDED: ComboBox_AddString(m_controls->FontStretchCombo, L"Ultra Expanded"); break;
						default: continue;
					}

					if (v == closestStretch) {
						ComboBox_SetCurSel(m_controls->FontStretchCombo, ComboBox_GetCount(m_controls->FontStretchCombo) - 1);
						m_element.Lookup.Stretch = v;
					}
				}
				break;
			}

			default:
				break;
		}

		if (restoreFontSizeInCommonWay) {
			constexpr std::array<float, 31> sizes{ {
				8.f, 9.f, 9.6f, 10.f, 11.f, 12.f, 13.f, 14.f, 15.f, 16.f, 17.f, 18.f, 18.4f, 19.f,
				20.f, 21.f, 22.f, 23.f, 24.f, 26.f, 28.f, 30.f, 32.f, 34.f, 36.f, 38.f, 40.f, 45.f, 46.f, 68.f, 90.f } };

			for (const auto size : sizes)
				ComboBox_AddString(m_controls->FontSizeCombo, std::format(L"{:g}", size).c_str());

			auto fontSizeRestored = false;
			const auto prevSize = m_element.Size;
			for (size_t i = 0; i < sizes.size(); i++) {
				ComboBox_AddString(m_controls->FontSizeCombo, std::format(L"{:g}", sizes[i]).c_str());
				if (sizes[i] >= m_element.Size && !fontSizeRestored) {
					ComboBox_SetCurSel(m_controls->FontSizeCombo, i);
					m_element.Size = sizes[i];
					fontSizeRestored = true;
				}
			}
			if (!fontSizeRestored)
				ComboBox_SetCurSel(m_controls->FontSizeCombo, ComboBox_GetCount(m_controls->FontSizeCombo));
			ComboBox_SetText(m_controls->FontSizeCombo, std::format(L"{:g}", prevSize).c_str());
		}
	}

	void OnBaseFontChanged() {
		m_bBaseFontChanged = true;
		m_element.RefreshBaseFont();
		if (m_onFontChanged)
			m_onFontChanged();
	}

	void OnWrappedFontChanged() {
		m_bWrappedFontChanged = true;
		m_element.RefreshFont();
		if (m_onFontChanged)
			m_onFontChanged();
	}

	INT_PTR DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
		switch (message) {
			case WM_INITDIALOG:
				return Dialog_OnInitDialog();
			case WM_COMMAND:
			{
				switch (LOWORD(wParam)) {
					case IDOK: return OkButton_OnCommand(HIWORD(wParam));
					case IDCANCEL: return CancelButton_OnCommand(HIWORD(wParam));
					case IDC_COMBO_FONT_RENDERER: return FontRendererCombo_OnCommand(HIWORD(wParam));
					case IDC_COMBO_FONT: return FontCombo_OnCommand(HIWORD(wParam));
					case IDC_COMBO_FONT_SIZE: return FontSizeCombo_OnCommand(HIWORD(wParam));
					case IDC_COMBO_FONT_WEIGHT: return FontWeightCombo_OnCommand(HIWORD(wParam));
					case IDC_COMBO_FONT_STYLE: return FontStyleCombo_OnCommand(HIWORD(wParam));
					case IDC_COMBO_FONT_STRETCH: return FontStretchCombo_OnCommand(HIWORD(wParam));
					case IDC_EDIT_EMPTY_ASCENT: return EmptyAscentEdit_OnChange(HIWORD(wParam));
					case IDC_EDIT_EMPTY_LINEHEIGHT: return EmptyLineHeightEdit_OnChange(HIWORD(wParam));
					case IDC_CHECK_FREETYPE_NOHINTING:
					case IDC_CHECK_FREETYPE_NOBITMAP:
					case IDC_CHECK_FREETYPE_FORCEAUTOHINT:
					case IDC_CHECK_FREETYPE_NOAUTOHINT: return FreeTypeCheck_OnClick(HIWORD(wParam), LOWORD(wParam), reinterpret_cast<HWND>(lParam));
					case IDC_COMBO_DIRECTWRITE_RENDERMODE: return DirectWriteRenderModeCombo_OnCommand(HIWORD(wParam));
					case IDC_COMBO_DIRECTWRITE_MEASUREMODE: return DirectWriteMeasureModeCombo_OnCommand(HIWORD(wParam));
					case IDC_COMBO_DIRECTWRITE_GRIDFITMODE: return DirectWriteGridFitModeCombo_OnCommand(HIWORD(wParam));
					case IDC_EDIT_ADJUSTMENT_BASELINESHIFT: return AdjustmentBaselineShiftEdit_OnChange(HIWORD(wParam));
					case IDC_EDIT_ADJUSTMENT_LETTERSPACING: return AdjustmentLetterSpacingEdit_OnChange(HIWORD(wParam));
					case IDC_EDIT_ADJUSTMENT_HORIZONTALOFFSET: return AdjustmentHorizontalOffsetEdit_OnChange(HIWORD(wParam));
					case IDC_EDIT_ADJUSTMENT_GAMMA: return AdjustmentGammaEdit_OnChange(HIWORD(wParam));
					case IDC_LIST_CODEPOINTS: return CodepointsList_OnCommand(HIWORD(wParam));
					case IDC_BUTTON_CODEPOINTS_DELETE: return CodepointsDeleteButton_OnCommand(HIWORD(wParam));
					case IDC_CHECK_CODEPOINTS_OVERWRITE: return CodepointsOverwriteCheck_OnCommand(HIWORD(wParam));
					case IDC_EDIT_UNICODEBLOCKS_SEARCH: return UnicodeBlockSearchNameEdit_OnCommand(HIWORD(wParam));
					case IDC_CHECK_UNICODEBLOCKS_SHOWBLOCKSWITHANYOFCHARACTERSINPUT: return UnicodeBlockSearchShowBlocksWithAnyOfCharactersInput_OnCommand(HIWORD(wParam));
					case IDC_LIST_UNICODEBLOCKS_SEARCHRESULTS: return UnicodeBlockSearchResultList_OnCommand(HIWORD(wParam));
					case IDC_BUTTON_UNICODEBLOCKS_ADDALL: return UnicodeBlockSearchAddAll_OnCommand(HIWORD(wParam));
					case IDC_BUTTON_UNICODEBLOCKS_ADD: return UnicodeBlockSearchAdd_OnCommand(HIWORD(wParam));
					case IDC_EDIT_ADDCUSTOMRANGE_INPUT: return CustomRangeEdit_OnCommand(HIWORD(wParam));
					case IDC_BUTTON_ADDCUSTOMRANGE_ADD: return CustomRangeAdd_OnCommand(HIWORD(wParam));

				}
				return 0;
			}
			case WM_NCHITTEST:
			{
				const auto def = DefWindowProcW(m_controls->Window, message, wParam, lParam);
				return def == HTCLIENT ? HTCAPTION : def;
			}
			case WM_CLOSE:
			{
				EndDialog(m_controls->Window, 0);
				m_bOpened = false;
				return 0;
			}
			case WM_DESTROY:
			{
				m_bOpened = false;
				return 0;
			}
			case WmFontSizeTextChanged:
			{
				if (const auto f = GetWindowFloat(m_controls->FontSizeCombo); f != m_element.Size) {
					m_element.Size = f;
					OnBaseFontChanged();
				}
				return 0;
			}
		}
		return 0;
	}

	static INT_PTR __stdcall DlgProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
		if (message == WM_INITDIALOG) {
			auto& params = *reinterpret_cast<EditorDialog*>(lParam);
			params.m_controls = new ControlStruct{ hwnd };
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&params));
			return params.DlgProc(message, wParam, lParam);
		} else {
			return reinterpret_cast<EditorDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))->DlgProc(message, wParam, lParam);
		}
		return 0;
	}
};

void FontSet::ConsolidateFonts() {
	std::map<std::string, std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> knownFonts;
	for (auto& face : Faces) {
		for (auto& elem : face.Elements) {
			std::string fontKey;
			switch (elem.Renderer) {
				case Face::Element::RendererEnum::Empty:
					fontKey = std::format("empty:{:g}:{}:{}", elem.Size, elem.RendererSpecific.Empty.Ascent, elem.RendererSpecific.Empty.LineHeight);
					break;
				case Face::Element::RendererEnum::PrerenderedGameInstallation:
					fontKey = std::format("game:{}:{:g}", elem.Lookup.Name, elem.Size);
					break;
				case Face::Element::RendererEnum::DirectWrite:
					fontKey = std::format("directwrite:{}:{:g}:{:g}:{}:{}:{}:{}:{}:{}",
						elem.Lookup.Name,
						elem.Size,
						elem.Gamma,
						static_cast<uint32_t>(elem.Lookup.Weight),
						static_cast<uint32_t>(elem.Lookup.Stretch),
						static_cast<uint32_t>(elem.Lookup.Style),
						static_cast<uint32_t>(elem.RendererSpecific.DirectWrite.RenderMode),
						static_cast<uint32_t>(elem.RendererSpecific.DirectWrite.MeasureMode),
						static_cast<uint32_t>(elem.RendererSpecific.DirectWrite.GridFitMode)
					);
					break;
				case Face::Element::RendererEnum::FreeType:
					fontKey = std::format("freetype:{}:{:g}:{:g}:{}:{}:{}:{}",
						elem.Lookup.Name,
						elem.Size,
						elem.Gamma,
						static_cast<uint32_t>(elem.Lookup.Weight),
						static_cast<uint32_t>(elem.Lookup.Stretch),
						static_cast<uint32_t>(elem.Lookup.Style),
						static_cast<uint32_t>(elem.RendererSpecific.FreeType.LoadFlags)
					);
					break;
				default:
					throw std::runtime_error("Invalid renderer");
			}

			auto& known = knownFonts[fontKey];
			auto& base = elem.BaseFont;
			if (known) {
				base = known;
				elem.RefreshFont();
			} else if (base) {
				known = base;
			} else {
				elem.RefreshBaseFont();
				known = base;
			}
		}
		face.RefreshFont();
	}
}

static const char* const DefaultPreviewText = reinterpret_cast<const char*>(
	u8"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\r\n"
	u8"0英en-US: _The_ (89) quick brown foxes jump over the [01] lazy dogs.\r\n"
	u8"1日ja-JP: _パングラム_(pangram)で[23]つの字体（フォント）をテストします。\r\n"
	u8"2中zh-CN: _(天)地玄黃_，宇[宙]洪荒。蓋此身髮，4大5常。\r\n"
	u8"3韓ko-KR: 45 _다(람)쥐_ 67 헌 쳇바퀴에 타[고]파.\r\n"
	u8"4露ru-RU: Съешь (ж)е ещё этих мягких 23 [ф]ранцузских булок да 45 выпей чаю.\r\n"
	u8"5泰th-TH: เป็นมนุษย์สุดประเสริฐเลิศคุณค่า\r\n"
	u8"6\r\n"
	u8"7\r\n"
	u8"8\r\n"
	u8"9\r\n"
	u8"\r\n"
	u8"\r\n"
	// almost every script covered by Nirmala UI has too much triple character gpos entries to be usable in game, so don't bother
	);

FontSet NewFromTemplateFont(XivRes::GameFontType fontType) {
	FontSet res{};
	if (const auto pcszFmt = XivRes::FontGenerator::GetFontTexFilenameFormat(fontType)) {
		std::string_view filename(pcszFmt);
		filename = filename.substr(filename.rfind('/') + 1);
		res.TexFilenameFormat = filename;
	} else
		res.TexFilenameFormat = "font{}.tex";

	for (const auto& def : XivRes::FontGenerator::GetFontDefinition(fontType)) {
		std::string_view filename(def.Path);
		filename = filename.substr(filename.rfind('/') + 1);
		filename = filename.substr(0, filename.find('.'));

		std::string previewText;
		std::vector<std::pair<char32_t, char32_t>> codepoints;
		if (filename == "Jupiter_45" || filename == "Jupiter_90") {
			previewText = "123,456,789.000!!!";
			codepoints = { { U'0', U'9' }, { U'!', U'!' }, { U'.', U'.' }, { U',', U',' } };
		} else if (filename.starts_with("Meidinger_")) {
			previewText = "0123456789?!%+-./";
			codepoints = { { 0x0000, 0x10FFFF } };
		} else {
			previewText = DefaultPreviewText;
			codepoints = { { 0x0000, 0x10FFFF } };
		}
		res.Faces.emplace_back(FontSet::Face{
			.RuntimeTag = std::make_shared<void*>(),
			.Name = std::string(filename),
			.Elements = {{
				.RuntimeTag = std::make_shared<void*>(),
				.Size = def.Size,
				.WrapModifiers = {
					.Codepoints = std::move(codepoints),
				},
				.Renderer = FontSet::Face::Element::RendererEnum::PrerenderedGameInstallation,
				.Lookup = {
					.Name = def.Name,
				},
			}},
			.PreviewText = previewText,
			});
	}

	return res;
}

class BaseWindow {
	static std::set<BaseWindow*> s_windows;

public:
	BaseWindow() {
		s_windows.insert(this);
	}

	virtual ~BaseWindow() {
		s_windows.erase(this);
	}

	virtual bool ConsumeDialogMessage(MSG& msg) = 0;

	virtual bool ConsumeAccelerator(MSG& msg) = 0;

	static bool ConsumeMessage(MSG& msg) {
		for (auto& window : s_windows) {
			if (window->ConsumeAccelerator(msg))
				return true;
			if (window->ConsumeDialogMessage(msg))
				return true;
		}
		return false;
	}
};

std::set<BaseWindow*> BaseWindow::s_windows;

class ExportPreviewWindow : public BaseWindow {
	static constexpr auto ClassName = L"ExportPreviewWindowClass";

	enum : size_t {
		Id_None,
		Id_FaceListBox,
		Id_Edit,
		Id__Last,
	};
	static constexpr auto FaceListBoxWidth = 160;
	static constexpr auto EditHeight = 160;

	std::shared_ptr<XivRes::MemoryMipmapStream> m_pMipmap;
	bool m_bNeedRedraw = false;

	std::vector<std::pair<std::string, std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>>> m_fonts;

	HWND m_hWnd{};
	HFONT m_hUiFont{};

	HWND m_hFacesListBox{};
	HWND m_hEdit{};

	int m_nDrawLeft{};
	int m_nDrawTop{};

	LRESULT Window_OnCreate(HWND hwnd) {
		m_hWnd = hwnd;

		NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
		SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
		m_hUiFont = CreateFontIndirectW(&ncm.lfMessageFont);

		m_hFacesListBox = CreateWindowExW(0, WC_LISTBOXW, nullptr,
			WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
			0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_FaceListBox), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
		m_hEdit = CreateWindowExW(0, WC_EDITW, nullptr,
			WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_AUTOVSCROLL | ES_MULTILINE | ES_WANTRETURN,
			0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_Edit), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);

		SendMessage(m_hFacesListBox, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
		SendMessage(m_hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
		Edit_SetText(m_hEdit, XivRes::Unicode::Convert<std::wstring>(DefaultPreviewText).c_str());

		for (const auto& font : m_fonts)
			ListBox_AddString(m_hFacesListBox, XivRes::Unicode::Convert<std::wstring>(font.first).c_str());
		ListBox_SetCurSel(m_hFacesListBox, 0);

		SetWindowSubclass(m_hEdit, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
			if (msg == WM_GETDLGCODE && wParam == VK_TAB)
				return 0;
			if (msg == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && !(GetKeyState(VK_LWIN) & 0x8000) && !(GetKeyState(VK_RWIN) & 0x8000))
				Edit_SetSel(hWnd, 0, Edit_GetTextLength(hWnd));
			return DefSubclassProc(hWnd, msg, wParam, lParam);
		}, 1, 0);

		Window_OnSize();
		ShowWindow(m_hWnd, SW_SHOW);
		return 0;
	}

	LRESULT Window_OnSize() {
		RECT rc;
		GetClientRect(m_hWnd, &rc);

		auto hdwp = BeginDeferWindowPos(Id__Last);
		hdwp = DeferWindowPos(hdwp, m_hFacesListBox, nullptr, 0, 0, FaceListBoxWidth, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
		hdwp = DeferWindowPos(hdwp, m_hEdit, nullptr, FaceListBoxWidth, 0, (std::max<int>)(0, rc.right - rc.left - FaceListBoxWidth), EditHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		EndDeferWindowPos(hdwp);

		m_nDrawLeft = FaceListBoxWidth;
		m_nDrawTop = EditHeight;
		m_pMipmap = std::make_shared<XivRes::MemoryMipmapStream>(
			(std::max<int>)(32, rc.right - rc.left - m_nDrawLeft),
			(std::max<int>)(32, rc.bottom - rc.top - m_nDrawTop),
			1,
			XivRes::TextureFormat::A8R8G8B8);

		m_bNeedRedraw = true;
		InvalidateRect(m_hWnd, nullptr, FALSE);

		return 0;
	}

	LRESULT Window_OnPaint() {
		union {
			struct {
				BITMAPINFOHEADER bmih;
				DWORD bitfields[3];
			};
			BITMAPINFO bmi{};
		};

		PAINTSTRUCT ps;
		const auto hdc = BeginPaint(m_hWnd, &ps);
		if (m_bNeedRedraw) {
			m_bNeedRedraw = false;
			const auto pad = 16;
			const auto buf = m_pMipmap->View<XivRes::RGBA8888>();
			std::ranges::fill(buf, XivRes::RGBA8888{ 0x88, 0x88, 0x88, 0xFF });

			for (int y = pad; y < m_pMipmap->Height - pad; y++) {
				for (int x = pad; x < m_pMipmap->Width - pad; x++)
					buf[y * m_pMipmap->Width + x] = { 0x00, 0x00, 0x00, 0xFF };
			}

			auto sel = ListBox_GetCurSel(m_hFacesListBox);
			sel = (std::max)(0, (std::min)(static_cast<int>(m_fonts.size() - 1), sel));
			if (sel < m_fonts.size()) {
				const auto& font = *m_fonts.at(sel).second;
				XivRes::FontGenerator::TextMeasurer(font)
					.WithMaxWidth(m_pMipmap->Width - pad * 2)
					.Measure(GetWindowString(m_hEdit))
					.DrawTo(*m_pMipmap, font, 16, 16, { 0xFF, 0xFF, 0xFF, 0xFF }, { 0, 0, 0, 0 });
			}
		}

		bmih.biSize = sizeof bmih;
		bmih.biWidth = m_pMipmap->Width;
		bmih.biHeight = -m_pMipmap->Height;
		bmih.biPlanes = 1;
		bmih.biBitCount = 32;
		bmih.biCompression = BI_BITFIELDS;
		reinterpret_cast<XivRes::RGBA8888*>(&bitfields[0])->SetFrom(255, 0, 0, 0);
		reinterpret_cast<XivRes::RGBA8888*>(&bitfields[1])->SetFrom(0, 255, 0, 0);
		reinterpret_cast<XivRes::RGBA8888*>(&bitfields[2])->SetFrom(0, 0, 255, 0);
		StretchDIBits(hdc, m_nDrawLeft, m_nDrawTop, m_pMipmap->Width, m_pMipmap->Height, 0, 0, m_pMipmap->Width, m_pMipmap->Height, &m_pMipmap->View<XivRes::RGBA8888>()[0], &bmi, DIB_RGB_COLORS, SRCCOPY);
		EndPaint(m_hWnd, &ps);

		return 0;
	}

	LRESULT Window_OnDestroy() {
		DeleteFont(m_hUiFont);
		SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DefWindowProcW));
		delete this;
		return 0;
	}

	LRESULT Edit_OnCommand(uint16_t commandId) {
		switch (commandId) {
			case EN_CHANGE:
				m_bNeedRedraw = true;
				InvalidateRect(m_hWnd, nullptr, FALSE);
				return 0;
		}

		return 0;
	}

	LRESULT FaceListBox_OnCommand(uint16_t commandId) {
		switch (commandId) {
			case LBN_SELCHANGE:
			{
				m_bNeedRedraw = true;
				InvalidateRect(m_hWnd, nullptr, FALSE);
				return 0;
			}
		}
		return 0;
	}

	LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_COMMAND:
				switch (LOWORD(wParam)) {
					case Id_Edit: return Edit_OnCommand(HIWORD(wParam));
					case Id_FaceListBox: return FaceListBox_OnCommand(HIWORD(wParam));
				}
				break;

			case WM_CREATE: return Window_OnCreate(hwnd);
			case WM_SIZE: return Window_OnSize();
			case WM_PAINT: return Window_OnPaint();
			case WM_DESTROY: return Window_OnDestroy();
		}

		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	static LRESULT WINAPI WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		return reinterpret_cast<ExportPreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))->WndProc(hwnd, msg, wParam, lParam);
	}

	static LRESULT WINAPI WndProcInitial(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg != WM_NCCREATE)
			return DefWindowProcW(hwnd, msg, wParam, lParam);

		const auto pCreateStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
		const auto pImpl = reinterpret_cast<ExportPreviewWindow*>(pCreateStruct->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pImpl));
		SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcStatic));

		return pImpl->WndProc(hwnd, msg, wParam, lParam);
	}

	ExportPreviewWindow(std::vector<std::pair<std::string, std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>>> fonts)
		: m_fonts(fonts) {
		WNDCLASSEXW wcex{};
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.hInstance = g_hInstance;
		wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wcex.hbrBackground = GetStockBrush(WHITE_BRUSH);
		wcex.lpszClassName = ClassName;
		wcex.lpfnWndProc = ExportPreviewWindow::WndProcInitial;

		RegisterClassExW(&wcex);

		CreateWindowExW(0, ClassName, L"Export Preview", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
			CW_USEDEFAULT, CW_USEDEFAULT, 1200, 640,
			nullptr, nullptr, nullptr, this);
	}

public:
	static void ShowNew(std::vector<std::pair<std::string, std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>>> fonts) {
		new ExportPreviewWindow(std::move(fonts));
	}

	bool ConsumeDialogMessage(MSG& msg) override {
		if (IsDialogMessage(m_hWnd, &msg))
			return true;

		return false;
	}

	bool ConsumeAccelerator(MSG& msg) override {
		return false;
	}
};

class FontEditorWindow : public BaseWindow {
	static constexpr auto ClassName = L"FontEditorWindowClass";
	static constexpr float NoBaseFontSizes[]{ 9.6f, 10.f, 12.f, 14.f, 16.f, 18.f, 18.4f, 20.f, 23.f, 34.f, 36.f, 40.f, 45.f, 46.f, 68.f, 90.f, };

	enum : uint32_t {
		WmApp = WM_APP,
		WmOnFaceElementChanged,
	};

	enum : size_t {
		Id_None,
		Id_FaceListBox,
		Id_FaceElementListView,
		Id_Edit,
		Id__Last,
	};
	static constexpr auto FaceListBoxWidth = 160;
	static constexpr auto ListViewHeight = 160;
	static constexpr auto EditHeight = 40;

	const std::span<wchar_t*> m_args;

	bool m_bChanged = false;
	bool m_bPathIsNotReal = false;
	std::filesystem::path m_path;
	FontSet m_fontSet;
	FontSet::Face* m_pActiveFace = nullptr;

	std::shared_ptr<XivRes::MemoryMipmapStream> m_pMipmap;
	std::map<void**, std::unique_ptr<FontSet::Face::Element::EditorDialog>> m_editors;
	bool m_bNeedRedraw = false;
	bool m_bWordWrap = false;
	bool m_bKerning = false;
	bool m_bShowLineMetrics = true;

	HWND m_hWnd{};
	HACCEL m_hAccelerator{};
	HFONT m_hUiFont{};

	HWND m_hFacesListBox{};
	HWND m_hFaceElementsListView{};
	HWND m_hEdit{};

	int m_nDrawLeft{};
	int m_nDrawTop{};
	int m_nZoom = 1;

	bool m_bIsReorderingFaceElementList = false;

	LRESULT Window_OnCreate(HWND hwnd) {
		m_hWnd = hwnd;

		m_hAccelerator = LoadAcceleratorsW(g_hInstance, MAKEINTRESOURCEW(IDR_ACCELERATOR_FACEELEMENTEDITOR));

		NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
		SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
		m_hUiFont = CreateFontIndirectW(&ncm.lfMessageFont);

		m_hFacesListBox = CreateWindowExW(0, WC_LISTBOXW, nullptr,
			WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
			0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_FaceListBox), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
		m_hFaceElementsListView = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
			WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
			0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_FaceElementListView), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
		m_hEdit = CreateWindowExW(0, WC_EDITW, nullptr,
			WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_AUTOVSCROLL | ES_MULTILINE | ES_WANTRETURN,
			0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_Edit), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);

		ListView_SetExtendedListViewStyle(m_hFaceElementsListView, LVS_EX_FULLROWSELECT);

		SendMessage(m_hFacesListBox, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
		SendMessage(m_hFaceElementsListView, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
		SendMessage(m_hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);

		SetWindowSubclass(m_hEdit, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
			if (msg == WM_GETDLGCODE && wParam == VK_TAB)
				return 0;
			if (msg == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && !(GetKeyState(VK_LWIN) & 0x8000) && !(GetKeyState(VK_RWIN) & 0x8000))
				Edit_SetSel(hWnd, 0, Edit_GetTextLength(hWnd));
			return DefSubclassProc(hWnd, msg, wParam, lParam);
		}, 1, 0);

		const auto AddColumn = [this](int columnIndex, int width, const wchar_t* name) {
			LVCOLUMNW col{
				.mask = LVCF_TEXT | LVCF_WIDTH,
				.cx = width,
				.pszText = const_cast<wchar_t*>(name),
			};
			ListView_InsertColumn(m_hFaceElementsListView, columnIndex, &col);
		};
		AddColumn(ListViewCols::FamilyName, 120, L"Family");
		AddColumn(ListViewCols::SubfamilyName, 80, L"Subfamily");
		AddColumn(ListViewCols::Size, 80, L"Size");
		AddColumn(ListViewCols::LineHeight, 80, L"Line Height");
		AddColumn(ListViewCols::Ascent, 80, L"Ascent");
		AddColumn(ListViewCols::HorizontalOffset, 120, L"Horizontal Offset");
		AddColumn(ListViewCols::LetterSpacing, 100, L"Letter Spacing");
		AddColumn(ListViewCols::Gamma, 60, L"Gamma");
		AddColumn(ListViewCols::Codepoints, 80, L"Codepoints");
		AddColumn(ListViewCols::Overwrite, 70, L"Overwrite");
		AddColumn(ListViewCols::GlyphCount, 60, L"Glyphs");
		AddColumn(ListViewCols::Renderer, 180, L"Renderer");
		AddColumn(ListViewCols::Lookup, 300, L"Lookup");

		if (m_args.size() >= 2 && std::filesystem::exists(m_args[1])) {
			try {
				OpenFile(m_args[1]);
			} catch (const std::exception& e) {
				MessageBoxW(m_hWnd, std::format(L"Failed to open file: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
			}
		}
		if (m_path.empty())
			Menu_File_New(XivRes::GameFontType::font);

		Window_OnSize();
		ShowWindow(m_hWnd, SW_SHOW);
		return 0;
	}

	LRESULT Window_OnSize() {
		RECT rc;
		GetClientRect(m_hWnd, &rc);

		auto hdwp = BeginDeferWindowPos(Id__Last);
		hdwp = DeferWindowPos(hdwp, m_hFacesListBox, nullptr, 0, 0, FaceListBoxWidth, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
		hdwp = DeferWindowPos(hdwp, m_hFaceElementsListView, nullptr, FaceListBoxWidth, 0, (std::max<int>)(0, rc.right - rc.left - FaceListBoxWidth), ListViewHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		hdwp = DeferWindowPos(hdwp, m_hEdit, nullptr, FaceListBoxWidth, ListViewHeight, (std::max<int>)(0, rc.right - rc.left - FaceListBoxWidth), EditHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		EndDeferWindowPos(hdwp);

		m_nDrawLeft = FaceListBoxWidth;
		m_nDrawTop = static_cast<int>(EditHeight + ListViewHeight);
		m_bNeedRedraw = true;

		return 0;
	}

	LRESULT Window_OnPaint() {
		union {
			struct {
				BITMAPINFOHEADER bmih;
				DWORD bitfields[3];
			};
			BITMAPINFO bmi{};
		};

		PAINTSTRUCT ps;
		const auto hdc = BeginPaint(m_hWnd, &ps);
		if (m_bNeedRedraw) {
			m_bNeedRedraw = false;

			RECT rc;
			GetClientRect(m_hWnd, &rc);
			m_pMipmap = std::make_shared<XivRes::MemoryMipmapStream>(
				(std::max<int>)(1, (rc.right - rc.left - m_nDrawLeft + m_nZoom - 1) / m_nZoom),
				(std::max<int>)(1, (rc.bottom - rc.top - m_nDrawTop + m_nZoom - 1) / m_nZoom),
				1,
				XivRes::TextureFormat::A8R8G8B8);

			const auto pad = 16 / m_nZoom;
			const auto buf = m_pMipmap->View<XivRes::RGBA8888>();
			std::ranges::fill(buf, XivRes::RGBA8888{ 0x88, 0x88, 0x88, 0xFF });

			for (int y = pad; y < m_pMipmap->Height - pad; y++) {
				for (int x = pad; x < m_pMipmap->Width - pad; x++)
					buf[y * m_pMipmap->Width + x] = { 0x00, 0x00, 0x00, 0xFF };
			}

			if (m_pActiveFace) {
				auto& face = *m_pActiveFace;

				const auto& mergedFont = *face.EnsureFont();

				if (int lineHeight = mergedFont.GetLineHeight(), ascent = mergedFont.GetAscent(); lineHeight > 0 && m_bShowLineMetrics) {
					if (ascent < lineHeight) {
						for (int y = pad, y_ = m_pMipmap->Height - pad; y < y_; y += lineHeight) {
							for (int y2 = y + ascent, y2_ = (std::min)(y_, y + lineHeight); y2 < y2_; y2++)
								for (int x = pad; x < m_pMipmap->Width - pad; x++)
									buf[y2 * m_pMipmap->Width + x] = { 0x33, 0x33, 0x33, 0xFF };
						}
					} else if (ascent == lineHeight) {
						for (int y = pad, y_ = m_pMipmap->Height - pad; y < y_; y += 2 * lineHeight) {
							for (int y2 = y + lineHeight, y2_ = (std::min)(y_, y + 2 * lineHeight); y2 < y2_; y2++)
								for (int x = pad; x < m_pMipmap->Width - pad; x++)
									buf[y2 * m_pMipmap->Width + x] = { 0x33, 0x33, 0x33, 0xFF };
						}
					}
				}

				if (!face.PreviewText.empty()) {
					XivRes::FontGenerator::TextMeasurer(mergedFont)
						.WithMaxWidth(m_bWordWrap ? m_pMipmap->Width - pad * 2 : (std::numeric_limits<int>::max)())
						.WithUseKerning(m_bKerning)
						.Measure(face.PreviewText)
						.DrawTo(*m_pMipmap, mergedFont, pad, pad, { 0xFF, 0xFF, 0xFF, 0xFF }, { 0, 0, 0, 0 });
				}
			}
		}

		bmih.biSize = sizeof bmih;
		bmih.biWidth = m_pMipmap->Width;
		bmih.biHeight = -m_pMipmap->Height;
		bmih.biPlanes = 1;
		bmih.biBitCount = 32;
		bmih.biCompression = BI_BITFIELDS;
		reinterpret_cast<XivRes::RGBA8888*>(&bitfields[0])->SetFrom(255, 0, 0, 0);
		reinterpret_cast<XivRes::RGBA8888*>(&bitfields[1])->SetFrom(0, 255, 0, 0);
		reinterpret_cast<XivRes::RGBA8888*>(&bitfields[2])->SetFrom(0, 0, 255, 0);
		RECT rc;
		GetClientRect(m_hWnd, &rc);
		StretchDIBits(hdc, m_nDrawLeft, m_nDrawTop, m_pMipmap->Width * m_nZoom, m_pMipmap->Height * m_nZoom, 0, 0, m_pMipmap->Width, m_pMipmap->Height, &m_pMipmap->View<XivRes::RGBA8888>()[0], &bmi, DIB_RGB_COLORS, SRCCOPY);
		EndPaint(m_hWnd, &ps);

		return 0;
	}

	LRESULT Window_OnInitMenuPopup(HMENU hMenu, int index, bool isWindowMenu) {
		{
			const MENUITEMINFOW mii{ .cbSize = sizeof mii, .fMask = MIIM_STATE, .fState = static_cast<UINT>(m_bWordWrap ? MFS_CHECKED : 0) };
			SetMenuItemInfoW(hMenu, ID_VIEW_WORDWRAP, FALSE, &mii);
		}
		{
			const MENUITEMINFOW mii{ .cbSize = sizeof mii, .fMask = MIIM_STATE, .fState = static_cast<UINT>(m_bKerning ? MFS_CHECKED : 0) };
			SetMenuItemInfoW(hMenu, ID_VIEW_KERNING, FALSE, &mii);
		}
		{
			const MENUITEMINFOW mii{ .cbSize = sizeof mii, .fMask = MIIM_STATE, .fState = static_cast<UINT>(m_bShowLineMetrics ? MFS_CHECKED : 0) };
			SetMenuItemInfoW(hMenu, ID_VIEW_SHOWLINEMETRICS, FALSE, &mii);
		}
		return 0;
	}

	LRESULT Window_OnFaceElementChanged(FontSet::Face::Element& element) {
		if (!m_bChanged)
			UpdateWindowTitle(true);
		LVFINDINFOW lvfi{ .flags = LVFI_PARAM, .lParam = reinterpret_cast<LPARAM>(element.RuntimeTag.get()) };
		const auto index = ListView_FindItem(m_hFaceElementsListView, -1, &lvfi);
		if (index != -1)
			element.UpdateText(m_hFaceElementsListView, index);

		m_pActiveFace->RefreshFont();
		Redraw();
		return 0;
	}

	LRESULT Window_OnMouseMove(uint16_t states, int16_t x, int16_t y) {
		if (FaceElementsListView_OnDragProcessMouseMove(x, y))
			return 0;

		return 0;
	}

	LRESULT Window_OnMouseLButtonUp(uint16_t states, int16_t x, int16_t y) {
		if (FaceElementsListView_OnDragProcessMouseUp(x, y))
			return 0;

		return 0;
	}

	LRESULT Window_OnDestroy() {
		DeleteFont(m_hUiFont);
		PostQuitMessage(0);
		return 0;
	}

	LRESULT Menu_File_New(XivRes::GameFontType fontType) {
		if (ConfirmIfChanged())
			return 1;

		m_bChanged = false;
		m_bPathIsNotReal = true;
		switch (fontType) {
			case XivRes::GameFontType::font:
				m_path = "Untitled (font)";
				break;
			case XivRes::GameFontType::font_lobby:
				m_path = "Untitled (font_lobby)";
				break;
			case XivRes::GameFontType::chn_axis:
				m_path = "Untitled (chn_axis)";
				break;
			case XivRes::GameFontType::krn_axis:
				m_path = "Untitled (krn_axis)";
				break;
			default:
				m_path = "Untitled";
				break;

		}
		m_fontSet = NewFromTemplateFont(fontType);

		ReflectFontSetChange();
		UpdateWindowTitle(false);

		return 0;
	}

	LRESULT Menu_File_Open() {
		using namespace XivRes::FontGenerator;
		static constexpr COMDLG_FILTERSPEC fileTypes[] = {
			{ L"Preset JSON Files (*.json)", L"*.json" },
			{ L"All files (*.*)", L"*" },
		};
		const auto fileTypesSpan = std::span(fileTypes);

		if (ConfirmIfChanged())
			return 1;

		try {
			IFileOpenDialogPtr pDialog;
			DWORD dwFlags;
			SuccessOrThrow(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
			SuccessOrThrow(pDialog->SetFileTypes(static_cast<UINT>(fileTypesSpan.size()), fileTypesSpan.data()));
			SuccessOrThrow(pDialog->SetFileTypeIndex(0));
			SuccessOrThrow(pDialog->SetTitle(L"Open"));
			SuccessOrThrow(pDialog->GetOptions(&dwFlags));
			SuccessOrThrow(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
			switch (SuccessOrThrow(pDialog->Show(m_hWnd), { HRESULT_FROM_WIN32(ERROR_CANCELLED) })) {
				case HRESULT_FROM_WIN32(ERROR_CANCELLED):
					return 0;
			}

			IShellItemPtr pResult;
			PWSTR pszFileName;
			SuccessOrThrow(pDialog->GetResult(&pResult));
			SuccessOrThrow(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
			if (!pszFileName)
				throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");

			std::unique_ptr<std::remove_pointer<PWSTR>::type, decltype(CoTaskMemFree)*> pszFileNamePtr(pszFileName, CoTaskMemFree);

			OpenFile(pszFileName);

		} catch (const std::exception& e) {
			MessageBoxW(m_hWnd, std::format(L"Failed to open file: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
			return 1;
		}

		return 0;
	}

	LRESULT Menu_File_Save() {
		if (m_path.empty() || m_bPathIsNotReal)
			return Menu_File_SaveAs(true);

		try {
			nlohmann::json json;
			to_json(json, m_fontSet);
			const auto dump = json.dump();
			std::ofstream(m_path, std::ios::binary).write(&dump[0], dump.size());
			m_bChanged = false;
			UpdateWindowTitle(false);
		} catch (const std::exception& e) {
			MessageBoxW(m_hWnd, std::format(L"Failed to save file: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
			return 1;
		}

		return 0;
	}

	LRESULT Menu_File_SaveAs(bool changeCurrentFile) {
		using namespace XivRes::FontGenerator;
		static constexpr COMDLG_FILTERSPEC fileTypes[] = {
			{ L"Preset JSON Files (*.json)", L"*.json" },
			{ L"All files (*.*)", L"*" },
		};
		const auto fileTypesSpan = std::span(fileTypes);

		try {
			nlohmann::json json;
			to_json(json, m_fontSet);
			const auto dump = json.dump();

			IFileSaveDialogPtr pDialog;
			DWORD dwFlags;
			SuccessOrThrow(pDialog.CreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER));
			SuccessOrThrow(pDialog->SetFileTypes(static_cast<UINT>(fileTypesSpan.size()), fileTypesSpan.data()));
			SuccessOrThrow(pDialog->SetFileTypeIndex(0));
			SuccessOrThrow(pDialog->SetTitle(L"Save"));
			SuccessOrThrow(pDialog->SetDefaultExtension(L"json"));
			SuccessOrThrow(pDialog->GetOptions(&dwFlags));
			SuccessOrThrow(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
			switch (SuccessOrThrow(pDialog->Show(m_hWnd), { HRESULT_FROM_WIN32(ERROR_CANCELLED) })) {
				case HRESULT_FROM_WIN32(ERROR_CANCELLED):
					return 0;
			}

			IShellItemPtr pResult;
			PWSTR pszFileName;
			SuccessOrThrow(pDialog->GetResult(&pResult));
			SuccessOrThrow(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
			if (!pszFileName)
				throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");

			std::unique_ptr<std::remove_pointer<PWSTR>::type, decltype(CoTaskMemFree)*> pszFileNamePtr(pszFileName, CoTaskMemFree);

			std::ofstream(pszFileName, std::ios::binary).write(&dump[0], dump.size());
			m_path = pszFileName;
			m_bPathIsNotReal = false;
			m_bChanged = false;
			UpdateWindowTitle(false);

		} catch (const std::exception& e) {
			MessageBoxW(m_hWnd, std::format(L"Failed to save file: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
			return 1;
		}

		return 0;
	}

	LRESULT Menu_File_Exit() {
		if (ConfirmIfChanged())
			return 1;

		DestroyWindow(m_hWnd);
		return 0;
	}

	LRESULT Menu_Edit_Add() {
		if (!m_pActiveFace)
			return 0;

		std::set<int> indices;
		for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));)
			indices.insert(i);

		const auto count = ListView_GetItemCount(m_hFaceElementsListView);
		if (indices.empty())
			indices.insert(count);

		ListView_SetItemState(m_hFaceElementsListView, -1, 0, LVIS_SELECTED);

		auto& elements = m_pActiveFace->Elements;
		for (const auto pos : indices | std::views::reverse) {
			FontSet::Face::Element newElement{ .RuntimeTag = std::make_shared<void*>() };
			if (pos > 0) {
				const auto& ref = elements[static_cast<size_t>(pos) - 1];
				newElement.BaseFont = ref.BaseFont;
				newElement.Size = ref.Size;
				newElement.WrapModifiers = ref.WrapModifiers;
				newElement.WrapModifiers.Codepoints.clear();
				newElement.Renderer = ref.Renderer;
				newElement.Lookup = ref.Lookup;
				newElement.RendererSpecific = ref.RendererSpecific;
			}
			elements.emplace(elements.begin() + pos, std::move(newElement));

			auto& element = elements[pos];
			LVITEMW lvi{ .mask = LVIF_PARAM | LVIF_STATE, .iItem = pos, .state = LVIS_SELECTED, .stateMask = LVIS_SELECTED, .lParam = reinterpret_cast<LPARAM>(element.RuntimeTag.get()) };
			ListView_InsertItem(m_hFaceElementsListView, &lvi);
			element.UpdateText(m_hFaceElementsListView, lvi.iItem);
		}

		if (!m_bChanged)
			UpdateWindowTitle(true);
		m_pActiveFace->RefreshFont();
		Redraw();

		if (indices.size() == 1)
			ShowEditor(elements[*indices.begin()]);

		return 0;
	}

	LRESULT Menu_Edit_Cut() {
		if (Menu_Edit_Copy())
			return 1;

		Menu_Edit_Delete();
		return 0;
	}

	LRESULT Menu_Edit_Copy() {
		if (!m_pActiveFace)
			return 1;

		auto objs = nlohmann::json::array();
		for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));) {
			objs.emplace_back();
			to_json(objs.back(), m_pActiveFace->Elements[i]);
		}

		const auto wstr = XivRes::Unicode::Convert<std::wstring>(objs.dump());

		const auto clipboard = OpenClipboard(m_hWnd);
		if (!clipboard)
			return 1;
		EmptyClipboard();

		bool copied = false;
		HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (wstr.size() + 1) * 2);
		if (hg) {
			if (const auto pLock = GlobalLock(hg)) {
				memcpy(pLock, &wstr[0], (wstr.size() + 1) * 2);
				copied = SetClipboardData(CF_UNICODETEXT, pLock);
			}
			GlobalUnlock(hg);
			if (!copied)
				GlobalFree(hg);
		}
		CloseClipboard();
		return copied ? 0 : 1;
	}

	LRESULT Menu_Edit_Paste() {
		const auto clipboard = OpenClipboard(m_hWnd);
		if (!clipboard)
			return 0;

		std::string data;
		if (const auto pData = GetClipboardData(CF_UNICODETEXT))
			data = XivRes::Unicode::Convert<std::string>(reinterpret_cast<const wchar_t*>(pData));
		CloseClipboard();

		nlohmann::json parsed;
		std::vector<FontSet::Face::Element> parsedTemplateElements;
		try {
			parsed = nlohmann::json::parse(data);
			if (!parsed.is_array())
				return 0;
			for (const auto& p : parsed) {
				parsedTemplateElements.emplace_back();
				from_json(p, parsedTemplateElements.back());
			}
			if (parsedTemplateElements.empty())
				return 0;
		} catch (const nlohmann::json::exception&) {
			return 0;
		}

		std::set<int> indices;
		for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));)
			indices.insert(i);

		const auto count = ListView_GetItemCount(m_hFaceElementsListView);
		if (indices.empty())
			indices.insert(count);

		ListView_SetItemState(m_hFaceElementsListView, -1, 0, LVIS_SELECTED);

		auto& elements = m_pActiveFace->Elements;
		for (const auto pos : indices | std::views::reverse) {
			for (const auto& templateElement : parsedTemplateElements | std::views::reverse) {
				FontSet::Face::Element newElement{ templateElement };
				newElement.RuntimeTag = std::make_shared<void*>();
				elements.emplace(elements.begin() + pos, std::move(newElement));

				auto& element = elements[pos];
				LVITEMW lvi{ .mask = LVIF_PARAM | LVIF_STATE, .iItem = pos, .state = LVIS_SELECTED, .stateMask = LVIS_SELECTED, .lParam = reinterpret_cast<LPARAM>(element.RuntimeTag.get()) };
				ListView_InsertItem(m_hFaceElementsListView, &lvi);
				element.UpdateText(m_hFaceElementsListView, lvi.iItem);
			}
		}

		if (!m_bChanged)
			UpdateWindowTitle(true);
		m_pActiveFace->RefreshFont();
		Redraw();

		return 0;
	}

	LRESULT Menu_Edit_Delete() {
		if (!m_pActiveFace)
			return 0;
		std::set<int> indices;
		for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));)
			indices.insert(i);
		if (indices.empty())
			return 0;

		for (const auto index : indices | std::views::reverse) {
			ListView_DeleteItem(m_hFaceElementsListView, index);
			m_pActiveFace->Elements.erase(m_pActiveFace->Elements.begin() + index);
		}

		if (!m_bChanged)
			UpdateWindowTitle(true);
		m_pActiveFace->RefreshFont();
		Redraw();

		return 0;
	}

	LRESULT Menu_Edit_SelectAll() {
		ListView_SetItemState(m_hFaceElementsListView, -1, LVIS_SELECTED, LVIS_SELECTED);
		return 0;
	}

	LRESULT Menu_Edit_Details() {
		for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));)
			ShowEditor(m_pActiveFace->Elements[i]);
		return 0;
	}

	LRESULT Menu_Edit_ChangeParams(int baselineShift, int horizontalOffset, int letterSpacing, float fontSize) {
		auto any = false;
		for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));) {
			any = true;
			auto& e = m_pActiveFace->Elements[i];
			auto baseChanged = false;
			if (e.Renderer == FontSet::Face::Element::RendererEnum::Empty) {
				baseChanged |= !!baselineShift;
				baseChanged |= !!(letterSpacing + horizontalOffset);
				e.RendererSpecific.Empty.Ascent += baselineShift;
				e.RendererSpecific.Empty.LineHeight += letterSpacing + horizontalOffset;
			} else {
				e.WrapModifiers.BaselineShift += baselineShift;
				e.WrapModifiers.HorizontalOffset += horizontalOffset;
				e.WrapModifiers.LetterSpacing += letterSpacing;
			}
			if (fontSize != 0.f) {
				e.Size = std::roundf((e.Size + fontSize) * 10.f) / 10.f;
				baseChanged = true;
			}
			if (baseChanged)
				e.RefreshBaseFont();
			else
				e.RefreshFont();
			e.UpdateText(m_hFaceElementsListView, i);
		}
		if (!any)
			return 0;

		if (!m_bChanged)
			UpdateWindowTitle(true);
		m_pActiveFace->RefreshFont();
		Redraw();

		return 0;
	}

	LRESULT Menu_Edit_ToggleOverwrite() {
		auto any = false;
		for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));) {
			any = true;
			auto& e = m_pActiveFace->Elements[i];
			e.Overwrite = !e.Overwrite;
			e.RefreshFont();
			e.UpdateText(m_hFaceElementsListView, i);
		}
		if (!any)
			return 0;

		if (!m_bChanged)
			UpdateWindowTitle(true);
		m_pActiveFace->RefreshFont();
		Redraw();

		return 0;
	}

	LRESULT Menu_Edit_MoveUpOrDown(int direction) {
		std::vector<size_t> ids;
		for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));)
			ids.emplace_back(i);

		if (ids.empty())
			return 0;

		std::vector<size_t> allItems;
		allItems.resize(m_pActiveFace->Elements.size());
		for (auto i = 0; i < allItems.size(); i++)
			allItems[i] = i;

		std::ranges::sort(ids);
		if (direction > 0)
			std::ranges::reverse(ids);

		auto any = false;
		for (const auto& id : ids) {
			if (id + direction < 0 || id + direction >= allItems.size())
				continue;

			any = true;
			std::swap(allItems[id], allItems[id + direction]);
		}
		if (!any)
			return 0;

		std::map<LPARAM, size_t> newLocations;
		for (int i = 0, i_ = static_cast<int>(m_pActiveFace->Elements.size()); i < i_; i++) {
			LVITEMW lvi{ .mask = LVIF_PARAM, .iItem = i };
			ListView_GetItem(m_hFaceElementsListView, &lvi);
			newLocations[lvi.lParam] = allItems[i];
		}

		const auto listViewSortCallback = [](LPARAM lp1, LPARAM lp2, LPARAM ctx) -> int {
			auto& newLocations = *reinterpret_cast<std::map<LPARAM, size_t>*>(ctx);
			const auto nl = newLocations[lp1];
			const auto nr = newLocations[lp2];
			return nl == nr ? 0 : (nl > nr ? 1 : -1);
		};
		ListView_SortItems(m_hFaceElementsListView, listViewSortCallback, &newLocations);

		std::ranges::sort(m_pActiveFace->Elements, [&newLocations](const FontSet::Face::Element& l, const FontSet::Face::Element& r) -> bool {
			const auto nl = newLocations[reinterpret_cast<LPARAM>(l.RuntimeTag.get())];
			const auto nr = newLocations[reinterpret_cast<LPARAM>(r.RuntimeTag.get())];
			return nl < nr;
		});

		if (!m_bChanged)
			UpdateWindowTitle(true);
		m_pActiveFace->RefreshFont();
		Redraw();

		return 0;
	}

	LRESULT Menu_Edit_CreateEmptyCopyFromSelection() {
		if (!m_pActiveFace)
			return 0;

		const auto refIndex = ListView_GetNextItem(m_hFaceElementsListView, -1, LVNI_SELECTED);
		if (refIndex == -1)
			return 0;

		ListView_SetItemState(m_hFaceElementsListView, -1, 0, LVIS_SELECTED);

		auto& elements = m_pActiveFace->Elements;
		auto& ref = elements[refIndex];
		elements.emplace(elements.begin(), FontSet::Face::Element{
			.RuntimeTag = std::make_shared<void*>(),
			.Size = ref.Size,
			.RendererSpecific = {
				.Empty = {
					.Ascent = ref.EnsureFont()->GetAscent() + ref.WrapModifiers.BaselineShift,
					.LineHeight = ref.EnsureFont()->GetLineHeight(),
				},
			},
			});

		LVITEMW lvi{ .mask = LVIF_PARAM | LVIF_STATE, .iItem = 0, .state = LVIS_SELECTED, .stateMask = LVIS_SELECTED, .lParam = reinterpret_cast<LPARAM>(elements.front().RuntimeTag.get()) };
		ListView_InsertItem(m_hFaceElementsListView, &lvi);
		elements[0].UpdateText(m_hFaceElementsListView, lvi.iItem);

		if (!m_bChanged)
			UpdateWindowTitle(true);
		m_pActiveFace->RefreshFont();
		Redraw();

		return 0;
	}

	LRESULT Menu_View_NextOrPrevFont(int direction) {
		auto i = ListBox_GetCurSel(m_hFacesListBox);
		if (i + direction < 0 || i + direction >= static_cast<int>(m_fontSet.Faces.size()))
			return 0;

		i += direction;
		ListBox_SetCurSel(m_hFacesListBox, i);
		m_pActiveFace = &m_fontSet.Faces[i];
		ReflectFontElementChange();
		return 0;
	}

	LRESULT Menu_View_WordWrap() {
		m_bWordWrap = !m_bWordWrap;
		Redraw();
		return 0;
	}

	LRESULT Menu_View_Kerning() {
		m_bKerning = !m_bKerning;
		Redraw();
		return 0;
	}

	LRESULT Menu_View_ShowLineMetrics() {
		m_bShowLineMetrics = !m_bShowLineMetrics;
		Redraw();
		return 0;
	}

	LRESULT Menu_View_Zoom(int zoom) {
		m_nZoom = zoom;
		Redraw();
		return 0;
	}

	std::pair<std::vector<std::shared_ptr<XivRes::FontdataStream>>, std::vector<std::shared_ptr<XivRes::MemoryMipmapStream>>> CompileCurrentFontSet() {
		ShowWindow(m_hWnd, SW_HIDE);
		const auto hideWhilePacking = XivRes::Internal::CallOnDestruction([this]() { ShowWindow(m_hWnd, SW_SHOW); });

		XivRes::FontGenerator::FontdataPacker packer;
		{
			std::cout << "Resolving kerning pairs...\n";
			XivRes::Internal::ThreadPool pool;
			for (auto& face : m_fontSet.Faces)
				pool.Submit([&face]() { void(face.EnsureFont()->GetAllKerningPairs()); });
			pool.SubmitDoneAndWait();
		}

		for (auto& face : m_fontSet.Faces)
			packer.AddFont(face.MergedFont);

		packer.Compile();

		std::cout << std::endl;
		while (!packer.Wait(std::chrono::milliseconds(200))) {
			const auto descr = packer.GetProgressDescription();
			const auto prog = packer.GetProgress() * 100.f;
			std::cout << std::format("\rCompiling: {:>3.2f}%: {}", prog, descr);
		}
		std::cout << std::endl;

		const auto& fdts = packer.GetTargetFonts();
		const auto& mips = packer.GetMipmapStreams();
		if (mips.empty())
			throw std::runtime_error("No mipmap produced");

		return std::make_pair(fdts, mips);
	}

	LRESULT Menu_Export_Preview() {
		using namespace XivRes::FontGenerator;

		try {
			const auto [fdts, mips] = CompileCurrentFontSet();

			auto texturesAll = std::make_shared<XivRes::TextureStream>(mips[0]->Type, mips[0]->Width, mips[0]->Height, 1, 1, mips.size());
			for (size_t i = 0; i < mips.size(); i++)
				texturesAll->SetMipmap(0, i, mips[i]);

			std::vector<std::pair<std::string, std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>>> resultFonts;
			for (size_t i = 0; i < fdts.size(); i++)
				resultFonts.emplace_back(m_fontSet.Faces[i].Name, std::make_shared<XivRes::FontGenerator::GameFontdataFixedSizeFont>(fdts[i], mips, m_fontSet.Faces[i].Name, ""));

			ExportPreviewWindow::ShowNew(std::move(resultFonts));
			std::thread([texturesAll]() {XivRes::Internal::ShowTextureStream(*texturesAll); }).detach();
			return 0;

		} catch (const std::exception& e) {
			MessageBoxW(m_hWnd, std::format(L"Failed to export: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
			return 1;
		}
	}

	LRESULT Menu_Export_Raw() {
		using namespace XivRes::FontGenerator;

		try {
			IFileOpenDialogPtr pDialog;
			DWORD dwFlags;
			SuccessOrThrow(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
			SuccessOrThrow(pDialog->SetTitle(L"Export raw"));
			SuccessOrThrow(pDialog->GetOptions(&dwFlags));
			SuccessOrThrow(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS));
			switch (SuccessOrThrow(pDialog->Show(m_hWnd), { HRESULT_FROM_WIN32(ERROR_CANCELLED) })) {
				case HRESULT_FROM_WIN32(ERROR_CANCELLED):
					return 0;
			}

			IShellItemPtr pResult;
			PWSTR pszFileName;
			SuccessOrThrow(pDialog->GetResult(&pResult));
			SuccessOrThrow(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
			if (!pszFileName)
				throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");

			std::unique_ptr<std::remove_pointer<PWSTR>::type, decltype(CoTaskMemFree)*> pszFileNamePtr(pszFileName, CoTaskMemFree);
			const auto basePath = std::filesystem::path(pszFileName);

			const auto [fdts, mips] = CompileCurrentFontSet();

			std::vector<char> buf(32768);

			XivRes::TextureStream textureOne(mips[0]->Type, mips[0]->Width, mips[0]->Height, 1, 1, 1);
			for (size_t i = 0; i < mips.size(); i++) {
				textureOne.SetMipmap(0, 0, mips[i]);

				std::ofstream out(basePath / std::format(m_fontSet.TexFilenameFormat, i + 1), std::ios::binary);
				size_t pos = 0;
				for (size_t read, pos = 0; (read = textureOne.ReadStreamPartial(pos, &buf[0], buf.size())); pos += read)
					out.write(&buf[0], read);
			}

			for (size_t i = 0; i < fdts.size(); i++) {
				std::ofstream out(basePath / std::format("{}.fdt", m_fontSet.Faces[i].Name), std::ios::binary);
				size_t pos = 0;
				for (size_t read, pos = 0; (read = fdts[i]->ReadStreamPartial(pos, &buf[0], buf.size())); pos += read)
					out.write(&buf[0], read);
			}

		} catch (const std::exception& e) {
			MessageBoxW(m_hWnd, std::format(L"Failed to export: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
			return 1;
		}

		return 0;
	}

	enum class CompressionMode {
		CompressWhilePacking,
		CompressAfterPacking,
		DoNotCompress,
	};

	LRESULT Menu_Export_TTMP(CompressionMode compressionMode) {
		using namespace XivRes::FontGenerator;
		static constexpr COMDLG_FILTERSPEC fileTypes[] = {
			{ L"TTMP2 file (*.ttmp2)", L"*.ttmp2" },
			{ L"ZIP file (*.zip)", L"*.zip" },
			{ L"All files (*.*)", L"*" },
		};
		const auto fileTypesSpan = std::span(fileTypes);

		std::wstring tmpPath, finalPath;
		try {
			nlohmann::json json;
			to_json(json, m_fontSet);
			const auto dump = json.dump();

			IFileSaveDialogPtr pDialog;
			DWORD dwFlags;
			SuccessOrThrow(pDialog.CreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER));
			SuccessOrThrow(pDialog->SetFileTypes(static_cast<UINT>(fileTypesSpan.size()), fileTypesSpan.data()));
			SuccessOrThrow(pDialog->SetFileTypeIndex(0));
			SuccessOrThrow(pDialog->SetTitle(L"Save"));
			SuccessOrThrow(pDialog->SetFileName(std::format(L"{}.ttmp2", m_path.filename().replace_extension(L"").wstring()).c_str()));
			SuccessOrThrow(pDialog->SetDefaultExtension(L"json"));
			SuccessOrThrow(pDialog->GetOptions(&dwFlags));
			SuccessOrThrow(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
			switch (SuccessOrThrow(pDialog->Show(m_hWnd), { HRESULT_FROM_WIN32(ERROR_CANCELLED) })) {
				case HRESULT_FROM_WIN32(ERROR_CANCELLED):
					return 0;
			}

			{
				IShellItemPtr pResult;
				PWSTR pszFileName;
				SuccessOrThrow(pDialog->GetResult(&pResult));
				SuccessOrThrow(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
				if (!pszFileName)
					throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");

				finalPath = pszFileName;
				CoTaskMemFree(pszFileName);
			}

			LARGE_INTEGER li;
			QueryPerformanceCounter(&li);
			tmpPath = std::format(L"{}.{:016X}.tmp", finalPath, li.QuadPart);

			zlib_filefunc64_def ffunc;
			fill_win32_filefunc64W(&ffunc);
			zipFile zf = zipOpen2_64(&tmpPath[0], APPEND_STATUS_CREATE, nullptr, &ffunc);
			if (!zf)
				throw std::runtime_error("Failed to create target file");
			auto zfclose = XivRes::Internal::CallOnDestruction([&zf, &dump]() { zipClose(zf, &dump[0]); });

			const auto [fdts, mips] = CompileCurrentFontSet();

			std::stringstream ttmpl;
			std::vector<char> ttmpd;

			for (size_t i = 0; i < fdts.size(); i++) {
				const auto targetFileName = std::format("common/font/{}.fdt", m_fontSet.Faces[i].Name);

				std::cout << std::format("Packing file: {}\n", targetFileName);
				XivRes::BinaryPackedFileStream packedStream(targetFileName, fdts[i], compressionMode == CompressionMode::CompressWhilePacking ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION);

				const auto pos = ttmpd.size();
				ttmpl << nlohmann::json::object({
					{ "DatFile", "000000" },
					{ "FullPath", targetFileName },
					{ "ModOffset", pos },
					{ "ModSize", packedStream.StreamSize() },
					}) << std::endl;

				ttmpd.resize(pos + packedStream.StreamSize());
				ReadStream(packedStream, 0, std::span(ttmpd).subspan(pos));
			}

			for (size_t i = 0; i < mips.size(); i++) {
				const auto targetFileName = std::format("common/font/{}", std::format(m_fontSet.TexFilenameFormat, i + 1));
				std::cout << std::format("Packing file: {}\n", targetFileName);

				const auto& mip = mips[i];
				auto textureOne = std::make_shared<XivRes::TextureStream>(mip->Type, mip->Width, mip->Height, 1, 1, 1);
				textureOne->SetMipmap(0, 0, mip);

				XivRes::TexturePackedFileStream packedStream(targetFileName, std::move(textureOne), compressionMode == CompressionMode::CompressWhilePacking ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION);

				const auto pos = ttmpd.size();
				ttmpl << nlohmann::json::object({
					{ "DatFile", "000000" },
					{ "FullPath", targetFileName },
					{ "ModOffset", pos },
					{ "ModSize", packedStream.StreamSize() },
					}) << std::endl;

				ttmpd.resize(pos + packedStream.StreamSize());
				ReadStream(packedStream, 0, std::span(ttmpd).subspan(pos));
			}

			zip_fileinfo zi{};
			zi.tmz_date.tm_sec = zi.tmz_date.tm_min = zi.tmz_date.tm_hour =
				zi.tmz_date.tm_mday = zi.tmz_date.tm_mon = zi.tmz_date.tm_year = 0;
			zi.dosDate = 0;
			zi.internal_fa = 0;
			zi.external_fa = 0;
			FILETIME ft{}, ftLocal{};
			GetSystemTimeAsFileTime(&ft);
			FileTimeToLocalFileTime(&ft, &ftLocal);
			FileTimeToDosDateTime(&ftLocal, ((LPWORD)&zi.dosDate) + 1, ((LPWORD)&zi.dosDate) + 0);

			uint64_t totalWriteSize, written = 0;
			const auto ChunkSize = 256 * 1024;
			{
				const auto ttmpls = ttmpl.str();
				totalWriteSize = ttmpls.size() + ttmpd.size();

				if (const auto err = zipOpenNewFileInZip3_64(zf, "TTMPL.mpl", &zi,
					NULL, 0, NULL, 0, NULL /* comment*/,
					compressionMode == CompressionMode::CompressAfterPacking ? Z_DEFLATED : 0,
					compressionMode == CompressionMode::CompressAfterPacking ? Z_BEST_COMPRESSION : 0,
					0, -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
					nullptr, crc32_z(0, reinterpret_cast<const uint8_t*>(&ttmpls[0]), ttmpls.size()), 0))
					throw std::runtime_error(std::format("Failed to create TTMPL.mpl inside zip: {}", err));
				std::unique_ptr<decltype(zf), decltype(zipCloseFileInZip)*> ziClose(&zf, zipCloseFileInZip);

				for (size_t offset = 0; offset < ttmpls.size(); offset += ChunkSize) {
					const auto writeSize = (std::min<size_t>)(ttmpls.size() - offset, ChunkSize);
					if (const auto err = zipWriteInFileInZip(zf, reinterpret_cast<const uint8_t*>(&ttmpls[offset]), static_cast<uint32_t>(writeSize)))
						throw std::runtime_error(std::format("Failed to write to TTMPL.mpl inside zip: {}", err));

					written += writeSize;
					std::cout << std::format("\rSaving: {:>3.2f}%", 100.f * written / totalWriteSize);
				}
			}
			{
				if (const auto err = zipOpenNewFileInZip3_64(zf, "TTMPD.mpd", &zi,
					NULL, 0, NULL, 0, NULL /* comment*/,
					compressionMode == CompressionMode::CompressAfterPacking ? Z_DEFLATED : 0,
					compressionMode == CompressionMode::CompressAfterPacking ? Z_BEST_COMPRESSION : 0,
					0, -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
					nullptr, crc32_z(0, reinterpret_cast<const uint8_t*>(&ttmpd[0]), ttmpd.size()), 0))
					throw std::runtime_error(std::format("Failed to create TTMPD.mpd inside zip: {}", err));
				std::unique_ptr<decltype(zf), decltype(zipCloseFileInZip)*> ziClose(&zf, zipCloseFileInZip);

				for (size_t offset = 0; offset < ttmpd.size(); offset += ChunkSize) {
					const auto writeSize = (std::min<size_t>)(ttmpd.size() - offset, ChunkSize);
					if (const auto err = zipWriteInFileInZip(zf, reinterpret_cast<const uint8_t*>(&ttmpd[offset]), static_cast<uint32_t>(writeSize)))
						throw std::runtime_error(std::format("Failed to write to TTMPL.mpl inside zip: {}", err));

					written += writeSize;
					std::cout << std::format("\rSaving: {:>3.2f}%", 100.f * written / totalWriteSize);
				}
			}
			std::cout << std::endl;

			zfclose.Clear();

			std::cout << "Remaining temporary file to final file.\n";
			try {
				std::filesystem::remove(finalPath);
			} catch (...) {
				// ignore
			}
			std::filesystem::rename(tmpPath, finalPath);
			std::cout << "Done!\n";

		} catch (const std::exception& e) {
			if (!tmpPath.empty()) {
				try {
					std::filesystem::remove(tmpPath);
				} catch (...) {
					// ignore
				}
			}
			MessageBoxW(m_hWnd, std::format(L"Failed to export: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
			return 1;
		}

		return 0;
	}

	LRESULT Edit_OnCommand(uint16_t commandId) {
		switch (commandId) {
			case EN_CHANGE:
				if (m_pActiveFace) {
					auto& face = *m_pActiveFace;
					face.PreviewText = XivRes::Unicode::Convert<std::string>(GetWindowString(m_hEdit));
					if (!m_bChanged)
						UpdateWindowTitle(true);
					Redraw();
				} else
					return -1;
				return 0;
		}

		return 0;
	}

	LRESULT FaceListBox_OnCommand(uint16_t commandId) {
		switch (commandId) {
			case LBN_SELCHANGE:
			{
				const auto iItem = ListBox_GetCurSel(m_hFacesListBox);
				if (iItem != LB_ERR) {
					m_pActiveFace = &m_fontSet.Faces[iItem];
					ReflectFontElementChange();
				}
				return 0;
			}
		}
		return 0;
	}

	LRESULT FaceElementsListView_OnBeginDrag(NM_LISTVIEW& nmlv) {
		if (!m_pActiveFace)
			return -1;

		m_bIsReorderingFaceElementList = true;
		SetCapture(m_hWnd);
		SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
		return 0;
	}

	bool FaceElementsListView_OnDragProcessMouseUp(int16_t x, int16_t y) {
		if (!m_bIsReorderingFaceElementList)
			return false;

		m_bIsReorderingFaceElementList = false;
		ReleaseCapture();
		FaceElementsListView_DragProcessDragging(x, y);
		return true;
	}

	bool FaceElementsListView_OnDragProcessMouseMove(int16_t x, int16_t y) {
		if (!m_bIsReorderingFaceElementList)
			return false;

		FaceElementsListView_DragProcessDragging(x, y);
		return true;
	}

	bool FaceElementsListView_DragProcessDragging(int16_t x, int16_t y) {
		// Determine the dropped item
		LVHITTESTINFO lvhti{
			.pt = {x, y},
		};
		ClientToScreen(m_hWnd, &lvhti.pt);
		ScreenToClient(m_hFaceElementsListView, &lvhti.pt);
		ListView_HitTest(m_hFaceElementsListView, &lvhti);

		// Out of the ListView?
		if (lvhti.iItem == -1) {
			POINT ptRef{};
			ListView_GetItemPosition(m_hFaceElementsListView, 0, &ptRef);
			if (lvhti.pt.y < ptRef.y)
				lvhti.iItem = 0;
			else {
				RECT rcListView;
				GetClientRect(m_hFaceElementsListView, &rcListView);
				ListView_GetItemPosition(m_hFaceElementsListView, ListView_GetItemCount(m_hFaceElementsListView) - 1, &ptRef);
				if (lvhti.pt.y >= ptRef.y || lvhti.pt.y >= rcListView.bottom - rcListView.top)
					lvhti.iItem = ListView_GetItemCount(m_hFaceElementsListView) - 1;
				else
					return false;
			}
		}

		auto& face = *m_pActiveFace;

		// Rearrange the items
		std::set<int> sourceIndices;
		for (auto iPos = -1; -1 != (iPos = ListView_GetNextItem(m_hFaceElementsListView, iPos, LVNI_SELECTED));)
			sourceIndices.insert(iPos);

		struct SortInfoType {
			std::vector<int> oldIndices;
			std::vector<int> newIndices;
			std::map<LPARAM, int> sourcePtrs;
		} sortInfo;
		sortInfo.oldIndices.reserve(face.Elements.size());
		for (int i = 0, i_ = static_cast<int>(face.Elements.size()); i < i_; i++) {
			LVITEMW lvi{ .mask = LVIF_PARAM, .iItem = i };
			ListView_GetItem(m_hFaceElementsListView, &lvi);
			sortInfo.sourcePtrs[lvi.lParam] = i;
			if (!sourceIndices.contains(i))
				sortInfo.oldIndices.push_back(i);
		}

		{
			int i = (std::max<int>)(0, 1 + lvhti.iItem - static_cast<int>(sourceIndices.size()));
			for (const auto sourceIndex : sourceIndices)
				sortInfo.oldIndices.insert(sortInfo.oldIndices.begin() + i++, sourceIndex);
		}

		sortInfo.newIndices.resize(sortInfo.oldIndices.size());
		auto changed = false;
		for (int i = 0, i_ = static_cast<int>(sortInfo.oldIndices.size()); i < i_; i++) {
			changed |= i != sortInfo.oldIndices[i];
			sortInfo.newIndices[sortInfo.oldIndices[i]] = i;
		}

		if (!changed)
			return false;

		const auto listViewSortCallback = [](LPARAM lp1, LPARAM lp2, LPARAM ctx) -> int {
			auto& sortInfo = *reinterpret_cast<SortInfoType*>(ctx);
			const auto il = sortInfo.sourcePtrs[lp1];
			const auto ir = sortInfo.sourcePtrs[lp2];
			const auto nl = sortInfo.newIndices[il];
			const auto nr = sortInfo.newIndices[ir];
			return nl == nr ? 0 : (nl > nr ? 1 : -1);
		};
		ListView_SortItems(m_hFaceElementsListView, listViewSortCallback, &sortInfo);

		std::ranges::sort(face.Elements, [&sortInfo](const FontSet::Face::Element& l, const FontSet::Face::Element& r) -> bool {
			const auto il = sortInfo.sourcePtrs[reinterpret_cast<LPARAM>(l.RuntimeTag.get())];
			const auto ir = sortInfo.sourcePtrs[reinterpret_cast<LPARAM>(r.RuntimeTag.get())];
			const auto nl = sortInfo.newIndices[il];
			const auto nr = sortInfo.newIndices[ir];
			return nl < nr;
		});

		if (!m_bChanged)
			UpdateWindowTitle(true);
		m_pActiveFace->RefreshFont();
		Redraw();

		return true;
	}

	LRESULT FaceElementsListView_OnDblClick(NMITEMACTIVATE& nmia) {
		if (nmia.iItem == -1)
			return 0;
		if (m_pActiveFace == nullptr)
			return 0;
		if (nmia.iItem >= m_pActiveFace->Elements.size())
			return 0;
		ShowEditor(m_pActiveFace->Elements[nmia.iItem]);
		return 0;
	}

	void OpenFile(std::filesystem::path path) {
		const auto s = ReadStreamIntoVector<char>(XivRes::FileStream(path));
		const auto j = nlohmann::json::parse(s.begin(), s.end());
		FontSet fontSet;
		from_json(j, fontSet);

		for (auto& face : fontSet.Faces) {
			face.RuntimeTag = std::make_shared<void*>();
			for (auto& element : face.Elements)
				element.RuntimeTag = std::make_shared<void*>();
		}

		m_fontSet = std::move(fontSet);
		m_path = std::move(path);
		m_bPathIsNotReal = false;
		m_bChanged = false;

		ReflectFontSetChange();
		UpdateWindowTitle(false);
	}

	void UpdateWindowTitle(bool markChanged) {
		m_bChanged |= markChanged;

		SetWindowTextW(m_hWnd, std::format(
			L"{} - Font Editor{}",
			m_path.filename().c_str(),
			m_bChanged ? L" *" : L""
		).c_str());
	}

	bool ConfirmIfChanged() {
		if (m_bChanged) {
			switch (MessageBoxW(m_hWnd, L"There are unsaved changes. Do you want to save your changes?", GetWindowString(m_hWnd).c_str(), MB_YESNOCANCEL)) {
				case IDYES:
					if (Menu_File_Save())
						return true;
					break;
				case IDNO:
					break;
				case IDCANCEL:
					return true;
			}
		}
		return false;
	}

	void ShowEditor(FontSet::Face::Element& element) {
		auto& pEditorWindow = m_editors[element.RuntimeTag.get()];
		if (pEditorWindow && pEditorWindow->IsOpened()) {
			pEditorWindow->Activate();
		} else {
			pEditorWindow = std::make_unique<FontSet::Face::Element::EditorDialog>(m_hWnd, element, [this, &element]() {
				PostMessageW(m_hWnd, WmOnFaceElementChanged, 0, reinterpret_cast<LPARAM>(&element));
			});
		}
	}

	void ReflectFontSetChange() {
		void** currentTag = nullptr;
		if (int curSel = ListBox_GetCurSel(m_hFacesListBox); curSel != LB_ERR)
			currentTag = reinterpret_cast<void**>(ListBox_GetItemData(m_hFacesListBox, curSel));

		ListBox_ResetContent(m_hFacesListBox);
		auto selectionRestored = false;
		for (int i = 0, i_ = static_cast<int>(m_fontSet.Faces.size()); i < i_; i++) {
			auto& face = m_fontSet.Faces[i];
			ListBox_AddString(m_hFacesListBox, XivRes::Unicode::Convert<std::wstring>(face.Name).c_str());
			ListBox_SetItemData(m_hFacesListBox, i, face.RuntimeTag.get());
			if (currentTag == face.RuntimeTag.get()) {
				ListBox_SetCurSel(m_hFacesListBox, i);
				selectionRestored = true;
			}
		}

		if (!selectionRestored) {
			if (m_fontSet.Faces.empty())
				m_pActiveFace = nullptr;
			else {
				ListBox_SetCurSel(m_hFacesListBox, 0);
				m_pActiveFace = m_fontSet.Faces.empty() ? nullptr : &m_fontSet.Faces[0];
			}

			ReflectFontElementChange();
			Redraw();
		}
	}

	void ReflectFontElementChange() {
		if (!m_pActiveFace) {
			ListView_DeleteAllItems(m_hFaceElementsListView);
			return;
		}

		std::map<LPARAM, size_t> activeElementTags;
		for (auto& element : m_pActiveFace->Elements) {
			const auto lp = reinterpret_cast<LPARAM>(element.RuntimeTag.get());
			activeElementTags[lp] = activeElementTags.size();
			if (LVFINDINFOW lvfi{ .flags = LVFI_PARAM, .lParam = lp };
				ListView_FindItem(m_hFaceElementsListView, -1, &lvfi) != -1)
				continue;

			LVITEMW lvi{ .mask = LVIF_PARAM, .iItem = ListView_GetItemCount(m_hFaceElementsListView), .lParam = lp };
			ListView_InsertItem(m_hFaceElementsListView, &lvi);
			element.UpdateText(m_hFaceElementsListView, lvi.iItem);
		}

		for (int i = 0, i_ = ListView_GetItemCount(m_hFaceElementsListView); i < i_;) {
			LVITEMW lvi{ .mask = LVIF_PARAM, .iItem = i };
			ListView_GetItem(m_hFaceElementsListView, &lvi);
			if (!activeElementTags.contains(lvi.lParam)) {
				i_--;
				ListView_DeleteItem(m_hFaceElementsListView, i);
			} else
				i++;
		}

		const auto listViewSortCallback = [](LPARAM lp1, LPARAM lp2, LPARAM ctx) -> int {
			const auto& activeElementTags = *reinterpret_cast<const std::map<LPARAM, size_t>*>(ctx);
			const auto nl = activeElementTags.at(lp1);
			const auto nr = activeElementTags.at(lp2);
			return nl == nr ? 0 : (nl > nr ? 1 : -1);
		};
		ListView_SortItems(m_hFaceElementsListView, listViewSortCallback, &activeElementTags);

		Edit_SetText(m_hEdit, XivRes::Unicode::Convert<std::wstring>(m_pActiveFace->PreviewText).c_str());
		Redraw();
	}

	void Redraw() {
		if (!m_pActiveFace)
			return;

		m_bNeedRedraw = true;
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}

	LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_COMMAND:
				switch (LOWORD(wParam)) {
					case Id_Edit: return Edit_OnCommand(HIWORD(wParam));
					case Id_FaceListBox: return FaceListBox_OnCommand(HIWORD(wParam));
					case ID_FILE_NEW_MAINGAMEFONT: return Menu_File_New(XivRes::GameFontType::font);
					case ID_FILE_NEW_LOBBYFONT: return Menu_File_New(XivRes::GameFontType::font_lobby);
					case ID_FILE_NEW_CHNAXIS: return Menu_File_New(XivRes::GameFontType::chn_axis);
					case ID_FILE_NEW_KRNAXIS: return Menu_File_New(XivRes::GameFontType::krn_axis);
					case ID_FILE_OPEN: return Menu_File_Open();
					case ID_FILE_SAVE: return Menu_File_Save();
					case ID_FILE_SAVEAS: return Menu_File_SaveAs(true);
					case ID_FILE_SAVECOPYAS: return Menu_File_SaveAs(false);
					case ID_FILE_EXIT: return Menu_File_Exit();
					case ID_EDIT_ADD:return Menu_Edit_Add();
					case ID_EDIT_CUT: return Menu_Edit_Cut();
					case ID_EDIT_COPY: return Menu_Edit_Copy();
					case ID_EDIT_PASTE: return Menu_Edit_Paste();
					case ID_EDIT_DELETE: return Menu_Edit_Delete();
					case ID_EDIT_SELECTALL:return Menu_Edit_SelectAll();
					case ID_EDIT_DETAILS:return Menu_Edit_Details();
					case ID_EDIT_DECREASEBASELINESHIFT: return Menu_Edit_ChangeParams(-1, 0, 0, 0);
					case ID_EDIT_INCREASEBASELINESHIFT: return Menu_Edit_ChangeParams(+1, 0, 0, 0);
					case ID_EDIT_DECREASEHORIZONTALOFFSET: return Menu_Edit_ChangeParams(0, -1, 0, 0);
					case ID_EDIT_INCREASEHORIZONTALOFFSET: return Menu_Edit_ChangeParams(0, +1, 0, 0);
					case ID_EDIT_DECREASELETTERSPACING: return Menu_Edit_ChangeParams(0, 0, -1, 0);
					case ID_EDIT_INCREASELETTERSPACING: return Menu_Edit_ChangeParams(0, 0, +1, 0);
					case ID_EDIT_DECREASEFONTSIZEBY1: return Menu_Edit_ChangeParams(0, 0, 0, -1.f);
					case ID_EDIT_INCREASEFONTSIZEBY1: return Menu_Edit_ChangeParams(0, 0, 0, +1.f);
					case ID_EDIT_DECREASEFONTSIZEBY0_2: return Menu_Edit_ChangeParams(0, 0, 0, -0.2f);
					case ID_EDIT_INCREASEFONTSIZEBY0_2: return Menu_Edit_ChangeParams(0, 0, 0, +0.2f);
					case ID_EDIT_TOGGLEOVERWRITE: return Menu_Edit_ToggleOverwrite();
					case ID_EDIT_MOVEUP: return Menu_Edit_MoveUpOrDown(-1);
					case ID_EDIT_MOVEDOWN: return Menu_Edit_MoveUpOrDown(+1);
					case ID_EDIT_CREATEEMPTYCOPYFROMSELECTION: return Menu_Edit_CreateEmptyCopyFromSelection();
					case ID_VIEW_PREVIOUSFONT: return Menu_View_NextOrPrevFont(-1);
					case ID_VIEW_NEXTFONT: return Menu_View_NextOrPrevFont(1);
					case ID_VIEW_WORDWRAP: return Menu_View_WordWrap();
					case ID_VIEW_KERNING: return Menu_View_Kerning();
					case ID_VIEW_SHOWLINEMETRICS: return Menu_View_ShowLineMetrics();
					case ID_VIEW_100: return Menu_View_Zoom(1);
					case ID_VIEW_200: return Menu_View_Zoom(2);
					case ID_VIEW_300: return Menu_View_Zoom(3);
					case ID_VIEW_400: return Menu_View_Zoom(4);
					case ID_VIEW_500: return Menu_View_Zoom(5);
					case ID_VIEW_600: return Menu_View_Zoom(6);
					case ID_VIEW_700: return Menu_View_Zoom(7);
					case ID_VIEW_800: return Menu_View_Zoom(8);
					case ID_VIEW_900: return Menu_View_Zoom(9);
					case ID_EXPORT_PREVIEW: return Menu_Export_Preview();
					case ID_EXPORT_RAW: return Menu_Export_Raw();
					case ID_EXPORT_TOTTMP_COMPRESSWHILEPACKING: return Menu_Export_TTMP(CompressionMode::CompressWhilePacking);
					case ID_EXPORT_TOTTMP_COMPRESSAFTERPACKING: return Menu_Export_TTMP(CompressionMode::CompressAfterPacking);
					case ID_EXPORT_TOTTMP_DONOTCOMPRESS: return Menu_Export_TTMP(CompressionMode::DoNotCompress);
				}
				break;

			case WM_NOTIFY:
				switch (auto& hdr = *reinterpret_cast<NMHDR*>(lParam); hdr.idFrom) {
					case Id_FaceElementListView:
						switch (hdr.code) {
							case LVN_BEGINDRAG: return FaceElementsListView_OnBeginDrag(*(reinterpret_cast<NM_LISTVIEW*>(lParam)));
							case NM_DBLCLK: return FaceElementsListView_OnDblClick(*(reinterpret_cast<NMITEMACTIVATE*>(lParam)));
						}
						break;
				}
				return 0;

			case WM_CREATE: return Window_OnCreate(hwnd);
			case WmOnFaceElementChanged: return Window_OnFaceElementChanged(*reinterpret_cast<FontSet::Face::Element*>(lParam));
			case WM_MOUSEMOVE: return Window_OnMouseMove(static_cast<uint16_t>(wParam), LOWORD(lParam), HIWORD(lParam));
			case WM_LBUTTONUP: return Window_OnMouseLButtonUp(static_cast<uint16_t>(wParam), LOWORD(lParam), HIWORD(lParam));
			case WM_SIZE: return Window_OnSize();
			case WM_PAINT: return Window_OnPaint();
			case WM_INITMENUPOPUP: return Window_OnInitMenuPopup(reinterpret_cast<HMENU>(wParam), LOWORD(lParam), !!HIWORD(lParam));
			case WM_CLOSE: return Menu_File_Exit();
			case WM_DESTROY: return Window_OnDestroy();
		}

		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	static LRESULT WINAPI WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		return reinterpret_cast<FontEditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))->WndProc(hwnd, msg, wParam, lParam);
	}

	static LRESULT WINAPI WndProcInitial(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg != WM_NCCREATE)
			return DefWindowProcW(hwnd, msg, wParam, lParam);

		const auto pCreateStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
		const auto pImpl = reinterpret_cast<FontEditorWindow*>(pCreateStruct->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pImpl));
		SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcStatic));

		return pImpl->WndProc(hwnd, msg, wParam, lParam);
	}

public:
	FontEditorWindow(std::span<wchar_t*> args)
		: m_args(args) {
		WNDCLASSEXW wcex{};
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.hInstance = g_hInstance;
		wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wcex.hbrBackground = GetStockBrush(WHITE_BRUSH);
		wcex.lpszClassName = ClassName;
		wcex.lpszMenuName = MAKEINTRESOURCEW(IDR_FONTEDITOR);
		wcex.lpfnWndProc = FontEditorWindow::WndProcInitial;

		RegisterClassExW(&wcex);

		CreateWindowExW(0, ClassName, L"Font Editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
			CW_USEDEFAULT, CW_USEDEFAULT, 1200, 640,
			nullptr, nullptr, nullptr, this);
	}

	bool ConsumeDialogMessage(MSG& msg) override {
		if (IsDialogMessage(m_hWnd, &msg))
			return true;

		for (const auto& e : m_editors | std::views::values)
			if (e && e->IsOpened() && e->ConsumeDialogMessage(msg))
				return true;

		return false;
	}

	bool ConsumeAccelerator(MSG& msg) override {
		if (!m_hAccelerator)
			return false;

		if (GetForegroundWindow() != m_hWnd)
			return false;

		if (msg.message == WM_KEYDOWN && msg.hwnd == m_hEdit) {
			if (msg.wParam == VK_RETURN || msg.wParam == VK_INSERT || msg.wParam == VK_DELETE)
				return false;
			if (!(GetKeyState(VK_CONTROL) & 0x8000) || msg.wParam == 'C' || msg.wParam == 'X' || msg.wParam == 'V' || msg.wParam == 'A')
				return false;
		}
		return TranslateAccelerator(m_hWnd, m_hAccelerator, &msg);
	}
};

int wmain(int argc, wchar_t** argv) {
	g_hInstance = GetModuleHandle(nullptr);
	FontEditorWindow window(std::span(argv, argc));
	for (MSG msg{}; GetMessageW(&msg, nullptr, 0, 0);) {
		if (BaseWindow::ConsumeMessage(msg))
			continue;

		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return 0;
}
