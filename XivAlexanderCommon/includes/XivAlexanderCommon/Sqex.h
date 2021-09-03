#pragma once

#include <algorithm>
#include <span>
#include <type_traits>

#include "Utils_Win32_Handle.h"

namespace Sqex {
	enum class SqexLanguage {
		Undefined = 0,
		Japanese = 1,
		English = 2,
		German = 3,
		French = 4,
		ChineseSimplified = 5,
		ChineseTraditional = 6,
		Korean = 7,
	};

	template<typename T, T DefaultValue = static_cast<T>(0)>
	struct LE {
	private:
		union {
			T value;
		};

	public:
		LE(T defaultValue = DefaultValue)
			: value(defaultValue) {
		}

		operator T() const {
			return Value();
		}

		LE<T, DefaultValue>& operator= (T newValue) {
			Value(std::move(newValue));
			return *this;
		}

		LE<T, DefaultValue>& operator+= (T newValue) {
			Value(Value() + std::move(newValue));
			return *this;
		}

		LE<T, DefaultValue>& operator-= (T newValue) {
			Value(Value() - std::move(newValue));
			return *this;
		}

		T Value() const {
			return value;
		}

		void Value(T newValue) {
			value = std::move(newValue);
		}
	};

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

	class RandomAccessStream {
	public:
		RandomAccessStream();
		RandomAccessStream(RandomAccessStream&&) = delete;
		RandomAccessStream(const RandomAccessStream&) = delete;
		RandomAccessStream& operator=(RandomAccessStream&&) = delete;
		RandomAccessStream& operator=(const RandomAccessStream&) = delete;
		virtual ~RandomAccessStream();

		[[nodiscard]] virtual uint32_t StreamSize() const = 0;
		virtual size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const = 0;

		void ReadStream(uint64_t offset, void* buf, size_t length) const;

		template<typename T>
		T ReadStream(uint64_t offset) const {
			T buf;
			ReadStream(offset, &buf, sizeof T);
			return buf;
		}
		template<typename T>
		void ReadStream(uint64_t offset, std::span<T> buf) const {
			ReadStream(offset, buf.data(), buf.size_bytes());
		}
		template<typename T>
		std::vector<T> ReadStreamIntoVector(uint64_t offset, size_t count, size_t maxCount = SIZE_MAX) const {
			if (count > maxCount)
				throw std::runtime_error("trying to read too many");
			std::vector<T> result(count);
			ReadStream(offset, std::span(result));
			return result;
		}
	};

	class FileRandomAccessStream : public RandomAccessStream {
		Utils::Win32::File m_file;
		const uint64_t m_offset;
		const uint32_t m_size;

	public:
		FileRandomAccessStream(Utils::Win32::File file, uint64_t offset, uint32_t length);
		~FileRandomAccessStream() override;

		[[nodiscard]] uint32_t StreamSize() const override;
		size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;
	};
}
