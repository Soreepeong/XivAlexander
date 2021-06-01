#include "pch.h"
#include "App_Misc_FreeGameMutex.h"

#define NT_SUCCESS(x) (((x)) >= 0)
#define STATUS_INFO_LENGTH_MISMATCH 0xc0000004

#define SystemHandleInformation 16
#define ObjectBasicInformation 0
#define ObjectNameInformation 1
#define ObjectTypeInformation 2

typedef NTSTATUS(NTAPI _NtQuerySystemInformation)(
	ULONG SystemInformationClass,
	PVOID SystemInformation,
	ULONG SystemInformationLength,
	PULONG ReturnLength
	);
typedef NTSTATUS(NTAPI _NtQueryObject)(
	HANDLE ObjectHandle,
	ULONG ObjectInformationClass,
	PVOID ObjectInformation,
	ULONG ObjectInformationLength,
	PULONG ReturnLength
	);

typedef struct _UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR Buffer;
} UNICODE_STRING, * PUNICODE_STRING;

typedef struct _SYSTEM_HANDLE {
	ULONG ProcessId;
	BYTE ObjectTypeNumber;
	BYTE Flags;
	USHORT Handle;
	PVOID Object;
	ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE, * PSYSTEM_HANDLE;

typedef struct _SYSTEM_HANDLE_INFORMATION {
	ULONG HandleCount;
	SYSTEM_HANDLE Handles[1];
} SYSTEM_HANDLE_INFORMATION, * PSYSTEM_HANDLE_INFORMATION;

typedef enum _POOL_TYPE {
	NonPagedPool,
	PagedPool,
	NonPagedPoolMustSucceed,
	DontUseThisType,
	NonPagedPoolCacheAligned,
	PagedPoolCacheAligned,
	NonPagedPoolCacheAlignedMustS
} POOL_TYPE, * PPOOL_TYPE;

typedef struct _OBJECT_TYPE_INFORMATION {
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
} OBJECT_TYPE_INFORMATION, * POBJECT_TYPE_INFORMATION;

template<typename T>
T* GetLibraryProcAddress(const wchar_t* szLibraryName, const char* szProcName) {
	const auto hModule = GetModuleHandleW(szLibraryName);
	if (!hModule)
		return nullptr;
	return reinterpret_cast<T*>(GetProcAddress(hModule, szProcName));
}
const auto NtQueryObject = GetLibraryProcAddress<_NtQueryObject>(L"ntdll.dll", "NtQueryObject");
const auto NtQuerySystemInformation = GetLibraryProcAddress<_NtQuerySystemInformation>(L"ntdll.dll", "NtQuerySystemInformation");

static std::vector<SYSTEM_HANDLE> EnumerateLocalHandles() {
	if (!NtQuerySystemInformation)
		throw std::exception("Failed to find ntdll.dll!NtQuerySystemInformation");

	NTSTATUS status;
	std::vector<SYSTEM_HANDLE> result;
	std::vector<char> handleInfoBuffer;
	handleInfoBuffer.resize(0x10000);

	// NtQuerySystemInformation won't give us the correct buffer size, so we guess by doubling the buffer size.
	while ((status = NtQuerySystemInformation(SystemHandleInformation, &handleInfoBuffer[0], static_cast<ULONG>(handleInfoBuffer.size()), NULL)) == STATUS_INFO_LENGTH_MISMATCH)
		handleInfoBuffer.resize(handleInfoBuffer.size() * 2);

	// NtQuerySystemInformation stopped giving us STATUS_INFO_LENGTH_MISMATCH.
	if (!NT_SUCCESS(status))
		throw std::exception(Utils::FormatString("NtQuerySystemInformation returned %d.", status).c_str());

	const auto pHandleInfo = reinterpret_cast<PSYSTEM_HANDLE_INFORMATION>(handleInfoBuffer.data());
	for (size_t i = 0; i < pHandleInfo->HandleCount; i++)
		if (pHandleInfo->Handles[i].ProcessId == GetCurrentProcessId())
			result.push_back(pHandleInfo->Handles[i]);
	return result;
}

static std::wstring GetHandleObjectName(HANDLE hHandle) {
	if (!NtQueryObject)
		throw std::exception("Failed to find ntdll.dll!NtQueryObject");

	ULONG returnLength = 0;
	if (NT_SUCCESS(NtQueryObject(hHandle, ObjectNameInformation, nullptr, 0, &returnLength)))
		return L"";

	std::vector<char> objectNameInfo;
	objectNameInfo.resize(returnLength);
	if (!NT_SUCCESS(NtQueryObject(hHandle, ObjectNameInformation, &objectNameInfo[0], returnLength, NULL)))
		throw std::exception(Utils::FormatString("Failed to get object name for handle %p", hHandle).c_str());

	const auto pObjectName = reinterpret_cast<PUNICODE_STRING>(objectNameInfo.data());
	if (pObjectName->Length)
		return pObjectName->Buffer;
	return L"";
}

void App::Misc::FreeGameMutex::FreeGameMutex() {
	// Create a mutex to figure out ObjectTypeNumber.
	Utils::Win32Handle hMutexTemp(CreateMutexW(nullptr, false, nullptr));
	const auto allHandles = EnumerateLocalHandles();

	std::vector<char> objectNameInfo;
	objectNameInfo.resize(0x1000);

	BYTE mutexTypeNumber = 0x11;
	for (const auto& handle : allHandles) {
		HANDLE hObject = (HANDLE)handle.Handle;
		if (hObject == hMutexTemp) {
			mutexTypeNumber = handle.ObjectTypeNumber;
			break;
		}
	}

	for (const auto& handle : allHandles) {
		HANDLE hObject = (HANDLE)handle.Handle;

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
				Misc::Logger::GetLogger().Format(
					LogCategory::General, 
					"Freed game mutex %s.", 
					Utils::ToUtf8(name).c_str());
			}
		} catch (std::exception& e) {
			Misc::Logger::GetLogger().Format(
				LogCategory::General, 
				"Failed to process handle %p(type %2x): %s",
				hObject, handle.ObjectTypeNumber, e.what());
		}
	}
}
