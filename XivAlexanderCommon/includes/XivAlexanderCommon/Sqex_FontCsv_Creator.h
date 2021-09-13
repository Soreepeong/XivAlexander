#pragma once

#include <memory>
#include <set>

#include "Sqex_FontCsv_CreateConfig.h"
#include "Sqex_FontCsv_SeCompatibleDrawableFont.h"
#include "Sqex_Texture_Mipmap.h"
#include "Sqex_Texture_ModifiableTextureStream.h"
#include "Utils_ListenerManager.h"

namespace Sqex::FontCsv {
	struct FontCreationProgress {
		size_t Progress;
		size_t Max;
		bool Finished;
		int Indeterminate;

		FontCreationProgress& operator+=(const FontCreationProgress& p) {
			Finished &= p.Finished;
			Indeterminate += p.Indeterminate;
			Max += p.Max;
			Progress += p.Progress;
			return *this;
		}

		template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
		[[nodiscard]] T Scale(T max) const {
			return static_cast<T>(static_cast<double>(Progress) / static_cast<double>(Max) * static_cast<double>(max));
		}
	};

	class FontCsvCreator {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		static constexpr uint32_t AutoAscentDescent = UINT32_MAX;

		float SizePoints = 0;
		uint32_t AscentPixels = 0;
		uint32_t DescentPixels = 0;
		uint16_t GlobalOffsetYModifier = 0;
		int MinGlobalOffsetX = 0;
		int MaxGlobalOffsetX = 255;
		std::set<char32_t> AlwaysApplyKerningCharacters = { U' ' };
		bool AlignToBaseline = true;
		uint8_t BorderThickness = 0;
		uint8_t BorderOpacity = 0;

		FontCsvCreator(const Utils::Win32::Semaphore& semaphore = nullptr);
		~FontCsvCreator();

		void AddCharacter(char32_t codePoint, const SeCompatibleDrawableFont<uint8_t>* font, bool replace = false, bool extendRange = true);
		void AddCharacter(const SeCompatibleDrawableFont<uint8_t>* font, bool replace = false, bool extendRange = true);
		void AddKerning(const SeCompatibleDrawableFont<uint8_t>* font, char32_t left, char32_t right, int distance, bool replace = false);
		void AddKerning(const SeCompatibleDrawableFont<uint8_t>* font, bool replace = false);
		void AddFont(const SeCompatibleDrawableFont<uint8_t>* font, bool replace = false, bool extendRange = true);

		[[nodiscard]] const FontCreationProgress& GetProgress() const;

		ListenerManager<FontCsvCreator, void, const std::exception&> OnError;

		void Cancel();

		class RenderTarget {
			const uint16_t m_textureWidth;
			const uint16_t m_textureHeight;
			const uint16_t m_glyphGap;

			std::mutex m_mtx;

		public:
			struct AllocatedSpace {
				SSIZE_T drawOffsetX;
				SSIZE_T drawOffsetY;
				uint16_t Index;
				uint16_t X;
				uint16_t Y;
				uint8_t BoundingHeight;
			};

		private:
			std::vector<std::shared_ptr<Texture::MemoryBackedMipmap>> m_mipmaps;
			std::map<std::tuple<char32_t, const SeCompatibleDrawableFont<uint8_t>*, uint8_t, uint8_t>, AllocatedSpace> m_drawnGlyphs;
			uint16_t m_currentX;
			uint16_t m_currentY;
			uint16_t m_currentLineHeight;

		public:
			RenderTarget(uint16_t textureWidth, uint16_t textureHeight, uint16_t glyphGap);
			~RenderTarget();

			template<typename TextureTypeSupportingRGBA = Texture::RGBA4444, Texture::CompressionType CompressionType = Texture::CompressionType::RGBA4444>
			void Finalize() {
				auto mipmaps = std::move(m_mipmaps);
				while (mipmaps.size() % 4)
					mipmaps.push_back(std::make_shared<Texture::MemoryBackedMipmap>(
						mipmaps[0]->Width(), mipmaps[0]->Height(), Texture::CompressionType::L8_1,
						std::vector<uint8_t>(static_cast<size_t>(mipmaps[0]->Width()) * mipmaps[0]->Height())));

				for (size_t i = 0; i < mipmaps.size() / 4; ++i) {
					m_mipmaps.push_back(std::make_shared<Texture::MemoryBackedMipmap>(
						mipmaps[0]->Width(), mipmaps[0]->Height(), CompressionType,
						std::vector<uint8_t>(sizeof TextureTypeSupportingRGBA * mipmaps[0]->Width() * mipmaps[0]->Height())));

					const auto target = m_mipmaps.back()->View<TextureTypeSupportingRGBA>();
					const auto b = mipmaps[i * 4 + 0]->View<uint8_t>();
					const auto g = mipmaps[i * 4 + 1]->View<uint8_t>();
					const auto r = mipmaps[i * 4 + 2]->View<uint8_t>();
					const auto a = mipmaps[i * 4 + 3]->View<uint8_t>();
					for (size_t j = 0; j < target.size(); ++j)
						target[j].SetFrom(
							r[j] * TextureTypeSupportingRGBA::MaxR / 255, 
							g[j] * TextureTypeSupportingRGBA::MaxG / 255,
							b[j] * TextureTypeSupportingRGBA::MaxB / 255,
							a[j] * TextureTypeSupportingRGBA::MaxA / 255
						);
				}
			}
			[[nodiscard]] std::vector<std::shared_ptr<const Texture::MipmapStream>> AsMipmapStreamVector() const;
			[[nodiscard]] std::vector<std::shared_ptr<Texture::ModifiableTextureStream>> AsTextureStreamVector() const;

			AllocatedSpace Draw(char32_t c, const SeCompatibleDrawableFont<uint8_t>* font, SSIZE_T drawOffsetX, SSIZE_T drawOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, uint8_t borderThickness, uint8_t borderOpacity);

			[[nodiscard]] uint16_t TextureWidth() const { return m_textureWidth; }
			[[nodiscard]] uint16_t TextureHeight() const { return m_textureHeight; }
		};
		std::shared_ptr<ModifiableFontCsvStream> Compile(RenderTarget& renderTarget);
	};

	class FontSetsCreator {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		FontSetsCreator(CreateConfig::FontCreateConfig config, std::filesystem::path gamePath, LONG maxCoreCount = 0);
		~FontSetsCreator();

		struct ResultFontSet {
			std::map<std::string, std::shared_ptr<ModifiableFontCsvStream>> Fonts;
			std::vector<std::shared_ptr<Texture::ModifiableTextureStream>> Textures;
		};

		struct ResultFontSets {
			std::map<std::string, ResultFontSet> Result;

			[[nodiscard]] std::map<Sqpack::EntryPathSpec, std::shared_ptr<const RandomAccessStream>> GetAllStreams() const;
		};

		[[nodiscard]] const ResultFontSets& GetResult() const;

		bool Wait(DWORD timeout = INFINITE) const;

		const std::string& GetError() const;

		[[nodiscard]] FontCreationProgress GetProgress() const;

	};
}
