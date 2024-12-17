#pragma once

#include <map>
#include <mutex>

#include "XivAlexanderCommon/Utils/CallOnDestruction.h"
#include "XivAlexanderCommon/Utils/Win32/Closeable.h"
#include "XivAlexanderCommon/Utils/Win32/Handle.h"

namespace Utils::Win32 {
	class ModuleMemoryBlocks;
	class Process : public Handle {
		mutable std::mutex m_moduleMemoryMutex;
		mutable std::map<HMODULE, std::shared_ptr<ModuleMemoryBlocks>> m_moduleMemory;

	public:
		Process();
		Process(std::nullptr_t);
		Process(HANDLE hProcess, bool ownership);
		Process(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId);
		Process(Process&& r) noexcept;
		Process(const Process& r);
		~Process() override;

		Process& operator=(Process&& r) noexcept;
		Process& operator=(const Process& r);
		Process& operator=(std::nullptr_t) override;

		static Process& Current();

		Process& Attach(HANDLE r, bool ownership, const std::string& errorMessage);
		HANDLE Detach() override;

		void Clear() override;

		enum class ModuleNameCompareMode {
			FullPath = 0,
			FileNameWithExtension = 1,
			FileNameWithoutExtension = 2,
		};
		[[nodiscard]] std::vector<HMODULE> EnumModules() const;
		[[nodiscard]] HMODULE AddressOf(std::filesystem::path path, ModuleNameCompareMode compareMode = ModuleNameCompareMode::FullPath, bool require = true) const;
		[[nodiscard]] std::filesystem::path PathOf(HMODULE hModule = nullptr) const;
		[[nodiscard]] bool IsCurrentProcessPseudoHandle() const;
		[[nodiscard]] bool IsProcess64Bits() const;
		DWORD GetId() const;

		void Terminate(DWORD dwExitCode, bool errorIfAlreadyTerminated = false) const;
		DWORD WaitAndGetExitCode() const;

		int CallRemoteFunction(void* rpfn, void* rpParam, const char* pcszDescription) const;

		[[nodiscard]] std::pair<void*, void*> FindImportedFunction(HMODULE hModule, const std::filesystem::path& dllName, const char* pszFunctionName, uint32_t hintOrOrdinal = 0) const;
		[[nodiscard]] void* FindExportedFunction(HMODULE hModule, const char* pszFunctionName, USHORT ordinal = 0, bool require = true) const;
		HMODULE LoadModule(const std::filesystem::path& path) const;
		int UnloadModule(HMODULE hModule) const;

		std::vector<MEMORY_BASIC_INFORMATION> GetCommittedImageAllocation(const std::filesystem::path& path) const;
		std::vector<MEMORY_BASIC_INFORMATION> GetCommittedImageAllocation() const;

		ModuleMemoryBlocks& GetModuleMemoryBlockManager(HMODULE hModule) const;

		size_t ReadMemory(void* lpBase, size_t offset, void* buf, size_t len, bool readFull = true) const;

		template<typename T>
		size_t ReadMemory(void* lpBase, size_t offset, std::span<T> buf, bool readFull = true) const {
			return ReadMemory(lpBase, offset, buf.data(), buf.size_bytes(), readFull) / sizeof(T);
		}

		template<typename T>
		std::vector<T> ReadMemory(void* lpBase, size_t offset, size_t count, bool readFull = true) const {
			std::vector<T> buf;
			buf.resize(count);
			buf.resize(ReadMemory(lpBase, offset, std::span(buf), readFull));
			return buf;
		}

		template<typename T>
		T ReadMemory(void* lpBase, size_t offset) const {
			T buf;
			ReadMemory(lpBase, offset, std::span(&buf, &buf + 1));
			return buf;
		}

		void WriteMemory(void* lpTarget, const void* lpSource, size_t len, bool forceWrite = false) const;

