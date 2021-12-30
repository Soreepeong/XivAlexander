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

		static VirtualSqPacks* Instance();

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
		std::shared_ptr<Sqex::RandomAccessStream> GetOriginalEntry(const Sqex::Sqpack::EntryPathSpec& pathSpec) const;

		void MarkIoRequest();

		struct TtmpSet {
			Implementation* Impl = nullptr;

			bool Allocated = false;
			bool Enabled = false;
			std::filesystem::path ListPath;
			std::filesystem::path RenameTo;
			Sqex::ThirdParty::TexTools::TTMPL List;
			Utils::Win32::Handle DataFile;
			nlohmann::json Choices;

			void FixChoices();
			void ApplyChanges(bool announce = true);

			using TraverseCallbackResult = Sqex::ThirdParty::TexTools::TTMPL::TraverseCallbackResult;

			TraverseCallbackResult ForEachEntry(bool choiceOnly, std::function<TraverseCallbackResult(const Sqex::ThirdParty::TexTools::ModEntry&)> cb) const;

			void TryCleanupUnusedFiles();
		};

		struct NestedTtmp {
			uint64_t Index{ UINT64_MAX };
			std::filesystem::path Path;
			std::shared_ptr<NestedTtmp> Parent;
			std::optional<std::vector<std::shared_ptr<NestedTtmp>>> Children;
			std::optional<TtmpSet> Ttmp;

			bool IsGroup() const {
				return Children.has_value();
			}

			enum TraverseCallbackResult {
				Continue,
				Break,
				Delete,
			};

			TraverseCallbackResult Traverse(const std::function<TraverseCallbackResult(NestedTtmp&)>& cb);
			TraverseCallbackResult Traverse(const std::function<TraverseCallbackResult(const NestedTtmp&)>& cb) const;
			size_t Count() const;
			bool IsAnythingEnabled() const;
			void Sort();
			void RemoveEmptyChildren();
			std::shared_ptr<NestedTtmp> Find(const std::filesystem::path& path);
		};

		std::shared_ptr<NestedTtmp> GetTtmps() const;

		void AddNewTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately = true);
		void DeleteTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately = true);
		void RescanTtmp();

		Utils::ListenerManager<Implementation, void> OnTtmpSetsChanged;
	};
}
