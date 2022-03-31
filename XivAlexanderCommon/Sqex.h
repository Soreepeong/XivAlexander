#pragma once
#include <algorithm>
#include <fstream>
#include <functional>
#include <mutex>
#include <filesystem>
#include <span>
#include <type_traits>

namespace Sqex {
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

	template<typename T>
	union ByteOrderStorage {
		T Value;
		char Bytes[sizeof T];

		static ByteOrderStorage<T> FromWithoutSwap(T value) {
			return ByteOrderStorage<T>(value);
		}

		static T ToWithoutSwap(ByteOrderStorage<T> storage) {
			return storage.Value;
		}

		static ByteOrderStorage<T> FromWithSwap(T value);

		static T ToWithSwap(ByteOrderStorage<T> storage);
	};

	template<typename T>
	inline ByteOrderStorage<T> ByteOrderStorage<T>::FromWithSwap(T value) {
		if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
			return ByteOrderStorage<T>(value);

		else if constexpr (std::is_same_v<T, uint16_t> || std::is_same_v<T, int16_t>)
			return ByteOrderStorage<T>(static_cast<T>(_byteswap_ushort(static_cast<uint16_t>(value))));

		else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>)
			return ByteOrderStorage<T>(static_cast<T>(_byteswap_ulong(static_cast<uint32_t>(value))));

