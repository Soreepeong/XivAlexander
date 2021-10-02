#pragma once

namespace App::Misc {
	class VirtualSqPacks;
}

namespace App::Feature {
	class GameResourceOverrider {
		struct Implementation;
		const std::shared_ptr<Implementation> m_pImpl;
		static std::weak_ptr<Implementation> s_pImpl;

		static std::shared_ptr<Implementation> AcquireImplementation();

	public:
		GameResourceOverrider();
		~GameResourceOverrider();

		[[nodiscard]] bool CanUnload() const;

		[[nodiscard]] Misc::VirtualSqPacks* GetVirtualSqPacks() const;
	};
}
