#pragma once
#include "XivAlexanderCommon/Sqex.h"
#include "XivAlexanderCommon/Sqex/Texture.h"
#include "XivAlexanderCommon/Sqex/Texture/Mipmap.h"

namespace Sqex::Texture {
	class ModifiableTextureStream : public RandomAccessStream {
		Header m_header;
		std::vector<std::vector<std::shared_ptr<MipmapStream>>> m_repeats;
		std::vector<uint32_t> m_mipmapOffsets;
		uint32_t m_repeatedUnitSize;

	public:
		ModifiableTextureStream(const std::shared_ptr<RandomAccessStream>& stream);
		ModifiableTextureStream(Format type, uint16_t width, uint16_t height, uint16_t depth = 1, uint16_t mipmapCount = 1, uint16_t repeatCount = 1);
		~ModifiableTextureStream() override;

		[[nodiscard]] std::shared_ptr<MipmapStream> GetMipmap(size_t mipmapIndex, size_t repeatIndex) const;
		void SetMipmap(size_t mipmapIndex, size_t repeatIndex, std::shared_ptr<MipmapStream> mipmap);
		void Resize(size_t mipmapCount, size_t repeatCount);

		[[nodiscard]] uint64_t StreamSize() const override;
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		[[nodiscard]] Format GetType() const;
		[[nodiscard]] uint16_t GetWidth() const;
		[[nodiscard]] uint16_t GetHeight() const;
		[[nodiscard]] uint16_t GetDepth() const;
		[[nodiscard]] uint16_t GetMipmapCount() const;
		[[nodiscard]] uint16_t GetRepeatCount() const;
		[[nodiscard]] size_t CalculateRepeatUnitSize(size_t mipmapCount) const;
	};
}
