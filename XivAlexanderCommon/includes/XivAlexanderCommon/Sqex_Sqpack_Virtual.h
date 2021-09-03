#pragma once

#include "Utils_Win32_Handle.h"

namespace Sqex::Sqpack {

	class VirtualSqPack {
		struct Implementation;

		const std::unique_ptr<Implementation> m_pImpl;

	public:
		VirtualSqPack();
		~VirtualSqPack();

		struct AddEntryResult {
			size_t AddedCount;
			size_t ReplacedCount;
			size_t SkippedExistCount;

			AddEntryResult& operator+=(const AddEntryResult& r);
		};
		AddEntryResult AddEntriesFromSqPack(const std::filesystem::path& indexPath, bool overwriteExisting = true, bool overwriteUnknownSegments = false);
		AddEntryResult AddEntryFromFile(uint32_t PathHash, uint32_t NameHash, uint32_t FullPathHash, const std::filesystem::path& path, bool overwriteExisting = true);

		[[nodiscard]] size_t NumOfDataFiles() const;

		void Freeze(bool strict);

		size_t ReadIndex1(uint64_t offset, void* buf, size_t length) const;
		size_t ReadIndex2(uint64_t offset, void* buf, size_t length) const;
		size_t ReadData(uint32_t datIndex, uint64_t offset, void* buf, size_t length) const;

		[[nodiscard]] uint64_t SizeIndex1() const;
		[[nodiscard]] uint64_t SizeIndex2() const;
		[[nodiscard]] uint64_t SizeData(uint32_t datIndex) const;
	};

}
