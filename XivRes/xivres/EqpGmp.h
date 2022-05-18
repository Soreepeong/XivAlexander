#ifndef _XIVRES_EQPGMP_H_
#define _XIVRES_EQPGMP_H_

#include <cstdint>
#include <span>
#include <vector>

#include "Internal/ByteOrder.h"
#include "Internal/SpanCast.h"

#include "Common.h"
#include "IStream.h"

namespace XivRes {
	class EqpGmpFile {
		static constexpr size_t CountPerBlock = 160;

		std::vector<uint64_t> m_data;
		std::vector<size_t> m_populatedIndices;

	public:
		EqpGmpFile() {
			Clear();
		}

		EqpGmpFile(std::vector<uint64_t> data)
			: m_data(std::move(data)) {

			size_t populatedIndex = 0;
			for (size_t i = 0; i < 64; i++) {
				if (BlockBits() & (uint64_t{ 1 } << i))
					m_populatedIndices.push_back(populatedIndex++);
				else
					m_populatedIndices.push_back(SIZE_MAX);
			}
		}

		EqpGmpFile(const IStream& stream)
			: EqpGmpFile(ReadStreamIntoVector<uint64_t>(stream, 0)) {
		}

		EqpGmpFile& operator=(const EqpGmpFile& file) {
			m_data = file.m_data;
			m_populatedIndices = file.m_populatedIndices;
			return *this;
		}

		EqpGmpFile& operator=(EqpGmpFile&& file) {
			m_data = std::move(file.m_data);
			m_populatedIndices = std::move(file.m_populatedIndices);
			file.Clear();
			return *this;
		}

		void Clear() {
			m_data.clear();
			m_populatedIndices.clear();
			m_data.resize(CountPerBlock);
			m_data[0] = 1;
			m_populatedIndices.resize(64, SIZE_MAX);
			m_populatedIndices[0] = 0;
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
			const auto populatedIndex = m_populatedIndices.at(index);
			if (populatedIndex == SIZE_MAX)
				return {};
			return std::span(m_data).subspan(CountPerBlock * populatedIndex, CountPerBlock);
		}

		std::span<const uint64_t> Block(size_t index) const {
			const auto populatedIndex = m_populatedIndices.at(index);
			if (populatedIndex == SIZE_MAX)
				return {};
			return std::span(m_data).subspan(CountPerBlock * populatedIndex, CountPerBlock);
		}

		uint64_t& Parameter(size_t primaryId) {
			const auto blockIndex = primaryId / CountPerBlock;
			const auto subIndex = primaryId % CountPerBlock;
			const auto block = Block(blockIndex);
			if (block.empty())
				throw std::runtime_error("Must be expanded before accessing parameter for modification.");
			return block[subIndex];
		}

		uint64_t Parameter(size_t primaryId) const {
			return GetParameter(primaryId);
		}

		uint64_t GetParameter(size_t primaryId) const {
			const auto blockIndex = primaryId / CountPerBlock;
			const auto subIndex = primaryId % CountPerBlock;
			const auto block = Block(blockIndex);
			if (block.empty())
				return 0;
			return block[subIndex];
		}

		std::span<uint8_t> ParameterBytes(size_t primaryId) {
			return Internal::span_cast<uint8_t>(m_data, primaryId, 8);
		}

		std::span<const uint8_t> ParameterBytes(size_t primaryId) const {
			return Internal::span_cast<uint8_t>(m_data, primaryId, 8);
		}

		std::vector<uint64_t> ExpandCollapse(bool expand) {
			std::vector<uint64_t> newData;
			newData.reserve(CountPerBlock * 64);

			uint64_t populatedBits = 0;

			size_t sourceIndex = 0, targetIndex = 0;
			for (size_t i = 0; i < 64; i++) {
				if (m_data[0] & (1ULL << i)) {
					const auto currentSourceIndex = sourceIndex;
					sourceIndex++;

					if (!expand) {
						bool isAllZeros = true;
						for (size_t j = currentSourceIndex * CountPerBlock, j_ = j + CountPerBlock; isAllZeros && j < j_; ++j) {
							isAllZeros = m_data[j] == 0;
						}
						if (isAllZeros)
							continue;
					}
					populatedBits |= 1ULL << i;
					newData.resize(newData.size() + CountPerBlock);
					std::copy_n(&m_data[currentSourceIndex * CountPerBlock], CountPerBlock, &newData[targetIndex * CountPerBlock]);
					targetIndex++;
				} else {
					if (expand) {
						populatedBits |= 1ULL << i;
						newData.resize(newData.size() + CountPerBlock);
						targetIndex++;
					}
				}
			}
			newData[0] = populatedBits;

			return newData;
		}
	};
}

#endif
