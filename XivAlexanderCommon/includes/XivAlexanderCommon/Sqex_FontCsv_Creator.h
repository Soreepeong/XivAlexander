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
		float SizePoints = 0;
		uint32_t AscentPixels = 0;
		uint32_t DescentPixels = 0;
		uint16_t GlobalOffsetYModifier = 0;
		int MaxGlobalOffsetX = 4;
		std::set<char32_t> AlwaysApplyKerningCharacters = { U' ' };
		bool AlignToBaseline = true;

		Creator();
		~Creator();

		void AddCharacter(char32_t codePoint, std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>> font, bool replace = false);
		void AddCharacter(const std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>& font, bool replace = false);
		void AddKerning(const std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>& font, char32_t left, char32_t right, int distance, bool replace = false);
		void AddKerning(const std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>& font, bool replace = false);
		void AddFont(const std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>& font, bool replace = false);

		class RenderTarget {
			const uint16_t m_textureWidth;
			const uint16_t m_textureHeight;
			const uint16_t m_glyphGap;

			std::mutex m_mtx;

		public:
			struct AllocatedSpace {
				uint16_t Index;
				uint16_t X;
				uint16_t Y;
				Texture::MemoryBackedMipmap* Mipmap;
			};
			
		private:
			std::vector<std::shared_ptr<Texture::MemoryBackedMipmap>> m_mipmaps;
			std::map<std::tuple<char32_t, const SeCompatibleDrawableFont<uint8_t>*>, AllocatedSpace> m_drawnGlyphs;
			uint16_t m_currentX;
			uint16_t m_currentY;
			uint16_t m_currentLineHeight;

		public:
			RenderTarget(uint16_t textureWidth, uint16_t textureHeight, uint16_t glyphGap);
			~RenderTarget();

			void Finalize();
			[[nodiscard]] std::vector<std::shared_ptr<const Texture::MipmapStream>> AsMipmapStreamVector() const;

			AllocatedSpace Draw(char32_t c, const SeCompatibleDrawableFont<uint8_t>* font, SSIZE_T drawOffsetX, SSIZE_T drawOffsetY, uint16_t boundingWidth, uint16_t boundingHeight);

			[[nodiscard]] uint16_t TextureWidth() const { return m_textureWidth; }
			[[nodiscard]] uint16_t TextureHeight() const { return m_textureHeight; }
		};
		std::shared_ptr<ModifiableFontCsvStream> Compile(RenderTarget& renderTarget) const;
	};
}
