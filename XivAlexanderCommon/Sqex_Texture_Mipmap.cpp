#include "pch.h"
#include "Sqex_Texture_Mipmap.h"

#include "XaDxtDecompression.h"

std::shared_ptr<const Sqex::Texture::MipmapStream> Sqex::Texture::MipmapStream::ViewARGB8888(Format type) const {
	if (type != Format::A8R8G8B8 && type != Format::X8R8G8B8 && type != Format::Unknown)
		throw std::invalid_argument("invalid argb8888 compression type");

	if (m_type == Format::A8R8G8B8 || m_type == Format::X8R8G8B8) {
		auto res = std::static_pointer_cast<const MipmapStream>(shared_from_this());
		if (m_type == type || type == Format::Unknown)
			return res;
		else
			return std::make_shared<WrappedMipmapStream>(this->Width(), this->Height(), this->Layers(), type, std::move(res));
	}

	if (type == Format::Unknown)
		return MemoryBackedMipmap::NewARGB8888From(this, Format::A8R8G8B8);
	else
		return MemoryBackedMipmap::NewARGB8888From(this, type);
}

std::shared_ptr<Sqex::Texture::MipmapStream> Sqex::Texture::MipmapStream::FromTexture(std::shared_ptr<RandomAccessStream> stream, size_t mipmapIndex) {
	const auto header = stream->ReadStream<Header>(0);
	const auto offsets = stream->ReadStreamIntoVector<uint16_t>(sizeof header, header.MipmapCount);
	if (mipmapIndex >= offsets.size())
		throw std::invalid_argument(std::format("mipmapIndex={} > mipmapCount={}", mipmapIndex, offsets.size()));

	const auto dataSize = RawDataLength(header.Type, header.Width >> mipmapIndex, header.Height >> mipmapIndex, header.Layers);
	if (mipmapIndex == offsets.size() - 1) {
		if (stream->StreamSize() - offsets[mipmapIndex] < dataSize)
			throw std::runtime_error("overlapping mipmap data detected");
	} else {
		if (static_cast<size_t>(offsets[mipmapIndex + 1] - offsets[mipmapIndex]) < dataSize)
			throw std::runtime_error("overlapping mipmap data detected");
	}

	return std::make_shared<WrappedMipmapStream>(header.Width >> mipmapIndex, header.Height >> mipmapIndex, header.Layers, header.Type,
		std::make_shared<RandomAccessStreamPartialView>(stream, offsets[mipmapIndex], dataSize)
		);
}

