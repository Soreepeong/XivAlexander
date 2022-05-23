#include <ranges>

#include "pch.h"
#include "resource.h"

#include "XivRes/FontGenerator/WrappingFixedSizeFont.h"
#include "XivRes/FontGenerator/DirectWriteFixedSizeFont.h"
#include "XivRes/FontGenerator/FontdataPacker.h"
#include "XivRes/FontGenerator/FreeTypeFixedSizeFont.h"
#include "XivRes/FontGenerator/GameFontdataFixedSizeFont.h"
#include "XivRes/FontGenerator/MergedFixedSizeFont.h"
#include "XivRes/FontGenerator/TextMeasurer.h"
#include "XivRes/FontGenerator/WrappingFixedSizeFont.h"

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
		KerningPairCount,
		Overwrite,
		Renderer,
		Lookup,
	};
};

struct FaceElement {
	static const std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont> Test(XivRes::FontGenerator::GameFontFamily family, float size) {
		static std::map<XivRes::GameFontType, std::weak_ptr<XivRes::FontGenerator::GameFontdataSet>> s_fontSet;

		std::shared_ptr<XivRes::FontGenerator::GameFontdataSet> strong;

		switch (family) {
			case XivRes::FontGenerator::GameFontFamily::AXIS:
			case XivRes::FontGenerator::GameFontFamily::Jupiter:
			case XivRes::FontGenerator::GameFontFamily::JupiterN:
			case XivRes::FontGenerator::GameFontFamily::MiedingerMid:
			case XivRes::FontGenerator::GameFontFamily::Meidinger:
			case XivRes::FontGenerator::GameFontFamily::TrumpGothic:
			{
				auto& weak = s_fontSet[XivRes::GameFontType::font];
				strong = weak.lock();
				if (!strong)
					weak = strong = std::make_shared<XivRes::FontGenerator::GameFontdataSet>(XivRes::GameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)").GetFonts(XivRes::GameFontType::font));
				break;
			}

			case XivRes::FontGenerator::GameFontFamily::ChnAXIS:
			{
				auto& weak = s_fontSet[XivRes::GameFontType::chn_axis];
				strong = weak.lock();
				if (!strong)
					weak = strong = std::make_shared<XivRes::FontGenerator::GameFontdataSet>(XivRes::GameReader(R"(C:\Program Files (x86)\SNDA\FFXIV\game)").GetFonts(XivRes::GameFontType::chn_axis));
				break;
			}

			case XivRes::FontGenerator::GameFontFamily::KrnAXIS:
			{
				auto& weak = s_fontSet[XivRes::GameFontType::krn_axis];
				strong = weak.lock();
				if (!strong)
					weak = strong = std::make_shared<XivRes::FontGenerator::GameFontdataSet>(XivRes::GameReader(R"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game)").GetFonts(XivRes::GameFontType::krn_axis));
				break;
			}
		}

		return strong ? strong->GetFont(family, size) : nullptr;
	}

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

	struct PrerenderedGameFontDef {
		XivRes::FontGenerator::GameFontFamily FontFamily;
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

		std::pair<std::shared_ptr<XivRes::IStream>, int> ResolveStream() const {
			using namespace XivRes::FontGenerator;

			IDWriteFactory3Ptr factory;
			SuccessOrThrow(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(&factory)));

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
		PrerenderedGameFontDef PrerenderedGame;
		XivRes::FontGenerator::FreeTypeFixedSizeFont::CreateStruct FreeType;
		XivRes::FontGenerator::DirectWriteFixedSizeFont::CreateStruct DirectWrite;
	};

	std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont> BaseFont;
	std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont> Font;
	float Size = 0.f;
	bool Overwrite = false;
	XivRes::FontGenerator::WrapModifiers WrapModifiers;
	RendererEnum Renderer = RendererEnum::PrerenderedGameInstallation;

	FontLookupStruct Lookup;
	RendererSpecificStruct RendererSpecific;

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
					BaseFont = Test(RendererSpecific.PrerenderedGame.FontFamily, Size);
					break;

				case RendererEnum::DirectWrite:
				{
					auto [pStream, index] = Lookup.ResolveStream();
					BaseFont = std::make_shared<XivRes::FontGenerator::DirectWriteFixedSizeFont>(pStream, index, Size, RendererSpecific.DirectWrite);
					break;
				}

