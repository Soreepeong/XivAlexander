#pragma once

#include "Sqex_Sqpack.h"
#include "Sqex_Sqpack_EntryProvider.h"
#include "Utils_ListenerManager.h"
#include "Utils_Win32_Handle.h"

namespace Sqex::Sqpack {
	class VirtualSqPack {
		struct Implementation;

		const uint64_t m_maxFileSize;

		SqpackHeader m_sqpackIndexHeader{};
		SqIndex::Header m_sqpackIndexSubHeader{};
		std::vector<char> m_sqpackIndexSegment2;
		std::vector<SqIndex::Segment3Entry> m_sqpackIndexSegment3;

		SqpackHeader m_sqpackIndex2Header{};
		SqIndex::Header m_sqpackIndex2SubHeader{};
		std::vector<char> m_sqpackIndex2Segment2;
		std::vector<SqIndex::Segment3Entry> m_sqpackIndex2Segment3;

		SqpackHeader m_sqpackDataHeader{};
		std::vector<SqData::Header> m_sqpackDataSubHeaders;
		
	public:
		const std::string DatExpac;
		const std::string DatName;

	private:
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		VirtualSqPack(std::string ex, std::string name, uint64_t maxFileSize = SqData::Header::MaxFileSize_MaxValue);
		~VirtualSqPack();

	private:
		Utils::Win32::File OpenFile(
			_In_opt_ std::filesystem::path curItemPath,
			_In_opt_ Utils::Win32::File alreadyOpenedFile = {});

	public:
		Utils::ListenerManager<Implementation, void, const std::string&> Log;

		struct AddEntryResult {
			std::vector<EntryProvider*> Added;
			std::vector<EntryProvider*> Replaced;
			std::vector<EntryProvider*> SkippedExisting;

			AddEntryResult& operator+=(const AddEntryResult& r);
			[[nodiscard]] _Maybenull_ EntryProvider* AnyItem() const;
			[[nodiscard]] std::vector<EntryProvider*> AllEntries() const;
		};
		AddEntryResult AddEntriesFromSqPack(const std::filesystem::path& indexPath, bool overwriteExisting = true, bool overwriteUnknownSegments = false);
		AddEntryResult AddEntryFromFile(EntryPathSpec pathSpec, const std::filesystem::path& path, bool overwriteExisting = true);
		AddEntryResult AddEntriesFromTTMP(const std::filesystem::path& extractedDir, bool overwriteExisting = true);

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
