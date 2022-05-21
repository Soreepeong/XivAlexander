// #define _XIVRES_FONTGENERATOR_DIRECTWRITEFIXEDSIZEFONT_H_

#ifndef _XIVRES_FONTGENERATOR_DIRECTWRITEFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_DIRECTWRITEFIXEDSIZEFONT_H_

#include <filesystem>

#include "IFixedSizeFont.h"

#include "../Internal/BitmapCopy.h"
#include "../Internal/TrueTypeUtils.h"

#include <comdef.h>
#include <dwrite_3.h>
#pragma comment(lib, "dwrite.lib")

_COM_SMARTPTR_TYPEDEF(IDWriteFactory, __uuidof(IDWriteFactory));
_COM_SMARTPTR_TYPEDEF(IDWriteFactory3, __uuidof(IDWriteFactory3));
_COM_SMARTPTR_TYPEDEF(IDWriteFont, __uuidof(IDWriteFont));
_COM_SMARTPTR_TYPEDEF(IDWriteFontCollection, __uuidof(IDWriteFontCollection));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace, __uuidof(IDWriteFontFace));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace1, __uuidof(IDWriteFontFace1));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFaceReference, __uuidof(IDWriteFontFaceReference));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFamily, __uuidof(IDWriteFontFamily));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFile, __uuidof(IDWriteFontFile));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFileLoader, __uuidof(IDWriteFontFileLoader));
_COM_SMARTPTR_TYPEDEF(IDWriteFontSetBuilder, __uuidof(IDWriteFontSetBuilder));
_COM_SMARTPTR_TYPEDEF(IDWriteGdiInterop, __uuidof(IDWriteGdiInterop));
_COM_SMARTPTR_TYPEDEF(IDWriteGlyphRunAnalysis, __uuidof(IDWriteGlyphRunAnalysis));
_COM_SMARTPTR_TYPEDEF(IDWriteLocalFontFileLoader, __uuidof(IDWriteLocalFontFileLoader));

namespace XivRes::FontGenerator {
	class DirectWriteFixedSizeFont : public DefaultAbstractFixedSizeFont {
		static HRESULT SuccessOrThrow(HRESULT hr, std::initializer_list<HRESULT> acceptables = {}) {
			if (SUCCEEDED(hr))
				return hr;

			for (const auto& h : acceptables) {
				if (h == hr)
					return hr;
			}

			const auto err = _com_error(hr);
			wchar_t* pszMsg = nullptr;
			FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
							  FORMAT_MESSAGE_FROM_SYSTEM |
							  FORMAT_MESSAGE_IGNORE_INSERTS,
						  nullptr,
						  hr,
						  MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
						  reinterpret_cast<LPWSTR>(&pszMsg),
						  0,
						  NULL);
			if (pszMsg) {
				std::unique_ptr<wchar_t, decltype(LocalFree)*> pszMsgFree(pszMsg, LocalFree);

				throw std::runtime_error(std::format(
					"DirectWrite Error (HRESULT=0x{:08X}): {}",
					static_cast<uint32_t>(hr),
					Unicode::Convert<std::string>(std::wstring(pszMsg))
				));

			} else {
				throw std::runtime_error(std::format(
					"DirectWrite Error (HRESULT=0x{:08X})",
					static_cast<uint32_t>(hr),
					Unicode::Convert<std::string>(std::wstring(pszMsg))
				));
			}
		}

		class IStreamBasedDWriteFontFileLoader : public IDWriteFontFileLoader {
			class IStreamAsDWriteFontFileStream : public IDWriteFontFileStream {
				const std::shared_ptr<IStream> m_stream;

				std::atomic_uint32_t m_nRef = 1;

				IStreamAsDWriteFontFileStream(std::shared_ptr<IStream> pStream)
					: m_stream(std::move(pStream)) {}

			public:
				static IStreamAsDWriteFontFileStream* New(std::shared_ptr<IStream> pStream) {
					return new IStreamAsDWriteFontFileStream(std::move(pStream));
				}

				HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override {
					if (riid == __uuidof(IUnknown))
						*ppvObject = static_cast<IUnknown*>(this);
					else if (riid == __uuidof(IDWriteFontFileStream))
						*ppvObject = static_cast<IDWriteFontFileStream*>(this);
					else
						*ppvObject = nullptr;

					if (!*ppvObject)
						return E_NOINTERFACE;

					AddRef();
					return S_OK;
				}

				ULONG __stdcall AddRef(void) override {
					return ++m_nRef;
				}

