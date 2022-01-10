#pragma once

#include "Sqex_Sqpack.h"

namespace Sqex::Sqpack {
	class EntryProvider : public RandomAccessStream {
		EntryPathSpec m_pathSpec;

	public:
		EntryProvider(EntryPathSpec pathSpec);

		bool UpdatePathSpec(const EntryPathSpec& r);

		[[nodiscard]] const EntryPathSpec& PathSpec() const;
		[[nodiscard]] virtual SqData::FileEntryType EntryType() const = 0;
		[[nodiscard]] std::string DescribeState() const override { return std::format("EntryProvider({})", m_pathSpec); }
	};
}
