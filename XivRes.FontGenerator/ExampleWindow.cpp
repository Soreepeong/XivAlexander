#include "pch.h"

#include "XivRes/FontGenerator/WrappingFixedSizeFont.h"
#include "XivRes/FontGenerator/FontdataPacker.h"
#include "XivRes/FontGenerator/FreeTypeFixedSizeFont.h"
#include "XivRes/FontGenerator/GameFontdataFixedSizeFont.h"
#include "XivRes/FontGenerator/MergedFixedSizeFont.h"
#include "XivRes/FontGenerator/TextMeasurer.h"

class WindowImpl {
	static constexpr auto ClassName = L"ExampleWindowClass";
	static constexpr float NoBaseFontSizes[]{ 9.6f, 10.f, 12.f, 14.f, 16.f, 18.f, 18.4f, 20.f, 23.f, 34.f, 36.f, 40.f, 45.f, 46.f, 68.f, 90.f, };

	const XivRes::FontGenerator::GameFontdataSet fontGlos = XivRes::GameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)").GetFonts(XivRes::GameFontType::font);
	const XivRes::FontGenerator::GameFontdataSet fontChns = XivRes::GameReader(R"(C:\Program Files (x86)\SNDA\FFXIV\game)").GetFonts(XivRes::GameFontType::chn_axis);
	const XivRes::FontGenerator::GameFontdataSet fontKrns = XivRes::GameReader(R"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game)").GetFonts(XivRes::GameFontType::krn_axis);

	enum : size_t {
		Id_None,
		Id_Combo,
		Id_List,
		Id_Edit,
		Id__Last,
	};
	static constexpr auto ListViewHeight = 100;
	static constexpr auto EditHeight = 100;

	std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont> m_fontBase;
	std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont> m_fontMerged;
	std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont> m_fontPacked;

	static std::weak_ptr<ATOM> s_pAtom;
	std::shared_ptr<ATOM> m_pAtom;
	std::shared_ptr<XivRes::MemoryMipmapStream> m_pMipmap;
	bool m_bNeedRedraw = false;

	HWND m_hWnd{};
	HFONT m_hUiFont{};

	HWND m_hBaseFontStatic{};
	HWND m_hBaseFontCombo{};
	HWND m_hExtraFontsList{};
	HWND m_hEdit{};

	int m_nDrawTop{};

	LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_CREATE:
				m_hWnd = hwnd;
				return OnCreate();

			case WM_COMMAND:
				switch (LOWORD(wParam)) {
					case Id_Combo:
						switch (HIWORD(wParam)) {
							case CBN_SELCHANGE:
								LoadFonts();
								m_bNeedRedraw = true;
								InvalidateRect(hwnd, nullptr, FALSE);
								return 0;
						}
						break;

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

			case WM_SIZE:
				OnSize();
				return 0;

			case WM_PAINT:
				OnPaint();
				return 0;

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

		m_hBaseFontStatic = CreateWindowExW(0, WC_STATICW, L"Base font: ",
			WS_CHILD | WS_TABSTOP | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
			0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_None), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
		m_hBaseFontCombo = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
			WS_CHILD | WS_TABSTOP | WS_VISIBLE | CBS_HASSTRINGS | CBS_DROPDOWNLIST,
			0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_Combo), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
		m_hExtraFontsList = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
			WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | LVS_REPORT,
			0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_List), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
		m_hEdit = CreateWindowExW(0, WC_EDITW, nullptr,
			WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_AUTOVSCROLL | ES_MULTILINE | ES_WANTRETURN,
			0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_Edit), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);

		ListView_SetExtendedListViewStyle(m_hExtraFontsList, LVS_EX_FULLROWSELECT);

		SendMessage(m_hBaseFontStatic, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
		SendMessage(m_hBaseFontCombo, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
		SendMessage(m_hExtraFontsList, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
		SendMessage(m_hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);

		SetWindowSubclass(m_hEdit, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
			if (msg == WM_GETDLGCODE && wParam == VK_TAB)
				return 0;
			if (msg == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && !(GetKeyState(VK_LWIN) & 0x8000) && !(GetKeyState(VK_RWIN) & 0x8000))
				Edit_SetSel(hWnd, 0, Edit_GetTextLength(hWnd));
			return DefSubclassProc(hWnd, msg, wParam, lParam);
		}, 1, 0);

		{
			int colIndex = 0;
			LVCOLUMNW col{};
			col.mask = LVCF_TEXT | LVCF_WIDTH;

			col.cx = 180;
			col.pszText = const_cast<wchar_t*>(L"Name");
			ListView_InsertColumn(m_hExtraFontsList, colIndex++, &col);

			col.cx = 40;
			col.pszText = const_cast<wchar_t*>(L"Size");
			ListView_InsertColumn(m_hExtraFontsList, colIndex++, &col);

			col.cx = 80;
			col.pszText = const_cast<wchar_t*>(L"Line Height");
			ListView_InsertColumn(m_hExtraFontsList, colIndex++, &col);

			col.cx = 60;
			col.pszText = const_cast<wchar_t*>(L"Ascent");
			ListView_InsertColumn(m_hExtraFontsList, colIndex++, &col);

			col.cx = 100;
			col.pszText = const_cast<wchar_t*>(L"Letter Spacing");
			ListView_InsertColumn(m_hExtraFontsList, colIndex++, &col);

			col.cx = 80;
			col.pszText = const_cast<wchar_t*>(L"¥Ä Current Y");
			ListView_InsertColumn(m_hExtraFontsList, colIndex++, &col);

			col.cx = 170;
			col.pszText = const_cast<wchar_t*>(L"Codepoints/Unicode Blocks");
			ListView_InsertColumn(m_hExtraFontsList, colIndex++, &col);

			col.cx = 60;
			col.pszText = const_cast<wchar_t*>(L"Glyphs");
			ListView_InsertColumn(m_hExtraFontsList, colIndex++, &col);

			col.cx = 70;
			col.pszText = const_cast<wchar_t*>(L"Overwrite");
			ListView_InsertColumn(m_hExtraFontsList, colIndex++, &col);

			col.cx = 180;
			col.pszText = const_cast<wchar_t*>(L"Renderer");
			ListView_InsertColumn(m_hExtraFontsList, colIndex++, &col);

			col.cx = 60;
			col.pszText = const_cast<wchar_t*>(L"Gamma");
			ListView_InsertColumn(m_hExtraFontsList, colIndex++, &col);

			LVITEMW item{};
			item.mask = LVIF_TEXT | LVIF_PARAM;
			item.iItem = 0;
			item.iSubItem = 0;
			item.pszText = const_cast<wchar_t*>(LR"(AXIS_18)");
			ListView_InsertItem(m_hExtraFontsList, &item);
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"18px"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"24px"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"19px"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"0px"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"0px"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"(All)"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"1234"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"No"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"Prerendered(Game)"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"1.4"));

			item.iItem = 1;
			item.iSubItem = 0;
			item.pszText = const_cast<wchar_t*>(L"Source Han Sans K Regular");
			item.lParam = reinterpret_cast<LPARAM>(new std::wstring(LR"(C:\Windows\Fonts\SourceHanSansK-Regular.otf)"));
			ListView_InsertItem(m_hExtraFontsList, &item);
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"18px"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"22px"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"18px"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"0px"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"0px"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"Hangul Compatibility Jamo, Hangul Jamo, Hangul Jamo Extended-A, Hangul Jamo Extended-B, Hangul Syllables"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"5678"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"No"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"DirectWrite(Natural Symmetric)"));
			ListView_SetItemText(m_hExtraFontsList, item.iItem, ++item.iSubItem, const_cast<wchar_t*>(L"1.4"));
		}

		for (const auto& pcsz : {
			L"AXIS_96",
			L"AXIS_12",
			L"AXIS_14",
			L"AXIS_18",
			L"AXIS_36",
			L"Jupiter_16",
			L"Jupiter_20",
			L"Jupiter_23",
			L"Jupiter_45",
			L"Jupiter_46",
			L"Jupiter_90",
			L"Meidinger_16",
			L"Meidinger_20",
			L"Meidinger_40",
			L"MiedingerMid_10",
			L"MiedingerMid_12",
			L"MiedingerMid_14",
			L"MiedingerMid_18",
			L"MiedingerMid_36",
			L"TrumpGothic_184",
			L"TrumpGothic_23",
			L"TrumpGothic_34",
			L"TrumpGothic_68",
			L"ChnAXIS_120",
			L"ChnAXIS_140",
			L"ChnAXIS_180",
			L"ChnAXIS_360",
			L"KrnAXIS_120",
			L"KrnAXIS_140",
			L"KrnAXIS_180",
			L"KrnAXIS_360",
			L"None (9.6)",
			L"None (10)",
			L"None (12)",
			L"None (14)",
			L"None (16)",
			L"None (18)",
			L"None (18.4)",
			L"None (20)",
			L"None (23)",
			L"None (34)",
			L"None (36)",
			L"None (40)",
			L"None (45)",
			L"None (46)",
			L"None (68)",
			L"None (90)",
			}) {
			ComboBox_AddString(m_hBaseFontCombo, pcsz);
		}

		ComboBox_SetCurSel(m_hBaseFontCombo, 3);

		OnSize();
		LoadFonts();

		ShowWindow(m_hWnd, SW_SHOW);

		return 0;
	}

	LRESULT OnSize() {
		RECT rc;
		GetClientRect(m_hWnd, &rc);

		auto hdwp = BeginDeferWindowPos(Id__Last);
		const auto nComboItemHeight = ComboBox_GetItemHeight(m_hBaseFontCombo);
		const auto nComboboxHeight = nComboItemHeight + 8;
		hdwp = DeferWindowPos(hdwp, m_hBaseFontStatic, nullptr, 0, 0, 80, nComboboxHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		hdwp = DeferWindowPos(hdwp, m_hBaseFontCombo, nullptr, 80, 0, 160, nComboItemHeight * (ComboBox_GetCount(m_hBaseFontCombo) + 1) + nComboboxHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		hdwp = DeferWindowPos(hdwp, m_hExtraFontsList, nullptr, 0, nComboboxHeight, (std::max<int>)(0, rc.right - rc.left), ListViewHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		hdwp = DeferWindowPos(hdwp, m_hEdit, nullptr, 0, nComboboxHeight + ListViewHeight, (std::max<int>)(0, rc.right - rc.left), EditHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		EndDeferWindowPos(hdwp);

		m_nDrawTop = static_cast<int>(nComboboxHeight + EditHeight + ListViewHeight);
		m_pMipmap = std::make_shared<XivRes::MemoryMipmapStream>(
			(std::max<int>)(32, rc.right - rc.left),
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
			auto m2 = XivRes::FontGenerator::TextMeasurer(*m_fontPacked).WithMaxWidth(m_pMipmap->Width - pad * 2).Measure(&s[0], s.size());

			m1.DrawTo(*m_pMipmap, *m_fontMerged, 16, 16, { 0xFF, 0xFF, 0xFF, 0xFF }, { 0, 0, 0, 0 });
			m2.DrawTo(*m_pMipmap, *m_fontPacked, 16, 16 + m1.Occupied.GetHeight(), { 0xFF, 0xFF, 0xFF, 0xFF }, { 0, 0, 0, 0 });
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
		StretchDIBits(hdc, 0, m_nDrawTop, m_pMipmap->Width, m_pMipmap->Height, 0, 0, m_pMipmap->Width, m_pMipmap->Height, &m_pMipmap->View<XivRes::RGBA8888>()[0], &bmi, DIB_RGB_COLORS, SRCCOPY);
		EndPaint(m_hWnd, &ps);

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
			wcex.lpfnWndProc = WindowImpl::WndProcInitial;

			const auto res = RegisterClassExW(&wcex);
			if (!res)
				throw std::runtime_error("");

			s_pAtom = m_pAtom = std::make_shared<ATOM>(res);
		}

		CreateWindowExW(0, reinterpret_cast<LPCWSTR>(*m_pAtom), L"Testing", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
			CW_USEDEFAULT, CW_USEDEFAULT, 1200, 640,
			nullptr, nullptr, nullptr, this);
	}

	void LoadFonts() {
		size_t fontIndex = ComboBox_GetCurSel(m_hBaseFontCombo);
		if (fontIndex < fontGlos.Count())
			m_fontBase = fontGlos[fontIndex];
		else if ((fontIndex -= fontGlos.Count()) < fontChns.Count())
			m_fontBase = fontChns[fontIndex];
		else if ((fontIndex -= fontChns.Count()) < fontKrns.Count())
			m_fontBase = fontKrns[fontIndex];
		else if ((fontIndex -= fontKrns.Count()) < _countof(NoBaseFontSizes))
			m_fontBase = std::make_shared<XivRes::FontGenerator::EmptyFixedSizeFont>(
				NoBaseFontSizes[fontIndex],
				static_cast<int>(std::round(NoBaseFontSizes[fontIndex] * 4 / 3 * 4 / 5)),
				static_cast<int>(std::round(NoBaseFontSizes[fontIndex] * 4 / 3)));

		std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> mergeFontList;
		mergeFontList.emplace_back(m_fontBase);

		for (int i = 1, i_ = ListView_GetItemCount(m_hExtraFontsList); i < i_; i++) {
			LVITEMW item{};
			item.mask = LVIF_PARAM;
			item.iItem = i;
			item.iSubItem = 0;
			ListView_GetItem(m_hExtraFontsList, &item);
			mergeFontList.emplace_back(std::make_shared<XivRes::FontGenerator::FreeTypeFixedSizeFont>(*reinterpret_cast<const std::wstring*>(item.lParam), 0, m_fontBase->GetSize()));
		}

		auto merge = std::make_shared<XivRes::FontGenerator::MergedFixedSizeFont>(std::move(mergeFontList));

		m_fontMerged = merge;
		XivRes::FontGenerator::FontdataPacker packer;
		if (const auto p = dynamic_cast<const XivRes::FontGenerator::GameFontdataFixedSizeFont*>(m_fontBase.get()))
			packer.SetHorizontalOffset(p->GetHorizontalOffset());
		packer.AddFont(merge);
		auto [fdts, texs] = packer.Compile();
		auto res = std::make_shared<XivRes::TextureStream>(texs[0]->Type, texs[0]->Width, texs[0]->Height, 1, 1, texs.size());
		for (size_t i = 0; i < texs.size(); i++)
			res->SetMipmap(0, i, texs[i]);
		m_fontPacked = std::make_shared<XivRes::FontGenerator::GameFontdataFixedSizeFont>(fdts[0], texs);
	}

	void TestFontLsb(int horizontalOffset) {
		const auto cps = m_fontMerged->GetAllCodepoints();
		std::set<const XivRes::Internal::UnicodeBlocks::BlockDefinition*> visitedBlocks;
		std::set<char32_t> ignoreDist;

		int nOffenders = 0;
		for (const auto& c : cps) {
			if (ignoreDist.contains(c))
				continue;

			XivRes::FontGenerator::GlyphMetrics gm;
			m_fontMerged->GetGlyphMetrics(c, gm);
			if (gm.X1 < -horizontalOffset) {
				auto& block = XivRes::Internal::UnicodeBlocks::GetCorrespondingBlock(c);
				if (block.Flags & XivRes::Internal::UnicodeBlocks::RTL)
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
				u8u32v = XivRes::Internal::UnicodeCodePointToUtf8Uint32(c);
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

	HWND Handle() const {
		return m_hWnd;
	}
};

std::weak_ptr<ATOM> WindowImpl::s_pAtom;

void ShowExampleWindow() {
	WindowImpl window;
	for (MSG msg{}; GetMessageW(&msg, nullptr, 0, 0);) {
		if (!IsDialogMessageW(window.Handle(), &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
}
