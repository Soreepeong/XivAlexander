#pragma once

#include <FontChanger.Presets/Structs.h>

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// On screen, y down: x' = M11 x + M12 y, y' = M21 x + M22 y (FontChanger's screen matrix); a * b applies b, then a.
	struct Matrix2x2 {
		float M11 = 1;
		float M12 = 0;
		float M21 = 0;
		float M22 = 1;

		static constexpr Matrix2x2 Identity() { return {}; }

		static constexpr Matrix2x2 CreateScale(float x, float y) { return {x, 0, 0, y}; }

		static Matrix2x2 Of(const FontChanger::FixedSizeFont::font_render_transformation_matrix& m) { return {m.M11, m.M12, m.M21, m.M22}; }
		static Matrix2x2 Of(const FontChanger::Structs::TransformStruct& t) { return Of(t.GetScreenMatrix()); }

		[[nodiscard]] FontChanger::FixedSizeFont::font_render_transformation_matrix ToMatrix() const { return {M11, M12, M21, M22}; }

		[[nodiscard]] bool IsIdentity() const { return *this == Identity(); }

		friend constexpr Matrix2x2 operator*(const Matrix2x2& a, const Matrix2x2& b) {
			return {
				a.M11 * b.M11 + a.M12 * b.M21,
				a.M11 * b.M12 + a.M12 * b.M22,
				a.M21 * b.M11 + a.M22 * b.M21,
				a.M21 * b.M12 + a.M22 * b.M22,
			};
		}

		constexpr Matrix2x2& operator*=(const Matrix2x2& b) { return *this = *this * b; }

		bool operator==(const Matrix2x2&) const = default;
	};
}
