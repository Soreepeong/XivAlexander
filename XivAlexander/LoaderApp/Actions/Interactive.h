#pragma once

#include "LoaderApp/Arguments.h"

namespace XivAlexander::LoaderApp::Actions {
	/// The loader's window; it stays unelevated, and only the operations that need it elevate a child.
	class Interactive {
		const Arguments& m_args;

	public:
		Interactive(const Arguments& args);

		int Run();
	};
}
