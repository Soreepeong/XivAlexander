#pragma once

#include <memory>

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class PathRewriter;

	class SqpackLookupHooks {
	public:
		explicit SqpackLookupHooks(const PathRewriter& rewriter);
		~SqpackLookupHooks();

	private:
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;
	};
}
