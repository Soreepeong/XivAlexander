#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace Utils {
	template<typename T, size_t Capacity_>
	class RingBuffer {
		static_assert(Capacity_ > 0);
		static_assert(Capacity_ <= (std::numeric_limits<size_t>::max)());

		std::array<T, Capacity_> m_storage;
		size_t m_head = 0;
		size_t m_size = 0;

	public:
		[[nodiscard]] std::span<T> Buffer() { return {m_storage.data(), Capacity()}; }

		[[nodiscard]] std::span<const T> Buffer() const { return {m_storage.data(), Capacity()}; }

		[[nodiscard]] static constexpr size_t Capacity() { return Capacity_; }

		[[nodiscard]] size_t Readable() const { return m_size; }

		[[nodiscard]] size_t Writable() const { return Capacity() - m_size; }

		[[nodiscard]] std::span<const T> Head() const { return {m_storage.data() + m_head, HeadSize()}; }

		[[nodiscard]] std::span<T> Tail() { return {m_storage.data() + TailIndex(), TailSize()}; }

		void Consume(size_t count) {
			m_head = (m_head + count) % Capacity();
			m_size -= count;
		}

		void Commit(size_t count) { m_size += count; }

		void Clear() { m_head = m_size = 0; }

		void Compact() {
			if (m_head <= 0)
				return;
			std::rotate(m_storage.begin(), m_storage.begin() + m_head, m_storage.end());
			m_head = 0;
		}

		size_t Splice(size_t index, size_t count) {
			if (count == 0)
				return 0;

			if (const auto behind = m_size - index - count; index <= behind) {
				MoveUp(count, 0, index);
				m_head = (m_head + count) % Capacity();
			} else {
				MoveDown(index, index + count, behind);
			}

			m_size -= count;
			return count;
		}

		[[nodiscard]] const T& operator[](size_t index) const {
			return m_storage[(m_head + index) % Capacity()];
		}

		[[nodiscard]] T& operator[](size_t index) {
			return m_storage[(m_head + index) % Capacity()];
		}

		template<typename TValue>
			requires ((sizeof(TValue) % sizeof(T) == 0) && std::is_trivially_copyable_v<TValue>)
		[[nodiscard]] TValue ReadAs(size_t index) const {
			constexpr auto count = sizeof(TValue) / sizeof(T);
			TValue value;
			auto* out = reinterpret_cast<uint8_t*>(&value);
			const auto start = (m_head + index) % Capacity();
			const auto first = std::min(count, Capacity() - start);
			std::memcpy(out, m_storage.data() + start, first * sizeof(T));
			if (first < count)
				std::memcpy(out + first * sizeof(T), m_storage.data(), (count - first) * sizeof(T));
			return value;
		}

		bool ReadExactlyAt(size_t index, std::span<T> dst) const {
			if (index >= Readable() || dst.size() > Readable() - index)
				return false;
			const auto start = (m_head + index) % Capacity();
			const auto first = std::min(dst.size(), static_cast<size_t>(Capacity() - start));
			std::memcpy(dst.data(), m_storage.data() + start, first * sizeof(T));
			if (first < dst.size())
				std::memcpy(dst.data() + first, m_storage.data(), (dst.size() - first) * sizeof(T));
			return true;
		}

		size_t ReadAt(size_t index, std::span<T> dst) const {
			if (index >= Readable())
				return 0;
			if (dst.size() > Readable() - index)
				dst = dst.subspan(0, Readable() - index);
			if (dst.empty())
				return 0;
			const auto start = (m_head + index) % Capacity();
			const auto first = std::min(dst.size(), static_cast<size_t>(Capacity() - start));
			std::memcpy(dst.data(), m_storage.data() + start, first * sizeof(T));
			if (first < dst.size())
				std::memcpy(dst.data() + first, m_storage.data(), (dst.size() - first) * sizeof(T));
			return static_cast<size_t>(dst.size());
		}

		[[nodiscard]] bool StartsWith(size_t index, std::span<const T> text) const {
			if (text.empty())
				return true;
			const auto start = (m_head + index) % Capacity();
			const auto first = std::min(text.size(), static_cast<size_t>(Capacity() - start));
			if (std::memcmp(m_storage.data() + start, text.data(), first * sizeof(T)) != 0)
				return false;
			return first == text.size()
				|| std::memcmp(m_storage.data(), text.data() + first, (text.size() - first) * sizeof(T)) == 0;
		}

		[[nodiscard]] bool StartsWith(size_t index, std::string_view text) const requires (sizeof(T) == 1) {
			return StartsWith(index, std::span(reinterpret_cast<const T*>(text.data()), text.size()));
		}

	private:
		/// Copies \p length items from \p from down onto \p to, which is below it. Lowest first, so that
		/// what a later run reads has not been written over yet; each run is whole on both sides, which
		/// leaves the overlap between them for memmove to sort out.
		void MoveDown(size_t to, size_t from, size_t length) {
			while (length) {
				const auto run = RunUp(to, from, length);
				std::memmove(&(*this)[to], &(*this)[from], run * sizeof(T));
				to = to + run;
				from = from + run;
				length -= run;
			}
		}

		/// The same the other way, so highest first, for \p to above \p from.
		void MoveUp(size_t to, size_t from, size_t length) {
			while (length) {
				const auto run = RunDown(to, from, length);
				length -= run;
				std::memmove(&(*this)[to + length], &(*this)[from + length], run * sizeof(T));
			}
		}

		/// \returns How much of \p a and \p b can be taken at once going up: neither may cross the wrap.
		[[nodiscard]] size_t RunUp(size_t a, size_t b, size_t length) const {
			return std::min<size_t>({
				length,
				(Capacity() - (m_head + a) % Capacity()),
				(Capacity() - (m_head + b) % Capacity()),
			});
		}

		/// \returns The same going down, measured back from the far end of each.
		[[nodiscard]] size_t RunDown(size_t a, size_t b, size_t length) const {
			return std::min<size_t>({
				length,
				((m_head + a + length - 1) % Capacity() + 1),
				((m_head + b + length - 1) % Capacity() + 1),
			});
		}

		[[nodiscard]] size_t HeadSize() const {
			return std::min<size_t>(m_size, Capacity() - m_head);
		}

		[[nodiscard]] size_t TailSize() const {
			return std::min<size_t>(Writable(), Capacity() - TailIndex());
		}

		[[nodiscard]] size_t TailIndex() const {
			return (m_head + m_size) % Capacity();
		}
	};
}
