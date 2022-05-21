#ifndef _XIVRES_STREAM_H_
#define _XIVRES_STREAM_H_

#include <algorithm>
#include <fstream>
#include <functional>
#include <mutex>
#include <filesystem>
#include <span>
#include <type_traits>

#include "Internal/ByteOrder.h"
#include "Internal/SpanCast.h"
#include "Internal/Misc.h"

namespace XivRes {
	class PartialViewStream;

	class IStream {
	public:
		[[nodiscard]] virtual std::streamsize StreamSize() const = 0;

		[[nodiscard]] virtual std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const = 0;

		virtual void EnableBuffering(bool bEnable) = 0;

		virtual void Flush() const = 0;

		virtual std::unique_ptr<IStream> SubStream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) = 0;
	};

	inline void ReadStream(const IStream& stream, std::streamoff offset, void* buf, std::streamsize length) {
		if (stream.ReadStreamPartial(offset, buf, length) != length)
			throw std::runtime_error("Reached end of stream before reading all of the requested data.");
	}

	template<typename T>
	inline T ReadStream(const IStream& stream, std::streamoff offset) {
		T buf;
		ReadStream(stream, offset, &buf, sizeof T);
		return buf;
	}

	template<typename T>
	inline void ReadStream(const IStream& stream, std::streamoff offset, std::span<T> buf) {
		ReadStream(stream, offset, buf.data(), buf.size_bytes());
	}

	template<typename T>
	inline std::vector<T> ReadStreamIntoVector(const IStream& stream, std::streamoff offset, size_t count, size_t maxCount = SIZE_MAX) {
		if (count > maxCount)
			throw std::runtime_error("trying to read too many");
		std::vector<T> result(count);
		ReadStream(stream, offset, std::span(result));
		return result;
	}

	template<typename T>
	inline std::vector<T> ReadStreamIntoVector(const IStream& stream, size_t maxCount = SIZE_MAX) {
		return ReadStreamIntoVector<T>(stream, 0, (std::min)(maxCount, static_cast<size_t>(stream.StreamSize() / sizeof T)));
	}

	class DefaultAbstractStream : public IStream, public std::enable_shared_from_this<DefaultAbstractStream> {
	public:
		DefaultAbstractStream() = default;
		DefaultAbstractStream(IStream&&) = delete;
		DefaultAbstractStream(const IStream&) = delete;
		IStream& operator=(IStream&&) = delete;
		IStream& operator=(const IStream&) = delete;
		virtual ~DefaultAbstractStream() = default;

		[[nodiscard]] virtual std::streamsize StreamSize() const = 0;

		virtual std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const = 0;

		virtual void EnableBuffering(bool bEnable) {}

		virtual void Flush() const {}

		virtual std::unique_ptr<IStream> SubStream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)());
	};

	class PartialViewStream : public DefaultAbstractStream {
		const std::shared_ptr<const IStream> m_streamSharedPtr;

	public:
		const IStream& m_stream;
		const std::streamoff m_offset;

	private:
		const std::streamsize m_size;

	public:
		PartialViewStream(const IStream& stream, std::streamoff offset = 0, std::streamsize length = (std::numeric_limits<std::streamsize>::max)())
			: m_stream(stream)
			, m_offset(offset)
			, m_size((std::min)(length, m_stream.StreamSize() < offset ? 0 : m_stream.StreamSize() - offset)) {}

		PartialViewStream(const PartialViewStream&) = default;

		PartialViewStream(std::shared_ptr<const IStream> stream, std::streamoff offset = 0, std::streamsize length = (std::numeric_limits<std::streamsize>::max)())
			: m_streamSharedPtr(std::move(stream))
			, m_stream(*m_streamSharedPtr)
			, m_offset(offset)
			, m_size((std::min)(length, m_stream.StreamSize() < offset ? 0 : m_stream.StreamSize() - offset)) {}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_size;
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (offset >= m_size)
				return 0;
			length = (std::min)(length, m_size - offset);
			return m_stream.ReadStreamPartial(m_offset + offset, buf, length);
		}

		std::unique_ptr<IStream> SubStream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) override {
			return std::make_unique<PartialViewStream>(m_streamSharedPtr, m_offset + offset, (std::min)(length, m_size));
		}
	};

	inline std::unique_ptr<IStream> DefaultAbstractStream::SubStream(std::streamoff offset, std::streamsize length) {
		return std::make_unique<PartialViewStream>(shared_from_this(), offset, length);
	}

