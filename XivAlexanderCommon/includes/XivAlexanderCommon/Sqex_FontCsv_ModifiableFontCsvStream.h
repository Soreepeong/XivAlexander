#pragma once

#include "Sqex_FontCsv.h"

namespace Sqex::FontCsv {
	class ModifiableFontCsvStream : public RandomAccessStream {
		FontCsvHeader m_fcsv;
		FontTableHeader m_fthd;
		std::vector<FontTableEntry> m_fontTableEntries;
		KerningHeader m_knhd;
		std::vector<KerningEntry> m_kerningEntries;

	public:
		ModifiableFontCsvStream();
		ModifiableFontCsvStream(const RandomAccessStream& stream, bool strict = false);
		
		[[nodiscard]] uint64_t StreamSize() const override;
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		[[nodiscard]] const FontTableEntry* GetFontEntry(char32_t c) const;
		[[nodiscard]] int GetKerningDistance(char32_t l, char32_t r) const;
		[[nodiscard]] auto& GetFontTableEntries() const { return m_fontTableEntries; }
		[[nodiscard]] auto& GetKerningEntries() const { return m_kerningEntries; }

		void AddFontEntry(char32_t c, uint16_t textureIndex, uint16_t textureOffsetX, uint16_t textureOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, uint8_t nextOffsetX, uint8_t currentOffsetY);
		void AddKerning(char32_t l, char32_t r, int rightOffset);

		[[nodiscard]] uint16_t TextureWidth() const { return m_fthd.TextureWidth; }
		[[nodiscard]] uint16_t TextureHeight() const { return m_fthd.TextureHeight; }
		void TextureWidth(uint16_t v) { m_fthd.TextureWidth = v; }
		void TextureHeight(uint16_t v) { m_fthd.TextureHeight = v; }

		[[nodiscard]] float Points() const { return m_fthd.Points; }
		void Points(float v) { m_fthd.Points = v; }

		[[nodiscard]] uint32_t LineHeight() const { return m_fthd.LineHeight; }
		void LineHeight(uint32_t v) { m_fthd.LineHeight = v; }

		[[nodiscard]] uint32_t Ascent() const { return m_fthd.Ascent; }
		void Ascent(uint32_t v) { m_fthd.Ascent = v; }
	};
}
