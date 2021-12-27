#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "Sqex.h"

namespace Sqex::EqpGmp {
	static constexpr size_t CountPerBlock = 160;

	class ExpandedFile;

	std::vector<uint64_t> ExpandCollapse(const std::vector<uint64_t>& data, bool expand);

	class CollapsedFile {
		std::vector<uint64_t> m_data;

	public:
		CollapsedFile() : m_data(CountPerBlock) { m_data[0] = 1; }
		CollapsedFile(std::vector<uint64_t> data) : m_data(std::move(data)) {}
		CollapsedFile(const RandomAccessStream& stream) : m_data(stream.ReadStreamIntoVector<uint64_t>(0)) {}
		CollapsedFile(const CollapsedFile& file) : m_data(file.m_data) {}
		CollapsedFile(CollapsedFile&& file) : m_data(std::move(file.m_data)) {
			file.m_data.resize(CountPerBlock);
			file.m_data[0] = 1;
		}
		CollapsedFile(const ExpandedFile& file);
		CollapsedFile& operator=(const CollapsedFile& file) {
			m_data = file.m_data;
			return *this;
		}
		CollapsedFile& operator=(CollapsedFile&& file) {
			m_data = std::move(file.m_data);
			file.m_data.resize(CountPerBlock);
			file.m_data[0] = 1;
			return *this;
		}

		const std::vector<uint64_t>& Data() const {
			return m_data;
		}

		std::vector<uint8_t> DataBytes() const {
			std::vector<uint8_t> res(std::span(m_data).size_bytes());
			memcpy(&res[0], &m_data[0], res.size());
			return res;
		}

		uint64_t& BlockBits() {
			return m_data[0];
		}

		const uint64_t& BlockBits() const {
			return m_data[0];
		}

		std::span<uint64_t> Block(size_t index) {
			if (!(BlockBits() & (uint64_t{ 1 } << index)))
				return {};
			size_t populatedIndex = 0;
			for (size_t i = 0; i < index; i++) {
				if (BlockBits() & (uint64_t{ 1 } << i))
					populatedIndex++;
			}
			return std::span(m_data).subspan(CountPerBlock * populatedIndex, CountPerBlock);
		}

		std::span<const uint64_t> Block(size_t index) const {
			if (!(BlockBits() & (uint64_t{ 1 } << index)))
				return {};
			size_t populatedIndex = 0;
			for (size_t i = 0; i < index; i++) {
				if (BlockBits() & (uint64_t{ 1 } << i))
					populatedIndex++;
			}
			return std::span(m_data).subspan(CountPerBlock * populatedIndex, CountPerBlock);
		}
	};

	class ExpandedFile {
		std::vector<uint64_t> m_data;

	public:
		ExpandedFile() : m_data(CountPerBlock * 64) { m_data[0] = UINT64_MAX; }
		ExpandedFile(const ExpandedFile& file) : m_data(file.m_data) {}
		ExpandedFile(ExpandedFile&& file) : m_data(std::move(file.m_data)) {
			file.m_data.resize(CountPerBlock * 64);
			file.m_data[0] = UINT64_MAX;
		}
		ExpandedFile(const CollapsedFile& file) : m_data(ExpandCollapse(file.Data(), true)) {}
		ExpandedFile& operator=(const ExpandedFile& file) {
			m_data = file.m_data;
			return *this;
		}
		ExpandedFile& operator=(ExpandedFile&& file) {
			m_data = std::move(file.m_data);
			file.m_data.resize(CountPerBlock);
			file.m_data[0] = 1;
			return *this;
		}

		const std::vector<uint64_t>& Data() const {
			return m_data;
		}

		std::vector<uint8_t> DataBytes() const {
			std::vector<uint8_t> res(std::span(m_data).size_bytes());
			memcpy(&res[0], &m_data[0], res.size());
			return res;
		}

		std::span<uint64_t> Block(size_t index) {
			return std::span(m_data).subspan(CountPerBlock * index, CountPerBlock);
		}

		std::span<const uint64_t> Block(size_t index) const {
			return std::span(m_data).subspan(CountPerBlock * index, CountPerBlock);
		}

		uint64_t& Parameter(size_t primaryId) {
			return m_data[primaryId];
		}

		const uint64_t& Parameter(size_t primaryId) const {
			return m_data[primaryId];
		}

		std::span<uint8_t> ParameterBytes(size_t primaryId) {
			return std::span(reinterpret_cast<uint8_t*>(&m_data[primaryId]), 8);
		}

		std::span<const uint8_t> ParameterBytes(size_t primaryId) const {
			return std::span(reinterpret_cast<const uint8_t*>(&m_data[primaryId]), 8);
		}
	};

	inline CollapsedFile::CollapsedFile(const ExpandedFile& file) : m_data(ExpandCollapse(file.Data(), false)) {}
}
