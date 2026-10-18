#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// 8-bit coverage, relative to the pen on the baseline.
	struct RasterGlyph {
		int Advance = 0;
		int Left = 0;
		int Top = 0;
		int Width = 0;
		int Height = 0;
		std::vector<uint8_t> Alpha;

		// Empty, or R, G, B subpixel coverage per pixel of Alpha (which is their mean); resampling drops it.
		std::vector<uint8_t> Lcd;

		[[nodiscard]] bool HasLcd() const { return !Lcd.empty(); }

		// Scaled about the pen, shifted right and down by a fraction of a pixel.
		static void ScaledBounds(int left, int top, int width, int height, float scale, float shiftX, float shiftY, int& x0, int& y0, int& w, int& h);

		// About the pen, shifted by a fraction of a pixel; shrunk, a pixel averages the source area it covers, enlarged, 4 x 4 bilinear samples.
		[[nodiscard]] RasterGlyph Scaled(float scale, float shiftX, float shiftY) const;

		// About the pen; each pixel averages the source columns it covers.
		[[nodiscard]] RasterGlyph SqueezedX(float scale) const;

		[[nodiscard]] RasterGlyph Trimmed() const;

		// In another box relative to the pen: what is outside it cut off, the room it adds empty.
		[[nodiscard]] RasterGlyph Reboxed(int left, int top, int width, int height) const;

		// The pieces share one pen; overlaps keep the larger coverage.
		static RasterGlyph Merge(std::vector<RasterGlyph> pieces, int advance);

		static void BlitMax(std::span<uint8_t> buffer, int width, int height, const RasterGlyph& g, int x, int y);

		// Into 3 bytes a pixel; g's Alpha stands in for all three without Lcd.
		static void BlitMaxLcd(std::span<uint8_t> buffer, int width, int height, const RasterGlyph& g, int x, int y);

		// What an element's gamma maps coverage through; empty for a gamma of 1 (or none).
		static std::vector<uint8_t> GammaTable(float gamma);

		// Maps coverage to coverage^exponent.
		static std::vector<uint8_t> CoverageTable(float exponent);

		[[nodiscard]] RasterGlyph WithCoverage(const std::vector<uint8_t>& table) const;

	private:
		// Per destination pixel from first, the source pixels it covers and by how much (summing to 1 / scale).
		static std::vector<std::vector<std::pair<int, float>>> AreaWeights(int first, int count, float origin, float scale, int sourceSize);

		[[nodiscard]] float Bilinear(float u, float v) const;
		[[nodiscard]] float At(int x, int y) const;
	};
}