				case RendererEnum::FreeType:
				{
					auto [pStream, index] = Lookup.ResolveStream();
					BaseFont = std::make_shared<XivRes::FontGenerator::FreeTypeFixedSizeFont>(*pStream, index, Size, RendererSpecific.FreeType);
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

	static void AddToList(HWND hListView, int nListIndex, FaceElement element) {
		auto pElement = new FaceElement(std::move(element));
		LVITEMW item{};
		item.mask = LVIF_PARAM;
		item.iItem = nListIndex;
		item.lParam = reinterpret_cast<LPARAM>(pElement);
		ListView_InsertItem(hListView, &item);

		if (!pElement->Font)
			pElement->RefreshFont();
		pElement->UpdateText(hListView, nListIndex);
	}

	void UpdateText(HWND hListView, int nListIndex) const {
		std::wstring buf;
		ListView_SetItemText(hListView, nListIndex, ListViewCols::FamilyName, &(buf = XivRes::Unicode::Convert<std::wstring>(Font->GetFamilyName()))[0]);
		ListView_SetItemText(hListView, nListIndex, ListViewCols::SubfamilyName, &(buf = XivRes::Unicode::Convert<std::wstring>(Font->GetSubfamilyName()))[0]);
		ListView_SetItemText(hListView, nListIndex, ListViewCols::Size, &(buf = std::format(L"{:g}px", Font->GetSize()))[0]);
		ListView_SetItemText(hListView, nListIndex, ListViewCols::LineHeight, &(buf = std::format(L"{}px", Font->GetLineHeight()))[0]);
		if (WrapModifiers.BaselineShift) {
			ListView_SetItemText(hListView, nListIndex, ListViewCols::Ascent, &(buf = std::format(L"{}px({:+}px)", BaseFont->GetAscent(), WrapModifiers.BaselineShift))[0]);
		} else {
			ListView_SetItemText(hListView, nListIndex, ListViewCols::Ascent, &(buf = std::format(L"{}px", BaseFont->GetAscent()))[0]);
		}
		ListView_SetItemText(hListView, nListIndex, ListViewCols::HorizontalOffset, &(buf = std::format(L"{}px (avg {}, max {})", WrapModifiers.HorizontalOffset, BaseFont->GetRecommendedHorizontalOffset(), BaseFont->GetMaximumRequiredHorizontalOffset()))[0]);
		ListView_SetItemText(hListView, nListIndex, ListViewCols::LetterSpacing, &(buf = std::format(L"{}px", WrapModifiers.LetterSpacing))[0]);
		ListView_SetItemText(hListView, nListIndex, ListViewCols::Codepoints, &(buf = GetRangeRepresentation())[0]);
		ListView_SetItemText(hListView, nListIndex, ListViewCols::GlyphCount, &(buf = std::format(L"{}", Font->GetAllCodepoints().size()))[0]);
		ListView_SetItemText(hListView, nListIndex, ListViewCols::KerningPairCount, &(buf = std::format(L"{}", Font->GetAllKerningPairs().size()))[0]);
		ListView_SetItemText(hListView, nListIndex, ListViewCols::Overwrite, &(buf = Overwrite ? L"Yes" : L"No")[0]);
		ListView_SetItemText(hListView, nListIndex, ListViewCols::Gamma, &(buf = std::format(L"{:g}", WrapModifiers.Gamma))[0]);
		ListView_SetItemText(hListView, nListIndex, ListViewCols::Renderer, &(buf = GetRendererRepresentation())[0]);
		ListView_SetItemText(hListView, nListIndex, ListViewCols::Lookup, &(buf = GetLookupRepresentation())[0]);
	}

	std::wstring GetRangeRepresentation() const {
		if (WrapModifiers.Codepoints.empty())
			return L"(All)";

		if (WrapModifiers.Codepoints.size() == 1) {
			std::vector<char32_t> charVec(BaseFont->GetAllCodepoints().begin(), BaseFont->GetAllCodepoints().end());
			for (const auto& [c1, c2] : WrapModifiers.Codepoints) {
				const auto left = std::ranges::lower_bound(charVec, c1);
				const auto right = std::ranges::upper_bound(charVec, c2);
				const auto count = right - left;

				const auto blk = std::lower_bound(XivRes::Unicode::UnicodeBlocks::Blocks.begin(), XivRes::Unicode::UnicodeBlocks::Blocks.end(), c1, [](const auto& l, const auto& r) { return l.First < r; });
				if (blk != XivRes::Unicode::UnicodeBlocks::Blocks.end() && blk->First == c1 && blk->Last == c2) {
					if (c1 == c2) {
						return std::format(
							L"U+{:04X} {} ({}) [{}]",
							static_cast<int>(c1),
							XivRes::Unicode::Convert<std::wstring>(blk->Name),
							count,
							XivRes::Unicode::ConvertFromChar<std::wstring>(c1)
						).c_str();
					} else {
						return std::format(
							L"U+{:04X} ~ U+{:04X} {} ({}) [{}] ~ [{}]",
							static_cast<int>(c1),
							static_cast<int>(c2),
							XivRes::Unicode::Convert<std::wstring>(blk->Name),
							count,
							XivRes::Unicode::ConvertFromChar<std::wstring>(c1),
							XivRes::Unicode::ConvertFromChar<std::wstring>(c2)
						);
					}
				} else if (c1 == c2) {
					return std::format(
						L"U+{:04X} ({}) [{}]",
						static_cast<int>(c1),
						count,
						XivRes::Unicode::ConvertFromChar<std::wstring>(c1)
					);
				} else {
					return std::format(
						L"U+{:04X} ~ U+{:04X} ({}) [{}] ~ [{}]",
						static_cast<int>(c1),
						static_cast<int>(c2),
						count,
						XivRes::Unicode::ConvertFromChar<std::wstring>(c1),
						XivRes::Unicode::ConvertFromChar<std::wstring>(c2)
					);
				}
			}
		}

		return std::format(L"{} ranges", WrapModifiers.Codepoints.size());
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

class FaceElement::EditorDialog {
	FaceElement& m_element;
	FaceElement m_elementOriginal;
	HWND m_hParentWnd;
	bool m_bOpened = false;
	std::function<void()> m_onFontChanged;

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
		HWND UnicodeBlockSearchAdd = GetDlgItem(Window, IDC_BUTTON_UNICODEBLOCKS_ADD);
		HWND CustomRangeEdit = GetDlgItem(Window, IDC_EDIT_ADDCUSTOMRANGE_INPUT);
		HWND CustomRangePreview = GetDlgItem(Window, IDC_EDIT_ADDCUSTOMRANGE_PREVIEW);
		HWND CustomRangeAdd = GetDlgItem(Window, IDC_BUTTON_ADDCUSTOMRANGE_ADD);
	} *m_controls = nullptr;

public:
	EditorDialog(HWND hParentWnd, FaceElement& element, std::function<void()> onFontChanged)
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
		return 0;
	}

	INT_PTR FontCombo_OnCommand(uint16_t notiCode) {
		if (notiCode != CBN_SELCHANGE)
			return 0;

		RepopulateFontSubComboBox();
		m_element.Lookup.Name = XivRes::Unicode::Convert<std::string>(GetWindowString(m_controls->FontCombo));
		OnBaseFontChanged();
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

		if (const auto n = GetWindowFloat(m_controls->AdjustmentGammaEdit); n != m_element.WrapModifiers.Gamma) {
			m_element.WrapModifiers.Gamma = n;
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
					if (*it < 0x10000)
						containingChars.push_back(static_cast<wchar_t>(*it));
					else {
						const auto ptr = containingChars.size();
						containingChars.resize(ptr + 2);
						containingChars.resize(ptr + XivRes::Unicode::Encode(&containingChars[ptr], *it));
					}
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
			if (c1 == c2)
				description += std::format(L"U+{:04X}", static_cast<uint32_t>(c1));
			else
				description += std::format(L"U+{:04X} ~ U+{:04X}", static_cast<uint32_t>(c1), static_cast<uint32_t>(c2));
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
		ComboBox_AddString(m_controls->FontRendererCombo, L"Empty");
		ComboBox_AddString(m_controls->FontRendererCombo, L"Prerendered (Game)");
		ComboBox_AddString(m_controls->FontRendererCombo, L"DirectWrite");
		ComboBox_AddString(m_controls->FontRendererCombo, L"FreeType");
		ComboBox_SetCurSel(m_controls->FontRendererCombo, static_cast<int>(m_element.Renderer));
		ComboBox_SetText(m_controls->FontCombo, XivRes::Unicode::Convert<std::wstring>(m_element.Lookup.Name).c_str());
		ComboBox_SetText(m_controls->FontSizeCombo, std::format(L"{:g}", m_element.Size).c_str());
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
		Edit_SetText(m_controls->AdjustmentGammaEdit, std::format(L"{:g}", m_element.WrapModifiers.Gamma).c_str());

		for (const auto& controlHwnd : {
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
					L"U+{:04X} {} ({}) [{}]",
					static_cast<int>(c1),
					XivRes::Unicode::Convert<std::wstring>(block->Name),
					count,
					XivRes::Unicode::ConvertFromChar<std::wstring>(c1)
				).c_str());
			} else {
				ListBox_AddString(m_controls->CodepointsList, std::format(
					L"U+{:04X} ~ U+{:04X} {} ({}) [{}] ~ [{}]",
					static_cast<int>(c1),
					static_cast<int>(c2),
					XivRes::Unicode::Convert<std::wstring>(block->Name),
					count,
					XivRes::Unicode::ConvertFromChar<std::wstring>(c1),
					XivRes::Unicode::ConvertFromChar<std::wstring>(c2)
				).c_str());
			}
		} else if (c1 == c2) {
			ListBox_AddString(m_controls->CodepointsList, std::format(
				L"U+{:04X} ({}) [{}]",
				static_cast<int>(c1),
				count,
				XivRes::Unicode::ConvertFromChar<std::wstring>(c1)
			).c_str());
		} else {
			ListBox_AddString(m_controls->CodepointsList, std::format(
				L"U+{:04X} ~ U+{:04X} ({}) [{}] ~ [{}]",
				static_cast<int>(c1),
				static_cast<int>(c2),
				count,
				XivRes::Unicode::ConvertFromChar<std::wstring>(c1),
				XivRes::Unicode::ConvertFromChar<std::wstring>(c2)
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
				L"U+{:04X} ~ U+{:04X} {} ({}) [{}] ~ [{}]",
				static_cast<uint32_t>(block.First),
				static_cast<uint32_t>(block.Last),
				XivRes::Unicode::Convert<std::wstring>(nameView),
				right - left,
				XivRes::Unicode::ConvertFromChar<std::wstring>(block.First),
				XivRes::Unicode::ConvertFromChar<std::wstring>(block.Last)
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
				EnableWindow(m_controls->AdjustmentGammaEdit, TRUE);
				EnableWindow(m_controls->CodepointsList, TRUE);
				EnableWindow(m_controls->CodepointsDeleteButton, TRUE);
				EnableWindow(m_controls->CodepointsOverwriteCheck, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchNameEdit, TRUE);
				EnableWindow(m_controls->UnicodeBlockSearchResultList, TRUE);
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
				ComboBox_AddString(m_controls->FontCombo, L"AXIS");
				ComboBox_AddString(m_controls->FontCombo, L"Jupiter");
				ComboBox_AddString(m_controls->FontCombo, L"JupiterN");
				ComboBox_AddString(m_controls->FontCombo, L"Meidinger");
				ComboBox_AddString(m_controls->FontCombo, L"MiedingerMid");
				ComboBox_AddString(m_controls->FontCombo, L"TrumpGothic");
				ComboBox_AddString(m_controls->FontCombo, L"ChnAXIS");
				ComboBox_AddString(m_controls->FontCombo, L"KrnAXIS");
				ComboBox_SetCurSel(m_controls->FontCombo, 0);
				break;

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

				for (const auto size : {
					8.f, 9.f, 9.6f, 10.f, 11.f, 12.f, 13.f, 14.f, 15.f, 16.f, 17.f, 18.f, 18.4f, 19.f,
					20.f, 21.f, 22.f, 23.f, 24.f, 26.f, 28.f, 30.f, 32.f, 34.f, 36.f, 38.f, 40.f, 45.f, 46.f, 68.f, 90.f })
					ComboBox_AddString(m_controls->FontSizeCombo, std::format(L"{:g}", size).c_str());

				break;
			}

			default:
				break;
		}

		RefreshUnicodeBlockSearchResults();
	}

	void OnBaseFontChanged() {
		m_element.RefreshBaseFont();
		if (m_onFontChanged)
			m_onFontChanged();
	}

	void OnWrappedFontChanged() {
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

	static std::wstring GetWindowString(HWND hwnd) {
		std::wstring buf;
		int capacity = 128;
		do {
			capacity *= 2;
			buf.resize(capacity);
			buf.resize(GetWindowTextW(hwnd, &buf[0], capacity));
		} while (buf.size() == capacity);
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
};

class WindowImpl {
	static constexpr auto ClassName = L"FontEditorWindowClass";
	static constexpr float NoBaseFontSizes[]{ 9.6f, 10.f, 12.f, 14.f, 16.f, 18.f, 18.4f, 20.f, 23.f, 34.f, 36.f, 40.f, 45.f, 46.f, 68.f, 90.f, };

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

	std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont> m_fontMerged;
	// std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont> m_fontPacked;

	static std::weak_ptr<ATOM> s_pAtom;
	std::shared_ptr<ATOM> m_pAtom;
	std::shared_ptr<XivRes::MemoryMipmapStream> m_pMipmap;
	std::map<FaceElement*, std::unique_ptr<FaceElement::EditorDialog>> m_editors;
	bool m_bNeedRedraw = false;

	HWND m_hWnd{};
	HFONT m_hUiFont{};

	HWND m_hFacesListBox{};
	HWND m_hFaceElementsListView{};
	HWND m_hEdit{};

	int m_nDrawLeft{};
	int m_nDrawTop{};

	LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_CREATE:
				m_hWnd = hwnd;
				return OnCreate();

			case WM_COMMAND:
				switch (LOWORD(wParam)) {
					case Id_Edit:
						switch (HIWORD(wParam)) {
							case EN_CHANGE:
								m_bNeedRedraw = true;
								InvalidateRect(hwnd, nullptr, FALSE);
								return 0;
						}
						break;
				}
				break;

			case WM_NOTIFY:
				return OnNotify(*reinterpret_cast<NMHDR*>(lParam));

			case WM_MOUSEMOVE:
				return OnMouseMove(static_cast<uint16_t>(wParam), LOWORD(lParam), HIWORD(lParam));

			case WM_LBUTTONUP:
				return OnMouseLButtonUp(static_cast<uint16_t>(wParam), LOWORD(lParam), HIWORD(lParam));

			case WM_SIZE:
				return OnSize();

			case WM_PAINT:
				return OnPaint();

			case WM_DESTROY:
				DeleteFont(m_hUiFont);
				PostQuitMessage(0);
				return 0;
		}

		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	LRESULT OnCreate() {
		NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
		SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
		m_hUiFont = CreateFontIndirectW(&ncm.lfMessageFont);

		m_hFacesListBox = CreateWindowExW(0, WC_LISTBOXW, nullptr,
			WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | LBS_NOINTEGRALHEIGHT,
			0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_FaceListBox), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
		m_hFaceElementsListView = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
			WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | LVS_REPORT,
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
		AddColumn(ListViewCols::Size, 40, L"Size");
		AddColumn(ListViewCols::LineHeight, 80, L"Line Height");
		AddColumn(ListViewCols::Ascent, 80, L"Ascent");
		AddColumn(ListViewCols::HorizontalOffset, 120, L"Horizontal Offset");
		AddColumn(ListViewCols::LetterSpacing, 100, L"Letter Spacing");
		AddColumn(ListViewCols::Gamma, 60, L"Gamma");
		AddColumn(ListViewCols::Codepoints, 80, L"Codepoints");
		AddColumn(ListViewCols::Overwrite, 70, L"Overwrite");
		AddColumn(ListViewCols::GlyphCount, 60, L"Glyphs");
		AddColumn(ListViewCols::KerningPairCount, 60, L"Kerning Pairs");
		AddColumn(ListViewCols::Renderer, 180, L"Renderer");
		AddColumn(ListViewCols::Lookup, 300, L"Lookup");

		FaceElement::AddToList(m_hFaceElementsListView, ListView_GetItemCount(m_hFaceElementsListView), {
			.Size = 36.f,
			.Renderer = FaceElement::RendererEnum::PrerenderedGameInstallation,
			.Lookup = {
				.Name = "AXIS",
			},
			.RendererSpecific = {
				.PrerenderedGame = {
					.FontFamily = XivRes::FontGenerator::GameFontFamily::AXIS,
				},
			},
			});

		//FaceElement::AddToList(m_hFaceElementsListView, ListView_GetItemCount(m_hFaceElementsListView), {
		//	.Size = 36.f,
		//	.Renderer = FaceElement::RendererEnum::FreeType,
		//	.Lookup = {
		//		.Name = "Segoe UI",
		//	},
		//	.RendererSpecific = {
		//		.FreeType = {
		//			.LoadFlags = FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT
		//		},
		//	},
		//	});

		//FaceElement::AddToList(m_hFaceElementsListView, ListView_GetItemCount(m_hFaceElementsListView), {
		//	.Size = 36.f,
		//	.WrapModifiers = {
		//		.LetterSpacing = -1,
		//	},
		//	.Renderer = FaceElement::RendererEnum::DirectWrite,
		//	.Lookup = {
		//		.Name = "Source Han Sans K",
		//	},
		//	.RendererSpecific = {
		//		.DirectWrite = {
		//			.RenderMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,
		//			.MeasureMode = DWRITE_MEASURING_MODE_GDI_CLASSIC,
		//			.GridFitMode = DWRITE_GRID_FIT_MODE_ENABLED,
		//		},
		//	},
		//	});

		FaceElement::AddToList(m_hFaceElementsListView, ListView_GetItemCount(m_hFaceElementsListView), {
			.Size = 36.f,
			.WrapModifiers = {
				.LetterSpacing = -1,
			},
			.Renderer = FaceElement::RendererEnum::DirectWrite,
			.Lookup = {
				.Name = "Sarabun",
			},
			.RendererSpecific = {
				.DirectWrite = {
					.RenderMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,
					.MeasureMode = DWRITE_MEASURING_MODE_GDI_CLASSIC,
					.GridFitMode = DWRITE_GRID_FIT_MODE_ENABLED,
				},
			},
			});

		OnSize();
		OnFontChanged();

		ShowWindow(m_hWnd, SW_SHOW);

		return 0;
	}

	LRESULT OnSize() {
		RECT rc;
		GetClientRect(m_hWnd, &rc);

		auto hdwp = BeginDeferWindowPos(Id__Last);
		hdwp = DeferWindowPos(hdwp, m_hFacesListBox, nullptr, 0, 0, FaceListBoxWidth, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
		hdwp = DeferWindowPos(hdwp, m_hFaceElementsListView, nullptr, FaceListBoxWidth, 0, (std::max<int>)(0, rc.right - rc.left - FaceListBoxWidth), ListViewHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		hdwp = DeferWindowPos(hdwp, m_hEdit, nullptr, FaceListBoxWidth, ListViewHeight, (std::max<int>)(0, rc.right - rc.left - FaceListBoxWidth), EditHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		EndDeferWindowPos(hdwp);

		m_nDrawLeft = FaceListBoxWidth;
		m_nDrawTop = static_cast<int>(EditHeight + ListViewHeight);
		m_pMipmap = std::make_shared<XivRes::MemoryMipmapStream>(
			(std::max<int>)(32, rc.right - rc.left - FaceListBoxWidth),
			(std::max<int>)(32, rc.bottom - rc.top - m_nDrawTop),
			1,
			XivRes::TextureFormat::A8R8G8B8);
		m_bNeedRedraw = true;

		return 0;
	}

	LRESULT OnPaint() {
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

			std::wstring s(Edit_GetTextLength(m_hEdit) + 1, L'\0');
			s.resize(Edit_GetText(m_hEdit, &s[0], static_cast<int>(s.size())));

			auto m1 = XivRes::FontGenerator::TextMeasurer(*m_fontMerged).WithMaxWidth(m_pMipmap->Width - pad * 2).Measure(&s[0], s.size());
			// auto m2 = XivRes::FontGenerator::TextMeasurer(*m_fontPacked).WithMaxWidth(m_pMipmap->Width - pad * 2).Measure(&s[0], s.size());

			m1.DrawTo(*m_pMipmap, *m_fontMerged, 16, 16, { 0xFF, 0xFF, 0xFF, 0xFF }, { 0, 0, 0, 0 });
			// m2.DrawTo(*m_pMipmap, *m_fontPacked, 16, 16 + m1.Occupied.GetHeight(), { 0xFF, 0xFF, 0xFF, 0xFF }, { 0, 0, 0, 0 });
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

	class ListViewDragStruct {
		WindowImpl& Window;

		bool IsDragging = false;
		bool IsChanged = false;

	public:
		ListViewDragStruct(WindowImpl& window) : Window(window) {}

		LRESULT OnListViewBeginDrag(NM_LISTVIEW& nmlv) {
			IsDragging = true;
			IsChanged = false;
			SetCapture(Window.m_hWnd);
			SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
			return 0;
		}

		bool ProcessMouseUp(int16_t x, int16_t y) {
			if (!IsDragging)
				return false;

			IsDragging = false;
			ReleaseCapture();
			if (IsChanged |= ProcessDragging(x, y)) {
				Window.OnFontChanged();
			}
			return true;
		}

		bool ProcessMouseMove(int16_t x, int16_t y) {
			if (!IsDragging)
				return false;

			IsChanged |= ProcessDragging(x, y);
			return true;
		}

	private:
		bool ProcessDragging(int16_t x, int16_t y) {
			const auto hListView = Window.m_hFaceElementsListView;

			// Determine the dropped item
			LVHITTESTINFO lvhti{
				.pt = {x, y},
			};
			ClientToScreen(Window.m_hWnd, &lvhti.pt);
			ScreenToClient(hListView, &lvhti.pt);
			ListView_HitTest(hListView, &lvhti);

			// Out of the ListView?
			if (lvhti.iItem == -1) {
				POINT ptRef{};
				ListView_GetItemPosition(hListView, 0, &ptRef);
				if (lvhti.pt.y < ptRef.y)
					lvhti.iItem = 0;
				else {
					RECT rcListView;
					GetClientRect(hListView, &rcListView);
					ListView_GetItemPosition(hListView, ListView_GetItemCount(hListView) - 1, &ptRef);
					if (lvhti.pt.y >= ptRef.y || lvhti.pt.y >= rcListView.bottom - rcListView.top)
						lvhti.iItem = ListView_GetItemCount(hListView) - 1;
					else
						return false;
				}
			}

			// Rearrange the items
			std::set<int> sourceIndices;
			for (auto iPos = -1; -1 != (iPos = ListView_GetNextItem(hListView, iPos, LVNI_SELECTED));)
				sourceIndices.insert(iPos);

			struct SortInfoType {
				std::vector<int> oldIndices;
				std::vector<int> newIndices;
				std::map<LPARAM, int> sourcePtrs;
			} sortInfo;
			sortInfo.oldIndices.reserve(ListView_GetItemCount(hListView));
			for (int i = 0, i_ = ListView_GetItemCount(hListView); i < i_; i++) {
				LVITEMW lvi{ .mask = LVIF_PARAM, .iItem = i, };
				ListView_GetItem(hListView, &lvi);
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

			auto k = [](LPARAM lp1, LPARAM lp2, LPARAM ctx) -> int {
				auto& sortInfo = *reinterpret_cast<SortInfoType*>(ctx);
				const auto il = sortInfo.sourcePtrs[lp1];
				const auto ir = sortInfo.sourcePtrs[lp2];
				const auto nl = sortInfo.newIndices[il];
				const auto nr = sortInfo.newIndices[ir];
				return nl == nr ? 0 : (nl > nr ? 1 : -1);
			};
			ListView_SortItems(hListView, k, &sortInfo);

			return true;
		}
	} m_listViewDrag{ *this };

	LRESULT OnListViewDblClick(NMITEMACTIVATE& nmia) {
		if (nmia.iItem == -1)
			return 0;

		LVITEMW lvi{ .mask = LVIF_PARAM, .iItem = nmia.iItem };
		ListView_GetItem(m_hFaceElementsListView, &lvi);

		auto& element = *reinterpret_cast<FaceElement*>(lvi.lParam);
		auto& pEditorWindow = m_editors[&element];
		if (pEditorWindow && pEditorWindow->IsOpened())
			pEditorWindow->Activate();
		else {
			pEditorWindow = std::make_unique<FaceElement::EditorDialog>(m_hWnd, element, [this, &element, iItem = lvi.iItem]() {
				OnFontChanged();
				element.UpdateText(m_hFaceElementsListView, iItem);
			});
		}
		return 0;
	}

	LRESULT OnNotify(NMHDR& hdr) {
		switch (hdr.idFrom) {
			case Id_FaceElementListView:
				switch (hdr.code) {
					case LVN_BEGINDRAG:
						return m_listViewDrag.OnListViewBeginDrag(*(reinterpret_cast<NM_LISTVIEW*>(&hdr)));

					case NM_DBLCLK:
						OnListViewDblClick(*(reinterpret_cast<NMITEMACTIVATE*>(&hdr)));
						return 0;
				}
				break;
		}

		return 0;
	}

	LRESULT OnMouseMove(uint16_t states, int16_t x, int16_t y) {
		if (m_listViewDrag.ProcessMouseMove(x, y))
			return 0;

		return 0;
	}

	LRESULT OnMouseLButtonUp(uint16_t states, int16_t x, int16_t y) {
		if (m_listViewDrag.ProcessMouseUp(x, y))
			return 0;

		return 0;
	}

	static LRESULT WINAPI WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		return reinterpret_cast<WindowImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))->WndProc(hwnd, msg, wParam, lParam);
	}

public:
	static LRESULT WINAPI WndProcInitial(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg != WM_NCCREATE)
			return DefWindowProcW(hwnd, msg, wParam, lParam);

		const auto pCreateStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
		const auto pImpl = reinterpret_cast<WindowImpl*>(pCreateStruct->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pImpl));
		SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcStatic));

		return pImpl->WndProc(hwnd, msg, wParam, lParam);
	}

