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
			X1 = Y1 = X2 = Y2 = AdvanceX = 0;
		}

		[[nodiscard]] MetricType GetWidth() const { return X2 - X1; }

		[[nodiscard]] MetricType GetHeight() const { return Y2 - Y1; }

		[[nodiscard]] MetricType GetArea() const { return GetWidth() * GetHeight(); }

		[[nodiscard]] bool IsEffectivelyEmpty() const { return X1 == X2 || Y1 == Y2; }

#ifdef _WINDOWS_
		GlyphMetrics& SetFrom(const RECT& r) {
			X1 = r.left;
			Y1 = r.top;
			X2 = r.right;
			Y2 = r.bottom;
			return *this;
		}

		operator RECT() const {
			return { static_cast<LONG>(X1), static_cast<LONG>(Y1), static_cast<LONG>(X2), static_cast<LONG>(Y2) };
		}

		struct AsMutableRectPtrType {
			GlyphMetrics& m;
			RECT r;
			operator RECT* () {
				return &r;
			}

			AsMutableRectPtrType(GlyphMetrics& m)
				: m(m)
				, r(m) {

			}
			~AsMutableRectPtrType() { m.SetFrom(r); }
		} AsMutableRectPtr() {
			return AsMutableRectPtrType(*this);
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
			const auto prevLeft = X1;
			X1 = (std::min)(X1, r.X1);
			Y1 = (std::min)(Y1, r.Y1);
			X2 = (std::max)(X2, r.X2);
			Y2 = (std::max)(Y2, r.Y2);
			if (prevLeft + AdvanceX > r.X1 + r.AdvanceX)
				AdvanceX = prevLeft + AdvanceX - X1;
			else
				AdvanceX = r.X1 + AdvanceX - X1;
			return *this;
		}
	};

	class IFixedSizeFont {
	public:
		virtual std::string GetFamilyName() const = 0;

		virtual std::string GetSubfamilyName() const = 0;

		virtual float GetSize() const = 0;

		virtual int GetAscent() const = 0;

		virtual int GetLineHeight() const = 0;

		virtual const std::set<char32_t>& GetAllCodepoints() const = 0;

		virtual bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const = 0;

		virtual const void* GetBaseFontGlyphUniqid(char32_t c) const = 0;

		virtual const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const = 0;

		virtual int GetAdjustedAdvanceX(char32_t left, char32_t right) const = 0;

		virtual bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const = 0;

		virtual bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const = 0;

		virtual std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const = 0;

		virtual const IFixedSizeFont* GetBaseFont(char32_t codepoint) const = 0;
	};

	class DefaultAbstractFixedSizeFont : public IFixedSizeFont {
	public:
		const void* GetBaseFontGlyphUniqid(char32_t c) const override {
			const auto& codepoints = GetAllCodepoints();
			const auto it = codepoints.find(c);
			return it == codepoints.end() ? nullptr : &*it;
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			GlyphMetrics gm;
			if (!GetGlyphMetrics(left, gm))
				return 0;

			const auto& kerningPairs = GetAllKerningPairs();
			if (const auto it = kerningPairs.find(std::make_pair(left, right)); it != kerningPairs.end())
				return gm.AdvanceX + it->second;

			return gm.AdvanceX;
		}
	};

	class EmptyFixedSizeFont : public IFixedSizeFont {
	public:
		struct CreateStruct {
			int Ascent = 0;
			int LineHeight = 0;
		};

	private:
		float m_size = 0.f;
		CreateStruct m_fontDef;

	public:
		EmptyFixedSizeFont(float size, CreateStruct fontDef)
			: m_size(size)
			, m_fontDef(fontDef) {}

		EmptyFixedSizeFont() = default;
		EmptyFixedSizeFont(EmptyFixedSizeFont&&) = default;
		EmptyFixedSizeFont(const EmptyFixedSizeFont& r) = default;
		EmptyFixedSizeFont& operator=(EmptyFixedSizeFont&&) = default;
		EmptyFixedSizeFont& operator=(const EmptyFixedSizeFont&) = default;

		std::string GetFamilyName() const override {
			return "Empty";
		}

		std::string GetSubfamilyName() const override {
			return "Regular";
		}

		float GetSize() const override {
			return m_size;
		}

		int GetAscent() const override {
			return m_fontDef.Ascent;
		}

		int GetLineHeight() const override {
			return m_fontDef.LineHeight;
		}

		const std::set<char32_t>& GetAllCodepoints() const override {
			static const std::set<char32_t> s_empty;
			return s_empty;
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			gm.Clear();
			return false;
		}

		const void* GetBaseFontGlyphUniqid(char32_t c) const override {
			return nullptr;
		}

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override {
			static const std::map<std::pair<char32_t, char32_t>, int> s_empty;
			return s_empty;
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			return 0;
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const override {
			return false;
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			return false;
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			return std::make_shared<EmptyFixedSizeFont>(*this);
		}

		const IFixedSizeFont* GetBaseFont(char32_t codepoint) const override {
			return this;
		}
	};
}

#endif
