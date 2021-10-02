#pragma once

namespace App::Misc {
	class VirtualSqPacks;
}

namespace App::Feature::GameResourceOverrider {
	struct Implementation;
	extern std::shared_ptr<Implementation> s_pImpl;

	void Enable();

	[[nodiscard]] bool Enabled();

	[[nodiscard]] Misc::VirtualSqPacks* GetVirtualSqPacks();
}
