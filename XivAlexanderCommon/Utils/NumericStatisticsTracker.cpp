#include "pch.h"
#include "XivAlexanderCommon/Utils/NumericStatisticsTracker.h"
#include "XivAlexanderCommon/Utils/Utils.h"

Utils::NumericStatisticsTracker::Entry::Entry(int64_t value, int64_t maxAgeUs)
	: Value(value)
	, TimestampUs(Utils::QpcUs())
	, ExpiryUs(maxAgeUs == INT64_MAX ? INT64_MAX : TimestampUs + maxAgeUs) {
}

Utils::NumericStatisticsTracker::NumericStatisticsTracker(size_t trackCount, int64_t emptyValue, int64_t maxAgeUs)
	: m_trackCount(trackCount)
	, m_emptyValue(emptyValue)
	, m_maxAgeUs(maxAgeUs) {
}

Utils::NumericStatisticsTracker::~NumericStatisticsTracker() = default;

void Utils::NumericStatisticsTracker::AddValue(int64_t v) {
	m_values.emplace_back(v, m_maxAgeUs);
	void(RemoveExpired(m_values.back().TimestampUs));
}

const std::deque<Utils::NumericStatisticsTracker::Entry>& Utils::NumericStatisticsTracker::RemoveExpired(int64_t nowUs) const {
	while (!m_values.empty() && (
		m_values.size() > m_trackCount || 
		m_values.front().ExpiryUs < nowUs
	))
		m_values.pop_front();
	return m_values;
}

int64_t Utils::NumericStatisticsTracker::InvalidValue() const {
	return m_emptyValue;
}

int64_t Utils::NumericStatisticsTracker::Latest() const {
	const auto& vals = RemoveExpired();
	if (vals.empty())
		return m_emptyValue;
	return vals.back().Value;
}

int64_t Utils::NumericStatisticsTracker::Min(int64_t sinceUs) const {
	const auto& vals = RemoveExpired();
	auto found = false;
	int64_t minValue{};
	for (const auto& v : std::ranges::reverse_view(vals)) {
		if (v.TimestampUs < sinceUs)
			break;
		if (!found || minValue > v.Value)
			minValue = v.Value;
		found = true;
	}
	return found ? minValue : m_emptyValue;
}

int64_t Utils::NumericStatisticsTracker::Max(int64_t sinceUs) const {
	const auto& vals = RemoveExpired();
	auto found = false;
	int64_t maxValue{};
	for (const auto& v : std::ranges::reverse_view(vals)) {
		if (v.TimestampUs < sinceUs)
			break;
		if (!found || maxValue < v.Value)
			maxValue = v.Value;
		found = true;
	}
	return found ? maxValue : m_emptyValue;
}

int64_t Utils::NumericStatisticsTracker::Median(int64_t sinceUs) const {
	const auto& vals = RemoveExpired();

	std::vector<int64_t> sorted;
	sorted.reserve(vals.size());
	for (const auto& v : std::ranges::reverse_view(vals)) {
		if (v.TimestampUs < sinceUs)
			break;
		sorted.emplace_back(v.Value);
	}
	if (sorted.empty())
		return m_emptyValue;
	std::ranges::sort(sorted);

	if (sorted.size() % 2 == 0) {
		// even
		return (sorted[sorted.size() / 2] + sorted[sorted.size() / 2 - 1]) / 2;
	} else {
		// odd
		return sorted[sorted.size() / 2];
	}
}

int64_t Utils::NumericStatisticsTracker::Mean(int64_t sinceUs) const {
	const auto& vals = RemoveExpired();
	int64_t count = 0;
	int64_t acc{};
	for (const auto& v : std::ranges::reverse_view(vals)) {
		if (v.TimestampUs < sinceUs)
			break;
		acc += v.Value;
		++count;
	}
	return count ? acc / count : m_emptyValue;
}

std::pair<int64_t, int64_t> Utils::NumericStatisticsTracker::MeanAndDeviation(int64_t sinceUs) const {
	const auto& vals = RemoveExpired();

	int64_t count = 0;
	int64_t acc{};
	for (const auto& v : std::ranges::reverse_view(vals)) {
		if (v.TimestampUs < sinceUs)
			break;
		acc += v.Value;
		++count;
	}

	if (count == 0)
		return {m_emptyValue, 0};
	if (count == 1)
		return {acc, 0};
	const auto mean = acc / count;

	int64_t diffSquaredSum = 0;
	for (const auto& v : std::ranges::reverse_view(vals)) {
		if (v.TimestampUs < sinceUs)
			break;
		diffSquaredSum += (v.Value - mean) * (v.Value - mean);
	}

	return {mean, static_cast<int64_t>(std::sqrt(diffSquaredSum / count))};
}

int64_t Utils::NumericStatisticsTracker::Deviation(int64_t sinceUs) const {
	return MeanAndDeviation(sinceUs).second;
}

size_t Utils::NumericStatisticsTracker::Count(int64_t sinceUs) const {
	const auto& vals = RemoveExpired();
	if (!sinceUs)
		return vals.size();

	size_t count = 0;
	for (const auto& v : std::ranges::reverse_view(vals)) {
		if (v.TimestampUs < sinceUs)
			break;
		++count;
	}
	return count;
}

int64_t Utils::NumericStatisticsTracker::NextBlankInUs() const {
	if (RemoveExpired().size() < m_trackCount)
		return 0;
	return m_values.front().Value - Entry(0, 0).Value;
}

double Utils::NumericStatisticsTracker::CountFractional(int64_t sinceUs) const {
	const auto& vals = RemoveExpired();
	if (!sinceUs)
		return static_cast<double>(vals.size());

	size_t count = 0;
	int64_t lastTimestamp = INT64_MIN;
	for (const auto& v : std::ranges::reverse_view(vals)) {
		if (v.TimestampUs < sinceUs) {
			if (lastTimestamp != INT64_MIN) {
				const auto window = lastTimestamp - v.TimestampUs;
				const auto elapsed = sinceUs - v.TimestampUs;
				if (window > elapsed)
					return static_cast<double>(count) + static_cast<double>(elapsed) / static_cast<double>(window);
			}
			break;
		}
		++count;
		lastTimestamp = v.TimestampUs;
	}
	return static_cast<double>(count);
}
