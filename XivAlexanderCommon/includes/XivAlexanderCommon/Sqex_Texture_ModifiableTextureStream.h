#pragma once
#include "Sqex.h"
#include "Sqex_Texture.h"
#include "Sqex_Texture_Mipmap.h"

namespace Sqex::Texture {
	class ModifiableTextureStream : public RandomAccessStream {
		Header m_header;
		std::vector<std::shared_ptr<MipmapStream>> m_mipmaps;
		std::vector<uint32_t> m_mipmapOffsets;

	public:
		ModifiableTextureStream(const std::shared_ptr<RandomAccessStream>& stream);
		ModifiableTextureStream(Format type, uint16_t width, uint16_t height, uint16_t depth = 1);
		~ModifiableTextureStream() override;

		void AppendMipmap(std::shared_ptr<MipmapStream> mipmap);
		void TruncateMipmap(size_t count);

		[[nodiscard]] uint64_t StreamSize() const override;
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;
	};
}
