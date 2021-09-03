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
		ModifiableFontCsvStream(float pt, uint16_t textureWidth, uint16_t textureHeight);
		ModifiableFontCsvStream(const RandomAccessStream& stream, bool strict = false);
		
		[[nodiscard]] uint32_t StreamSize() const override;
		size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;

		[[nodiscard]] const FontTableEntry* GetFontEntry(char32_t c) const;
		[[nodiscard]] int GetKerningDistance(char32_t l, char32_t r) const;
		[[nodiscard]] auto& GetFontTableEntries() const { return m_fontTableEntries; }
		[[nodiscard]] auto& GetKerningEntries() const { return m_kerningEntries; }

		void AddFontEntry(char32_t c, uint16_t textureIndex, uint16_t textureOffsetX, uint16_t textureOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, uint8_t nextOffsetX, uint8_t currentOffsetY);
		void AddKerning(char32_t l, char32_t r, int rightOffset);
	};
}