	WindowImpl() {
		auto atom = s_pAtom.lock();
		if (!atom) {
			WNDCLASSEXW wcex{};
			wcex.cbSize = sizeof(WNDCLASSEX);
			wcex.style = CS_HREDRAW | CS_VREDRAW;
			wcex.hInstance = GetModuleHandleW(nullptr);
			wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wcex.hbrBackground = GetStockBrush(WHITE_BRUSH);
			wcex.lpszClassName = ClassName;
			wcex.lpszMenuName = MAKEINTRESOURCEW(IDR_FONTEDITOR);
			wcex.lpfnWndProc = WindowImpl::WndProcInitial;

			const auto res = RegisterClassExW(&wcex);
			if (!res)
				throw std::runtime_error("");

			s_pAtom = m_pAtom = std::make_shared<ATOM>(res);
		}

		CreateWindowExW(0, reinterpret_cast<LPCWSTR>(*m_pAtom), L"Font Editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
			CW_USEDEFAULT, CW_USEDEFAULT, 1200, 640,
			nullptr, nullptr, nullptr, this);
	}

	void OnFontChanged() {
		std::vector<std::pair<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>, bool>> mergeFontList;

		for (int i = 0, i_ = ListView_GetItemCount(m_hFaceElementsListView); i < i_; i++) {
			LVITEMW item{};
			item.mask = LVIF_PARAM;
			item.iItem = i;
			item.iSubItem = 0;
			ListView_GetItem(m_hFaceElementsListView, &item);

			auto& element = *reinterpret_cast<FaceElement*>(item.lParam);
			mergeFontList.emplace_back(element.Font, element.Overwrite);
		}

		auto merge = std::make_shared<XivRes::FontGenerator::MergedFixedSizeFont>(std::move(mergeFontList));

		//XivRes::FontGenerator::FontdataPacker packer;
		//packer.AddFont(merge);
		//auto [fdts, texs] = packer.Compile();
		//auto res = std::make_shared<XivRes::TextureStream>(texs[0]->Type, texs[0]->Width, texs[0]->Height, 1, 1, texs.size());
		//for (size_t i = 0; i < texs.size(); i++)
		//	res->SetMipmap(0, i, texs[i]);
		//m_fontMerged = std::make_shared<XivRes::FontGenerator::GameFontdataFixedSizeFont>(fdts[0], texs, "Test", "Test");

		m_fontMerged = merge;

		m_bNeedRedraw = true;
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}

