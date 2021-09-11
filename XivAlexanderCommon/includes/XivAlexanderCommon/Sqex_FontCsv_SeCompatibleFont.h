#pragma once

#include "Sqex.h"
#include "Sqex_FontCsv_ModifiableFontCsvStream.h"

namespace Sqex::FontCsv {

	struct GlyphMeasurement {
		bool empty = true;
		SSIZE_T left = 0;
		SSIZE_T top = 0;
		SSIZE_T right = 0;
		SSIZE_T bottom = 0;
		SSIZE_T offsetX = 0;

		void AdjustToIntersection(GlyphMeasurement& r, SSIZE_T srcWidth, SSIZE_T srcHeight, SSIZE_T destWidth, SSIZE_T destHeight);
		[[nodiscard]] SSIZE_T Width() const { return right - left; }
		[[nodiscard]] SSIZE_T Height() const { return bottom - top; }
		[[nodiscard]] SSIZE_T Area() const { return Width() * Height(); }
		[[nodiscard]] bool EffectivelyEmpty() const { return empty || (left == right && top == bottom); }

		GlyphMeasurement& SetFrom(const RECT& r) {
			*this = {
				!r.left && !r.top && !r.right && !r.bottom,
				r.left, r. top, r.right, r.bottom, 0
			};
			return *this;
		}

		operator bool() const {
			return !empty;
		}

		operator RECT() const {
			return { static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(right), static_cast<LONG>(bottom) };
		}

		struct AsMutableRectPtrType {
			GlyphMeasurement& m;
			RECT r;
			operator RECT* () {
				return &r;
			}

			AsMutableRectPtrType(GlyphMeasurement& m) : m(m), r(m) {}
			~AsMutableRectPtrType() { m.SetFrom(r); }
		} AsMutableRectPtr() {
			return AsMutableRectPtrType(*this);
		}

		[[nodiscard]] struct AsConstRectPtrType {
			RECT r;
			operator RECT* () { return &r; }
			AsConstRectPtrType(const GlyphMeasurement& m) : r(m) {}
		} AsConstRectPtr() const {
			return AsConstRectPtrType(*this);
		}

		template<typename Mul, typename Div>
		GlyphMeasurement& Scale(Mul mul, Div div) {
			if constexpr (std::is_floating_point_v<Mul> || std::is_floating_point_v<Div>) {
				left = static_cast<decltype(left)>(static_cast<double>(left) * mul / div);
				top = static_cast<decltype(top)>(static_cast<double>(top) * mul / div);
				right = static_cast<decltype(right)>(static_cast<double>(right) * mul / div);
				bottom = static_cast<decltype(bottom)>(static_cast<double>(bottom) * mul / div);
				offsetX = static_cast<decltype(offsetX)>(static_cast<double>(offsetX) * mul / div);
			} else {
				static_assert(std::is_integral_v<Mul> && std::is_integral_v<Div>);
				left = static_cast<decltype(left)>(left * static_cast<SSIZE_T>(mul) / div);
				top = static_cast<decltype(top)>(top * static_cast<SSIZE_T>(mul) / div);
				right = static_cast<decltype(right)>(right * static_cast<SSIZE_T>(mul) / div);
				bottom = static_cast<decltype(bottom)>(bottom * static_cast<SSIZE_T>(mul) / div);
				offsetX = static_cast<decltype(offsetX)>(offsetX * static_cast<SSIZE_T>(mul) / div);
			}
			return *this;
		}

		GlyphMeasurement& Translate(SSIZE_T x, SSIZE_T y) {
			left += x;
			right += x;
			top += y;
			bottom += y;
			return *this;
		}

		GlyphMeasurement& ExpandToFit(const GlyphMeasurement& r) {
			if (empty) {
				left = r.left;
				top = r.top;
				right = r.right;
				bottom = r.bottom;
				empty = false;
			} else {
				left = std::min(left, r.left);
				top = std::min(top, r.top);
				right = std::max(right, r.right);
				bottom = std::max(bottom, r.bottom);
			}
			return *this;
		}
	};

	class SeCompatibleFont {
		mutable GlyphMeasurement m_maxBoundingBox = { true };

	public:
		SeCompatibleFont() = default;
		virtual ~SeCompatibleFont() = default;

		[[nodiscard]] virtual bool HasCharacter(char32_t) const = 0;
		[[nodiscard]] virtual SSIZE_T GetCharacterWidth(char32_t c) const = 0;
		[[nodiscard]] virtual float Size() const = 0;
		[[nodiscard]] virtual const std::vector<char32_t>& GetAllCharacters() const = 0;
		[[nodiscard]] virtual GlyphMeasurement MaxBoundingBox() const;
		[[nodiscard]] virtual uint32_t Ascent() const = 0;
		[[nodiscard]] virtual uint32_t Descent() const = 0;
		[[nodiscard]] virtual uint32_t Height() const { return Ascent() + Descent(); }
		[[nodiscard]] virtual const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const = 0;
		[[nodiscard]] virtual SSIZE_T GetKerning(char32_t l, char32_t r, SSIZE_T defaultOffset = 0) const;

		[[nodiscard]] virtual GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const = 0;
		[[nodiscard]] virtual GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, const std::u32string& s) const;
		[[nodiscard]] virtual GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, const std::string& s) const {
			return Measure(x, y, ToU32(s));
		}

		[[nodiscard]] virtual SSIZE_T GetOffsetX(char32_t c) const { return Measure(0, 0, c).offsetX; }
	};

	class SeFont : public virtual SeCompatibleFont {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		SeFont(std::shared_ptr<const ModifiableFontCsvStream> stream);
		~SeFont() override;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] SSIZE_T GetCharacterWidth(char32_t c) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t Descent() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const override;

		using SeCompatibleFont::Measure;
		[[nodiscard]] static GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, const FontTableEntry& entry);
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const override;

	protected:
		[[nodiscard]] const ModifiableFontCsvStream& GetStream() const;
	};

	class CascadingFont : public virtual SeCompatibleFont {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		CascadingFont(std::vector<std::shared_ptr<SeCompatibleFont>> fontList);
		CascadingFont(std::vector<std::shared_ptr<SeCompatibleFont>> fontList, float normalizedSize, uint32_t ascent, uint32_t descent);
		~CascadingFont() override;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] SSIZE_T GetCharacterWidth(char32_t c) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t Descent() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const override;

		using SeCompatibleFont::Measure;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const override;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, const std::u32string& s) const override;

	protected:
		[[nodiscard]] const std::vector<std::shared_ptr<SeCompatibleFont>>& GetFontList() const;
	};
}