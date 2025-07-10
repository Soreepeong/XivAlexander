#pragma once

#include <XivAlexanderCommon/Sqex.h>
#include <XivAlexanderCommon/Sqex/Sqpack.h>
#include <XivAlexanderCommon/Sqex/ThirdParty/TexTools.h>
#include <XivAlexanderCommon/Utils/Win32/Handle.h>

#include "XivAlexanderCommon/Utils/ListenerManager.h"

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Window {
	class ProgressPopupWindow;
}

namespace XivAlexander::Apps::MainApp::Internal {
	class VirtualSqPacks {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		VirtualSqPacks(Apps::MainApp::App& App, std::filesystem::path sqpackPath);
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
		std::shared_ptr<Sqex::RandomAccessStream> GetOriginalEntry(const Sqex::Sqpack::EntryPathSpec& pathSpec) const;

		void MarkIoRequest();

		struct TtmpGroupChoices {
			std::string GroupName;
			std::vector<bool> Options;
		};

		struct TtmpPageChoices {
			std::vector<TtmpGroupChoices> Groups;
		};

		struct TtmpChoices {
			bool Enabled;
			std::string ModName;
		};

		struct TtmpSet {
			bool Allocated = false;
			std::filesystem::path ListPath;
			Sqex::ThirdParty::TexTools::TTMPL List;
			Utils::Win32::Handle DataFile;
			nlohmann::json Choices;

			void FixChoices();

			using TraverseCallbackResult = Sqex::ThirdParty::TexTools::TTMPL::TraverseCallbackResult;

			void ForEachEntry(bool choiceOnly, std::function<void(const Sqex::ThirdParty::TexTools::ModEntry&)> cb) const;
			TraverseCallbackResult ForEachEntryInterruptible(bool choiceOnly, std::function<TraverseCallbackResult(const Sqex::ThirdParty::TexTools::ModEntry&)> cb) const;

			void TryCleanupUnusedFiles();
		};

		struct NestedTtmp {
			uint64_t Index{ UINT64_MAX };
			std::filesystem::path Path;
			std::shared_ptr<NestedTtmp> Parent;
			bool Enabled = true;

			std::optional<std::vector<std::shared_ptr<NestedTtmp>>> Children;
			std::optional<TtmpSet> Ttmp;
			std::optional<std::filesystem::path> RenameTo;

			bool IsGroup() const {
				return Children.has_value();
			}

			enum TraverseCallbackResult {
				Continue,
				Break,
				Delete,
			};

			void Traverse(bool traverseEnabledOnly, const std::function<void(NestedTtmp&)>& cb);
			void Traverse(bool traverseEnabledOnly, const std::function<void(const NestedTtmp&)>& cb) const;
			TraverseCallbackResult TraverseInterruptible(bool traverseEnabledOnly, const std::function<TraverseCallbackResult(NestedTtmp&)>& cb);
			TraverseCallbackResult TraverseInterruptible(bool traverseEnabledOnly, const std::function<TraverseCallbackResult(const NestedTtmp&)>& cb) const;
			size_t Count() const;
			void Sort();
			void RemoveEmptyChildren();
			std::shared_ptr<NestedTtmp> Find(const std::filesystem::path& path);
		};

		std::shared_ptr<NestedTtmp> GetTtmps() const;

		void AddNewTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately, Apps::MainApp::Window::ProgressPopupWindow& progressWindow);
		void DeleteTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately = true);
		void RescanTtmp(Apps::MainApp::Window::ProgressPopupWindow& progressWindow);
		void ApplyTtmpChanges(NestedTtmp& nestedTtmp, bool announce = true);

		Utils::ListenerManager<Implementation, void> OnTtmpSetsChanged;
	};
}