	void TestFontLsb(int horizontalOffset) {
		const auto& cps = m_fontMerged->GetAllCodepoints();
		std::set<const XivRes::Unicode::UnicodeBlocks::BlockDefinition*> visitedBlocks;
		std::set<char32_t> ignoreDist;

		int nOffenders = 0;
		for (const auto& c : cps) {
			if (ignoreDist.contains(c))
				continue;

			XivRes::FontGenerator::GlyphMetrics gm;
			m_fontMerged->GetGlyphMetrics(c, gm);
			if (gm.X1 < -horizontalOffset) {
				auto& block = XivRes::Unicode::UnicodeBlocks::GetCorrespondingBlock(c);
				if (block.Flags & XivRes::Unicode::UnicodeBlocks::RTL)
					continue;

				nOffenders++;
				if (!visitedBlocks.contains(&block)) {
					std::cout << std::format("{}\n", block.Name);
					visitedBlocks.insert(&block);
				}

				union {
					uint32_t u8u32v;
					char b[5]{};
				};
				u8u32v = XivRes::Unicode::CodePointToUtf8Uint32(c);
				std::reverse(b, b + strlen(b));

				if (gm.AdvanceX)
					std::cout << std::format("* {}\tU+{:04x} {}~{} +{}\n", b, (int)c, gm.X1, gm.X2, gm.AdvanceX);
				else
					std::cout << std::format("* {}\tU+{:04x} {}~{}\n", b, (int)c, gm.X1, gm.X2);
			}
		}

		std::cout << std::format("Total {} * {}\n", nOffenders, cps.size());
	}

	~WindowImpl() {
		auto atom = *m_pAtom;
		m_pAtom = nullptr;
		if (!s_pAtom.lock())
			UnregisterClassW(reinterpret_cast<LPCWSTR>(atom), nullptr);
	}

	bool ConsumeDialogMessage(MSG& msg) {
		if (IsDialogMessage(m_hWnd, &msg))
			return true;

		for (const auto& e : m_editors | std::views::values)
			if (e && e->IsOpened() && e->ConsumeDialogMessage(msg))
				return true;

		return false;
	}
};

std::weak_ptr<ATOM> WindowImpl::s_pAtom;

void ShowExampleWindow() {
	WindowImpl window;
	for (MSG msg{}; GetMessageW(&msg, nullptr, 0, 0);) {
		if (!window.ConsumeDialogMessage(msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
}
