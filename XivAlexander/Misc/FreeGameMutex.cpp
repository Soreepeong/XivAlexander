#include "pch.h"
#include "Misc/FreeGameMutex.h"

#include "Utils/Win32/Handle.h"

#include "Config.h"
#include "Misc/Logger.h"
#include "resource.h"

namespace {
	bool NtSuccess(NTSTATUS x) {
		return x >= 0;
	}

	constexpr auto STATUS_INFO_LENGTH_MISMATCH = static_cast<NTSTATUS>(0xc0000004);

	enum class NtSystemInformationClass : ULONG {
		SystemHandleInformation = 16
	};

	enum class NtProcessInformationClass : ULONG {
		ProcessHandleInformation = 51
	};

	enum class NtObjectInformationClass : ULONG {
		ObjectBasicInformation [[maybe_unused]] = 0,
		ObjectNameInformation [[maybe_unused]] = 1,
		ObjectTypeInformation [[maybe_unused]] = 2,
	};

	struct SYSTEM_HANDLE {
		[[maybe_unused]] ULONG ProcessId;
		[[maybe_unused]] BYTE ObjectTypeNumber;
		[[maybe_unused]] BYTE Flags;
		[[maybe_unused]] USHORT Handle;
		[[maybe_unused]] PVOID Object;
		[[maybe_unused]] ACCESS_MASK GrantedAccess;
	};

	struct SYSTEM_HANDLE_INFORMATION {
		ULONG HandleCount;
		SYSTEM_HANDLE Handles[1];
	};

	struct PROCESS_HANDLE_TABLE_ENTRY_INFO {
		[[maybe_unused]] HANDLE HandleValue;
		[[maybe_unused]] ULONG_PTR HandleCount;
		[[maybe_unused]] ULONG_PTR PointerCount;
		[[maybe_unused]] ACCESS_MASK GrantedAccess;
		[[maybe_unused]] ULONG ObjectTypeIndex;
		[[maybe_unused]] ULONG HandleAttributes;
		[[maybe_unused]] ULONG Reserved;
	};

	struct PROCESS_HANDLE_SNAPSHOT_INFORMATION {
		[[maybe_unused]] ULONG_PTR NumberOfHandles;
		[[maybe_unused]] ULONG_PTR Reserved;
		[[maybe_unused]] PROCESS_HANDLE_TABLE_ENTRY_INFO Handles[1];
	};

	struct LocalHandle {
		HANDLE Handle;
		ULONG ObjectTypeNumber;
		ACCESS_MASK GrantedAccess;
	};

	std::optional<std::vector<LocalHandle>> EnumerateLocalHandlesFromProcess() {
		const auto NtQueryInformationProcess = Utils::Win32::LoadedModule(GetModuleHandleW(L"ntdll.dll"), false)
			.GetProcAddress<NTSTATUS (NTAPI *)(
				HANDLE ProcessHandle,
				NtProcessInformationClass ProcessInformationClass,
				PVOID ProcessInformation,
				ULONG ProcessInformationLength,
				PULONG ReturnLength
			)>("NtQueryInformationProcess");
		if (!NtQueryInformationProcess)
			return std::nullopt;

		NTSTATUS status;
		std::vector<char> buffer(0x10000);
		ULONG returnLength = 0;
		while ((status = NtQueryInformationProcess(GetCurrentProcess(), NtProcessInformationClass::ProcessHandleInformation, &buffer[0], static_cast<ULONG>(buffer.size()), &returnLength)) == STATUS_INFO_LENGTH_MISMATCH)
			buffer.resize(std::max<size_t>(buffer.size() * 2, returnLength + 0x1000));
		if (!NtSuccess(status))
			return std::nullopt;

		std::vector<LocalHandle> result;
		const auto pInfo = reinterpret_cast<PROCESS_HANDLE_SNAPSHOT_INFORMATION*>(buffer.data());
		for (size_t i = 0; i < pInfo->NumberOfHandles; i++) {
			const auto& h = pInfo->Handles[i];
			result.emplace_back(h.HandleValue, h.ObjectTypeIndex, h.GrantedAccess);
		}
		return result;
	}

