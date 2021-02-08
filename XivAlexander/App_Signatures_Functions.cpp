#include "pch.h"
#include "App_Signatures.h"
#include "App_Signatures_Functions.h"

namespace App::Signatures::Functions {

	namespace Socket {
		FunctionSignature<SOCKET, int, int, int> socket("socket::socket", ::socket, 
			[](int af, int type, int protocol) { return socket.Thunked(af, type, protocol); });
		FunctionSignature<int, SOCKET, const sockaddr*, int> connect("socket::connect", ::connect, 
			[](SOCKET s, const sockaddr* name, int namelen) { return connect.Thunked(s, name, namelen); });
		FunctionSignature<int, int, fd_set*, fd_set*, fd_set*, const timeval*> select("socket::select", ::select, 
			[](int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const timeval* timeout) { return select.Thunked(nfds, readfds, writefds, exceptfds, timeout); });
		FunctionSignature<int, SOCKET, char*, int, int> recv("socket::recv", ::recv, 
			[](SOCKET s, char* buf, int len, int flags) { return recv.Thunked(s, buf, len, flags); });
		FunctionSignature<int, SOCKET, const char*, int, int> send("socket::send", ::send, 
			[](SOCKET s, const char* buf, int len, int flags) { return send.Thunked(s, buf, len, flags); });
		FunctionSignature<int, SOCKET> closesocket("socket::closesocket", ::closesocket,
			[](SOCKET s) { return closesocket.Thunked(s); });
	}

	namespace WinApi {
		// The game client's internal debugging code often trips when this function returns true,
		// so we return false instead for the ease of debugging.
		FunctionSignature<BOOL> IsDebuggerPresent("WinApi::IsDebuggerPresent", ::IsDebuggerPresent,
			[]() -> BOOL { return FALSE; }
		);

		// "Limit frame rate when client is inactive." triggers the game to call SleepEx in main message loop.
		FunctionSignature<DWORD, DWORD, BOOL> SleepEx("WinApi::SleepEx", ::SleepEx,
			[](DWORD dwMilliseconds, BOOL bAlertable) -> DWORD {
				return SleepEx.Thunked(dwMilliseconds, bAlertable);
			}
		);
	}

