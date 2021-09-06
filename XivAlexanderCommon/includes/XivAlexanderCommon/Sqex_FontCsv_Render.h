#pragma once

#include "Sqex.h"
#include "Sqex_FontCsv_ModifiableFontCsvStream.h"
#include "Sqex_Texture.h"
#include "Sqex_Texture_Mipmap.h"

namespace Sqex::FontCsv {
	class SeCompatibleFont {
		mutable RECT m_maxBoundingBox = { LONG_MAX, LONG_MAX, LONG_MAX, LONG_MAX };

	public:
		SeCompatibleFont() = default;
		virtual ~SeCompatibleFont() = default;

		[[nodiscard]] virtual bool HasCharacter(char32_t) const = 0;
		[[nodiscard]] virtual RECT GetBoundingBox(char32_t c, int offsetX = 0, int offsetY = 0) const = 0;
		[[nodiscard]] virtual int GetCharacterWidth(char32_t c) const = 0;
		[[nodiscard]] virtual float Size() const = 0;
		[[nodiscard]] virtual const std::vector<char32_t>& GetAllCharacters() const = 0;
		[[nodiscard]] virtual RECT MaxBoundingBox() const;
		[[nodiscard]] virtual uint32_t Ascent() const = 0;
		[[nodiscard]] virtual uint32_t Descent() const = 0;
		[[nodiscard]] virtual uint32_t Height() const { return Ascent() + Descent();  }
		[[nodiscard]] virtual const std::map<std::pair<char32_t, char32_t>, int>& GetKerningTable() const = 0;
		[[nodiscard]] virtual int GetKerning(char32_t l, char32_t r, int defaultOffset = 0) const;
		
		virtual RECT MeasureAndDraw(Texture::MemoryBackedMipmap* to, int x, int y, const std::u32string& s, Texture::RGBA8888 color = {0xFFFFFFFF}) const = 0;
		virtual RECT MeasureAndDraw(Texture::MemoryBackedMipmap* to, int x, int y, const std::string& s, Texture::RGBA8888 color = { 0xFFFFFFFF }) const {
			return MeasureAndDraw(to, x, y, ToU32(s), color);
		}
	};

	class SeFont final : public SeCompatibleFont {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		SeFont(std::shared_ptr<const ModifiableFontCsvStream> stream, std::vector<std::shared_ptr<const Texture::MipmapStream>> mipmaps);
		~SeFont() override;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] static RECT GetBoundingBox(const FontTableEntry& entry, int offsetX = 0, int offsetY = 0);
		[[nodiscard]] RECT GetBoundingBox(char32_t c, int offsetX = 0, int offsetY = 0) const override;
		[[nodiscard]] int GetCharacterWidth(char32_t c) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t Descent() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& GetKerningTable() const override;

		using SeCompatibleFont::MeasureAndDraw;
		RECT MeasureAndDraw(Texture::MemoryBackedMipmap* to, int x, int y, const std::u32string& s, Texture::RGBA8888 color = { 0xFFFFFFFF }) const override;
	};

	class CascadingFont : public SeCompatibleFont {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		CascadingFont(std::vector<std::shared_ptr<SeCompatibleFont>> fontList);
		CascadingFont(std::vector<std::shared_ptr<SeCompatibleFont>> fontList, float normalizedSize, uint32_t ascent, uint32_t descent);
		~CascadingFont() override;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] RECT GetBoundingBox(char32_t c, int offsetX = 0, int offsetY = 0) const override;
		[[nodiscard]] int GetCharacterWidth(char32_t c) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t Descent() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& GetKerningTable() const override;

		using SeCompatibleFont::MeasureAndDraw;
		RECT MeasureAndDraw(Texture::MemoryBackedMipmap* to, int x, int y, const std::u32string& s, Texture::RGBA8888 color = { 0xFFFFFFFF }) const override;
	};
}