		else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t>)
			return ByteOrderStorage<T>(static_cast<T>(_byteswap_uint64(static_cast<uint64_t>(value))));

		else {
			auto storage = ByteOrderStorage(value);
			std::reverse(storage.Bytes, storage.Bytes + sizeof T);
			return storage;
		}
	}

	template<typename T>
	inline T ByteOrderStorage<T>::ToWithSwap(ByteOrderStorage<T> storage) {
		if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
			return storage.Value;

		else if constexpr (std::is_same_v<T, uint16_t> || std::is_same_v<T, int16_t>)
			return static_cast<T>(_byteswap_ushort(static_cast<uint16_t>(storage.Value)));

		else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>)
			return static_cast<T>(_byteswap_ulong(static_cast<uint32_t>(storage.Value)));

		else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t>)
			return static_cast<T>(_byteswap_uint64(static_cast<uint64_t>(storage.Value)));

		else {
			std::reverse(storage.Bytes, storage.Bytes + sizeof T);
			return storage.Value;
		}
	}

	template<typename T, ByteOrderStorage<T> FromNative(T), T ToNative(ByteOrderStorage<T>)>
	union ByteOrder {
	private:
		ByteOrderStorage<T> m_value;

	public:
		ByteOrder(T defaultValue = 0)
			: m_value(FromNative(defaultValue)) {
		}

		ByteOrder<T, FromNative, ToNative>& operator= (T newValue) {
			m_value = FromNative(newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator+= (T newValue) {
			m_value = FromNative(ToNative(m_value) + newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator-= (T newValue) {
			m_value = FromNative(ToNative(m_value) - newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator*= (T newValue) {
			m_value = FromNative(ToNative(m_value) * newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator/= (T newValue) {
			m_value = FromNative(ToNative(m_value) / newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator%= (T newValue) {
			m_value = FromNative(ToNative(m_value) % newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator&= (T newValue) {
			m_value = FromNative(ToNative(m_value) & newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator|= (T newValue) {
			m_value = FromNative(ToNative(m_value) | newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator^= (T newValue) {
			m_value = FromNative(ToNative(m_value) ^ newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator<<= (T newValue) {
			m_value = FromNative(ToNative(m_value) << newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator>>= (T newValue) {
			m_value = FromNative(ToNative(m_value) >> newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator++ () {
			m_value = FromNative(ToNative(m_value) + 1);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator-- () {
			m_value = FromNative(ToNative(m_value) - 1);
			return *this;
		}

		T operator++ (int) {
			const auto v = ToNative(m_value);
			m_value = FromNative(v + 1);
			return v;
		}

		T operator-- (int) {
			const auto v = ToNative(m_value);
			m_value = FromNative(v - 1);
			return v;
		}

		T operator+() const {
			return ToNative(m_value);
		}

		T operator-() const {
			return -ToNative(m_value);
		}

		T operator~() const {
			return ~ToNative(m_value);
		}

		T operator!() const {
			return !ToNative(m_value);
		}

		operator T() const {
			return ToNative(m_value);
		}

		T operator *() const {
			return ToNative(m_value);
		}
	};

	template<typename T>
	using LE = ByteOrder<T, ByteOrderStorage<T>::FromWithoutSwap, ByteOrderStorage<T>::ToWithoutSwap>;
	template<typename T>
	using BE = ByteOrder<T, ByteOrderStorage<T>::FromWithSwap, ByteOrderStorage<T>::ToWithSwap>;

	template<typename T>
	T Clamp(T value, T minValue, T maxValue) {
		return (std::min)(maxValue, (std::max)(minValue, value));
	}

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
	class RandomAccessStreamPartialView;

	class RandomAccessStream : public std::enable_shared_from_this<RandomAccessStream> {
	public:
		RandomAccessStream() = default;
		RandomAccessStream(RandomAccessStream&&) = delete;
		RandomAccessStream(const RandomAccessStream&) = delete;
		RandomAccessStream& operator=(RandomAccessStream&&) = delete;
		RandomAccessStream& operator=(const RandomAccessStream&) = delete;
		virtual ~RandomAccessStream() = default;

		[[nodiscard]] virtual std::streamsize StreamSize() const = 0;

		virtual std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const = 0;

		void ReadStream(std::streamoff offset, void* buf, std::streamsize length) const {
			if (ReadStreamPartial(offset, buf, length) != length)
				throw std::runtime_error("Reached end of stream before reading all of the requested data.");
		}

		template<typename T>
		T ReadStream(std::streamoff offset) const {
			T buf;
			ReadStream(offset, &buf, sizeof T);
			return buf;
		}

		template<typename T>
		void ReadStream(std::streamoff offset, std::span<T> buf) const {
			ReadStream(offset, buf.data(), buf.size_bytes());
		}

		template<typename T>
		std::vector<T> ReadStreamIntoVector(std::streamoff offset, size_t count = SIZE_MAX, size_t maxCount = SIZE_MAX) const {
			if (count > maxCount)
				throw std::runtime_error("trying to read too many");
			if (count == SIZE_MAX)
				count = static_cast<size_t>(StreamSize() / sizeof T);
			std::vector<T> result(count);
			ReadStream(offset, std::span(result));
			return result;
		}

		virtual void EnableBuffering(bool bEnable) {}

		virtual void Flush() const {}

		virtual RandomAccessStreamPartialView SubStream(std::streamoff offset, std::streamsize length = std::numeric_limits<std::streamsize>::max());

		virtual std::shared_ptr<RandomAccessStreamPartialView> SubStreamShared(std::streamoff offset, std::streamsize length = std::numeric_limits<std::streamsize>::max());
	};

	class RandomAccessStreamPartialView : public RandomAccessStream {
		const std::shared_ptr<const RandomAccessStream> m_streamSharedPtr;

	public:
		const RandomAccessStream& BaseStream;
		const std::streamoff BaseOffset;

	private:
		const std::streamsize m_size;

	public:
		RandomAccessStreamPartialView(const RandomAccessStream& stream, std::streamoff offset = 0, std::streamsize length = std::numeric_limits<std::streamsize>::max())
			: BaseStream(stream)
			, BaseOffset(offset)
			, m_size(std::min(length, BaseStream.StreamSize() - offset)) {
		}

		RandomAccessStreamPartialView(const RandomAccessStreamPartialView&) = default;

		RandomAccessStreamPartialView(std::shared_ptr<const RandomAccessStream> stream, std::streamoff offset = 0, std::streamsize length = std::numeric_limits<std::streamsize>::max())
			: m_streamSharedPtr(std::move(stream))
			, BaseStream(*m_streamSharedPtr)
			, BaseOffset(offset)
			, m_size(std::min(length, BaseStream.StreamSize() - offset)) {
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_size;
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (offset >= m_size)
				return 0;
			length = std::min(length, m_size - offset);
			return BaseStream.ReadStreamPartial(BaseOffset + offset, buf, length);
		}
	};

	inline RandomAccessStreamPartialView RandomAccessStream::SubStream(std::streamoff offset, std::streamsize length) {
		return RandomAccessStreamPartialView(*this, offset, length);
	}

	inline std::shared_ptr<RandomAccessStreamPartialView> RandomAccessStream::SubStreamShared(std::streamoff offset, std::streamsize length) {
		return std::make_shared<RandomAccessStreamPartialView>(shared_from_this(), offset, length);
	}

	class FileRandomAccessStream : public RandomAccessStream {
		const std::filesystem::path m_path;
		mutable std::mutex m_mutex;
		mutable std::vector<std::ifstream> m_streams;

		class PooledObject {
			const FileRandomAccessStream& m_parent;
			mutable std::ifstream m_stream;

		public:
			PooledObject(const FileRandomAccessStream& parent)
				: m_parent(parent) {
				const auto lock = std::lock_guard(parent.m_mutex);
				if (parent.m_streams.empty())
					m_stream = std::ifstream(parent.m_path, std::ios::binary);
				else {
					m_stream = std::move(parent.m_streams.back());
					parent.m_streams.pop_back();
				}
			}

			~PooledObject() {
				const auto lock = std::lock_guard(m_parent.m_mutex);
				m_parent.m_streams.emplace_back(std::move(m_stream));
			}

			std::ifstream* operator->() const {
				return &m_stream;
			}
		};

	public:
		FileRandomAccessStream(std::filesystem::path path, bool openImmediately = true)
			: m_path(std::move(path)) {
			if (openImmediately)
				m_streams.emplace_back(m_path, std::ios::binary);
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			PooledObject stream(*this);
			stream->seekg(0, std::ios::end);
			return stream->tellg();
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			PooledObject stream(*this);
			stream->seekg(offset, std::ios::beg);
			stream->read(static_cast<char*>(buf), length);
			return stream->gcount();
		}
	};

	class MemoryRandomAccessStream : public RandomAccessStream {
		std::vector<uint8_t> m_buffer;
		std::span<uint8_t> m_view;

	public:
		MemoryRandomAccessStream() = default;
		
		MemoryRandomAccessStream(MemoryRandomAccessStream&& r) noexcept
			: m_buffer(std::move(r.m_buffer))
			, m_view(std::move(r.m_view)) {
			r.m_view = {};
		}

		MemoryRandomAccessStream(const MemoryRandomAccessStream& r)
			: m_buffer(r.m_buffer)
			, m_view(r.OwnsData() ? std::span(m_buffer) : r.m_view) {
		}

		MemoryRandomAccessStream(const RandomAccessStream& r)
			: m_buffer(static_cast<size_t>(r.StreamSize()))
			, m_view(std::span(m_buffer)) {
			r.ReadStream(0, m_view);
		}

		MemoryRandomAccessStream(std::vector<uint8_t> buffer)
			: m_buffer(std::move(buffer))
			, m_view(m_buffer) {
		}

		MemoryRandomAccessStream(std::span<uint8_t> view)
			: m_view(view) {
		}
		
		MemoryRandomAccessStream& operator=(std::vector<uint8_t>&& buf) noexcept {
			m_buffer = std::move(buf);
			m_view = std::span(m_buffer);
			return *this;
		}

		MemoryRandomAccessStream& operator=(const std::vector<uint8_t>& buf) {
			m_buffer = buf;
			m_view = std::span(m_buffer);
			return *this;
		}
		
		MemoryRandomAccessStream& operator=(MemoryRandomAccessStream&& r) noexcept {
			m_buffer = std::move(r.m_buffer);
			m_view = std::move(r.m_view);
			r.m_view = {};
			return *this;
		}
		
		MemoryRandomAccessStream& operator=(const MemoryRandomAccessStream& r) {
			if (r.OwnsData()) {
				m_buffer = r.m_buffer;
				m_view = std::span(m_buffer);
			} else {
				m_buffer.clear();
				m_view = r.m_view;
			}
			return *this;
		}

		[[nodiscard]] std::streamsize StreamSize() const override { return m_view.size(); }

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (offset >= m_view.size())
				return 0;
			if (offset + length > m_view.size())
				length = m_view.size() - offset;
			std::copy_n(&m_view[static_cast<size_t>(offset)], static_cast<size_t>(length), static_cast<char*>(buf));
			return length;
		}

		bool OwnsData() const {
			return !m_buffer.empty() && m_view.data() == m_buffer.data();
		}
	};
}
