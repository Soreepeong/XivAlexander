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
		SSIZE_T advanceX = 0;

		void AdjustToIntersection(GlyphMeasurement& r, SSIZE_T srcWidth, SSIZE_T srcHeight, SSIZE_T destWidth, SSIZE_T destHeight);
		[[nodiscard]] SSIZE_T Width() const { return right - left; }
		[[nodiscard]] SSIZE_T Height() const { return bottom - top; }
		[[nodiscard]] SSIZE_T Area() const { return Width() * Height(); }
		[[nodiscard]] bool EffectivelyEmpty() const { return empty || (left == right && top == bottom); }

		GlyphMeasurement& SetFrom(const RECT& r, bool keepAdvanceXIfNotEmpty = true) {
			empty = !r.left && !r.top && !r.right && !r.bottom;
			if (!empty) {
				left = r.left;
				top = r.top;
				right = r.right;
				bottom = r.bottom;
				if (!keepAdvanceXIfNotEmpty)
					advanceX = 0;
			} else {
				left = top = right = bottom = advanceX = 0;
			}
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
			const bool keepAdvanceXIfNotEmpty;
			operator RECT* () {
				return &r;
			}

			AsMutableRectPtrType(GlyphMeasurement& m, bool keepAdvanceXIfNotEmpty)
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
				advanceX = static_cast<decltype(advanceX)>(static_cast<double>(advanceX) * mul / div);
			} else {
				static_assert(std::is_integral_v<Mul> && std::is_integral_v<Div>);
				left = static_cast<decltype(left)>(left * static_cast<SSIZE_T>(mul) / div);
				top = static_cast<decltype(top)>(top * static_cast<SSIZE_T>(mul) / div);
				right = static_cast<decltype(right)>(right * static_cast<SSIZE_T>(mul) / div);
				bottom = static_cast<decltype(bottom)>(bottom * static_cast<SSIZE_T>(mul) / div);
				advanceX = static_cast<decltype(advanceX)>(advanceX * static_cast<SSIZE_T>(mul) / div);
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
			if (r.empty)
				return *this;
			if (empty) {
				left = r.left;
				top = r.top;
				right = r.right;
				bottom = r.bottom;
				advanceX = r.advanceX;
				empty = false;
			} else {
				const auto prevLeft = left;
				left = std::min(left, r.left);
				top = std::min(top, r.top);
				right = std::max(right, r.right);
				bottom = std::max(bottom, r.bottom);
				if (prevLeft + advanceX > r.left + r.advanceX)
					advanceX = prevLeft + advanceX - left;
				else
					advanceX = r.left + advanceX - left;
			}
			return *this;
		}
	};

	class SeCompatibleFont {
		mutable GlyphMeasurement m_maxBoundingBox = { true };

	protected:
		mutable int m_advanceWidthDelta = 0;

	public:
		SeCompatibleFont() = default;
		virtual ~SeCompatibleFont() = default;

		void AdvanceWidthDelta(int value) { m_advanceWidthDelta = value; }
		int AdvanceWidthDelta() const { return m_advanceWidthDelta; }

		[[nodiscard]] virtual bool HasCharacter(char32_t) const = 0;
		[[nodiscard]] virtual float Size() const = 0;
		[[nodiscard]] virtual const std::vector<char32_t>& GetAllCharacters() const = 0;
		[[nodiscard]] virtual GlyphMeasurement MaxBoundingBox() const;
		[[nodiscard]] virtual uint32_t Ascent() const = 0;
		[[nodiscard]] virtual uint32_t LineHeight() const = 0;
		[[nodiscard]] virtual const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const = 0;
		[[nodiscard]] virtual SSIZE_T GetKerning(char32_t l, char32_t r, SSIZE_T defaultOffset = 0) const;

		[[nodiscard]] virtual GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const = 0;
		[[nodiscard]] virtual GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, const std::u32string& s) const;
		[[nodiscard]] virtual GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, const std::string& s) const {
			return Measure(x, y, ToU32(s));
		}
	};

	class SeFont : public virtual SeCompatibleFont {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		SeFont(std::shared_ptr<const ModifiableFontCsvStream> stream);
		~SeFont() override;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t LineHeight() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const override;

		using SeCompatibleFont::Measure;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, const FontTableEntry& entry) const;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const override;

	protected:
		[[nodiscard]] const ModifiableFontCsvStream& GetStream() const;
	};

	class CascadingFont : public virtual SeCompatibleFont {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		CascadingFont(std::vector<std::shared_ptr<SeCompatibleFont>> fontList);
		CascadingFont(std::vector<std::shared_ptr<SeCompatibleFont>> fontList, float normalizedSize, uint32_t ascent, uint32_t lineHeight);
		~CascadingFont() override;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t LineHeight() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const override;

		using SeCompatibleFont::Measure;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const override;

	protected:
		[[nodiscard]] const std::vector<std::shared_ptr<SeCompatibleFont>>& GetFontList() const;
	};
}