		template<typename T>
		void WriteMemory(void* lpTarget, size_t offset, const T& data, bool makeWritableAsRequired = false) const {
			WriteMemory(static_cast<char*>(lpTarget) + offset, &data, sizeof data, makeWritableAsRequired);
		}

		template<typename T>
		void WriteMemory(void* lpTarget, size_t offset, const std::span<T>& data, bool makeWritableAsRequired = false) const {
			WriteMemory(static_cast<char*>(lpTarget) + offset, data.data(), data.size_bytes(), makeWritableAsRequired);
		}

		void* VirtualAlloc(void* lpBase, size_t size, DWORD flAllocType, DWORD flProtect, const void* lpSourceData = nullptr, size_t sourceDataSize = 0) const;
		template<typename T>
		T* VirtualAlloc(void* lpBase, size_t size, DWORD flAllocType, DWORD flProtect, const T* lpSourceData = nullptr, size_t sourceDataSize = 0) const {
			return static_cast<T*>(VirtualAlloc(lpBase, size, flAllocType, flProtect, static_cast<const void*>(lpSourceData), sourceDataSize));
		}

		template<typename T>
		T* VirtualAlloc(void* lpBase, DWORD flAllocType, DWORD flProtect, const std::span<T>& lpSourceData = nullptr) const {
			return static_cast<T*>(VirtualAlloc(lpBase, lpSourceData.size_bytes(), flAllocType, flProtect, lpSourceData.data(), lpSourceData.size_bytes()));
		}

		template<typename T>
		CallOnDestructionWithValue<T*> WithVirtualAlloc(void* lpBase, DWORD flAllocType, DWORD flProtect, const std::span<T>& lpSourceData = nullptr) const {
			const auto lpAddress = VirtualAlloc(lpBase, flAllocType, flProtect, lpSourceData);
			return CallOnDestructionWithValue(lpAddress, [this, lpAddress]() {
				VirtualFree(lpAddress);
			});
		}

		void VirtualFree(void* lpBase, size_t size = 0, DWORD dwFreeType = MEM_RELEASE) const;

		DWORD VirtualProtect(void* lpBase, size_t offset, size_t length, DWORD newProtect) const;

		CallOnDestructionWithValue<DWORD> WithVirtualProtect(void* lpBase, size_t offset, size_t length, DWORD newProtect) const;

		void FlushInstructionsCache(void* lpBase, size_t size) const;
	};

	class ProcessBuilder {
		bool m_bPrependPathToArgument = true;
		bool m_bUseShowWindow = false;
		bool m_bUseSize = false;
		bool m_bUsePosition = false;
		WORD m_wShowWindow = 0;
		DWORD m_dwWidth = 0;
		DWORD m_dwHeight = 0;
		DWORD m_dwX = 0;
		DWORD m_dwY = 0;

		std::filesystem::path m_path;
		std::filesystem::path m_dir;
		std::wstring m_args;
		std::vector<Handle> m_inheritedHandles;
		Process m_parentProcess;
		bool m_bNoWindow = false;

		bool m_environInitialized = false;
		std::map<std::wstring, std::wstring> m_environ;
		
		Handle m_hStdin;
		Handle m_hStdout;
		Handle m_hStderr;

	public:
		ProcessBuilder();
		ProcessBuilder(ProcessBuilder&&) noexcept;
		ProcessBuilder(const ProcessBuilder&);
		ProcessBuilder& operator=(ProcessBuilder&&) noexcept;
		ProcessBuilder& operator=(const ProcessBuilder&);
		~ProcessBuilder();

		std::pair<Process, Thread> Run();