	namespace DXGISwapChain {
		std::map<LUID, std::vector<void*>, CompareLuid> dxgiSwapChainVtable;
		const std::map<LUID, std::vector<void*>, CompareLuid>& _GetDxgiSwapChainVtable() {
			Utils::Win32Handle<HWND, DestroyWindow> hwnd(CreateWindowExW(0, L"STATIC", L"",
				WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, 0, NULL, NULL, 0));

			DXGI_SWAP_CHAIN_DESC desc;
			ZeroMemory(&desc, sizeof(desc));

			RECT rcWnd;
			if (GetClientRect(hwnd, &rcWnd)) {
				IDXGIFactory1* pFactory = nullptr;
				IDXGISwapChain* pSwapChain = nullptr;
				ID3D11Device* pDevice = nullptr;
				D3D_FEATURE_LEVEL featureLevel;
				ID3D11DeviceContext* pDeviceContext = nullptr;
				IDXGIAdapter1* pAdapter = nullptr;
				DXGI_ADAPTER_DESC1 adapterDesc;
				int i;

				desc.BufferDesc.Width = rcWnd.right - rcWnd.left;
				desc.BufferDesc.Height = rcWnd.bottom - rcWnd.top;
				desc.BufferDesc.RefreshRate.Numerator = 60;
				desc.BufferDesc.RefreshRate.Denominator = 1;
				desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
				desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
				desc.BufferDesc.Scaling = DXGI_MODE_SCALING_CENTERED;
				desc.SampleDesc.Count = 1;
				desc.SampleDesc.Quality = 0;
				desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				desc.BufferCount = 2;
				desc.OutputWindow = hwnd;
				desc.Windowed = true;
				desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

				if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory))))
					goto cleanup;

				for (i = 0; ; ++i) {
					pAdapter = nullptr;

					if (FAILED(pFactory->EnumAdapters1(i, &pAdapter)))
						break;

					if (FAILED(D3D11CreateDeviceAndSwapChain(pAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &desc, &pSwapChain, &pDevice, &featureLevel, &pDeviceContext)))
						goto cleanup_inner;

					if (pSwapChain) {
						void** vtbl = *(void***)pSwapChain;
						if (FAILED(pAdapter->GetDesc1(&adapterDesc)))
							goto cleanup_inner;

						if (i == 1)
							dxgiSwapChainVtable[adapterDesc.AdapterLuid] = std::vector(vtbl, vtbl + VTCOUNT);
					}

				cleanup_inner:;
					if (pDevice)
						pDevice->Release();
					if (pDeviceContext)
						pDeviceContext->Release();
					if (pSwapChain)
						pSwapChain->Release();
				}

			cleanup:;
				if (pFactory)
					pFactory->Release();
			}

			return dxgiSwapChainVtable;
		}

		FunctionSignature<HRESULT, IDXGISwapChain*, UINT, UINT> Present("DXGISwapChain::Present", []() -> void* {
			return _GetDxgiSwapChainVtable().begin()->second[VTPresent];
			}, [](IDXGISwapChain* pSwapChain, UINT p1, UINT p2) { return Present.Thunked(pSwapChain, p1, p2); });
	}

	// These signatures worked ever since v3.5, so...

	// Function called to process event in main event loop.
	FunctionSignature<size_t> PeekMessageAndProcess(
		"PeekMessageAndProcess",
		SectionFilterTextOnly,
		"\x40\x53\x48\x83\xEC\x60\x48\x89\x6C\x24\x70\x48\x8D\x4C\x24\x30\x33\xED\x45\x33\xC9\x45\x33\xC0\x00\x00\x00\x00\x00\x00\xBB\x0A",
		"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\x00\x00\xFF\xFF",
		[]() { return PeekMessageAndProcess.Thunked(); }
	);

	// Called when an in-game window has been hidden.
	// Note: Whether calling this function works is unknown.
	FunctionSignature<size_t, void*> HideFFXIVWindow(
		"HideFFXIVWindow",
		SectionFilterTextOnly,
		"\x40\x57\x48\x83\xEC\x20\x48\x8B\xF9\x48\x8B\x89\xC8\x00\x00\x00\x48\x85\xC9\x0F\x00\x00\x00\x00\x00\x8B\x87\xB0\x01\x00\x00\xC1\xE8\x07\xA8\x01",
		"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF",
		[](void* pThis) { return HideFFXIVWindow.Thunked(pThis); }
	);

	// Called when an in-game window has been shown.
	// Note: Whether calling this function works is ShowFFXIVWindow.
	FunctionSignature<size_t, void*> ShowFFXIVWindow(
		"ShowFFXIVWindow",
		SectionFilterTextOnly,
		"\x40\x53\x48\x83\xEC\x40\x48\x8B\x91\xC8\x00\x00\x00\x48\x8B\xD9\x48\x85\xD2",
		"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF",
		[](void* pThis) { return ShowFFXIVWindow.Thunked(pThis); }
	);

	// Called when a chat or command text has been input.
	FunctionSignature<size_t, void*, void*, size_t> OnNewChatItem(
		"OnNewChatItem",
		SectionFilterTextOnly,
		"\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x20\x48\x8B\xF9\x48\x83\xC1\x48\xE8\x00\x00\x00\x00\x8B\x77\x14\x8D\x46\x01\x89\x47\x14\x81\xFE\x30\x75\x00\x00",
		"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF",
		[](void* pThis, void* p1, size_t p2) {
			/*
			typedef struct _CHATITEM {
				int timestamp;
				union {
					struct {
						char code2;
						char code1;
					};
					short code;
				};
				short _u1;
				char chat[1];
			} CHATITEM, * PCHATITEM;

			PCHATITEM pItem = reinterpret_cast<PCHATITEM>(p1);
			const auto st = Utils::EpochToLocalSystemTime(pItem->timestamp * 1000ULL);
			Misc::Logger::GetLogger().Log(Utils::FormatString("Chat: [%04d-%02d-%02d %02d:%02d:%02d / %04x / u1=%04x] %s", 
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
				pItem->code, pItem->_u1, pItem->chat));
			//*/
			return OnNewChatItem.Thunked(pThis, p1, p2); 
		}
	);
}
