#pragma once

#include "Sqex_Sqpack.h"
#include "Sqex_Sqpack_EntryProvider.h"
#include "Utils_ListenerManager.h"
#include "Utils_Win32_Handle.h"

namespace Sqex::ThirdParty::TexTools {
	struct TTMPL;
}

namespace Sqex::Sqpack {
	class Creator {
		const uint64_t m_maxFileSize;
		
	public:
		const std::string DatExpac;
		const std::string DatName;

	private:
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		Creator(std::string ex, std::string name, uint64_t maxFileSize = SqData::Header::MaxFileSize_MaxValue);
		~Creator();

		ListenerManager<Implementation, void, const std::string&> Log;

		struct AddEntryResult {
			std::vector<EntryProvider*> Added;
			std::vector<EntryProvider*> Replaced;
			std::vector<EntryProvider*> SkippedExisting;
			std::map<EntryPathSpec, std::string> Error;

			AddEntryResult& operator+=(const AddEntryResult& r);
			AddEntryResult& operator+=(AddEntryResult&& r);
			[[nodiscard]] _Maybenull_ EntryProvider* AnyItem() const;
			[[nodiscard]] std::vector<EntryProvider*> AllSuccessfulEntries() const;
		};
		AddEntryResult AddEntriesFromSqPack(const std::filesystem::path& indexPath, bool overwriteExisting = true, bool overwriteUnknownSegments = false);
		AddEntryResult AddEntryFromFile(EntryPathSpec pathSpec, const std::filesystem::path& path, bool overwriteExisting = true);
		AddEntryResult AddEntriesFromTTMP(const std::filesystem::path& extractedDir, bool overwriteExisting = true);
		AddEntryResult AddEntriesFromTTMP(const ThirdParty::TexTools::TTMPL& ttmpl, const Win32::Handle& ttmpd, const nlohmann::json& choices, bool overwriteExisting = true);
		void ReserveSpacesFromTTMP(const ThirdParty::TexTools::TTMPL& ttmpl);
		AddEntryResult AddEntry(std::shared_ptr<EntryProvider> provider, bool overwriteExisting = true);
		void ReserveSwappableSpace(EntryPathSpec pathSpec, uint32_t size);
		
	private:
		template<SqIndex::Header::IndexType IndexType, typename FileEntryType, bool UseFolders>
		class IndexViewBase;
		template<SqIndex::Header::IndexType IndexType>
		class IndexView;
		class DataView;

	public:
		struct SqpackViews {
			std::shared_ptr<RandomAccessStream> Index;
			std::shared_ptr<RandomAccessStream> Index2;
			std::vector<std::shared_ptr<RandomAccessStream>> Data;
			std::map<EntryPathSpec, uint64_t> EntryOffsets;
			std::map<EntryPathSpec, EntryProvider*> EntryProviders;
		};
		SqpackViews AsViews(bool strict);

		std::shared_ptr<RandomAccessStream> operator[](const EntryPathSpec& pathSpec) const;
		std::vector<EntryPathSpec> AllPathSpec() const;
	};

}