				ULONG __stdcall Release(void) override {
					const auto newRef = --m_nRef;
					if (!newRef)
						delete this;
					return newRef;
				}

				HRESULT __stdcall ReadFileFragment(void const** pFragmentStart, uint64_t fileOffset, uint64_t fragmentSize, void** pFragmentContext) override {
					*pFragmentContext = nullptr;
					*pFragmentStart = nullptr;

					if (const auto pMemoryStream = dynamic_cast<MemoryStream*>(m_stream.get())) {
						try {
							*pFragmentStart = pMemoryStream->View(static_cast<std::streamoff>(fileOffset), static_cast<std::streamsize>(fragmentSize)).data();
							return S_OK;
						} catch (const std::out_of_range&) {
							return E_INVALIDARG;
						}

					} else {
						const auto size = static_cast<uint64_t>(m_stream->StreamSize());
						if (fileOffset <= size && fileOffset + fragmentSize <= size && fragmentSize <= (std::numeric_limits<size_t>::max)()) {
							auto pVec = new std::vector<uint8_t>();
							try {
								pVec->resize(static_cast<size_t>(fragmentSize));
								if (m_stream->ReadStreamPartial(static_cast<std::streamoff>(fileOffset), pVec->data(), static_cast<std::streamsize>(fragmentSize)) == fragmentSize) {
									*pFragmentStart = pVec->data();
									*pFragmentContext = pVec;
									return S_OK;
								}
							} catch (...) {
								// pass
							}
							delete pVec;
							return E_FAIL;

						} else
							return E_INVALIDARG;
					}
				}

				void __stdcall ReleaseFileFragment(void* fragmentContext) override {
					if (fragmentContext)
						delete static_cast<std::vector<uint8_t>*>(fragmentContext);
				}

				HRESULT __stdcall GetFileSize(uint64_t* pFileSize) override {
					*pFileSize = static_cast<uint16_t>(m_stream->StreamSize());
					return S_OK;
				}

				HRESULT __stdcall GetLastWriteTime(uint64_t* pLastWriteTime) override {
					*pLastWriteTime = 0;
					return E_NOTIMPL; // E_NOTIMPL by design -- see method documentation in dwrite.h.
				}
			};

			IStreamBasedDWriteFontFileLoader() = default;
			IStreamBasedDWriteFontFileLoader(IStreamBasedDWriteFontFileLoader&&) = delete;
			IStreamBasedDWriteFontFileLoader(const IStreamBasedDWriteFontFileLoader&) = delete;
			IStreamBasedDWriteFontFileLoader& operator=(IStreamBasedDWriteFontFileLoader&&) = delete;
			IStreamBasedDWriteFontFileLoader& operator=(const IStreamBasedDWriteFontFileLoader&) = delete;

		public:
			static IStreamBasedDWriteFontFileLoader& GetInstance() {
				static IStreamBasedDWriteFontFileLoader s_instance;
				return s_instance;
			}

			HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override {
				if (riid == __uuidof(IUnknown))
					*ppvObject = static_cast<IUnknown*>(this);
				else if (riid == __uuidof(IDWriteFontFileLoader))
					*ppvObject = static_cast<IDWriteFontFileLoader*>(this);
				else
					*ppvObject = nullptr;

				if (!*ppvObject)
					return E_NOINTERFACE;

				AddRef();
				return S_OK;
			}

			ULONG __stdcall AddRef(void) override {
				return 1;
			}

			ULONG __stdcall Release(void) override {
				return 0;
			}

			HRESULT STDMETHODCALLTYPE CreateStreamFromKey(
				_In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
				uint32_t fontFileReferenceKeySize,
				_COM_Outptr_ IDWriteFontFileStream** pFontFileStream
			) {
				if (fontFileReferenceKeySize != sizeof(std::shared_ptr<MemoryStream>))
					return E_INVALIDARG;

				auto pMemoryStream = *static_cast<const std::shared_ptr<MemoryStream>*>(fontFileReferenceKey);
				*pFontFileStream = IStreamAsDWriteFontFileStream::New(pMemoryStream);
				return S_OK;
			}
		};

		class DWriteFontTable {
			const IDWriteFontFacePtr m_pFontFace;
			const void* m_pData;
			void* m_pTableContext;
			uint32_t m_nSize;
			BOOL m_bExists;

		public:
			DWriteFontTable(IDWriteFontFace* pFace, uint32_t tag)
				: m_pFontFace(pFace, true) {
				SuccessOrThrow(pFace->TryGetFontTable(tag, &m_pData, &m_nSize, &m_pTableContext, &m_bExists));
			}