void Sqex::Texture::MipmapStream::Show(std::string title) const {
	static constexpr int Margin = 0;

	struct State {
		union {
			struct {
				BITMAPINFOHEADER bmih;
				DWORD bitfields[3];
			};
			BITMAPINFO bmi{};
		};
		std::wstring title;
		int showmode;
		std::vector<uint8_t> buf;
		std::vector<uint8_t> transparent;
		bool closed;
		const MipmapStream* stream;

		HWND hwnd;

		POINT renderOffset;

		POINT down;
		POINT downOrig;
		bool dragging;
		bool isLeft;
		bool dragMoved;
		int zoomFactor;

		[[nodiscard]] auto GetZoom() const {
			return std::pow(2, 1. * zoomFactor / WHEEL_DELTA / 8);
		}

		void Draw(HDC hdc, const RECT& clip) const {
			RECT wrt;
			GetClientRect(hwnd, &wrt);
			const auto newdc = !hdc;
			if (newdc)
				hdc = GetDC(hwnd);
			const auto zoom = GetZoom();
			const auto dw = static_cast<int>(stream->Width() * zoom);
			const auto dh = static_cast<int>(stream->Height() * zoom);
			IntersectClipRect(hdc, clip.left, clip.top, clip.right, clip.bottom);
			if (showmode == 0)
				SetStretchBltMode(hdc, zoomFactor < 0 ? HALFTONE : COLORONCOLOR);
			SetBrushOrgEx(hdc, renderOffset.x, renderOffset.y, nullptr);
			StretchDIBits(hdc, renderOffset.x, renderOffset.y, dw, dh, 0, 0, stream->Width(), stream->Height(), showmode == 0 ? &transparent[0] : &buf[0], &bmi, DIB_RGB_COLORS, SRCCOPY);
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
			const auto zwidth = static_cast<int>(stream->Width() * GetZoom());
			const auto zheight = static_cast<int>(stream->Height() * GetZoom());
			if (renderOffset.x < rt.right - rt.left - Margin - zwidth)
				renderOffset.x = rt.right - rt.left - Margin - zwidth;
			if (renderOffset.x > Margin)
				renderOffset.x = Margin;
			if (renderOffset.y < rt.bottom - rt.top - Margin - zheight)
				renderOffset.y = rt.bottom - rt.top - Margin - zheight;
			if (renderOffset.y > Margin)
				renderOffset.y = Margin;
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		void UpdateTitle() {
			auto w = std::format(L"{}: {:.2f}%", title, 100. * GetZoom());
			switch (showmode) {
				case 0:
					w += L" (All channels)";
					break;
				case 1:
					w += L" (Red)";
					break;
				case 2:
					w += L" (Green)";
					break;
				case 3:
					w += L" (Blue)";
					break;
				case 4:
					w += L" (Alpha)";
					break;
			}
			SetWindowTextW(hwnd, w.c_str());
		}
	} state{};

	state.title = FromUtf8(title);
	state.buf = ViewARGB8888()->ReadStreamIntoVector<uint8_t>(0);
	{
		state.transparent = state.buf;
		const auto w = static_cast<size_t>(Width());
		const auto h = static_cast<size_t>(Height());
		const auto view = std::span(reinterpret_cast<RGBA8888*>(&state.transparent[0]), w * h);
		for (size_t i = 0; i < h; ++i) {
			for (size_t j = 0; j < w; ++j) {
				auto& v = view[i * w + j];
				auto bg = (i / 8 + j / 8) % 2 ? RGBA8888(255, 255, 255, 255) : RGBA8888(150, 150, 150, 255);
				v.R = (v.R * v.A + bg.R * (255U - v.A)) / 255U;
				v.G = (v.G * v.A + bg.G * (255U - v.A)) / 255U;
				v.B = (v.B * v.A + bg.B * (255U - v.A)) / 255U;
			}
		}
	}
	state.stream = this;

	state.bmih.biSize = sizeof state.bmih;
	state.bmih.biWidth = Width();
	state.bmih.biHeight = -Height();
	state.bmih.biPlanes = 1;
	state.bmih.biBitCount = 32;
	state.bmih.biCompression = BI_BITFIELDS;
	state.bitfields[0] = 0x0000FF;
	state.bitfields[1] = 0x00FF00;
	state.bitfields[2] = 0xFF0000;

	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hInstance = GetModuleHandleW(nullptr);
	wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wcex.hbrBackground = GetStockBrush(WHITE_BRUSH);
	wcex.lpszClassName = L"Sqex::Texture::MipmapStream::Show";

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
					return 0;
				}
				case WM_KEYDOWN:
				{
					if (wParam == VK_ESCAPE) {
						DestroyWindow(hwnd);
						return 0;
					} else if (wParam == VK_LEFT) {
						state.renderOffset.x += 8;
						state.ClipPan();
						return 0;
					} else if (wParam == VK_RIGHT) {
						state.renderOffset.x -= 8;
						state.ClipPan();
						return 0;
					} else if (wParam == VK_UP) {
						state.renderOffset.y += 8;
						state.ClipPan();
						return 0;
					} else if (wParam == VK_DOWN) {
						state.renderOffset.y -= 8;
						state.ClipPan();
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
						const auto zwidth = static_cast<int>(state.stream->Width() * state.GetZoom());
						const auto zheight = static_cast<int>(state.stream->Height() * state.GetZoom());
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
							state.showmode = (state.showmode + 1) % 5;
							switch (state.showmode) {
								case 0:
									state.bitfields[0] = 0x0000FF;
									state.bitfields[1] = 0x00FF00;
									state.bitfields[2] = 0xFF0000;
									break;
								case 1:
									state.bitfields[0] = state.bitfields[1] = state.bitfields[2] = 0xFFU << 0;
									break;
								case 2:
									state.bitfields[0] = state.bitfields[1] = state.bitfields[2] = 0xFFU << 8;
									break;
								case 3:
									state.bitfields[0] = state.bitfields[1] = state.bitfields[2] = 0xFFU << 16;
									break;
								case 4:
									state.bitfields[0] = state.bitfields[1] = state.bitfields[2] = 0xFFU << 24;
									break;
							}
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

	const auto unreg = CallOnDestruction([&]() {
		UnregisterClassW(wcex.lpszClassName, wcex.hInstance);
	});

	RECT rc{ 0, 0, Width(), Height() };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	state.hwnd = CreateWindowExW(0, wcex.lpszClassName, state.title.c_str(), WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		std::max(128L, std::min(1920L, rc.right - rc.left)),
		std::max(128L, std::min(1080L, rc.bottom - rc.top)),
		nullptr, nullptr, nullptr, nullptr);
	if (!state.hwnd)
		throw Win32::Error("CreateWindowExW");
	SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

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

std::shared_ptr<Sqex::Texture::MemoryBackedMipmap> Sqex::Texture::MemoryBackedMipmap::NewARGB8888From(const MipmapStream* stream, Format type) {
	if (type != Format::A8R8G8B8 && type != Format::X8R8G8B8)
		throw std::invalid_argument("invalid argb8888 compression type");

	const auto width = stream->Width();
	const auto height = stream->Height();
	const auto pixelCount = static_cast<size_t>(width) * height;
	const auto cbSource = static_cast<size_t>(stream->StreamSize());

	std::vector<uint8_t> result(pixelCount * sizeof RGBA8888);
	const auto rgba8888view = std::span(reinterpret_cast<RGBA8888*>(&result[0]), result.size() / sizeof RGBA8888);
	uint32_t pos = 0, read = 0;
	uint8_t buf8[8192];
	switch (stream->Type()) {
		case Format::L8:
		case Format::A8:
		{
			if (cbSource < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0; i < len; ++pos, ++i) {
					rgba8888view[pos].Value = buf8[i] * 0x10101UL | 0xFF000000UL;
					// result[pos].R = result[pos].G = result[pos].B = result[pos].A = buf8[i];
				}
			}
			break;
		}

		case Format::A4R4G4B4:
		{
			if (cbSource < pixelCount * sizeof RGBA4444)
				throw std::runtime_error("Truncated data detected");
			const auto view = std::span(reinterpret_cast<RGBA4444*>(buf8), sizeof buf8 / sizeof RGBA4444);
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof RGBA4444; i < count; ++pos, ++i)
					rgba8888view[pos].SetFrom(view[i].R * 17, view[i].G * 17, view[i].B * 17, view[i].A * 17);
			}
			break;
		}

		case Format::A1R5G5B5:
		{
			if (cbSource < pixelCount * sizeof RGBA5551)
				throw std::runtime_error("Truncated data detected");
			const auto view = std::span(reinterpret_cast<RGBA5551*>(buf8), sizeof buf8 / sizeof RGBA5551);
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof RGBA5551; i < count; ++pos, ++i)
					rgba8888view[pos].SetFrom(view[i].R * 255 / 31, view[i].G * 255 / 31, view[i].B * 255 / 31, view[i].A * 255);
			}
			break;
		}

		case Format::A8R8G8B8:
		case Format::X8R8G8B8:
			if (cbSource < pixelCount * sizeof RGBA8888)
				throw std::runtime_error("Truncated data detected");
			stream->ReadStream(0, std::span(rgba8888view));
			break;

		case Format::A16B16G16R16F:
		{
			if (cbSource < pixelCount * sizeof RGBAHHHH)
				throw std::runtime_error("Truncated data detected");
			stream->ReadStream(0, std::span(rgba8888view));
			const auto view = std::span(reinterpret_cast<RGBAHHHH*>(buf8), sizeof buf8 / sizeof RGBAHHHH);
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof RGBAHHHH; i < count; ++pos, ++i)
					rgba8888view[pos].SetFromF(view[i]);
			}
			break;
		}

		case Format::A32B32G32R32F:
		{
			if (cbSource < pixelCount * sizeof RGBAFFFF)
				throw std::runtime_error("Truncated data detected");
			stream->ReadStream(0, std::span(rgba8888view));
			const auto view = std::span(reinterpret_cast<RGBAFFFF*>(buf8), sizeof buf8 / sizeof RGBAFFFF);
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof RGBAFFFF; i < count; ++pos, ++i)
					rgba8888view[pos].SetFromF(view[i]);
			}
			break;
		}

		case Format::DXT1:
		{
			if (cbSource < pixelCount * 8)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 8, pos += 8) {
					DecompressBlockDXT1(
						pos / 2 % width,
						pos / 2 / width * 4,
						width, &buf8[i], reinterpret_cast<uint32_t*>(&rgba8888view[0]));
				}
			}
			break;
		}

		case Format::DXT3:
		{
			if (cbSource < pixelCount * 16)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
					DecompressBlockDXT1(
						pos / 4 % width,
						pos / 4 / width * 4,
						width, &buf8[i], reinterpret_cast<uint32_t*>(&rgba8888view[0]));
					for (size_t dy = 0; dy < 4; dy += 1) {
						for (size_t dx = 0; dx < 4; dx += 2) {
							rgba8888view[dy * width + dx].A = 17 * (buf8[i + dy * 2 + dx / 2] & 0xF);
							rgba8888view[dy * width + dx + 1].A = 17 * (buf8[i + dy * 2 + dx / 2] >> 4);
						}
					}
				}
			}
			break;
		}

		case Format::DXT5:
		{
			if (cbSource < pixelCount * 16)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
					DecompressBlockDXT5(
						pos / 4 % width,
						pos / 4 / width * 4,
						width, &buf8[i], reinterpret_cast<uint32_t*>(&rgba8888view[0]));
				}
			}
			break;
		}

		case Format::Unknown:
		default:
			throw std::runtime_error("Unsupported type");
	}

	return std::make_shared<MemoryBackedMipmap>(stream->Width(), stream->Height(), stream->Layers(), type, std::move(result));
}

uint64_t Sqex::Texture::MemoryBackedMipmap::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	const auto available = static_cast<size_t>(std::min(m_data.size() - offset, length));
	std::copy_n(&m_data[static_cast<size_t>(offset)], available, static_cast<char*>(buf));
	return available;
}