#ifdef _WINDOWS_
	class FileStream : public DefaultAbstractStream {
		const std::filesystem::path m_path;
		const HANDLE m_hFile;
		HANDLE m_hDummyEvent{};

	public:
		FileStream(std::filesystem::path path)
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

		~FileStream() override {
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
	class FileStream : public DefaultAbstractStream {
		const std::filesystem::path m_path;
		mutable std::mutex m_mutex;
		mutable std::vector<std::ifstream> m_streams;

		class PooledObject {
			const FileStream& m_parent;
			mutable std::ifstream m_stream;

		public:
			PooledObject(const FileStream& parent)
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
		FileStream(std::filesystem::path path)
			: m_path(std::move(path)) {
			m_streams.emplace_back(m_path, std::ios::binary);
		}

		~FileStream() override = default;

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

	class MemoryStream : public DefaultAbstractStream {
		std::vector<uint8_t> m_buffer;
		std::span<uint8_t> m_view;

	public:
		MemoryStream() = default;

		MemoryStream(MemoryStream&& r) noexcept
			: m_buffer(std::move(r.m_buffer))
			, m_view(std::move(r.m_view)) {
			r.m_view = {};
		}

		MemoryStream(const MemoryStream& r)
			: m_buffer(r.m_buffer)
			, m_view(r.OwnsData() ? std::span(m_buffer) : r.m_view) {}

		MemoryStream(const IStream& r)
			: m_buffer(static_cast<size_t>(r.StreamSize()))
			, m_view(std::span(m_buffer)) {
			ReadStream(r, 0, m_view);
		}

		MemoryStream(std::vector<uint8_t> buffer)
			: m_buffer(std::move(buffer))
			, m_view(m_buffer) {}

		MemoryStream(std::span<uint8_t> view)
			: m_view(view) {}

		MemoryStream& operator=(std::vector<uint8_t>&& buf) noexcept {
			m_buffer = std::move(buf);
			m_view = std::span(m_buffer);
			return *this;
		}

		MemoryStream& operator=(const std::vector<uint8_t>& buf) {
			m_buffer = buf;
			m_view = std::span(m_buffer);
			return *this;
		}

		MemoryStream& operator=(MemoryStream&& r) noexcept {
			m_buffer = std::move(r.m_buffer);
			m_view = std::move(r.m_view);
			r.m_view = {};
			return *this;
		}

		MemoryStream& operator=(const MemoryStream& r) {
			if (r.OwnsData()) {
				m_buffer = r.m_buffer;
				m_view = std::span(m_buffer);
			} else {
				m_buffer.clear();
				m_view = r.m_view;
			}
			return *this;
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_view.size();
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (offset >= static_cast<std::streamoff>(m_view.size()))
				return 0;
			if (offset + length > static_cast<std::streamoff>(m_view.size()))
				length = m_view.size() - offset;
			std::copy_n(&m_view[static_cast<size_t>(offset)], static_cast<size_t>(length), static_cast<char*>(buf));
			return length;
		}

		[[nodiscard]] bool OwnsData() const {
			return !m_buffer.empty() && m_view.data() == m_buffer.data();
		}

		std::span<const uint8_t> View(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const {
			return m_view.subspan(static_cast<size_t>(offset), static_cast<size_t>(length));
		}
	};
}

#endif