			~DWriteFontTable() {
				if (m_bExists)
					m_pFontFace->ReleaseFontTable(m_pTableContext);
			}

			operator bool() const {
				return m_bExists;
			}

			template<typename T = uint8_t>
			std::span<const T> GetSpan() const {
				if (!m_bExists)
					return {};

				return { static_cast<const T*>(m_pData), m_nSize };
			}
		};

	public:
		struct CreateStruct {
			int FaceIndex = 0;
			float Size = 12.f;
			DWRITE_RENDERING_MODE RenderMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL;
			DWRITE_MEASURING_MODE MeasureMode = DWRITE_MEASURING_MODE_GDI_CLASSIC;
			DWRITE_GRID_FIT_MODE GridFitMode = DWRITE_GRID_FIT_MODE_ENABLED;
		};

	private:
		struct ParsedInfoStruct {
			std::shared_ptr<IStream> Stream;
			std::set<char32_t> Characters;
			std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
			DWRITE_FONT_METRICS1 Metrics;
			CreateStruct Params;

			int ScaleFromFontUnit(int fontUnitValue) const {
				return static_cast<int>(std::roundf(static_cast<float>(fontUnitValue) * Params.Size / static_cast<float>(Metrics.designUnitsPerEm)));
			}
		};

		IDWriteFactory3Ptr m_factory;
		IDWriteFontFace1Ptr m_fontFace;
		std::shared_ptr<const ParsedInfoStruct> m_info;
		mutable std::vector<uint8_t> m_drawBuffer;

	public:
		DirectWriteFixedSizeFont(std::filesystem::path path, CreateStruct params)
			: DirectWriteFixedSizeFont(std::make_shared<MemoryStream>(FileStream(path)), std::move(params)) {}

