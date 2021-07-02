#pragma once

#include <deque>

namespace Utils {
	class NumericStatisticsTracker {
		const size_t m_trackCount;
		const int64_t m_emptyValue;
		const uint64_t m_maxAge;
		mutable std::deque<int64_t> m_values;
		mutable std::deque<uint64_t> m_expiryTimestamp;

	public:
		NumericStatisticsTracker(size_t trackCount, int64_t emptyValue, uint64_t maxAge = UINT64_MAX);
		~NumericStatisticsTracker();

		void AddValue(int64_t);

	private:
		[[nodiscard]] const std::deque<int64_t>& RemoveExpired() const;

	public:
		[[nodiscard]] int64_t InvalidValue() const;
		[[nodiscard]] int64_t Latest() const;
		[[nodiscard]] int64_t Min() const;
		[[nodiscard]] int64_t Max() const;
		[[nodiscard]] int64_t Mean() const;
		[[nodiscard]] int64_t Median() const;
		[[nodiscard]] int64_t Deviation() const;
		[[nodiscard]] size_t Count() const;
		[[nodiscard]] uint64_t NextBlankIn() const;
	};
}

