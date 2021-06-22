#include "pch.h"
#include "App_Misc_FreeGameMutex.h"

static bool NtSuccess(NTSTATUS x) {
	return x >= 0;
}
static constexpr auto STATUS_INFO_LENGTH_MISMATCH = static_cast<NTSTATUS>(0xc0000004);

enum class NtSystemInformationClass : ULONG {
	SystemHandleInformation = 16
};
enum class NtObjectInformationClass : ULONG {
	ObjectBasicInformation = 0,
	ObjectNameInformation = 1,
	ObjectTypeInformation = 2,
};

typedef NTSTATUS(NTAPI NtQuerySystemInformationType)(
	NtSystemInformationClass SystemInformationClass,
	PVOID SystemInformation,
	ULONG SystemInformationLength,
	PULONG ReturnLength
	);
typedef NTSTATUS(NTAPI NtQueryObjectType)(
	HANDLE ObjectHandle,
	NtObjectInformationClass ObjectInformationClass,
	PVOID ObjectInformation,
	ULONG ObjectInformationLength,
	PULONG ReturnLength
	);

struct UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR Buffer;
};

struct SYSTEM_HANDLE {
	ULONG ProcessId;
	BYTE ObjectTypeNumber;
	BYTE Flags;
	USHORT Handle;
	PVOID Object;
	ACCESS_MASK GrantedAccess;
};

struct SYSTEM_HANDLE_INFORMATION {
	ULONG HandleCount;
	SYSTEM_HANDLE Handles[1];
};

enum POOL_TYPE {
	NonPagedPool,
	PagedPool,
	NonPagedPoolMustSucceed,
	DontUseThisType,
	NonPagedPoolCacheAligned,
	PagedPoolCacheAligned,
	NonPagedPoolCacheAlignedMustS
};

struct OBJECT_TYPE_INFORMATION {
	UNICODE_STRING Name;
	ULONG TotalNumberOfObjects;
	ULONG TotalNumberOfHandles;
	ULONG TotalPagedPoolUsage;
	ULONG TotalNonPagedPoolUsage;
	ULONG TotalNamePoolUsage;
	ULONG TotalHandleTableUsage;
	ULONG HighWaterNumberOfObjects;
	ULONG HighWaterNumberOfHandles;
	ULONG HighWaterPagedPoolUsage;
	ULONG HighWaterNonPagedPoolUsage;
	ULONG HighWaterNamePoolUsage;
	ULONG HighWaterHandleTableUsage;
	ULONG InvalidAttributes;
	GENERIC_MAPPING GenericMapping;
	ULONG ValidAccess;
	BOOLEAN SecurityRequired;
	BOOLEAN MaintainHandleCount;
	USHORT MaintainTypeList;
	POOL_TYPE PoolType;
	ULONG PagedPoolUsage;
	ULONG NonPagedPoolUsage;
};

template<typename T>
T* GetLibraryProcAddress(const wchar_t* szLibraryName, const char* szProcName) {
	const auto hModule = GetModuleHandleW(szLibraryName);
	if (!hModule)
		return nullptr;
	return reinterpret_cast<T*>(GetProcAddress(hModule, szProcName));
}
const auto NtQueryObject = GetLibraryProcAddress<NtQueryObjectType>(L"ntdll.dll", "NtQueryObject");
const auto NtQuerySystemInformation = GetLibraryProcAddress<NtQuerySystemInformationType>(L"ntdll.dll", "NtQuerySystemInformation");

static std::vector<SYSTEM_HANDLE> EnumerateLocalHandles() {
	if (!NtQuerySystemInformation)
		throw std::runtime_error("Failed to find ntdll.dll!NtQuerySystemInformation");

	NTSTATUS status;
	std::vector<SYSTEM_HANDLE> result;
	std::vector<char> handleInfoBuffer;
	handleInfoBuffer.resize(0x10000);

	// NtQuerySystemInformation won't give us the correct buffer size, so we guess by doubling the buffer size.
	while ((status = NtQuerySystemInformation(NtSystemInformationClass::SystemHandleInformation, &handleInfoBuffer[0], static_cast<ULONG>(handleInfoBuffer.size()), nullptr)) == STATUS_INFO_LENGTH_MISMATCH)
		handleInfoBuffer.resize(handleInfoBuffer.size() * 2);

	// NtQuerySystemInformation stopped giving us STATUS_INFO_LENGTH_MISMATCH.
	if (!NtSuccess(status))
		throw std::runtime_error(std::format("NtQuerySystemInformation returned {:d}.", status));

	const auto pHandleInfo = reinterpret_cast<SYSTEM_HANDLE_INFORMATION*>(handleInfoBuffer.data());
	for (size_t i = 0; i < pHandleInfo->HandleCount; i++)
		if (pHandleInfo->Handles[i].ProcessId == GetCurrentProcessId())
			result.push_back(pHandleInfo->Handles[i]);
	return result;
}

static std::wstring GetHandleObjectName(HANDLE hHandle) {
	if (!NtQueryObject)
		throw std::runtime_error("Failed to find ntdll.dll!NtQueryObject");

	ULONG returnLength = 0;
	if (NtSuccess(NtQueryObject(hHandle, NtObjectInformationClass::ObjectNameInformation, nullptr, 0, &returnLength)))
		return L"";

	std::vector<char> objectNameInfo;
	objectNameInfo.resize(returnLength);
	if (!NtSuccess(NtQueryObject(hHandle, NtObjectInformationClass::ObjectNameInformation, &objectNameInfo[0], returnLength, NULL)))
		throw std::runtime_error(std::format("Failed to get object name for handle {:p}", hHandle));

	const auto pObjectName = reinterpret_cast<UNICODE_STRING*>(objectNameInfo.data());
	if (pObjectName->Length)
		return pObjectName->Buffer;
	return L"";
}

void App::Misc::FreeGameMutex::FreeGameMutex() {
	// Create a mutex to figure out ObjectTypeNumber.
	const Utils::Win32::Closeable::Handle hMutexTemp(CreateMutexW(nullptr, false, nullptr),
		Utils::Win32::Closeable::Handle::Null,
		"App::Misc::FreeGameMutex::FreeGameMutex/CreateMutexW");
	const auto allHandles = EnumerateLocalHandles();

	std::vector<char> objectNameInfo;
	objectNameInfo.resize(0x1000);

	BYTE mutexTypeNumber = 0x11;
	for (const auto& handle : allHandles) {
		const auto hObject = reinterpret_cast<HANDLE>(handle.Handle);
		if (hObject == hMutexTemp) {
			mutexTypeNumber = handle.ObjectTypeNumber;
			break;
		}
	}

	for (const auto& handle : allHandles) {
		const auto hObject = reinterpret_cast<HANDLE>(handle.Handle);

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
				Logger::GetLogger().Format(
					LogCategory::General,
					"Freed game mutex {}.",
					Utils::ToUtf8(name));
			}
		} catch (std::exception& e) {
			Logger::GetLogger().Format(
				LogCategory::General,
				"Failed to process handle {:p}(type {:2x}): {}",
				hObject, handle.ObjectTypeNumber, e.what());
		}
	}
}
