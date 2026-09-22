#include "pch.h"
#include "Config.h"

#include "Utils/Win32/Process.h"
#include "XivAlexander.h"

std::weak_ptr<XivAlexander::Config> XivAlexander::Config::s_instance;

class XivAlexander::Config::ConfigCreator : public Config {
public:
	ConfigCreator(std::filesystem::path initializationConfigPath)
		: Config(std::move(initializationConfigPath)) {}

	~ConfigCreator() override = default;
};

std::shared_ptr<XivAlexander::Config> XivAlexander::Config::Acquire() {
	auto r = s_instance.lock();
	if (!r) {
		static std::mutex mtx;
		std::lock_guard lock(mtx);

		r = s_instance.lock();
		if (!r) {
			const auto dllDir = Dll::Module().PathOf().parent_path();
			s_instance = r = std::make_shared<ConfigCreator>(dllDir / "config.xivalexinit.json");
		}
	}
	return r;
}

std::filesystem::path XivAlexander::Config::TranslatePath(const std::filesystem::path& path, const std::filesystem::path& relativeTo) {
	return Utils::Win32::TranslatePath(path, relativeTo.empty() ? Dll::Module().PathOf().parent_path() : relativeTo);
}

XivAlexander::Config::Config(std::filesystem::path initializationConfigPath)
	: Init(this, std::move(initializationConfigPath), "")
	, Runtime(this, Init.ResolveRuntimeConfigPath(), xivres::util::unicode::convert<std::string>(Utils::Win32::Process::Current().PathOf().wstring()))
	, Game(this, Init.ResolveGameOpcodeConfigPath(), "") {
	Runtime.Reload();
	Game.Reload();
}

XivAlexander::Config::~Config() = default;


void XivAlexander::Config::Reload() {
	Init.Reload();
	Runtime.Reload();
	Game.Reload();
}

