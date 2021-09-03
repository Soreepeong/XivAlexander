#include "pch.h"
#include "Utils_Win32_InjectedModule.h"

#include "Utils_Win32_Process.h"

Utils::Win32::InjectedModule::InjectedModule()
	: m_rpModule(nullptr)
	, m_path()
	, m_hProcess() {
}

Utils::Win32::InjectedModule::InjectedModule(Process hProcess, std::filesystem::path path)
	: m_rpModule(hProcess.LoadModule(path))
	, m_path(std::move(path))
	, m_hProcess(std::move(hProcess)) {
}

Utils::Win32::InjectedModule::InjectedModule(const InjectedModule& r)
	: InjectedModule(r.m_hProcess, r.m_path) {
}

Utils::Win32::InjectedModule::InjectedModule(InjectedModule&& r) noexcept
	: m_rpModule(r.m_rpModule)
	, m_path(std::move(r.m_path))
	, m_hProcess(std::move(r.m_hProcess)) {
	r.m_rpModule = nullptr;
}

Utils::Win32::InjectedModule& Utils::Win32::InjectedModule::operator=(const InjectedModule& r) {
	if (&r == this)
		return *this;

	Clear();

	m_rpModule = r.m_hProcess ? r.m_hProcess.LoadModule(m_path) : nullptr;
	m_path = r.m_path;
	m_hProcess = r.m_hProcess;
	return *this;
}

Utils::Win32::InjectedModule& Utils::Win32::InjectedModule::operator=(InjectedModule&& r) noexcept {
	if (&r == this)
		return *this;

	m_path = std::move(r.m_path);
	m_hProcess = std::move(r.m_hProcess);
	m_rpModule = r.m_rpModule;
	r.m_rpModule = nullptr;
	return *this;
}

Utils::Win32::InjectedModule::~InjectedModule() {
	Clear();
}

int Utils::Win32::InjectedModule::Call(void* rpfn, void* rpParam, const char* pcszDescription) const {
	return m_hProcess.CallRemoteFunction(rpfn, rpParam, pcszDescription);
}

int Utils::Win32::InjectedModule::Call(const char* name, void* rpParam, const char* pcszDescription) const {
	auto addr = m_hProcess.FindExportedFunction(m_rpModule, name, 0, false);
	if (!addr)
		addr = m_hProcess.FindExportedFunction(m_rpModule, std::format("{}@{}", name, sizeof size_t).c_str(), 0, false);
	if (!addr)
		addr = m_hProcess.FindExportedFunction(m_rpModule, std::format("_{}@{}", name, sizeof size_t).c_str(), 0, false);
	if (!addr)
		throw std::out_of_range("function not found");
	return Call(addr, rpParam, pcszDescription);
}

void Utils::Win32::InjectedModule::Clear() {
	if (m_hProcess && m_rpModule) {
		try {
			try {
				Call("CallFreeLibrary", m_rpModule, "CallFreeLibrary");
			} catch (std::out_of_range&) {
				m_hProcess.UnloadModule(m_rpModule);
			}
		} catch (std::exception&) {

			// suppress error if the process is already dead
			if (WaitForSingleObject(m_hProcess, 0) == WAIT_TIMEOUT)
				throw;
		}
	}
	m_path.clear();
	m_hProcess = nullptr;
	m_rpModule = nullptr;
}
