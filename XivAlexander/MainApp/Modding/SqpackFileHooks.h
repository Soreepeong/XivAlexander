#pragma once

#include <filesystem>
#include <functional>
#include <memory>

#include <xivres/stream.h>

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class SqpackRebuildLock;

	class SqpackFileHooks {
	public:
		using Opener = std::function<std::shared_ptr<xivres::stream>(const std::filesystem::path& path)>;

		SqpackFileHooks(SqpackRebuildLock& ioGate, Opener opener);
		~SqpackFileHooks();

	private:
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;
	};
}
