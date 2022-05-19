#ifndef _XIVRES_INTERNAL_SPANCAST_H_
#define _XIVRES_INTERNAL_SPANCAST_H_

#include <climits>
#include <span>
#include <string>
#include <vector>

namespace XivRes::Internal {
	template<typename TTarget, typename TSource>
	std::span<TTarget> span_cast(size_t sourceCount, TSource* source, size_t sourceIndex = 0, size_t targetCount = SIZE_MAX, size_t targetCountUnitSize = sizeof TTarget) {
		if (targetCount == SIZE_MAX) {
			targetCountUnitSize = 1;
			targetCount = (sourceCount - sourceIndex) * sizeof TSource;
		}

		if (targetCount == 0)
			return {};

		if (targetCount * targetCountUnitSize + sourceIndex * sizeof TSource > sourceCount * sizeof TSource)
			throw std::out_of_range("target range exceeds source range");

		if (targetCount * targetCountUnitSize % sizeof TTarget)
			throw std::out_of_range("target size does not align to target type size");

		return { reinterpret_cast<TTarget*>(&source[sourceIndex]), targetCount * targetCountUnitSize / sizeof TTarget };
	}

	template<typename TTarget, typename TSource, typename = std::enable_if_t<!std::is_const_v<TSource>>>
	std::span<const TTarget> span_cast(size_t sourceCount, const TSource* source, size_t sourceIndex = 0, size_t targetCount = SIZE_MAX, size_t targetCountUnitSize = sizeof TTarget) {
		return span_cast<const TTarget, const TSource>(sourceCount, source, sourceIndex, targetCount, targetCountUnitSize);
	}

	template<typename TTarget, typename TSource, size_t TSourceCount>
	std::span<TTarget> span_cast(TSource(&source)[TSourceCount], size_t sourceIndex = 0, size_t targetCount = SIZE_MAX, size_t targetCountUnitSize = sizeof TTarget) {
		return span_cast<TTarget, TSource>(TSourceCount, source, sourceIndex, targetCount, targetCountUnitSize);
	}

	template<typename TTarget, typename TSource, size_t TSourceCount>
	std::span<const TTarget> span_cast(const TSource(&source)[TSourceCount], size_t sourceIndex = 0, size_t targetCount = SIZE_MAX, size_t targetCountUnitSize = sizeof TTarget) {
		return span_cast<const TTarget, const TSource>(TSourceCount, source, sourceIndex, targetCount, targetCountUnitSize);
	}

	template<typename TTarget, typename TSource>
	std::span<const TTarget> span_cast(const std::span<const TSource>& source, size_t sourceIndex = 0, size_t targetCount = SIZE_MAX, size_t targetCountUnitSize = sizeof TTarget) {
		return span_cast<const TTarget, const TSource>(source.size(), &source[0], sourceIndex, targetCount, targetCountUnitSize);
	}

	template<typename TTarget, typename TSource>
	std::span<TTarget> span_cast(const std::span<TSource>& source, size_t sourceIndex = 0, size_t targetCount = SIZE_MAX, size_t targetCountUnitSize = sizeof TTarget) {
		return span_cast<TTarget, TSource>(source.size(), &source[0], sourceIndex, targetCount, targetCountUnitSize);
	}

	template<typename TTarget, typename TSource>
	std::span<const TTarget> span_cast(const std::vector<TSource>& source, size_t sourceIndex = 0, size_t targetCount = SIZE_MAX, size_t targetCountUnitSize = sizeof TTarget) {
		return span_cast<const TTarget, const TSource>(source.size(), &source[0], sourceIndex, targetCount, targetCountUnitSize);
	}

	template<typename TTarget, typename TSource>
	std::span<TTarget> span_cast(std::vector<TSource>& source, size_t sourceIndex = 0, size_t targetCount = SIZE_MAX, size_t targetCountUnitSize = sizeof TTarget) {
		return span_cast<TTarget, TSource>(source.size(), &source[0], sourceIndex, targetCount, targetCountUnitSize);
	}

	template<typename TTarget, typename TSource, class TSourceTraits = std::char_traits<TSource>, class TSourceAlloc = std::allocator<TSource>>
	std::span<TTarget> span_cast(std::basic_string<TSource, TSourceTraits, TSourceAlloc>& source, size_t sourceIndex = 0, size_t targetCount = SIZE_MAX, size_t targetCountUnitSize = sizeof TTarget) {
		return span_cast<TTarget, TSource>(source.size(), &source[0], sourceIndex, targetCount, targetCountUnitSize);
	}

	template<typename TTarget, typename TSource, class TSourceTraits = std::char_traits<TSource>, class TSourceAlloc = std::allocator<TSource>>
	std::span<const TTarget> span_cast(const std::basic_string<TSource, TSourceTraits, TSourceAlloc>& source, size_t sourceIndex = 0, size_t targetCount = SIZE_MAX, size_t targetCountUnitSize = sizeof TTarget) {
		return span_cast<const TTarget, const TSource>(source.size(), &source[0], sourceIndex, targetCount, targetCountUnitSize);
	}

	template<typename TTarget, typename TSource>
	std::span<const TTarget> span_cast(const std::basic_string_view<TSource>& source, size_t sourceIndex = 0, size_t targetCount = SIZE_MAX, size_t targetCountUnitSize = sizeof TTarget) {
		return span_cast<const TTarget, const TSource>(source.size(), &source[0], sourceIndex, targetCount, targetCountUnitSize);
	}
}

#endif
