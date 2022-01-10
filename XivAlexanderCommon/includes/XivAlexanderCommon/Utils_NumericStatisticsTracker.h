#pragma once

#include <deque>
#include "XaMisc.h"

namespace Utils {
	class NumericStatisticsTracker {
		const size_t m_trackCount;
		const int64_t m_emptyValue;
		const int64_t m_maxAgeUs;

		struct Entry {
			const int64_t Value;
			const int64_t TimestampUs;
			const int64_t ExpiryUs;

			Entry(int64_t value, int64_t maxAgeUs);
		};

		mutable std::deque<Entry> m_values;

	public:
		NumericStatisticsTracker(size_t trackCount, int64_t emptyValue, int64_t maxAgeUs = INT64_MAX);
		~NumericStatisticsTracker();

		void AddValue(int64_t);
		void Clear() { m_values.clear(); };
		bool Empty() const { return m_values.empty(); }

	private:
		[[nodiscard]] const std::deque<Entry>& RemoveExpired(int64_t nowUs = Utils::QpcUs()) const;

	public:
		[[nodiscard]] int64_t InvalidValue() const;
		[[nodiscard]] int64_t Latest() const;
		[[nodiscard]] int64_t Min(int64_t sinceUs = 0) const;
		[[nodiscard]] int64_t Max(int64_t sinceUs = 0) const;
		[[nodiscard]] int64_t Median(int64_t sinceUs = 0) const;
		[[nodiscard]] std::pair<int64_t, int64_t> MeanAndDeviation(int64_t sinceUs = 0) const;
		[[nodiscard]] int64_t Mean(int64_t sinceUs = 0) const;
		[[nodiscard]] int64_t Deviation(int64_t sinceUs = 0) const;
		[[nodiscard]] size_t Count(int64_t sinceUs = 0) const;
		[[nodiscard]] int64_t NextBlankInUs() const;
		[[nodiscard]] double CountFractional(int64_t sinceUs = 0) const;
	};
}
