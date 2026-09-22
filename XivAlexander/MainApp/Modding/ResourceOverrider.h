#pragma once

#include <xivres/util.on_dtor.h>

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class VirtualSqPacks;

	class ResourceOverrider {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		ResourceOverrider(App& app);
		~ResourceOverrider();

		[[nodiscard]] std::optional<VirtualSqPacks>& GetVirtualSqPacks();

		[[nodiscard]] xivres::util::on_dtor OnVirtualSqPacksInitialized(std::function<void()>);
	};
}
