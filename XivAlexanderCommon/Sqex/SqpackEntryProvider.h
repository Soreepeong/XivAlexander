#pragma once

#include "Sqpack.h"

#include "RandomAccessStream.h"

namespace XivRes {
	class PackedFileUnpackingStream;

	class PackedFileStream : public RandomAccessStream {
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

		[[nodiscard]] virtual SqData::PackedFileType PackedFileType() const = 0;

		PackedFileUnpackingStream GetUnpackedStream(std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;

		std::unique_ptr<PackedFileUnpackingStream> GetUnpackedStreamPtr(std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;
	};
}

#include "SqpackEntryRawStream.h"

inline XivRes::PackedFileUnpackingStream XivRes::PackedFileStream::GetUnpackedStream(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return XivRes::PackedFileUnpackingStream(std::static_pointer_cast<const PackedFileStream>(shared_from_this()), obfuscatedHeaderRewrite);
}

inline std::unique_ptr<XivRes::PackedFileUnpackingStream> XivRes::PackedFileStream::GetUnpackedStreamPtr(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return std::make_unique<XivRes::PackedFileUnpackingStream>(std::static_pointer_cast<const PackedFileStream>(shared_from_this()), obfuscatedHeaderRewrite);
}
