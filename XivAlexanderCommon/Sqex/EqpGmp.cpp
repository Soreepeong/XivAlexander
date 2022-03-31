#include "XivAlexanderCommon/Sqex/EqpGmp.h"

std::vector<uint64_t> Sqex::EqpGmp::ExpandCollapse(const std::vector<uint64_t>& data, bool expand) {
	std::vector<uint64_t> newData;
	newData.reserve(CountPerBlock * 64);

	uint64_t populatedBits = 0;

	size_t sourceIndex = 0, targetIndex = 0;
	for (size_t i = 0; i < 64; i++) {
		if (data[0] & (1ULL << i)) {
			const auto currentSourceIndex = sourceIndex;
			sourceIndex++;

			if (!expand) {
				bool isAllZeros = true;
				for (size_t j = currentSourceIndex * CountPerBlock, j_ = j + CountPerBlock; isAllZeros && j < j_; ++j) {
					isAllZeros = data[j] == 0;
				}
				if (isAllZeros)
					continue;
			}
			populatedBits |= 1ULL << i;
			newData.resize(newData.size() + CountPerBlock);
			std::copy_n(&data[currentSourceIndex * CountPerBlock], CountPerBlock, &newData[targetIndex * CountPerBlock]);
			targetIndex++;
		} else {
			if (expand) {
				populatedBits |= 1ULL << i;
				newData.resize(newData.size() + CountPerBlock);
				targetIndex++;
			}
		}
	}
	newData[0] = populatedBits;

	return newData;
}
