#pragma once

#include <algorithm>
#include <mutex>
#include <span>
#include <type_traits>

#include "Utils_Win32_Handle.h"

namespace Sqex {
	// when used as game launch parameter, subtract by one.
	enum class Language {
		Unspecified = 0,
		Japanese = 1,
		English = 2,
		German = 3,
		French = 4,
		ChineseSimplified = 5,
		ChineseTraditional = 6,
		Korean = 7,
	};
	void to_json(nlohmann::json&, const Language&);
	void from_json(const nlohmann::json&, Language&);


	enum class Region {
		Unspecified = 0,
		Japan = 1,
		NorthAmerica = 2,
		Europe = 3,

		// Following 2 are only for sentinel purpoess.
		China = 100001,
		Korea = 100002,
	};
	void to_json(nlohmann::json&, const Region&);
	void from_json(const nlohmann::json&, Region&);

	class CorruptDataException : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

	template<typename T, T DefaultValue = static_cast<T>(0)>
	struct LE {
	private:
		T value;

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

	template<typename T, T DefaultValue = static_cast<T>(0)>
	struct BE {
	private:
		union {
			T value;
			char buf[sizeof T];
		};

	public:
		BE(T defaultValue = DefaultValue)
			: value(defaultValue) {
		}

		operator T() const {
			return Value();
		}

		BE<T, DefaultValue>& operator= (T newValue) {
			Value(std::move(newValue));
			return *this;
		}

		BE<T, DefaultValue>& operator+= (T newValue) {
			Value(Value() + std::move(newValue));
			return *this;
		}

		BE<T, DefaultValue>& operator-= (T newValue) {
			Value(Value() - std::move(newValue));
			return *this;
		}

		T Value() const {
			union {
				char tmp[sizeof T];
				T tval;
			};
			memcpy(tmp, buf, sizeof T);
			std::reverse(tmp, tmp + sizeof T);
			return tval;
		}

		void Value(T newValue) {
			union {
				char tmp[sizeof T];
				T tval;
			};
			tval = newValue;
			std::reverse(tmp, tmp + sizeof T);
			memcpy(buf, tmp, sizeof T);
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

	template<typename T>
	class RandomAccessStreamIterator;

	class RandomAccessStream : public std::enable_shared_from_this<RandomAccessStream> {
	public:
		RandomAccessStream();
		RandomAccessStream(RandomAccessStream&&) = delete;
		RandomAccessStream(const RandomAccessStream&) = delete;
		RandomAccessStream& operator=(RandomAccessStream&&) = delete;
		RandomAccessStream& operator=(const RandomAccessStream&) = delete;
		virtual ~RandomAccessStream();

		[[nodiscard]] virtual uint64_t StreamSize() const = 0;
		virtual uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const = 0;

		void ReadStream(uint64_t offset, void* buf, uint64_t length) const;

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
		std::vector<T> ReadStreamIntoVector(uint64_t offset, size_t count = SIZE_MAX, size_t maxCount = SIZE_MAX) const {
			if (count > maxCount)
				throw std::runtime_error("trying to read too many");
			if (count == SIZE_MAX)
				count = static_cast<size_t>(StreamSize() / sizeof T);
			std::vector<T> result(count);
			ReadStream(offset, std::span(result));
			return result;
		}
		template<typename T>
		RandomAccessStreamIterator<T> Iterator() const {
			return RandomAccessStreamIterator<T>(*this);
		}

		virtual std::string DescribeState() const { return {}; }
	};

	class RandomAccessStreamPartialView : public RandomAccessStream {
		const std::shared_ptr<RandomAccessStream> m_stream;
		const uint64_t m_offset;
		const uint64_t m_size;

	public:
		RandomAccessStreamPartialView(std::shared_ptr<RandomAccessStream> stream, uint64_t offset = 0, uint64_t length = UINT64_MAX)
			: m_stream(std::move(stream))
			, m_offset(offset)
			, m_size(std::min(length, m_stream->StreamSize() - offset)) {
		}

		[[nodiscard]] uint64_t StreamSize() const override { return m_size; }
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override {
			if (offset >= m_size)
				return 0;
			length = std::min(length, m_size - offset);
			return m_stream->ReadStreamPartial(m_offset + offset, buf, length);
		}

		std::string DescribeState() const override {
			return std::format("RandomAccessStreamPartialView({}, {}, {})", m_stream->DescribeState(), m_offset, m_size);
		}
	};

	class FileRandomAccessStream : public RandomAccessStream {
		const std::filesystem::path m_path;
		mutable std::shared_ptr<std::mutex> m_initializationMutex;
		mutable Utils::Win32::File m_file;
		const uint64_t m_offset;
		const uint64_t m_size;

	public:
		FileRandomAccessStream(Utils::Win32::File file, uint64_t offset = 0, uint64_t length = UINT64_MAX);
		FileRandomAccessStream(std::filesystem::path path, uint64_t offset = 0, uint64_t length = UINT64_MAX, bool openImmediately = true);
		~FileRandomAccessStream() override;

		[[nodiscard]] uint64_t StreamSize() const override;
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		std::string DescribeState() const override {
			return std::format("FileRandomAccessStream({}, {}, {})", m_file.ResolveName(), m_offset, m_size);
		}
	};
}
