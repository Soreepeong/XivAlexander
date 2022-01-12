#pragma once

#include "XivAlexanderCommon/Sqex/Sqpack/StreamDecoder.h"
#include "XivAlexanderCommon/Sqex/Sqpack/RandomAccessStreamAsEntryProviderView.h"

namespace Sqex::Sqpack {
	class EmptyStreamDecoder : public StreamDecoder {
		std::optional<ZlibReusableInflater> m_inflater;
		std::shared_ptr<Sqex::RandomAccessStreamPartialView> m_partialView;
		std::optional<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView> m_provider;

	public:
		EmptyStreamDecoder(const Sqex::Sqpack::SqData::FileEntryHeader& header, std::shared_ptr<const Sqex::Sqpack::EntryProvider> stream);

		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) override;
	};
}
