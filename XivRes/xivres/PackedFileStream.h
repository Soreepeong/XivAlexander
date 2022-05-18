#ifndef _XIVRES_PACKEDFILESTREAM_H_
#define _XIVRES_PACKEDFILESTREAM_H_

#include "Sqpack.h"
#include "IStream.h"

namespace XivRes {
	class PackedFileUnpackingStream;

	class PackedFileStream : public DefaultAbstractStream {
		SqpackPathSpec m_pathSpec;

	public:
		PackedFileStream(SqpackPathSpec pathSpec)
			: m_pathSpec(std::move(pathSpec)) {
		}

		bool UpdatePathSpec(const SqpackPathSpec& r) {
			if (m_pathSpec.HasOriginal() || !r.HasOriginal() || m_pathSpec != r)
				return false;

			m_pathSpec = r;
			return true;
		}

		[[nodiscard]] const SqpackPathSpec& PathSpec() const {
			return m_pathSpec;
		}

		[[nodiscard]] virtual PackedFileType GetPackedFileType() const = 0;

		PackedFileUnpackingStream GetUnpackedStream(std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;

		std::unique_ptr<PackedFileUnpackingStream> GetUnpackedStreamPtr(std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;
	};
}

#endif
