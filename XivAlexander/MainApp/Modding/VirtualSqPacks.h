#pragma once

#include <mutex>

#include <xivres/stream.h>
#include <xivres/sqpack.h>
#include <xivres/textools.h>
#include "MainApp/Modding/NestedTtmp.h"

#include <xivres/util.listener_manager.h>

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Window {
	class ProgressPopupWindow;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class SqpackRebuildLock;

	class VirtualSqPacks {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		VirtualSqPacks(App& App, std::filesystem::path sqpackPath, SqpackRebuildLock& ioGate);
		~VirtualSqPacks();

		std::shared_ptr<xivres::stream> OpenStream(const std::filesystem::path& path);

		bool EntryExists(const xivres::path_spec& pathSpec) const;
		[[nodiscard]] std::string DescribeEntrySource(const xivres::path_spec& pathSpec) const;
		std::shared_ptr<xivres::stream> GetOriginalEntry(const xivres::path_spec& pathSpec) const;
		std::string FindFutureReservationFor(const xivres::path_spec& pathSpec) const;

		std::string ReserveCrossSqpack(const xivres::path_spec& requested, const xivres::path_spec& target) const;

		std::shared_ptr<NestedTtmp> GetTtmps() const;

		[[nodiscard]] std::unique_lock<std::recursive_mutex> LockTtmps() const;

		void AddNewTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately, Window::ProgressPopupWindow& progressWindow);
		void DeleteTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately = true);
		void RescanTtmp(Window::ProgressPopupWindow& progressWindow);
		void ApplyTtmpChanges(NestedTtmp& nestedTtmp, bool announce = true);

		xivres::util::listener_manager<Implementation, void> OnTtmpSetsChanged;
	};
}
