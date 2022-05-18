#ifndef _XIVRES_EQDP_H_
#define _XIVRES_EQDP_H_

#include <cstdint>
#include <span>
#include <vector>

#include "Internal/ByteOrder.h"
#include "Internal/SpanCast.h"

#include "Common.h"
#include "IStream.h"

namespace XivRes {
	struct EqdpHeader {
		LE<uint16_t> Identifier;
		LE<uint16_t> BlockMemberCount;
		LE<uint16_t> BlockCount;
	};

	class EqdpFile {
	protected:
		std::vector<uint8_t> m_data;

	public:
		EqdpFile()
			: m_data(sizeof EqdpHeader) {}

		EqdpFile(std::vector<uint8_t> data)
			: m_data(std::move(data)) {}

		EqdpFile(const IStream& stream)
			: m_data(ReadStreamIntoVector<uint8_t>(stream, 0)) {}

		EqdpFile(EqdpFile&& file)
			: m_data(std::move(file.m_data)) {
			file.m_data.resize(sizeof EqdpHeader);
		}

		EqdpFile(const EqdpFile& file)
			: m_data(file.m_data) {}

		EqdpFile& operator=(EqdpFile&& file) {
			m_data = std::move(file.m_data);
			file.m_data.resize(sizeof EqdpHeader);
			return *this;
		}

		EqdpFile& operator=(const EqdpFile& file) {
			m_data = file.m_data;
		}

		const std::vector<uint8_t>& Data() const {
			return m_data;
		}

		EqdpHeader& Header() {
			return *reinterpret_cast<EqdpHeader*>(m_data.data());
		}

		const EqdpHeader& Header() const {
			return *reinterpret_cast<const EqdpHeader*>(m_data.data());
		}

		std::span<uint16_t> Indices() {
			return { reinterpret_cast<uint16_t*>(m_data.data() + sizeof EqdpHeader), Header().BlockCount };
		}

		std::span<const uint16_t> Indices() const {
			return { reinterpret_cast<const uint16_t*>(m_data.data() + sizeof EqdpHeader), Header().BlockCount };
		}

		size_t BaseOffset() const {
			return sizeof EqdpHeader + Indices().size_bytes();
		}

		std::span<uint16_t> Body() {
			return { reinterpret_cast<uint16_t*>(m_data.data() + BaseOffset()), static_cast<size_t>(Header().BlockCount * Header().BlockMemberCount) };
		}

		std::span<const uint16_t> Body() const {
			return { reinterpret_cast<const uint16_t*>(m_data.data() + BaseOffset()), static_cast<size_t>(Header().BlockCount * Header().BlockMemberCount) };
		}

		std::span<uint16_t> Block(size_t blockId) {
			const auto index = Indices()[blockId];
			if (index == UINT16_MAX)
				return {};

			return Body().subspan(index, Header().BlockMemberCount);
		}

		std::span<const uint16_t> Block(size_t blockId) const {
			const auto index = Indices()[blockId];
			if (index == UINT16_MAX)
				return {};

			return Body().subspan(index, Header().BlockMemberCount);
		}

		uint16_t& Set(size_t setId) {
			const auto b = Block(setId / Header().BlockMemberCount);
			if (b.empty())
				throw std::runtime_error("Must be expanded before accessing set for modification.");
			return b[setId % Header().BlockMemberCount];
		}

		uint16_t Set(size_t setId) const {
			return GetSet(setId);
		}

		uint16_t GetSet(size_t setId) const {
			const auto b = Block(setId / Header().BlockMemberCount);
			if (b.empty())
				return 0;
			return b[setId % Header().BlockMemberCount];
		}

		void ExpandCollapse(bool expand) {
			const auto baseOffset = BaseOffset();
			const auto& header = Header();
			const auto& body = Body();
			const auto indices = Indices();

			std::vector<uint8_t> newData;
			newData.resize(baseOffset + sizeof uint16_t * header.BlockCount * header.BlockMemberCount);
			*reinterpret_cast<EqdpHeader*>(&newData[0]) = header;
			const auto newIndices = Internal::span_cast<uint16_t>(newData, sizeof header, header.BlockCount);
			const auto newBody = Internal::span_cast<uint16_t>(newData, baseOffset, size_t{ 1 } *header.BlockCount * header.BlockMemberCount);
			uint16_t newBodyIndex = 0;

			for (size_t i = 0; i < indices.size(); ++i) {
				if (expand) {
					newIndices[i] = newBodyIndex;
					newBodyIndex += header.BlockMemberCount;
					if (indices[i] == UINT16_MAX)
						continue;
					std::copy_n(&body[indices[i]], header.BlockMemberCount, &newBody[newIndices[i]]);

				} else {
					auto isAllZeros = true;
					for (size_t j = indices[i], j_ = j + header.BlockMemberCount; isAllZeros && j < j_; j++) {
						isAllZeros = body[j] == 0;
					}
					if (isAllZeros) {
						newIndices[i] = UINT16_MAX;
					} else {
						newIndices[i] = newBodyIndex;
						newBodyIndex += header.BlockMemberCount;
						if (indices[i] == UINT16_MAX)
							continue;
						std::copy_n(&body[indices[i]], header.BlockMemberCount, &newBody[newIndices[i]]);
					}
				}
			}
			newData.resize(XivRes::Align<size_t>(baseOffset + newBodyIndex * sizeof uint16_t, 512).Alloc);
			m_data.swap(newData);
		}
	};
}

#endif