		ProcessBuilder& WithParent(HWND hWnd);
		ProcessBuilder& WithParent(Process h);
		ProcessBuilder& WithPath(std::filesystem::path);
		ProcessBuilder& WithWorkingDirectory(std::filesystem::path);
		ProcessBuilder& WithArgument(bool prependPathToArgument, const std::string&);
		ProcessBuilder& WithArgument(bool prependPathToArgument, std::wstring);
		ProcessBuilder& WithAppendArgument(const std::string&);
		ProcessBuilder& WithAppendArgument(const std::wstring&);
		ProcessBuilder& WithAppendArgument(std::initializer_list<std::string>);
		ProcessBuilder& WithAppendArgument(std::initializer_list<std::wstring>);
		template<typename ... Args>
		ProcessBuilder& WithAppendArgument(const char* format, Args&& ... args) {
			return WithAppendArgument(std::vformat(format, std::make_format_args(std::forward<Args&>(args)...)));
		}
		template<typename ... Args>
		ProcessBuilder& WithAppendArgument(const wchar_t* format, Args&& ... args) {
			return WithAppendArgument(std::vformat(format, std::make_wformat_args(std::forward<Args&>(args)...)));
		}
		ProcessBuilder& WithSize(DWORD width, DWORD height, bool use = true);
		ProcessBuilder& WithUnspecifiedSize();
		ProcessBuilder& WithPosition(DWORD x, DWORD y, bool use = true);
		ProcessBuilder& WithUnspecifiedPosition();
		ProcessBuilder& WithShow(WORD show, bool use = true);
		ProcessBuilder& WithUnspecifiedShow();
		ProcessBuilder& WithEnviron(std::wstring_view key, std::wstring value);
		ProcessBuilder& WithoutEnviron(const std::wstring& key);
		ProcessBuilder& WithStdin(Utils::Win32::Handle);
		ProcessBuilder& WithStdin(HANDLE = nullptr);
		ProcessBuilder& WithStdout(Utils::Win32::Handle);
		ProcessBuilder& WithStdout(HANDLE = nullptr);
		ProcessBuilder& WithStderr(Utils::Win32::Handle);
		ProcessBuilder& WithStderr(HANDLE = nullptr);
		ProcessBuilder& WithNoWindow(bool noWindow = true);

		Handle Inherit(HANDLE hSource);
		template<typename T, typename = std::is_base_of<T, Handle>>
		T Inherit(T source) {
			return T(Inherit(static_cast<HANDLE>(source)), false);
		}

	private:
		void InitializeEnviron();
	};

	Process RunProgram(RunProgramParams params);

	class ModuleMemoryBlocks {
	public:
		const Process CurrentProcess;
		const HMODULE CurrentModule;
		const IMAGE_DOS_HEADER DosHeader;
		const IMAGE_FILE_HEADER FileHeader;
		union {
			const WORD OptionalHeaderMagic;
			const IMAGE_OPTIONAL_HEADER32 OptionalHeader32;
			const IMAGE_OPTIONAL_HEADER64 OptionalHeader64;
			char OptionalHeaderRaw[sizeof(IMAGE_OPTIONAL_HEADER64)];
		};
		const std::vector<IMAGE_SECTION_HEADER> SectionHeaders;

	private:
		std::map<DWORD, std::vector<char>> m_readMemoryBlocks;

	public:
		ModuleMemoryBlocks(Process process, HMODULE hModule);
		~ModuleMemoryBlocks();

		bool AddressInDataDirectory(size_t rva, int directoryIndex);

	private:
		std::span<uint8_t> Read(size_t rva, size_t maxCount);

	public:
		template<typename T>
		std::vector<T> ReadAligned(size_t rva, size_t maxCount = SIZE_MAX) {
			const auto r = Read(rva, maxCount * sizeof(T));
			std::vector<T> res;
			res.resize(r.size() / sizeof(T));
			memcpy(&res[0], &r[0], res.size() * sizeof(T));
			return res;
		}

		template<typename T>
		std::vector<T> ReadDataDirectory(int index) {
			return ReadAligned<T>(OptionalHeaderMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC
				? OptionalHeader32.DataDirectory[index].VirtualAddress
				: OptionalHeader64.DataDirectory[index].VirtualAddress);
		}
	};
}
