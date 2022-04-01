#ifndef _XIVRES_COMMON_H_
#define _XIVRES_COMMON_H_

#include <functional>
#include <exception>

namespace XivRes {
	// when used as game launch parameter, subtract by one.
	enum class Language : uint16_t {
		Unspecified = 0,
		Japanese = 1,
		English = 2,
		German = 3,
		French = 4,
		ChineseSimplified = 5,
		ChineseTraditional = 6,
		Korean = 7,
	};

	static constexpr uint32_t EntryAlignment = 128;

	class CorruptDataException : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

	template<typename T, typename CountT = T>
	struct AlignResult {
		CountT Count;
		T Value;
		T By;
		T Alloc;
		T Pad;
		T Last;

		operator T() const {
			return Alloc;
		}

		void IterateChunkedBreakable(std::function<bool(CountT, T, T)> cb, T baseOffset = 0, CountT baseIndex = 0) const {
			if (Pad == 0) {
				for (CountT i = baseIndex; i < Count; ++i)
					if (!cb(i, baseOffset + i * By, By))
						return;
			} else {
				CountT i = baseIndex;
				for (; i < Count - 1; ++i)
					if (!cb(i, baseOffset + i * By, By))
						return;
				if (i == Count - 1)
					cb(i, baseOffset + i * By, Value - i * By);
			}
		}

		void IterateChunked(std::function<void(CountT, T, T)> cb, T baseOffset = 0, CountT baseIndex = 0) const {
			if (Pad == 0) {
				for (CountT i = baseIndex; i < Count; ++i)
					cb(i, baseOffset + i * By, By);
			} else {
				CountT i = baseIndex;
				for (; i < Count - 1; ++i)
					cb(i, baseOffset + i * By, By);
				if (i == Count - 1)
					cb(i, baseOffset + i * By, Value - i * By);
			}
		}
	};

	template<typename T, typename CountT = T>
	AlignResult<T, CountT> Align(T value, T by = static_cast<T>(EntryAlignment)) {
		const auto count = (value + by - 1) / by;
		const auto alloc = count * by;
		const auto pad = alloc - value;
		return {
			.Count = static_cast<CountT>(count),
			.Value = value,
			.By = by,
			.Alloc = static_cast<T>(alloc),
			.Pad = static_cast<T>(pad),
			.Last = value - (count - 1) * by,
		};
	}
}

#endif
