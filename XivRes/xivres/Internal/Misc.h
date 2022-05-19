#ifndef _XIVRES_INTERNAL_MISC_H_
#define _XIVRES_INTERNAL_MISC_H_

#include <algorithm>
#include <type_traits>

namespace XivRes::Internal {
	template<typename T>
	T Clamp(T value, T minValue, T maxValue) {
		return (std::min)(maxValue, (std::max)(minValue, value));
	}

	template<typename T, typename TFrom>
	T RangeCheckedCast(TFrom value) {
		if (value < (std::numeric_limits<T>::min)())
			throw std::range_error("Out of range");
		if (value > (std::numeric_limits<T>::max)())
			throw std::range_error("Out of range");
		return static_cast<T>(value);
	}

	template<typename T, size_t C>
	bool IsAllSameValue(T(&arr)[C], std::remove_cv_t<T> supposedValue = 0) {
		for (size_t i = 0; i < C; ++i) {
			if (arr[i] != supposedValue)
				return false;
		}
		return true;
	}

	template<typename T>
	bool IsAllSameValue(std::span<T> arr, std::remove_cv_t<T> supposedValue = 0) {
		for (const auto& e : arr)
			if (e != supposedValue)
				return false;
		return true;
	}
}

#endif
