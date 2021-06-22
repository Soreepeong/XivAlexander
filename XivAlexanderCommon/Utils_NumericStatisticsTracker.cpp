#include "pch.h"
#include "Utils_NumericStatisticsTracker.h"

Utils::NumericStatisticsTracker::NumericStatisticsTracker(size_t trackCount, int64_t emptyValue)
	: m_trackCount(trackCount)
	, m_emptyValue(emptyValue) {
}

Utils::NumericStatisticsTracker::~NumericStatisticsTracker() = default;

void Utils::NumericStatisticsTracker::AddValue(int64_t v) {
	m_values.push_back(v);
	while (m_values.size() > m_trackCount)
		m_values.pop_front();
}

int64_t Utils::NumericStatisticsTracker::InvalidValue() const {
	return m_emptyValue;
}

int64_t Utils::NumericStatisticsTracker::Min() const {
	if (m_values.empty())
		return m_emptyValue;
	return *std::min_element(m_values.begin(), m_values.end());
}

int64_t Utils::NumericStatisticsTracker::Max() const {
	if (m_values.empty())
		return m_emptyValue;
	return *std::max_element(m_values.begin(), m_values.end());
}

int64_t Utils::NumericStatisticsTracker::Mean() const {
	if (m_values.empty())
		return m_emptyValue;
	return static_cast<int64_t>(std::round(std::accumulate(m_values.begin(), m_values.end(), 0.0) / m_values.size()));
}

int64_t Utils::NumericStatisticsTracker::Median() const {
	if (m_values.empty())
		return m_emptyValue;

	std::vector<int64_t> sorted(m_values.size());
	std::partial_sort_copy(m_values.begin(), m_values.end(), sorted.begin(), sorted.end());

	if (sorted.size() % 2 == 0) {
		// even
		return (sorted[m_values.size() / 2] + sorted[m_values.size() / 2 - 1]) / 2;
	} else {
		// odd
		return sorted[m_values.size() / 2];
	}
}

int64_t Utils::NumericStatisticsTracker::Deviation() const {
	if (m_values.size() < 2)
		return m_emptyValue;

	const auto mean = std::accumulate(m_values.begin(), m_values.end(), 0.0) / m_values.size();

	std::vector<double> diff(m_values.size());
	std::transform(m_values.begin(), m_values.end(), diff.begin(), [mean](int64_t x) { return static_cast<double>(x) - mean; });
	const auto sum2 = std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
	return static_cast<int64_t>(std::round(std::sqrt(sum2 / m_values.size())));
}
