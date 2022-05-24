#ifdef _WINDOWS_
#ifndef _XIVRES_INTERNAL_TEXTUREPREVIEW_WINDOWS_H_
#define _XIVRES_INTERNAL_TEXTUREPREVIEW_WINDOWS_H_

#include <algorithm>
#include <type_traits>

#include "XivRes/TextureStream.h"

#include "../Internal/CallOnDestruction.h"

namespace XivRes::Internal {
	static void ShowTextureStream(const XivRes::TextureStream& texStream, std::wstring title = L"Preview") {
		static constexpr int Margin = 0;

		struct State {
			const XivRes::TextureStream& texStream;
			std::shared_ptr<XivRes::MipmapStream> stream;

			union {
				struct {
					BITMAPINFOHEADER bmih;
					DWORD bitfields[3];
				};
				BITMAPINFO bmi{};
			};
			std::wstring title;
			int showmode;
			int repeatIndex, mipmapIndex, depthIndex;
			std::vector<uint8_t> buf;
			std::vector<uint8_t> transparent;
			bool closed;

			HWND hwnd;

			POINT renderOffset;

			POINT down;
			POINT downOrig;
			bool dragging;
			bool isLeft;
			bool dragMoved;
			int zoomFactor;

			bool refreshPending = false;

			[[nodiscard]] auto GetZoom() const {
				return std::pow(2, 1. * zoomFactor / WHEEL_DELTA / 8);
			}

			void LoadMipmap(int repeatIndex, int mipmapIndex, int depthIndex) {
				this->repeatIndex = repeatIndex = (std::min)(texStream.GetRepeatCount() - 1, (std::max)(0, repeatIndex));
				this->mipmapIndex = mipmapIndex = (std::min)(texStream.GetMipmapCount() - 1, (std::max)(0, mipmapIndex));
				this->depthIndex = depthIndex = (std::min)(DepthCount() - 1, (std::max)(0, depthIndex));

				stream = XivRes::MemoryMipmapStream::AsARGB8888(*texStream.GetMipmap(repeatIndex, mipmapIndex));
				const auto planeSize = XivRes::TextureRawDataLength(stream->Type, stream->Width, stream->Height, 1);
				buf = ReadStreamIntoVector<uint8_t>(*stream, depthIndex * planeSize, planeSize);
				{
					transparent = buf;
					const auto w = static_cast<size_t>(stream->Width);
					const auto h = static_cast<size_t>(stream->Height);
					const auto view = std::span(reinterpret_cast<XivRes::RGBA8888*>(&transparent[0]), w * h);
					for (size_t i = 0; i < h; ++i) {
						for (size_t j = 0; j < w; ++j) {
							auto& v = view[i * w + j];
							auto bg = (i / 8 + j / 8) % 2 ? XivRes::RGBA8888(255, 255, 255, 255) : XivRes::RGBA8888(150, 150, 150, 255);
							v.R = (v.R * v.A + bg.R * (255U - v.A)) / 255U;
							v.G = (v.G * v.A + bg.G * (255U - v.A)) / 255U;
							v.B = (v.B * v.A + bg.B * (255U - v.A)) / 255U;
						}
					}
				}

				bmih.biSize = sizeof bmih;
				bmih.biWidth = stream->Width;
				bmih.biHeight = -stream->Height;
				bmih.biPlanes = 1;
				bmih.biBitCount = 32;
				bmih.biCompression = BI_BITFIELDS;

				ClipPan();
				UpdateTitle();
			}

			void Draw(HDC hdc, const RECT& clip) {
				RECT wrt;
				GetClientRect(hwnd, &wrt);
				const auto newdc = !hdc;
				if (newdc)
					hdc = GetDC(hwnd);
				const auto zoom = GetZoom();
				const auto dw = static_cast<int>(stream->Width * zoom);
				const auto dh = static_cast<int>(stream->Height * zoom);
				IntersectClipRect(hdc, clip.left, clip.top, clip.right, clip.bottom);
				if (showmode == 0)
					SetStretchBltMode(hdc, zoomFactor < 0 ? HALFTONE : COLORONCOLOR);
				(hdc, renderOffset.x, renderOffset.y, nullptr);
				switch (showmode) {
					case 0:
					case 1:
						reinterpret_cast<XivRes::RGBA8888*>(&bitfields[0])->SetFrom(255, 0, 0, 0);
						reinterpret_cast<XivRes::RGBA8888*>(&bitfields[1])->SetFrom(0, 255, 0, 0);
						reinterpret_cast<XivRes::RGBA8888*>(&bitfields[2])->SetFrom(0, 0, 255, 0);
						break;
					case 2:
						for (auto& bitfield : bitfields)
							reinterpret_cast<XivRes::RGBA8888*>(&bitfield)->SetFrom(255, 0, 0, 0);
						break;
					case 3:
						for (auto& bitfield : bitfields)
							reinterpret_cast<XivRes::RGBA8888*>(&bitfield)->SetFrom(0, 255, 0, 0);
						break;
					case 4:
						for (auto& bitfield : bitfields)
							reinterpret_cast<XivRes::RGBA8888*>(&bitfield)->SetFrom(0, 0, 255, 0);
						break;
					case 5:
						for (auto& bitfield : bitfields)
							reinterpret_cast<XivRes::RGBA8888*>(&bitfield)->SetFrom(0, 0, 0, 255);
						break;
				}
				StretchDIBits(hdc, renderOffset.x, renderOffset.y, dw, dh, 0, 0, stream->Width, stream->Height, showmode == 0 ? &transparent[0] : &buf[0], &bmi, DIB_RGB_COLORS, SRCCOPY);
				if (renderOffset.x > 0) {
					const auto rt = RECT{ 0, clip.top, renderOffset.x, clip.bottom };
					FillRect(hdc, &rt, GetStockBrush(WHITE_BRUSH));
				}
				if (renderOffset.x + dw < wrt.right - wrt.left) {
					const auto rt = RECT{ renderOffset.x + dw, clip.top, wrt.right - wrt.left, clip.bottom };
					FillRect(hdc, &rt, GetStockBrush(WHITE_BRUSH));
				}
				if (renderOffset.y > 0) {
					const auto rt = RECT{ clip.left, 0, clip.right, renderOffset.y };
					FillRect(hdc, &rt, GetStockBrush(WHITE_BRUSH));
				}
				if (renderOffset.y + dh < wrt.bottom - wrt.top) {
					const auto rt = RECT{ clip.left, renderOffset.y + dh, clip.right, wrt.bottom - wrt.top };
					FillRect(hdc, &rt, GetStockBrush(WHITE_BRUSH));
				}
				if (newdc)
					ReleaseDC(hwnd, hdc);
			}

			void ChangeZoom(int newZoomFactor, int nmx, int nmy) {
				POINT nm = { nmx, nmy };
				ScreenToClient(hwnd, &nm);
				const double mx = nm.x, my = nm.y;
				const auto ox = (mx - renderOffset.x) / GetZoom();
				const auto oy = (my - renderOffset.y) / GetZoom();

				zoomFactor = newZoomFactor;

				renderOffset.x = static_cast<int>(mx - ox * GetZoom());
				renderOffset.y = static_cast<int>(my - oy * GetZoom());

				ClipPan();
				UpdateTitle();
			}

			void ClipPan() {
				RECT rt;
				GetClientRect(hwnd, &rt);
				const auto zwidth = static_cast<int>(stream->Width * GetZoom());
				const auto zheight = static_cast<int>(stream->Height * GetZoom());
				if (renderOffset.x < rt.right - rt.left - Margin - zwidth)
					renderOffset.x = rt.right - rt.left - Margin - zwidth;
				if (renderOffset.x > Margin)
					renderOffset.x = Margin;
				if (renderOffset.y < rt.bottom - rt.top - Margin - zheight)
					renderOffset.y = rt.bottom - rt.top - Margin - zheight;
				if (renderOffset.y > Margin)
					renderOffset.y = Margin;
				InvalidateRect(hwnd, nullptr, FALSE);
				refreshPending = true;
			}

			void UpdateTitle() {
				auto w = std::format(L"{} (r{}/{} m{}/{} d{}/{}): {:.2f}%", title, 1 + repeatIndex, texStream.GetRepeatCount(), 1 + mipmapIndex, texStream.GetMipmapCount(), 1 + depthIndex, DepthCount(), 100. * GetZoom());
				switch (showmode) {
					case 0:
						w += L" (All channels)";
						break;
					case 1:
						w += L" (No alpha)";
						break;
					case 2:
						w += L" (Red)";
						break;
					case 3:
						w += L" (Green)";
						break;
					case 4:
						w += L" (Blue)";
						break;
					case 5:
						w += L" (Alpha)";
						break;
				}
				SetWindowTextW(hwnd, w.c_str());
			}

			int DepthCount() const {
				return (std::max)(1, texStream.GetDepth() / (1 << mipmapIndex));
			}
		} state{ .texStream = texStream, .title = std::move(title), };

		state.LoadMipmap(0, 0, 0);

		WNDCLASSEXW wcex{};
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.hInstance = GetModuleHandleW(nullptr);
		wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wcex.hbrBackground = GetStockBrush(WHITE_BRUSH);
		wcex.lpszClassName = L"XivRes::Internal::ShowTextureStream";

		wcex.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
			if (const auto pState = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
				auto& state = *pState;
				switch (msg) {
					case WM_CREATE:
					{
						state.hwnd = hwnd;
						state.UpdateTitle();
						return 0;
					}

					case WM_MOUSEWHEEL:
					{
						state.ChangeZoom(state.zoomFactor + GET_WHEEL_DELTA_WPARAM(wParam), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
						return 0;
					}

					case WM_PAINT:
					{
						PAINTSTRUCT ps;
						const auto hdc = BeginPaint(hwnd, &ps);
						state.Draw(hdc, ps.rcPaint);
						EndPaint(hwnd, &ps);
						state.refreshPending = false;
						return 0;
					}
					case WM_KEYDOWN:
					{
						switch (wParam) {
							case VK_ESCAPE:
								DestroyWindow(hwnd);
								return 0;
							case VK_LEFT:
								state.renderOffset.x += 8;
								state.ClipPan();
								return 0;
							case VK_RIGHT:
								state.renderOffset.x -= 8;
								state.ClipPan();
								return 0;
							case VK_UP:
								state.renderOffset.y += 8;
								state.ClipPan();
								return 0;
							case VK_DOWN:
								state.renderOffset.y -= 8;
								state.ClipPan();
								return 0;
							case 'Q':
								if (!state.refreshPending)
									state.LoadMipmap(state.repeatIndex - 1, state.mipmapIndex, state.depthIndex);
								return 0;
							case 'W':
								if (!state.refreshPending)
									state.LoadMipmap(state.repeatIndex + 1, state.mipmapIndex, state.depthIndex);
								return 0;
							case 'A':
								if (!state.refreshPending)
									state.LoadMipmap(state.repeatIndex, state.mipmapIndex - 1, state.depthIndex);
								return 0;
							case 'S':
								if (!state.refreshPending)
									state.LoadMipmap(state.repeatIndex, state.mipmapIndex + 1, state.depthIndex);
								return 0;
							case 'Z':
								if (!state.refreshPending)
									state.LoadMipmap(state.repeatIndex, state.mipmapIndex, (state.depthIndex + state.DepthCount() - 1) % state.DepthCount());
								return 0;
							case 'X':
								if (!state.refreshPending)
									state.LoadMipmap(state.repeatIndex, state.mipmapIndex, (state.depthIndex + 1) % state.DepthCount());
								return 0;
						}
						break;
					}

					case WM_LBUTTONDOWN:
					case WM_RBUTTONDOWN:
					{
						if (!state.dragging) {
							state.dragging = true;
							state.dragMoved = false;
							state.isLeft = msg == WM_LBUTTONDOWN;
							state.downOrig = { GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam) };
							ClientToScreen(hwnd, &state.downOrig);
							if (state.isLeft) {
								SetCursorPos(state.down.x = GetSystemMetrics(SM_CXSCREEN) / 2,
									state.down.y = GetSystemMetrics(SM_CYSCREEN) / 2);
							} else {
								state.down = state.downOrig;
							}
							ShowCursor(FALSE);
							SetCapture(hwnd);
						}
						return 0;
					}

					case WM_MOUSEMOVE:
					{
						if (state.dragging) {
							RECT rt;
							GetClientRect(hwnd, &rt);

							POINT screenCursorPos = { GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam) };
							ClientToScreen(hwnd, &screenCursorPos);

							auto displaceX = screenCursorPos.x - state.down.x;
							auto displaceY = screenCursorPos.y - state.down.y;
							const auto speed = (state.isLeft ? 1 : 4);
							if (!state.dragMoved) {
								if (!displaceX && !displaceY)
									return 0;
								state.dragMoved = true;
							}
							const auto zwidth = static_cast<int>(state.stream->Width * state.GetZoom());
							const auto zheight = static_cast<int>(state.stream->Height * state.GetZoom());
							if (state.renderOffset.x + displaceX * speed < rt.right - rt.left - Margin - zwidth)
								displaceX = (rt.right - rt.left - Margin - zwidth - state.renderOffset.x) / speed;
							if (state.renderOffset.x + displaceX * speed > Margin)
								displaceX = (Margin - state.renderOffset.x) / speed;
							if (state.renderOffset.y + displaceY * speed < rt.bottom - rt.top - Margin - zheight)
								displaceY = (rt.bottom - rt.top - Margin - zheight - state.renderOffset.y) / speed;
							if (state.renderOffset.y + displaceY * speed > Margin)
								displaceY = (Margin - state.renderOffset.y) / speed;

							if (state.isLeft)
								SetCursorPos(state.down.x, state.down.y);
							else {
								state.down.x += displaceX;
								state.down.y += displaceY;
							}
							state.renderOffset.x += displaceX * speed;
							state.renderOffset.y += displaceY * speed;
							InvalidateRect(hwnd, nullptr, FALSE);
						}
						return 0;
					}

					case WM_LBUTTONUP:
					case WM_RBUTTONUP:
					{
						if (!state.dragging || state.isLeft != (msg == WM_LBUTTONUP))
							return false;
						ReleaseCapture();
						SetCursorPos(state.downOrig.x, state.downOrig.y);
						ShowCursor(TRUE);

						state.dragging = false;
						if (!state.dragMoved) {
							if (state.isLeft) {
								state.showmode = (state.showmode + 1) % 6;
								state.UpdateTitle();
								InvalidateRect(hwnd, nullptr, FALSE);
							} else {
								state.renderOffset = {};
								state.ChangeZoom(0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
							}
						}
						return 0;
					}

					case WM_SIZE:
					{
						state.ClipPan();
						return 0;
					}

					case WM_NCDESTROY:
					{
						state.closed = true;
					}
				}
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		};
		RegisterClassExW(&wcex);

		const auto unreg = XivRes::Internal::CallOnDestruction([&]() {
			UnregisterClassW(wcex.lpszClassName, wcex.hInstance);
		});

		RECT rc{ 0, 0, texStream.GetWidth(), texStream.GetHeight() };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
		state.hwnd = CreateWindowExW(0, wcex.lpszClassName, state.title.c_str(), WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT,
			(std::max)(640L, (std::min)(1920L, rc.right - rc.left)),
			(std::max)(480L, (std::min)(1080L, rc.bottom - rc.top)),
			nullptr, nullptr, nullptr, nullptr);
		if (!state.hwnd)
			throw std::system_error(GetLastError(), std::system_category());
		SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

		state.UpdateTitle();
		ShowWindow(state.hwnd, SW_SHOW);

		MSG msg{};
		while (!state.closed && GetMessageW(&msg, nullptr, 0, 0)) {
			if (msg.hwnd != state.hwnd && IsDialogMessageW(msg.hwnd, &msg))
				continue;
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		if (msg.message == WM_QUIT)
			PostQuitMessage(0);
	}

}

#endif
#endif
