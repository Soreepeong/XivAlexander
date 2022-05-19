#ifndef _XIVRES_FONTGENERATOR_IFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_IFIXEDSIZEFONT_H_

#include <array>
#include <set>
#include <vector>

#include "../FontdataStream.h"
#include "../MipmapStream.h"
#include "../PixelFormats.h"

namespace XivRes::FontGenerator {
	struct GlyphMetrics {
		using MetricType = int;

		bool Empty = true;
		MetricType X1 = 0;
		MetricType Y1 = 0;
		MetricType X2 = 0;
		MetricType Y2 = 0;
		MetricType AdvanceX = 0;

		void AdjustToIntersection(GlyphMetrics& r, MetricType srcWidth, MetricType srcHeight, MetricType destWidth, MetricType destHeight) {
			if (X1 < 0) {
				r.X1 -= X1;
				X1 = 0;
			}
			if (r.X1 < 0) {
				X1 -= r.X1;
				r.X1 = 0;
			}
			if (Y1 < 0) {
				r.Y1 -= Y1;
				Y1 = 0;
			}
			if (r.Y1 < 0) {
				Y1 -= r.Y1;
				r.Y1 = 0;
			}
			if (X2 >= srcWidth) {
				r.X2 -= X2 - srcWidth;
				X2 = srcWidth;
			}
			if (r.X2 >= destWidth) {
				X2 -= r.X2 - destWidth;
				r.X2 = destWidth;
			}
			if (Y2 >= srcHeight) {
				r.Y2 -= Y2 - srcHeight;
				Y2 = srcHeight;
			}
			if (r.Y2 >= destHeight) {
				Y2 -= r.Y2 - destHeight;
				r.Y2 = destHeight;
			}

			if (X1 >= X2 || r.X1 >= r.X2 || Y1 >= Y2 || r.Y1 >= r.Y2)
				*this = r = {};
		}

		void Clear() {
			Empty = true;
			X1 = Y1 = X2 = Y2 = AdvanceX = 0;
		}

		[[nodiscard]] MetricType GetWidth() const { return X2 - X1; }

		[[nodiscard]] MetricType GetHeight() const { return Y2 - Y1; }

		[[nodiscard]] MetricType GetArea() const { return GetWidth() * GetHeight(); }

		[[nodiscard]] bool IsEffectivelyEmpty() const { return Empty || X1 == X2 || Y1 == Y2; }

		operator bool() const {
			return !Empty;
		}

#ifdef _WINDOWS_
		GlyphMetrics& SetFrom(const RECT& r, bool keepAdvanceXIfNotEmpty = true) {
			Empty = !r.left && !r.top && !r.right && !r.bottom;
			if (!Empty) {
				X1 = r.left;
				Y1 = r.top;
				X2 = r.right;
				Y2 = r.bottom;
				if (!keepAdvanceXIfNotEmpty)
					AdvanceX = 0;
			} else {
				X1 = Y2 = X2 = Y2 = AdvanceX = 0;
			}
			return *this;
		}

		operator RECT() const {
			return { static_cast<LONG>(X1), static_cast<LONG>(Y1), static_cast<LONG>(X2), static_cast<LONG>(Y2) };
		}

		struct AsMutableRectPtrType {
			GlyphMetrics& m;
			RECT r;
			const bool keepAdvanceXIfNotEmpty;
			operator RECT* () {
				return &r;
			}

			AsMutableRectPtrType(GlyphMetrics& m, bool keepAdvanceXIfNotEmpty)
				: m(m)
				, r(m)
				, keepAdvanceXIfNotEmpty(keepAdvanceXIfNotEmpty) {

			}
			~AsMutableRectPtrType() { m.SetFrom(r, keepAdvanceXIfNotEmpty); }
		} AsMutableRectPtr(bool keepAdvanceXIfNotEmpty = true) {
			return AsMutableRectPtrType(*this, keepAdvanceXIfNotEmpty);
		}

		[[nodiscard]] struct AsConstRectPtrType {
			RECT r;
			operator RECT* () { return &r; }
			AsConstRectPtrType(const GlyphMetrics& m) : r(m) {}
		} AsConstRectPtr() const {
			return AsConstRectPtrType(*this);
		}
#endif

		template<typename Mul, typename Div>
		GlyphMetrics& Scale(Mul mul, Div div) {
			const auto mMul = static_cast<MetricType>(mul);
			const auto mDiv = static_cast<MetricType>(div);
			X1 = X1 * mMul / mDiv;
			Y1 = Y1 * mMul / mDiv;
			X2 = X2 * mMul / mDiv;
			Y2 = Y2 * mMul / mDiv;
			AdvanceX = AdvanceX * mMul / mDiv;
			return *this;
		}

		GlyphMetrics& Translate(MetricType x, MetricType y) {
			X1 += x;
			X2 += x;
			Y1 += y;
			Y2 += y;
			return *this;
		}

		GlyphMetrics& ExpandToFit(const GlyphMetrics& r) {
			if (r.Empty)
				return *this;
			if (Empty) {
				X1 = r.X1;
				Y1 = r.Y1;
				X2 = r.X2;
				Y2 = r.Y2;
				AdvanceX = r.AdvanceX;
				Empty = false;
			} else {
				const auto prevLeft = X1;
				X1 = (std::min)(X1, r.X1);
				Y1 = (std::min)(Y1, r.Y1);
				X2 = (std::max)(X2, r.X2);
				Y2 = (std::max)(Y2, r.Y2);
				if (prevLeft + AdvanceX > r.X1 + r.AdvanceX)
					AdvanceX = prevLeft + AdvanceX - X1;
				else
					AdvanceX = r.X1 + AdvanceX - X1;
			}
			return *this;
		}
	};

