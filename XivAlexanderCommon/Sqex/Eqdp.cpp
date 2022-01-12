#include "pch.h"

#include "XivAlexanderCommon/Sqex/Eqdp.h"

std::vector<uint8_t> Sqex::Eqdp::ExpandCollapse(const File* file, bool expand) {
	const auto baseOffset = file->BaseOffset();
	const auto& header = file->Header();
	const auto& body = file->Body();
	const auto indices = file->Indices();

	std::vector<uint8_t> newData;
	newData.resize(baseOffset + sizeof uint16_t * header.BlockCount * header.BlockMemberCount);
	*reinterpret_cast<Header*>(&newData[0]) = header;
	const auto newIndices = std::span(reinterpret_cast<uint16_t*>(&newData[sizeof header]), header.BlockCount);
	const auto newBody = std::span(reinterpret_cast<uint16_t*>(&newData[baseOffset]), size_t{ 1 } * header.BlockCount * header.BlockMemberCount);
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
	newData.resize(Sqex::Align<size_t>(baseOffset + newBodyIndex * sizeof uint16_t, 512).Alloc);
	return newData;
}
