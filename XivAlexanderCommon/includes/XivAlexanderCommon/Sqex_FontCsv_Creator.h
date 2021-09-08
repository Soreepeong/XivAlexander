#pragma once

#include <memory>
#include <set>
#include "Sqex_FontCsv_SeCompatibleDrawableFont.h"
#include "Sqex_Texture_Mipmap.h"

namespace Sqex {
	namespace Texture {
		struct RGBA4444;
	}
}

namespace Sqex::FontCsv {
	class Creator {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		float Points = 0;
		uint32_t Ascent = 0;
		uint32_t Descent = 0;
		uint16_t GlobalOffsetYModifier = -1;
		int MaxLeftOffset = 4;
		std::set<char32_t> AlwaysApplyKerningCharacters = { U' ' };

		Creator();
		~Creator();

		void AddCharacter(char32_t codePoint, std::shared_ptr<SeCompatibleDrawableFont<uint8_t>> font, bool replace = false);
		void AddCharacter(const std::shared_ptr<SeCompatibleDrawableFont<uint8_t>>& font, bool replace = false);
		void AddKerning(char32_t left, char32_t right, int distance, bool replace = false);
		void AddKerning(const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& table, bool replace = false);

		class RenderTarget {
			const uint16_t m_textureWidth;
			const uint16_t m_textureHeight;
			const uint16_t m_glyphGap;

			std::vector<std::shared_ptr<Texture::MemoryBackedMipmap>> m_mipmaps;
			uint16_t m_currentX;
			uint16_t m_currentY;
			uint16_t m_currentLineHeight;

		public:
			RenderTarget(uint16_t textureWidth, uint16_t textureHeight, uint16_t glyphGap);
			~RenderTarget();

			void Finalize();
			[[nodiscard]] std::vector<std::shared_ptr<const Texture::MipmapStream>> AsMipmapStreamVector() const;

			struct AllocatedSpace {
				uint16_t Index;
				uint16_t X;
				uint16_t Y;
				Texture::MemoryBackedMipmap* Mipmap;
			};
			AllocatedSpace AllocateSpace(uint16_t boundingWidth, uint16_t boundingHeight);

			uint16_t TextureWidth() const { return m_textureWidth; }
			uint16_t TextureHeight() const { return m_textureHeight; }
		};
		std::shared_ptr<ModifiableFontCsvStream> Compile(RenderTarget& renderTarget) const;
	};
}
