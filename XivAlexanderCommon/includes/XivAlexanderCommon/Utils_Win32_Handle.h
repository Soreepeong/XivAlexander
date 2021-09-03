#pragma once

#include <functional>

#include "Utils_CallOnDestruction.h"
#include "Utils_Win32_Closeable.h"
#include "Utils_Win32_LoadedModule.h"

namespace Utils::Win32 {

	class Handle : public Closeable<HANDLE, CloseHandle> {
		static HANDLE DuplicateHandleNullable(HANDLE src);

	public:
		using Closeable<HANDLE, CloseHandle>::Closeable;
		Handle(Handle&& r) noexcept;
		Handle(const Handle& r);
		Handle& operator=(Handle&& r) noexcept;
		Handle& operator=(const Handle& r);
		~Handle() override;

		template<typename T>
		static T DuplicateFrom(HANDLE hProcess, HANDLE hSourceHandle, bool bInheritable = false) {
			HANDLE h;
			if (!DuplicateHandle(hProcess, hSourceHandle, GetCurrentProcess(), &h, 0, bInheritable ? TRUE : FALSE, DUPLICATE_SAME_ACCESS))
				throw Error("DuplicateHandle");

			T result;
			result.m_object = h;
			result.m_bOwnership = true;
			return result;
		}
		template<typename T>
		static T DuplicateFrom(HANDLE hSourceHandle, bool bInheritable = false) {
			return DuplicateFrom<T>(GetCurrentProcess(), hSourceHandle, bInheritable);
		}

		void Wait() const;
		[[nodiscard]] DWORD Wait(DWORD duration) const;
	};

	class ActivationContext : public Closeable<HANDLE, ReleaseActCtx> {
	public:
		using Closeable<HANDLE, ReleaseActCtx>::Closeable;
		ActivationContext(ActivationContext&& r) noexcept;
		ActivationContext& operator=(ActivationContext&& r) noexcept;
		explicit ActivationContext(const ACTCTXW& actctx);
		~ActivationContext() override;

		[[nodiscard]] CallOnDestruction With() const;
	};

	class Thread : public Handle {
	public:
		Thread();
		Thread(HANDLE hThread, bool ownership);
		Thread(std::wstring name, std::function<DWORD()> body, LoadedModule hLibraryToFreeAfterExecution = nullptr);
		Thread(std::wstring name, std::function<void()> body, LoadedModule hLibraryToFreeAfterExecution = nullptr);
		Thread(Thread&& r) noexcept;
		Thread(const Thread& r);
		Thread& operator =(Thread&& r) noexcept;
		Thread& operator =(const Thread& r);
		~Thread() override;

		[[nodiscard]] DWORD GetId() const;
		void Terminate(DWORD dwExitCode = 0, bool errorIfAlreadyTerminated = false) const;
	};

	class Event : public Handle {
	public:
		using Handle::Handle;
		~Event() override;

		static Event Create(
			_In_opt_ LPSECURITY_ATTRIBUTES lpEventAttributes = nullptr,
			_In_ BOOL bManualReset = TRUE,
			_In_ BOOL bInitialState = FALSE,
			_In_opt_ LPCWSTR lpName = nullptr
		);

		void Set() const;
		void Reset() const;
	};

	class File : public Handle {
	public:
		File();
		File(HANDLE hFile, bool ownership);
		File(File&& r) noexcept;
		File(const File& r);
		File& operator =(File&& r) noexcept;
		File& operator =(const File& r);
		~File() override;

		void Seek(int64_t offset, DWORD dwMoveMethod) const;

		static File Create(
			_In_ const std::filesystem::path& path,
			_In_ DWORD dwDesiredAccess,
			_In_ DWORD dwShareMode,
			_In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
			_In_ DWORD dwCreationDisposition,
			_In_ DWORD dwFlagsAndAttributes,
			_In_opt_ HANDLE hTemplateFile = nullptr
		);

		enum class PartialIoMode {
			AlwaysFull,
			AllowPartial,
			AllowPartialButRaiseEOF,
		};

		size_t Read(uint64_t offset, void* buf, size_t len, PartialIoMode readMode = PartialIoMode::AlwaysFull) const;

		template<typename T>
		size_t Read(uint64_t offset, std::span<T> buf, PartialIoMode readMode = PartialIoMode::AlwaysFull) const {
			return Read(offset, buf.data(), buf.size_bytes(), readMode) / sizeof T;
		}

		template<typename T>
		std::vector<T> Read(uint64_t offset, size_t count, PartialIoMode readMode = PartialIoMode::AlwaysFull) const {
			std::vector<T> buf;
			buf.resize(count);
			buf.resize(Read(offset, std::span(buf), readMode));
			return buf;
		}

		template<typename T>
		T Read(uint64_t offset) const {
			T buf;
			Read(offset, std::span(&buf, &buf + 1));
			return buf;
		}

		size_t Write(uint64_t offset, const void* buf, size_t len, PartialIoMode writeMode = PartialIoMode::AlwaysFull) const;

		template<typename T>
		size_t Write(uint64_t offset, std::span<T> buf, PartialIoMode writeMode = PartialIoMode::AlwaysFull) const {
			return Write(offset, buf.data(), buf.size_bytes(), writeMode) / sizeof T;
		}

		[[nodiscard]] uint64_t GetLength() const;

		[[nodiscard]] std::filesystem::path ResolveName(bool bOpenedPath = false, bool bNtPath = false) const;
	};
}
