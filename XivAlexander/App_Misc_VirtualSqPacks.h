#pragma once

#include <XivAlexanderCommon/Sqex.h>
#include <XivAlexanderCommon/Sqex_Sqpack.h>
#include <XivAlexanderCommon/Sqex_ThirdParty_TexTools.h>
#include <XivAlexanderCommon/Utils_Win32_Handle.h>

#include "XivAlexanderCommon/Utils_ListenerManager.h"

namespace App::Misc {
	class VirtualSqPacks {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		VirtualSqPacks(std::filesystem::path sqpackPath);
		~VirtualSqPacks();

		HANDLE Open(const std::filesystem::path& path);
		bool Close(HANDLE handle);

		struct OverlayedHandleData {
			Utils::Win32::Event IdentifierHandle;
			std::filesystem::path Path;
			LARGE_INTEGER FilePointer;
			std::shared_ptr<Sqex::RandomAccessStream> Stream;
		};

		OverlayedHandleData* Get(HANDLE handle);

		bool EntryExists(const Sqex::Sqpack::EntryPathSpec& pathSpec) const;

		void MarkIoRequest();

		struct TtmpSet {
			Implementation* Impl = nullptr;

			bool Allocated = false;
			bool Enabled = false;
			std::filesystem::path ListPath;
			std::filesystem::path RenameTo;
			Sqex::ThirdParty::TexTools::TTMPL List;
			Utils::Win32::File DataFile;
			nlohmann::json Choices;

			void FixChoices();
			void ApplyChanges(bool announce = true);
		};

		std::vector<TtmpSet>& TtmpSets();

		void AddNewTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately = true);
		void DeleteTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately = true);
		void RescanTtmp();

		Utils::ListenerManager<Implementation, void> OnTtmpSetsChanged;
	};
}
