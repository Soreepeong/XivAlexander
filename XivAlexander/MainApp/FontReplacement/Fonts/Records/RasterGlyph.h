#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Coverage placed relative to the pen on the baseline.
	struct RasterGlyph {
		int Advance = 0;
		int Left = 0;
		int Top = 0;
		int Width = 0;
		int Height = 0;
		std::vector<uint8_t> Alpha;

		// Empty, or R, G, B coverage per pixel of Alpha (their mean); resampling drops it.
		std::vector<uint8_t> Lcd;

		[[nodiscard]] bool HasLcd() const { return !Lcd.empty(); }

		static void ScaledBounds(int left, int top, int width, int height, float scale, float shiftX, float shiftY, int& x0, int& y0, int& w, int& h);
		[[nodiscard]] RasterGlyph Scaled(float scale, float shiftX, float shiftY) const;
		[[nodiscard]] RasterGlyph SqueezedX(float scale) const;
		[[nodiscard]] RasterGlyph Reboxed(int left, int top, int width, int height) const;
		static RasterGlyph Merge(std::vector<RasterGlyph> pieces, int advance);
		static void BlitMax(std::span<uint8_t> buffer, int width, int height, const RasterGlyph& g, int x, int y);
		static std::vector<uint8_t> CoverageTable(float exponent);
		[[nodiscard]] RasterGlyph WithCoverage(const std::vector<uint8_t>& table) const;

	private:
		// Each output pixel's weights sum to 1 / scale.
		static std::vector<std::vector<std::pair<int, float>>> AreaWeights(int first, int count, float origin, float scale, int sourceSize);

		static void BlitMaxLcd(std::span<uint8_t> buffer, int width, int height, const RasterGlyph& g, int x, int y);
		[[nodiscard]] float Bilinear(float u, float v) const;
		[[nodiscard]] float At(int x, int y) const;
	};
}