	class IFixedSizeFont {
	protected:
		static constexpr std::array<uint8_t, 256> LinearGammaTable{ {
			0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
			0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
			0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
			0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
			0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
			0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
			0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
			0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
			0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
			0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
			0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
			0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
			0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
			0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
		} };

		static constexpr std::array<uint8_t, 256> WindowsGammaTable{ {
			0x00, 0x05, 0x08, 0x0b, 0x0d, 0x0f, 0x12, 0x14, 0x16, 0x17, 0x19, 0x1b, 0x1d, 0x1e, 0x20, 0x22,
			0x23, 0x25, 0x26, 0x28, 0x29, 0x2b, 0x2c, 0x2e, 0x2f, 0x31, 0x32, 0x33, 0x35, 0x36, 0x37, 0x39,
			0x3a, 0x3b, 0x3c, 0x3e, 0x3f, 0x40, 0x41, 0x43, 0x44, 0x45, 0x46, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
			0x4d, 0x4e, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e,
			0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e,
			0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e,
			0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d,
			0x8e, 0x8f, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x97, 0x98, 0x99, 0x9a, 0x9b,
			0x9c, 0x9d, 0x9e, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
			0xaa, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb4, 0xb5, 0xb6,
			0xb7, 0xb8, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc0, 0xc1, 0xc2, 0xc3,
			0xc4, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcc, 0xcd, 0xce, 0xcf, 0xcf,
			0xd0, 0xd1, 0xd2, 0xd3, 0xd3, 0xd4, 0xd5, 0xd6, 0xd6, 0xd7, 0xd8, 0xd9, 0xd9, 0xda, 0xdb, 0xdc,
			0xdc, 0xdd, 0xde, 0xdf, 0xdf, 0xe0, 0xe1, 0xe2, 0xe2, 0xe3, 0xe4, 0xe5, 0xe5, 0xe6, 0xe7, 0xe8,
			0xe8, 0xe9, 0xea, 0xeb, 0xeb, 0xec, 0xed, 0xee, 0xee, 0xef, 0xf0, 0xf1, 0xf1, 0xf2, 0xf3, 0xf3,
			0xf4, 0xf5, 0xf6, 0xf6, 0xf7, 0xf8, 0xf9, 0xf9, 0xfa, 0xfb, 0xfb, 0xfc, 0xfd, 0xfe, 0xfe, 0xff,
		} };

	public:
		virtual float GetSize() const = 0;

		virtual int GetAscent() const = 0;

		virtual int GetLineHeight() const = 0;

		virtual size_t GetCodepointCount() const = 0;

		virtual std::set<char32_t> GetAllCodepoints() const = 0;

		virtual bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const = 0;

		virtual const void* GetGlyphUniqid(char32_t c) const = 0;

		virtual size_t GetKerningEntryCount() const = 0;

		virtual std::map<std::pair<char32_t, char32_t>, int> GetKerningPairs() const = 0;

		virtual int GetAdjustedAdvanceX(char32_t left, char32_t right) const = 0;

		virtual const std::array<uint8_t, 256>& GetGammaTable() const = 0;

		virtual void SetGammaTable(const std::array<uint8_t, 256>& gammaTable) = 0;

		virtual bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const = 0;

		virtual bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const = 0;

		virtual std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const = 0;
	};

	class DefaultAbstractFixedSizeFont : public IFixedSizeFont {

	};

	class FixedSizeFontConstView : public IFixedSizeFont {
		const IFixedSizeFont* m_pFont;

	public:
		FixedSizeFontConstView(const IFixedSizeFont* pFont) 
			: m_pFont(pFont) {}

		float GetSize() const override {
			return m_pFont->GetSize();
		}

		int GetAscent() const override {
			return m_pFont->GetAscent();
		}

		int GetLineHeight() const override {
			return m_pFont->GetLineHeight();
		}

		size_t GetCodepointCount() const override {
			return m_pFont->GetCodepointCount();
		}

		std::set<char32_t> GetAllCodepoints() const override {
			return m_pFont->GetAllCodepoints();
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			return m_pFont->GetGlyphMetrics(codepoint, gm);
		}

		const void* GetGlyphUniqid(char32_t c) const override {
			return m_pFont->GetGlyphUniqid(c);
		}

		size_t GetKerningEntryCount() const override {
			return m_pFont->GetKerningEntryCount();
		}

		std::map<std::pair<char32_t, char32_t>, int> GetKerningPairs() const override {
			return m_pFont->GetKerningPairs();
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			return m_pFont->GetAdjustedAdvanceX(left, right);
		}

		const std::array<uint8_t, 256>& GetGammaTable() const override {
			return m_pFont->GetGammaTable();
		}

		void SetGammaTable(const std::array<uint8_t, 256>& gammaTable) override {
			throw std::runtime_error("FixedSizeFontConstView does not support changing gamma table.");
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const override {
			return m_pFont->Draw(codepoint, pBuf, drawX, drawY, destWidth, destHeight, fgColor, bgColor);
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			return m_pFont->Draw(codepoint, pBuf, stride, drawX, drawY, destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			return m_pFont->GetThreadSafeView();
		}
	};
}

#endif
