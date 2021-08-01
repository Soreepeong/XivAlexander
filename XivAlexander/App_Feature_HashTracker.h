#pragma once

namespace App::Feature {
	class HashTracker {
		class Implementation;
		std::shared_ptr<Implementation> m_pImpl;
		static std::weak_ptr<Implementation> s_pImpl;

	public:
		HashTracker();
		~HashTracker();
	};
}
