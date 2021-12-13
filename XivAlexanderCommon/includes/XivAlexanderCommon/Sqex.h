#pragma once

#include <algorithm>
#include <mutex>
#include <span>
#include <type_traits>

#include "Utils_Win32_Handle.h"
#include "XaMisc.h"

namespace Sqex {
	using namespace Utils;

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

	void to_json(nlohmann::json&, const Language&);
	void from_json(const nlohmann::json&, Language&);

	enum class Region {
		Unspecified = 0,
		Japan = 1,
		NorthAmerica = 2,
		Europe = 3,
		China = 4,
		Korea = 5,
	};

	void to_json(nlohmann::json&, const Region&);
	void from_json(const nlohmann::json&, Region&);

	enum class GameReleaseRegion {
		Unspecified,
		International,
		Korean,
		Chinese,
	};

	void to_json(nlohmann::json&, const GameReleaseRegion&);
	void from_json(const nlohmann::json&, GameReleaseRegion&);

	class CorruptDataException : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

	static constexpr uint32_t EntryAlignment = 128;

	template<typename T, typename CountT = T>
	struct AlignResult {
		CountT Count;
		T Alloc;
		T Pad;

		operator T() const {
			return Alloc;
		}
	};

	template<typename T, typename CountT = T>
	AlignResult<T, CountT> Align(T value, T by = static_cast<T>(EntryAlignment)) {
		const auto count = (value + by - 1) / by;
		const auto alloc = count * by;
		const auto pad = alloc - value;
		return {
			.Count = static_cast<CountT>(count),
			.Alloc = static_cast<T>(alloc),
			.Pad = static_cast<T>(pad),
		};
	}


	template<typename T, size_t C>
	bool IsAllSameValue(T (&arr)[C], std::remove_cv_t<T> supposedValue = 0) {
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
		std::function<std::span<T>(size_t len, bool throwOnIncompleteRead)> AsLinearReader() const {
			return [this, buf = std::vector<T>(), ptr = uint64_t(), to = StreamSize()](size_t len, bool throwOnIncompleteRead) mutable {
				if (ptr == to)
					return std::span<T>();
				buf.resize(static_cast<size_t>(std::min<uint64_t>(len, to - ptr)));
				const auto read = ReadStreamPartial(ptr, buf.data(), buf.size());
				if (read < buf.size() && throwOnIncompleteRead)
					throw std::runtime_error("incomplete read");
				ptr += buf.size();
				return std::span(buf);
			};
		}

		virtual std::string DescribeState() const { return {}; }
	};

	class BufferedRandomAccessStream : public RandomAccessStream {
		const std::shared_ptr<RandomAccessStream> m_stream;
		const size_t m_bufferSize;
		const uint64_t m_streamSize;
#if INTPTR_MAX == INT64_MAX
		bool m_bEnableBuffering = true;
#else
		bool m_bEnableBuffering = false;
#endif
		mutable std::vector<void*> m_buffers;

	public:
		BufferedRandomAccessStream(std::shared_ptr<RandomAccessStream> stream, size_t bufferSize = 16384)
			: m_stream(std::move(stream))
			, m_bufferSize(bufferSize)
			, m_streamSize(m_stream->StreamSize())
			, m_buffers(Align<uint64_t, size_t>(m_streamSize, m_bufferSize).Count) {
		}

		~BufferedRandomAccessStream() override;

		[[nodiscard]] uint64_t StreamSize() const override { return m_streamSize; }

		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		void EnableBuffering(bool bEnable);

		void Flush() const;
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
		mutable Win32::Handle m_file;
		const uint64_t m_offset;
		const uint64_t m_size;

	public:
		FileRandomAccessStream(Win32::Handle file, uint64_t offset = 0, uint64_t length = UINT64_MAX);
		FileRandomAccessStream(std::filesystem::path path, uint64_t offset = 0, uint64_t length = UINT64_MAX, bool openImmediately = true);
		~FileRandomAccessStream() override;

		[[nodiscard]] uint64_t StreamSize() const override;
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		std::string DescribeState() const override {
			return std::format("FileRandomAccessStream({}, {}, {})", m_file.GetPathName(), m_offset, m_size);
		}
	};

	class MemoryRandomAccessStream : public RandomAccessStream {
		std::vector<uint8_t> m_buffer;

	public:
		MemoryRandomAccessStream() = default;
		
		MemoryRandomAccessStream(MemoryRandomAccessStream&& r) noexcept
			: m_buffer(std::move(r.m_buffer)) {
		}

		MemoryRandomAccessStream(const MemoryRandomAccessStream& r) 
			: m_buffer(r.m_buffer) {
		}

		template<typename...Args>
		MemoryRandomAccessStream(Args ...args)
			: m_buffer(std::forward<Args>(args)...) {
		}
		
		MemoryRandomAccessStream& operator=(std::vector<uint8_t>&& buf) noexcept {
			m_buffer = std::move(buf);
			return *this;
		}

		MemoryRandomAccessStream& operator=(const std::vector<uint8_t>& buf) {
			m_buffer = buf;
			return *this;
		}
		
		MemoryRandomAccessStream& operator=(MemoryRandomAccessStream&& r) noexcept {
			m_buffer = std::move(r.m_buffer);
			return *this;
		}
		
		MemoryRandomAccessStream& operator=(const MemoryRandomAccessStream& r) {
			m_buffer = r.m_buffer;
			return *this;
		}

		[[nodiscard]] uint64_t StreamSize() const override { return m_buffer.size(); }

		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override {
			if (offset >= m_buffer.size())
				return 0;
			if (offset + length > m_buffer.size())
				length = m_buffer.size() - offset;
			std::copy_n(&m_buffer[static_cast<size_t>(offset)], static_cast<size_t>(length), static_cast<char*>(buf));
			return length;
		}
	};
}
