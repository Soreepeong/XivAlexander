#pragma once

namespace App::Misc {
	class CrashMessageBoxHandler {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		CrashMessageBoxHandler();
		~CrashMessageBoxHandler();
	};
}
