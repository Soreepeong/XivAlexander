#pragma once

#include <d3d11.h>
#include <dwrite_3.h>

_COM_SMARTPTR_TYPEDEF(ID3D11Device, __uuidof(ID3D11Device));
_COM_SMARTPTR_TYPEDEF(ID3D11DeviceChild, __uuidof(ID3D11DeviceChild));
_COM_SMARTPTR_TYPEDEF(ID3D11DeviceContext, __uuidof(ID3D11DeviceContext));
_COM_SMARTPTR_TYPEDEF(ID3D11PixelShader, __uuidof(ID3D11PixelShader));
_COM_SMARTPTR_TYPEDEF(ID3D11Resource, __uuidof(ID3D11Resource));
_COM_SMARTPTR_TYPEDEF(ID3D11Texture2D, __uuidof(ID3D11Texture2D));
_COM_SMARTPTR_TYPEDEF(IDWriteFactory7, __uuidof(IDWriteFactory7));
_COM_SMARTPTR_TYPEDEF(IDWriteFont, __uuidof(IDWriteFont));
_COM_SMARTPTR_TYPEDEF(IDWriteFont3, __uuidof(IDWriteFont3));
_COM_SMARTPTR_TYPEDEF(IDWriteFontCollection, __uuidof(IDWriteFontCollection));
_COM_SMARTPTR_TYPEDEF(IDWriteFontCollection3, __uuidof(IDWriteFontCollection3));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace, __uuidof(IDWriteFontFace));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace3, __uuidof(IDWriteFontFace3));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace5, __uuidof(IDWriteFontFace5));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFile, __uuidof(IDWriteFontFile));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFileLoader, __uuidof(IDWriteFontFileLoader));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFamily2, __uuidof(IDWriteFontFamily2));
_COM_SMARTPTR_TYPEDEF(IDWriteGlyphRunAnalysis, __uuidof(IDWriteGlyphRunAnalysis));
_COM_SMARTPTR_TYPEDEF(IDWriteLocalFontFileLoader, __uuidof(IDWriteLocalFontFileLoader));
_COM_SMARTPTR_TYPEDEF(IDWriteRenderingParams, __uuidof(IDWriteRenderingParams));
_COM_SMARTPTR_TYPEDEF(IDWriteRenderingParams3, __uuidof(IDWriteRenderingParams3));
_COM_SMARTPTR_TYPEDEF(IDWriteTextAnalyzer, __uuidof(IDWriteTextAnalyzer));

namespace XivAlexander::Apps::MainApp::FontReplacement {
	inline void ThrowOnError(HRESULT hr, const char* what) {
		if (FAILED(hr))
			throw std::runtime_error(std::format("{} failed: 0x{:08X}", what, static_cast<uint32_t>(hr)));
	}

	// Away from zero at halves, as C's round (the plugin's Misc.StdRound).
	[[nodiscard]] inline float StdRound(float value) {
		return std::round(value);
	}

	[[nodiscard]] constexpr bool IsSurrogate(wchar_t c) {
		return c >= 0xD800 && c <= 0xDFFF;
	}

	// As presets compare names.
	struct LessIgnoringCase {
		bool operator()(std::string_view a, std::string_view b) const {
			return std::ranges::lexicographical_compare(a, b, [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
			});
		}
	};

	template<typename T>
	T& At(uintptr_t address) {
		return *reinterpret_cast<T*>(address);
	}

	template<typename T>
	T& At(const void* p, int offset) {
		return At<T>(reinterpret_cast<uintptr_t>(p) + offset);
	}

	[[nodiscard]] inline HRESULT CreateFontFace(IDWriteFont3* font, IDWriteFontFace5** face) {
		IDWriteFontFace3Ptr face3;
		if (const auto hr = font->CreateFontFace(&face3); FAILED(hr))
			return hr;
		return face3->QueryInterface(__uuidof(IDWriteFontFace5), reinterpret_cast<void**>(face));
	}

	[[nodiscard]] inline HRESULT GetFirstMatchingFont(IDWriteFontFamily2* family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, IDWriteFont3** font) {
		IDWriteFontPtr matched;
		if (const auto hr = family->GetFirstMatchingFont(weight, stretch, style, &matched); FAILED(hr))
			return hr;
		return matched->QueryInterface(__uuidof(IDWriteFont3), reinterpret_cast<void**>(font));
	}

	[[nodiscard]] inline ID3D11DevicePtr GetDevice(ID3D11DeviceChild* child) {
		ID3D11DevicePtr device;
		child->GetDevice(&device);
		return device;
	}

	[[nodiscard]] inline ID3D11DeviceContextPtr GetImmediateContext(ID3D11DeviceChild* child) {
		ID3D11DeviceContextPtr context;
		GetDevice(child)->GetImmediateContext(&context);
		return context;
	}
}