	std::vector<LocalHandle> EnumerateLocalHandlesFromSystem() {
		const auto NtQuerySystemInformation = Utils::Win32::LoadedModule(GetModuleHandleW(L"ntdll.dll"), false)
			.GetProcAddress<NTSTATUS(NTAPI *)(
				NtSystemInformationClass SystemInformationClass,
				PVOID SystemInformation,
				ULONG SystemInformationLength,
				PULONG ReturnLength
			)>("NtQuerySystemInformation");

		if (!NtQuerySystemInformation)
			throw std::runtime_error("ntdll.dll!NtQuerySystemInformation = null");

		NTSTATUS status;
		std::vector<LocalHandle> result;
		std::vector<char> handleInfoBuffer;
		handleInfoBuffer.resize(0x10000);

		while ((status = NtQuerySystemInformation(NtSystemInformationClass::SystemHandleInformation, &handleInfoBuffer[0], static_cast<ULONG>(handleInfoBuffer.size()), nullptr)) == STATUS_INFO_LENGTH_MISMATCH)
			handleInfoBuffer.resize(handleInfoBuffer.size() * 2);

		if (!NtSuccess(status))
			throw std::runtime_error(std::format("NtQuerySystemInformation(SystemHandleInformation) = {}", status));

		const auto pHandleInfo = reinterpret_cast<SYSTEM_HANDLE_INFORMATION*>(handleInfoBuffer.data());
		for (size_t i = 0; i < pHandleInfo->HandleCount; i++) {
			const auto& h = pHandleInfo->Handles[i];
			if (h.ProcessId == GetCurrentProcessId())
				result.emplace_back(reinterpret_cast<HANDLE>(static_cast<size_t>(h.Handle)), h.ObjectTypeNumber, h.GrantedAccess);
		}
		return result;
	}

	std::vector<LocalHandle> EnumerateLocalHandles() {
		if (auto result = EnumerateLocalHandlesFromProcess())
			return std::move(*result);
		return EnumerateLocalHandlesFromSystem();
	}

	std::wstring GetHandleObjectName(HANDLE hHandle) {
		const auto NtQueryObject = Utils::Win32::LoadedModule(GetModuleHandleW(L"ntdll.dll"), false)
			.GetProcAddress<NTSTATUS(NTAPI *)(
				HANDLE ObjectHandle,
				NtObjectInformationClass ObjectInformationClass,
				PVOID ObjectInformation,
				ULONG ObjectInformationLength,
				PULONG ReturnLength
			)>("NtQueryObject");

		if (!NtQueryObject)
			throw std::runtime_error("ntdll.dll!NtQueryObject = null");

		ULONG returnLength = 0;
		if (NtSuccess(NtQueryObject(hHandle, NtObjectInformationClass::ObjectNameInformation, nullptr, 0, &returnLength)))
			return L"";

		std::vector<char> objectNameInfo;
		objectNameInfo.resize(returnLength);
		if (const auto status = NtQueryObject(hHandle, NtObjectInformationClass::ObjectNameInformation, &objectNameInfo[0], returnLength, NULL); !NtSuccess(status))
			throw std::runtime_error(std::format("NtQueryObject({:p}, ObjectNameInformation) = {}", hHandle, status));

		const auto pObjectName = reinterpret_cast<UNICODE_STRING*>(objectNameInfo.data());
		if (pObjectName->Length)
			return pObjectName->Buffer;
		return L"";
	}
}

void XivAlexander::Misc::FreeGameMutex::FreeGameMutex() {
	// Create a mutex to figure out ObjectTypeNumber.
	const Utils::Win32::Handle hMutexTemp(CreateMutexW(nullptr, false, nullptr),
		Utils::Win32::Handle::Null,
		"App::Misc::FreeGameMutex::FreeGameMutex/CreateMutexW");
	const auto allHandles = EnumerateLocalHandles();

	std::vector<char> objectNameInfo;
	objectNameInfo.resize(0x1000);

	ULONG mutexTypeNumber = 0x11;
	for (const auto& handle : allHandles) {
		const auto hObject = handle.Handle;
		if (hObject == hMutexTemp) {
			mutexTypeNumber = handle.ObjectTypeNumber;
			break;
		}
	}

	const auto config = Config::Acquire();
	const auto logger = Logger::Acquire();

	for (const auto& handle : allHandles) {
		const auto hObject = handle.Handle;

		if (handle.ObjectTypeNumber != mutexTypeNumber)
			continue;

		if (handle.GrantedAccess == 0x0012019f
			|| handle.GrantedAccess == 0x001a019f
			|| handle.GrantedAccess == 0x00120189
			|| handle.GrantedAccess == 0x00100000)
			continue;

		try {
			const auto name = GetHandleObjectName(hObject);
			if (name.starts_with(L"\\BaseNamedObjects\\6AA83AB5-BAC4-4a36-9F66-A309770760CB")) {
				CloseHandle(hObject);
				logger->Format(
					LogCategory::General,
					"Freed game mutex {}.",
					xivres::util::unicode::convert<std::string>(name));
			}
		} catch (const std::exception& e) {
			logger->Format(
				LogCategory::General,
				config->Runtime.GetLangId(),
				IDS_ERROR_FREEGAMEMUTEX_CLOSEHANDLE,
				hObject, handle.ObjectTypeNumber, e.what());
		}
	}
}
