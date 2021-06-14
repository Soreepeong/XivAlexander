#pragma once

#include <deque>

namespace Utils {
	class NumericStatisticsTracker {
		const size_t m_trackCount;
		const size_t m_emptyValue;
		std::deque<int64_t> m_values;

	public:
		NumericStatisticsTracker(size_t trackCount, int64_t emptyValue);
		~NumericStatisticsTracker();

		void AddValue(int64_t);

		[[nodiscard]] int64_t InvalidValue() const;
		[[nodiscard]] int64_t Min() const;
		[[nodiscard]] int64_t Max() const;
		[[nodiscard]] int64_t Mean() const;
		[[nodiscard]] int64_t Median() const;
		[[nodiscard]] int64_t Deviation() const;
	};
}