		DirectWriteFixedSizeFont(std::shared_ptr<IStream> stream, CreateStruct params) {
			if (!stream)
				return;

			IDWriteFactoryPtr factory;
			SuccessOrThrow(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)));
			SuccessOrThrow(factory->RegisterFontFileLoader(&IStreamBasedDWriteFontFileLoader::GetInstance()), { DWRITE_E_ALREADYREGISTERED });
			SuccessOrThrow(factory.QueryInterface(decltype(m_factory)::GetIID(), &m_factory), { E_NOINTERFACE });

			auto info = std::make_shared<ParsedInfoStruct>();
			info->Stream = std::move(stream);
			info->Params = std::move(params);

			m_fontFace = FaceFromInfoStruct(m_factory, *info);
			m_fontFace->GetMetrics(&info->Metrics);

			uint32_t rangeCount;
			SuccessOrThrow(m_fontFace->GetUnicodeRanges(0, nullptr, &rangeCount), { E_NOT_SUFFICIENT_BUFFER });
			std::vector<DWRITE_UNICODE_RANGE> ranges(rangeCount);
			SuccessOrThrow(m_fontFace->GetUnicodeRanges(rangeCount, &ranges[0], &rangeCount));

			m_fontFace->GetMetrics(&info->Metrics);

			for (const auto& range : ranges)
				for (uint32_t i = range.first; i <= range.last; ++i)
					info->Characters.insert(static_cast<char32_t>(i));

			DWriteFontTable kernDataRef(m_fontFace, Internal::TrueType::Kern::DirectoryTableTag.NativeValue);
			DWriteFontTable gposDataRef(m_fontFace, Internal::TrueType::Gpos::DirectoryTableTag.NativeValue);
			DWriteFontTable cmapDataRef(m_fontFace, Internal::TrueType::Cmap::DirectoryTableTag.NativeValue);
			Internal::TrueType::Kern::View kern(kernDataRef.GetSpan<char>());
			Internal::TrueType::Gpos::View gpos(gposDataRef.GetSpan<char>());
			Internal::TrueType::Cmap::View cmap(cmapDataRef.GetSpan<char>());
			if (cmap && (kern || gpos)) {
				const auto cmapVector = cmap.GetGlyphToCharMap();

				if (kern)
					info->KerningPairs = kern.Parse(cmapVector);

				if (gpos) {
					const auto pairs = gpos.ExtractAdvanceX(cmapVector);
					// do not overwrite
					info->KerningPairs.insert(pairs.begin(), pairs.end());
				}

				for (auto it = info->KerningPairs.begin(); it != info->KerningPairs.end(); ) {
					it->second = info->ScaleFromFontUnit(it->second);
					if (it->second)
						++it;
					else
						it = info->KerningPairs.erase(it);
				}
			}

			m_info = std::move(info);
		}

		DirectWriteFixedSizeFont() = default;
		DirectWriteFixedSizeFont(DirectWriteFixedSizeFont&&) = default;
		DirectWriteFixedSizeFont& operator=(DirectWriteFixedSizeFont&&) = default;

		DirectWriteFixedSizeFont(const DirectWriteFixedSizeFont& r) : DirectWriteFixedSizeFont() {
			if (r.m_info == nullptr)
				return;

			IDWriteFactoryPtr factory;
			SuccessOrThrow(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)));
			SuccessOrThrow(factory->RegisterFontFileLoader(&IStreamBasedDWriteFontFileLoader::GetInstance()), { DWRITE_E_ALREADYREGISTERED });
			SuccessOrThrow(factory.QueryInterface(decltype(m_factory)::GetIID(), &m_factory), { E_NOINTERFACE });
			m_info = r.m_info;
			m_fontFace = FaceFromInfoStruct(m_factory, *m_info);
		}

		DirectWriteFixedSizeFont& operator=(const DirectWriteFixedSizeFont& r) {
			if (this == &r)
				return *this;

			if (r.m_info == nullptr) {
				m_factory.Release();
				m_fontFace.Release();
				m_info.reset();

			} else {
				IDWriteFactoryPtr factory;
				SuccessOrThrow(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)));
				SuccessOrThrow(factory->RegisterFontFileLoader(&IStreamBasedDWriteFontFileLoader::GetInstance()), { DWRITE_E_ALREADYREGISTERED });
				SuccessOrThrow(factory.QueryInterface(decltype(m_factory)::GetIID(), &m_factory), { E_NOINTERFACE });
				m_info = r.m_info;
				m_fontFace = FaceFromInfoStruct(m_factory, *m_info);
			}

			return *this;
		}

		float GetSize() const override {
			return m_info->Params.Size;
		}

		int GetAscent() const override {
			return m_info->ScaleFromFontUnit(m_info->Metrics.ascent);
		}

		int GetLineHeight() const override {
			return m_info->ScaleFromFontUnit(m_info->Metrics.ascent + m_info->Metrics.descent + m_info->Metrics.lineGap);
		}

		const std::set<char32_t>& GetAllCodepoints() const override {
			return m_info->Characters;
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			IDWriteGlyphRunAnalysisPtr analysis;
			if (!GetGlyphMetrics(codepoint, gm, analysis))
				return false;

			gm.Translate(0, GetAscent());
			return true;
		}

		const void* GetGlyphUniqid(char32_t c) const override {
			const auto it = m_info->Characters.find(c);
			return it == m_info->Characters.end() ? nullptr : &*it;
		}

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override {
			return m_info->KerningPairs;
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			GlyphMetrics gm;
			if (!GetGlyphMetrics(left, gm))
				return 0;

			if (const auto it = m_info->KerningPairs.find(std::make_pair(left, right)); it != m_info->KerningPairs.end())
				return gm.AdvanceX + it->second;

			return gm.AdvanceX;
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor, float gamma) const override {
			IDWriteGlyphRunAnalysisPtr analysis;
			GlyphMetrics gm;
			if (!GetGlyphMetrics(codepoint, gm, analysis))
				return false;

			auto src = gm;
			src.Translate(-src.X1, -src.Y1);
			auto dest = gm;
			dest.Translate(drawX, drawY + GetAscent());
			src.AdjustToIntersection(dest, src.GetWidth(), src.GetHeight(), destWidth, destHeight);
			if (src.IsEffectivelyEmpty() || dest.IsEffectivelyEmpty())
				return true;

			m_drawBuffer.resize(gm.GetArea());
			SuccessOrThrow(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, gm.AsConstRectPtr(), &m_drawBuffer[0], static_cast<uint32_t>(m_drawBuffer.size())));

			Internal::BitmapCopy::ToRGBA8888()
				.From(&m_drawBuffer[0], gm.GetWidth(), gm.GetHeight(), 1, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithBackgroundColor(bgColor)
				.WithGamma(gamma)
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);

			return true;
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity, float gamma) const override {
			IDWriteGlyphRunAnalysisPtr analysis;
			GlyphMetrics gm;
			if (!GetGlyphMetrics(codepoint, gm, analysis))
				return false;

			auto src = gm;
			src.Translate(-src.X1, -src.Y1);
			auto dest = gm;
			dest.Translate(drawX, drawY + GetAscent());
			src.AdjustToIntersection(dest, src.GetWidth(), src.GetHeight(), destWidth, destHeight);
			if (src.IsEffectivelyEmpty() || dest.IsEffectivelyEmpty())
				return true;

			m_drawBuffer.resize(gm.GetArea());
			SuccessOrThrow(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, gm.AsConstRectPtr(), &m_drawBuffer[0], static_cast<uint32_t>(m_drawBuffer.size())));

			Internal::BitmapCopy::ToL8()
				.From(&m_drawBuffer[0], gm.GetWidth(), gm.GetHeight(), 1, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, stride, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithForegroundOpacity(fgOpacity)
				.WithBackgroundColor(bgColor)
				.WithBackgroundOpacity(bgOpacity)
				.WithGamma(gamma)
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
			return true;
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			return std::make_shared<DirectWriteFixedSizeFont>(*this);
		}

	private:
		static IDWriteFontFace1Ptr FaceFromInfoStruct(IDWriteFactory* pFactory, const ParsedInfoStruct& info) {
			IDWriteFontFilePtr fontFile;
			SuccessOrThrow(pFactory->CreateCustomFontFileReference(&info.Stream, sizeof info.Stream, &IStreamBasedDWriteFontFileLoader::GetInstance(), &fontFile));

			BOOL bSupportedFontType;
			DWRITE_FONT_FILE_TYPE fileType;
			DWRITE_FONT_FACE_TYPE faceType;
			uint32_t faceCount;
			SuccessOrThrow(fontFile->Analyze(&bSupportedFontType, &fileType, &faceType, &faceCount));
			if (static_cast<uint32_t>(info.Params.FaceIndex) >= faceCount)
				throw std::out_of_range(std::format("Provided font file has {} faces; requested face is #{}.", info.Params.FaceIndex, faceCount));
			if (!bSupportedFontType)
				throw std::runtime_error("Provided font file is not supported by DirectWrite.");

			IDWriteFontFacePtr fontFace;
			IDWriteFontFile* pFontFileTmp = fontFile;
			SuccessOrThrow(pFactory->CreateFontFace(faceType, 1, &pFontFileTmp, static_cast<uint32_t>(info.Params.FaceIndex), DWRITE_FONT_SIMULATIONS_NONE, &fontFace));

			IDWriteFontFace1Ptr m_fontFace;
			SuccessOrThrow(fontFace.QueryInterface(decltype(m_fontFace)::GetIID(), &m_fontFace));

			uint32_t rangeCount;
			SuccessOrThrow(m_fontFace->GetUnicodeRanges(0, nullptr, &rangeCount), { E_NOT_SUFFICIENT_BUFFER });
			std::vector<DWRITE_UNICODE_RANGE> ranges(rangeCount);
			SuccessOrThrow(m_fontFace->GetUnicodeRanges(rangeCount, &ranges[0], &rangeCount));

			return m_fontFace;
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm, IDWriteGlyphRunAnalysisPtr& analysis) const {
			uint16_t glyphIndex;
			SuccessOrThrow(m_fontFace->GetGlyphIndices(reinterpret_cast<const uint32_t*>(&codepoint), 1, &glyphIndex));
			if (!glyphIndex) {
				gm.Clear();
				return false;
			}

			int32_t glyphAdvanceI;
			SuccessOrThrow(m_fontFace->GetGdiCompatibleGlyphAdvances(m_info->Params.Size, 1.f, nullptr, TRUE, FALSE, 1, &glyphIndex, &glyphAdvanceI));

			float glyphAdvance{};
			DWRITE_GLYPH_OFFSET glyphOffset{};
			const DWRITE_GLYPH_RUN run{
				.fontFace = m_fontFace,
				.fontEmSize = m_info->Params.Size,
				.glyphCount = 1,
				.glyphIndices = &glyphIndex,
				.glyphAdvances = &glyphAdvance,
				.glyphOffsets = &glyphOffset,
				.isSideways = FALSE,
				.bidiLevel = 0,
			};

			SuccessOrThrow(m_factory->CreateGlyphRunAnalysis(
				&run,
				nullptr,
				m_info->Params.RenderMode,
				m_info->Params.MeasureMode,
				m_info->Params.GridFitMode,
				DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
				0,
				0,
				&analysis));

			SuccessOrThrow(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, gm.AsMutableRectPtr()));
			gm.AdvanceX = m_info->ScaleFromFontUnit(glyphAdvanceI);
			gm.Empty = false;
			return gm;
		}
	};
}

#endif
