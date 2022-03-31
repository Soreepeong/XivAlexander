#pragma once
#include "../Sqpack.h"

namespace Sqex::Sqpack {
	class EntryProvider : public RandomAccessStream {
		EntryPathSpec m_pathSpec;

	public:
		EntryProvider(EntryPathSpec pathSpec)
			: m_pathSpec(std::move(pathSpec)) {
		}

		bool UpdatePathSpec(const EntryPathSpec& r) {
			if (m_pathSpec.HasOriginal() || !r.HasOriginal() || m_pathSpec != r)
				return false;

			m_pathSpec = r;
			return true;
		}

		[[nodiscard]] const EntryPathSpec& PathSpec() const {
			return m_pathSpec;
		}

		[[nodiscard]] virtual SqData::FileEntryType EntryType() const = 0;
	};
}
