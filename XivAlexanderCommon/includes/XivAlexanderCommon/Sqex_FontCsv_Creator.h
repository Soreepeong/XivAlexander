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
		uint16_t GlyphGap = 1;
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
			friend class Creator;
			const size_t m_textureWidth;
			const size_t m_textureHeight;

			std::vector<std::shared_ptr<Texture::MemoryBackedMipmap>> m_mipmaps;
			uint16_t m_currentX;
			uint16_t m_currentY;
			uint16_t m_currentLineHeight;

		public:
			RenderTarget(int textureWidth, int textureHeight);
			~RenderTarget();

			void Finalize();
			[[nodiscard]] std::vector<std::shared_ptr<const Texture::MipmapStream>> AsMipmapStreamVector() const;
		};
		std::shared_ptr<ModifiableFontCsvStream> Compile(RenderTarget& renderTargets) const;
	};
}
