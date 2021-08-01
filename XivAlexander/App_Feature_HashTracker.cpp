#include "pch.h"
#include "App_Feature_HashTracker.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_Hooks.h"

std::weak_ptr<App::Feature::HashTracker::Implementation> App::Feature::HashTracker::s_pImpl;

class App::Feature::HashTracker::Implementation {
public:
	const std::shared_ptr<Config> m_config;
	const std::shared_ptr<Misc::Logger> m_logger;
	const std::shared_ptr<Misc::DebuggerDetectionDisabler> m_debugger;
	std::vector<std::unique_ptr<Misc::Hooks::PointerFunction<uint32_t, uint32_t, const char*, size_t>>> fns;
	std::set<std::string> m_alreadyLogged;
	Utils::CallOnDestruction::Multiple m_cleanup;

	Implementation()
		: m_config(Config::Acquire())
		, m_logger(Misc::Logger::Acquire())
		, m_debugger(Misc::DebuggerDetectionDisabler::Acquire()) {
		for (auto ptr : Misc::Signatures::LookupForData([](const IMAGE_SECTION_HEADER& p) {
			return strncmp(reinterpret_cast<const char*>(p.Name), ".text", 5) == 0;
			},
			"\x40\x57\x48\x8d\x3d\x00\x00\x00\x00\x00\x8b\xd8\x4c\x8b\xd2\xf7\xd1\x00\x85\xc0\x74\x25\x41\xf6\xc2\x03\x74\x1f\x41\x0f\xb6\x12\x8b\xc1",
				"\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF",
				34,
			{}
				)) {
			fns.emplace_back(std::make_unique<Misc::Hooks::PointerFunction<uint32_t, uint32_t, const char*, size_t>>(
				"FFXIV::GeneralHashCalcFn",
				static_cast<uint32_t(__stdcall *)(uint32_t, const char*, size_t)>(ptr)
				));
			m_cleanup += fns.back()->SetHook([this, ptr, self = fns.back().get()](uint32_t initVal, const char* str, size_t len) {
				if (!str || !*str || !m_config->Runtime.UseHashTracker)
					return self->bridge(initVal, str, len);
				auto name = std::string(str);
				std::string ext, rest;
				if (const auto i1 = name.find_first_of('.'); i1 != std::string::npos) {
					ext = name.substr(i1);
					name.resize(i1);
					if (const auto i2 = ext.find_first_of('.', 1); i2 != std::string::npos) {
						rest = ext.substr(i2);
						ext.resize(i2);
					}
				}

				if (m_config->Runtime.HashTrackerLanguageOverride != Config::GameLanguage::Unspecified) {
					auto appendLanguageCode = false;
					if (name.ends_with("_ja")
						|| name.ends_with("_en")
						|| name.ends_with("_de")
						|| name.ends_with("_fr")
						|| name.ends_with("_ko")) {
						name = name.substr(0, name.size() - 3);
						appendLanguageCode = true;
					} else if (name.ends_with("_chs")
						|| name.ends_with("_cht")) {
						name = name.substr(0, name.size() - 4);
						appendLanguageCode = true;
					}
					if (appendLanguageCode) {
						switch (m_config->Runtime.HashTrackerLanguageOverride) {
						case Config::GameLanguage::Japanese:
							name += "_ja";
							break;
						case Config::GameLanguage::English:
							name += "_en";
							break;
						case Config::GameLanguage::German:
							name += "_de";
							break;
						case Config::GameLanguage::French:
							name += "_fr";
							break;
						case Config::GameLanguage::ChineseSimplified:
							name += "_chs";
							break;
						case Config::GameLanguage::ChineseTraditional:
							name += "_cht";
							break;
						case Config::GameLanguage::Korean:
							name += "_ko";
							break;
						}

						const auto newStr = name + ext + rest;
						Utils::Win32::Process::Current().WriteMemory(const_cast<char*>(str), newStr.c_str(), newStr.size() + 1, true);

						if (!m_config->Runtime.UseHashTrackerKeyLogging) {
							m_logger->Format(LogCategory::HashTracker, "{:x}: Loading {} instead", reinterpret_cast<size_t>(ptr), str);
						}
					}
				}
				const auto res = self->bridge(initVal, str, len);
				
				if (m_config->Runtime.UseHashTrackerKeyLogging) {
					if (m_alreadyLogged.find(name) == m_alreadyLogged.end()) {
						m_alreadyLogged.emplace(name);
						m_logger->Format(LogCategory::HashTracker, "{:x}: {},{},{} => {:08x}", reinterpret_cast<size_t>(ptr), name, ext, rest, res);
					}
				} else
					m_alreadyLogged.clear();

				return res;
			});
		}
	}

	~Implementation() = default;
};

App::Feature::HashTracker::HashTracker() {
	static std::mutex mtx;
	m_pImpl = s_pImpl.lock();
	if (!m_pImpl) {
		std::lock_guard lock(mtx);
		m_pImpl = s_pImpl.lock();
		if (!m_pImpl)
			s_pImpl = m_pImpl = std::make_unique<Implementation>();
	}
}

App::Feature::HashTracker::~HashTracker() = default;
