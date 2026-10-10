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

std::filesystem::path XivAlexander::Config::TranslateDirectoryPath(const std::filesystem::path& path, const std::filesystem::path& sqpackPath) {
	const auto str = path.wstring();
	const auto startsWith = [&str](std::wstring_view token) {
		return str.size() >= token.size() && _wcsnicmp(str.c_str(), token.data(), token.size()) == 0;
	};
	const auto rest = [&str](std::wstring_view token) {
		return std::filesystem::path(std::wstring_view(str).substr(token.size())).relative_path();
	};
	if (startsWith(ConfigDirectoryToken))
		return (Init.ResolveConfigStorageDirectoryPath() / rest(ConfigDirectoryToken)).lexically_normal();
	if (startsWith(SqpackDirectoryToken)) {
		const auto base = sqpackPath.empty() ? Utils::Win32::Process::Current().PathOf().parent_path() / L"sqpack" : sqpackPath;
		return (base / rest(SqpackDirectoryToken)).lexically_normal();
	}
	return TranslatePath(path);
}

XivAlexander::Config::Config(std::filesystem::path initializationConfigPath)
	: Init(this, std::move(initializationConfigPath), "")
	, Runtime(this, Init.ResolveRuntimeConfigPath(), xivres::util::unicode::convert<std::string>(Utils::Win32::Process::Current().PathOf().wstring()))
	, Game(this, Init.ResolveGameOpcodeConfigPath(), "")
	, PatchCode(Init.ResolvePatchCodeDirectoryPath()) {
	Runtime.Reload();
	Game.Reload();
	PatchCode.Reload();
}

XivAlexander::Config::~Config() = default;


void XivAlexander::Config::Reload() {
	Init.Reload();
	Runtime.Reload();
	Game.Reload();
	PatchCode.Reload();
}

