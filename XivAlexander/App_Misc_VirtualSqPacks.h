#pragma once

#include <XivAlexanderCommon/Sqex.h>
#include <XivAlexanderCommon/Sqex_Sqpack.h>
#include <XivAlexanderCommon/Utils_Win32_Handle.h>

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
	};
}
