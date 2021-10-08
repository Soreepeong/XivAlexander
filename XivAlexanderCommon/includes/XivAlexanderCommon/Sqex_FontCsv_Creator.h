#pragma once

#include <memory>
#include <set>

#include "Sqex_FontCsv_CreateConfig.h"
#include "Sqex_FontCsv_SeCompatibleDrawableFont.h"
#include "Sqex_Texture_Mipmap.h"
#include "Sqex_Texture_ModifiableTextureStream.h"
#include "Utils_ListenerManager.h"

namespace Sqex::FontCsv {
	struct FontGenerateProcess {
		size_t Progress = 0;
		size_t Max = 0;
		bool Finished = false;
		int Indeterminate = 1;

		FontGenerateProcess& operator+=(const FontGenerateProcess& p) {
			Finished &= p.Finished;
			Indeterminate += p.Indeterminate;
			Max += p.Max;
			Progress += p.Progress;
			return *this;
		}

		template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
		[[nodiscard]] T Scale(T max) const {
			if (Max == 0)
				return 0;
			return static_cast<T>(static_cast<double>(Progress) / static_cast<double>(Max) * static_cast<double>(max));
		}
	};

	class FontCsvCreator {
		struct CharacterPlan;
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		static constexpr uint32_t AutoVerticalValues = UINT32_MAX;

		float SizePoints = 0;
		uint32_t AscentPixels = 0;
		uint32_t LineHeightPixels = 0;
		uint16_t GlobalOffsetYModifier = 0;
		int MinGlobalOffsetX = 0;
		int MaxGlobalOffsetX = 255;
		std::set<char32_t> AlwaysApplyKerningCharacters = {U' '};
		bool AlignToBaseline = true;
		uint8_t BorderThickness = 0;
		uint8_t BorderOpacity = 0;
		bool CompactLayout = false;

		FontCsvCreator(const Win32::Semaphore& semaphore = nullptr);
		~FontCsvCreator();

		void AddCharacter(char32_t codePoint, const SeCompatibleDrawableFont<uint8_t>* font, bool replace = false, bool extendRange = true, int offsetXModifier = 0, int offsetYModifier = 0);
		void AddCharacter(const SeCompatibleDrawableFont<uint8_t>* font, bool replace = false, bool extendRange = true, int offsetXModifier = 0, int offsetYModifier = 0);
		void AddKerning(const SeCompatibleDrawableFont<uint8_t>* font, char32_t left, char32_t right, int distance, bool replace = false);
		void AddKerning(const SeCompatibleDrawableFont<uint8_t>* font, bool replace = false);
		void AddFont(const SeCompatibleDrawableFont<uint8_t>* font, bool replace = false, bool extendRange = true, int offsetXModifier = 0, int offsetYModifier = 0);

		ListenerManager<FontCsvCreator, void, const std::exception&> OnError;

		class RenderTarget {
			friend class FontCsvCreator;

			struct Implementation;
			const std::unique_ptr<Implementation> m_pImpl;

		public:
			RenderTarget(uint16_t textureWidth, uint16_t textureHeight, uint16_t glyphGap);
			~RenderTarget();

			void Finalize(Texture::Format textureFormat = Texture::Format::RGBA4444);

			[[nodiscard]] std::vector<std::shared_ptr<const Texture::MipmapStream>> AsMipmapStreamVector() const;
			[[nodiscard]] std::vector<std::shared_ptr<Texture::ModifiableTextureStream>> AsTextureStreamVector() const;

			[[nodiscard]] uint16_t TextureWidth() const;
			[[nodiscard]] uint16_t TextureHeight() const;

		protected:
			struct AllocatedSpace {
				uint16_t Index;
				uint16_t X;
				uint16_t Y;
				uint8_t BoundingHeight;
			};

			AllocatedSpace QueueDraw(char32_t c, const SeCompatibleDrawableFont<uint8_t>* font, SSIZE_T drawOffsetX, SSIZE_T drawOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, uint8_t borderThickness, uint8_t borderOpacity);
			bool WorkOnNextItem();
		};

		void Step0_CalcMax();
		void Step1_CalcBbox();
		void Step2_Layout(RenderTarget& renderTarget);
		void Step3_Draw(RenderTarget& target);

		[[nodiscard]] FontGenerateProcess GetProgress() const;
		[[nodiscard]] std::shared_ptr<ModifiableFontCsvStream> GetResult() const;
		void Cancel();
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
		
		void ProvideGameDirectory(Sqex::GameReleaseRegion, std::filesystem::path);
		void VerifyRequirements(
			const std::function<std::filesystem::path(const CreateConfig::GameIndexFile&)>& promptGameIndexFile,
			const std::function<bool(const CreateConfig::FontRequirement&)>& promptFontRequirement
		);
		void Start();
		[[nodiscard]] bool Wait(DWORD timeout = INFINITE) const;
		[[nodiscard]] HANDLE GetWaitableObject() const;
		[[nodiscard]] FontGenerateProcess GetProgress() const;
		[[nodiscard]] const ResultFontSets& GetResult() const;
		[[nodiscard]] const std::string& GetError() const;

	};
}
