#pragma once

#include <XivAlexanderCommon/Utils/CallOnDestruction.h>

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Internal {
	class VirtualSqPacks;

	class GameResourceOverrider {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		GameResourceOverrider(Apps::MainApp::App& app);
		~GameResourceOverrider();

		[[nodiscard]] std::optional<Internal::VirtualSqPacks>& GetVirtualSqPacks();

		[[nodiscard]] Utils::CallOnDestruction OnVirtualSqPacksInitialized(std::function<void()>);
	};
}
