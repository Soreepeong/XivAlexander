#include "pch.h"
#include "Utils_NumericStatisticsTracker.h"

Utils::NumericStatisticsTracker::NumericStatisticsTracker(size_t trackCount, int64_t emptyValue, uint64_t maxAge)
	: m_trackCount(trackCount)
	, m_emptyValue(emptyValue)
	, m_maxAge(maxAge) {
}

Utils::NumericStatisticsTracker::~NumericStatisticsTracker() = default;

void Utils::NumericStatisticsTracker::AddValue(int64_t v) {
	m_values.push_back(v);
	m_expiryTimestamp.push_back(m_maxAge == UINT64_MAX ? UINT64_MAX : GetTickCount64() + m_maxAge);
	while (m_values.size() > m_trackCount) {
		m_values.pop_front();
		m_expiryTimestamp.pop_front();
	}
}

const std::deque<int64_t>& Utils::NumericStatisticsTracker::RemoveExpired() const {
	const auto now = GetTickCount64();
	while (!m_expiryTimestamp.empty() && m_expiryTimestamp.front() < now) {
		m_values.pop_front();
		m_expiryTimestamp.pop_front();
	}
	return m_values;
}

int64_t Utils::NumericStatisticsTracker::InvalidValue() const {
	return m_emptyValue;
}

int64_t Utils::NumericStatisticsTracker::Latest() const {
	const auto& vals = RemoveExpired();
	if (vals.empty())
		return m_emptyValue;
	return vals.back();
}

int64_t Utils::NumericStatisticsTracker::Min() const {
	const auto& vals = RemoveExpired();
	if (vals.empty())
		return m_emptyValue;
	return *std::ranges::min_element(vals);
}

int64_t Utils::NumericStatisticsTracker::Max() const {
	const auto& vals = RemoveExpired();
	if (vals.empty())
		return m_emptyValue;
	return *std::ranges::max_element(vals);
}

int64_t Utils::NumericStatisticsTracker::Mean() const {
	const auto& vals = RemoveExpired();
	if (vals.empty())
		return m_emptyValue;
	return static_cast<int64_t>(std::round(std::accumulate(vals.begin(), vals.end(), 0.0) / vals.size()));
}

int64_t Utils::NumericStatisticsTracker::Median() const {
	const auto& vals = RemoveExpired();
	if (vals.empty())
		return m_emptyValue;

	std::vector<int64_t> sorted(vals.size());
	std::partial_sort_copy(vals.begin(), vals.end(), sorted.begin(), sorted.end());

	if (sorted.size() % 2 == 0) {
		// even
		return (sorted[vals.size() / 2] + sorted[vals.size() / 2 - 1]) / 2;
	} else {
		// odd
		return sorted[vals.size() / 2];
	}
}

int64_t Utils::NumericStatisticsTracker::Deviation() const {
	const auto& vals = RemoveExpired();
	if (vals.size() < 2)
		return 0;

	const auto mean = std::accumulate(vals.begin(), vals.end(), 0.0) / vals.size();

	std::vector<double> diff(vals.size());
	std::ranges::transform(vals, diff.begin(), [mean](int64_t x) { return static_cast<double>(x) - mean; });
	const auto sum2 = std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
	return static_cast<int64_t>(std::round(std::sqrt(sum2 / vals.size())));
}

size_t Utils::NumericStatisticsTracker::Count() const {
	return RemoveExpired().size();
}

uint64_t Utils::NumericStatisticsTracker::NextBlankIn() const {
	if (RemoveExpired().size() < m_trackCount)
		return 0;
	return m_expiryTimestamp.front() - GetTickCount64();
}
