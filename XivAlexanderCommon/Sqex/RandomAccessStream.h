#pragma once

#include <algorithm>
#include <fstream>
#include <functional>
#include <mutex>
#include <filesystem>
#include <span>
#include <type_traits>

#include "Sqex/internal/ByteOrder.h"
#include "Sqex/internal/SpanCast.h"
#include "Sqex/internal/Misc.h"

namespace XivRes {
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
		std::vector<T> ReadStreamIntoVector(std::streamoff offset, size_t count, size_t maxCount = SIZE_MAX) const {
			if (count > maxCount)
				throw std::runtime_error("trying to read too many");
			std::vector<T> result(count);
			ReadStream(offset, std::span(result));
			return result;
		}

		template<typename T>
		std::vector<T> ReadStreamIntoVector(size_t maxCount = SIZE_MAX) const {
			return ReadStreamIntoVector<T>(0, (std::min)(maxCount, static_cast<size_t>(StreamSize() / sizeof T)));
		}

		virtual void EnableBuffering(bool bEnable) {}

		virtual void Flush() const {}

		virtual RandomAccessStreamPartialView SubStream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)());

		virtual std::shared_ptr<RandomAccessStreamPartialView> SubStreamShared(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)());
	};

	class RandomAccessStreamPartialView : public RandomAccessStream {
		const std::shared_ptr<const RandomAccessStream> m_streamSharedPtr;

	public:
		const RandomAccessStream& m_stream;
		const std::streamoff m_offset;

	private:
		const std::streamsize m_size;

	public:
		RandomAccessStreamPartialView(const RandomAccessStream& stream, std::streamoff offset = 0, std::streamsize length = (std::numeric_limits<std::streamsize>::max)())
			: m_stream(stream)
			, m_offset(offset)
			, m_size((std::min)(length, m_stream.StreamSize() < offset ? 0 : m_stream.StreamSize() - offset)) {
		}

		RandomAccessStreamPartialView(const RandomAccessStreamPartialView&) = default;

		RandomAccessStreamPartialView(std::shared_ptr<const RandomAccessStream> stream, std::streamoff offset = 0, std::streamsize length = (std::numeric_limits<std::streamsize>::max)())
			: m_streamSharedPtr(std::move(stream))
			, m_stream(*m_streamSharedPtr)
			, m_offset(offset)
			, m_size((std::min)(length, m_stream.StreamSize() < offset ? 0 : m_stream.StreamSize() - offset)) {
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_size;
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (offset >= m_size)
				return 0;
			length = (std::min)(length, m_size - offset);
			return m_stream.ReadStreamPartial(m_offset + offset, buf, length);
		}

		RandomAccessStreamPartialView SubStream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) override {
			return RandomAccessStreamPartialView(m_stream, m_offset + offset, (std::min)(length, m_size));
		}

		std::shared_ptr<RandomAccessStreamPartialView> SubStreamShared(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) override {
			return std::make_shared<RandomAccessStreamPartialView>(m_streamSharedPtr, m_offset + offset, (std::min)(length, m_size));
		}
	};

	inline RandomAccessStreamPartialView RandomAccessStream::SubStream(std::streamoff offset, std::streamsize length) {
		return RandomAccessStreamPartialView(*this, offset, length);
	}

	inline std::shared_ptr<RandomAccessStreamPartialView> RandomAccessStream::SubStreamShared(std::streamoff offset, std::streamsize length) {
		return std::make_shared<RandomAccessStreamPartialView>(shared_from_this(), offset, length);
	}

#ifdef _WINDOWS_
	class FileRandomAccessStream : public RandomAccessStream {
		const std::filesystem::path m_path;
		const HANDLE m_hFile;
		HANDLE m_hDummyEvent{};

	public:
		FileRandomAccessStream(std::filesystem::path path)
			: m_path(std::move(path))
			, m_hFile(CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)) {

			if (m_hFile == INVALID_HANDLE_VALUE)
				throw std::system_error(std::error_code(GetLastError(), std::system_category()));

			m_hDummyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			if (m_hDummyEvent == 0) {
				auto err = std::system_error(std::error_code(GetLastError(), std::system_category()));
				CloseHandle(m_hFile);
				throw std::move(err);
			}
		}

		~FileRandomAccessStream() {
			CloseHandle(m_hDummyEvent);
			CloseHandle(m_hFile);
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			LARGE_INTEGER fs{};
			GetFileSizeEx(m_hFile, &fs);
			return static_cast<std::streamsize>(fs.QuadPart);
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			const int64_t ChunkSize = 0x10000000L;
			if (length > ChunkSize) {
				size_t totalRead = 0;
				for (std::streamoff i = 0; i < length; i += ChunkSize) {
					const auto toRead = static_cast<DWORD>((std::min<int64_t>)(ChunkSize, length - i));
					const auto read = ReadStreamPartial(offset + i, static_cast<char*>(buf) + i, toRead);
					totalRead += read;
					if (read != toRead)
						break;
				}
				return totalRead;

			} else {
				DWORD readLength = 0;
				OVERLAPPED ov{};
				ov.hEvent = m_hDummyEvent;
				ov.Offset = static_cast<DWORD>(offset);
				ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
				if (!ReadFile(m_hFile, buf, static_cast<DWORD>(length), &readLength, &ov)) {
					const auto err = GetLastError();
					if (err != ERROR_HANDLE_EOF)
						throw std::system_error(std::error_code(err, std::system_category()));
				}
				return readLength;
			}
		}
	};

#else
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
		FileRandomAccessStream(std::filesystem::path path)
			: m_path(std::move(path)) {
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
#endif

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
			if (offset >= static_cast<std::streamoff>(m_view.size()))
				return 0;
			if (offset + length > static_cast<std::streamoff>(m_view.size()))
				length = m_view.size() - offset;
			std::copy_n(&m_view[static_cast<size_t>(offset)], static_cast<size_t>(length), static_cast<char*>(buf));
			return length;
		}

		bool OwnsData() const {
			return !m_buffer.empty() && m_view.data() == m_buffer.data();
		}
	};
}
