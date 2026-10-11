#pragma once

#include <functional>

#include <xivres/util.on_dtor.h>
#include "Utils/Win32/Closeable.h"
#include "Utils/Win32/LoadedModule.h"

namespace Utils::Win32 {

	class Handle : public Closeable<HANDLE, CloseHandle> {
		static HANDLE DuplicateHandleNullable(HANDLE src);

	public:
		using Closeable::Closeable;
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
		[[nodiscard]] DWORD Wait(DWORD dwMilliseconds) const;
		[[nodiscard]] DWORD Wait(bool bWaitAll, std::initializer_list<HANDLE> waitWith, DWORD dwMilliseconds = INFINITE, bool bAlertable = false) const;

		void Seek(int64_t offset, DWORD dwMoveMethod) const;

		static Handle FromCreateFile(
			_In_ const std::filesystem::path& path,
			_In_ DWORD dwDesiredAccess,
			_In_ DWORD dwShareMode,
			_In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
			_In_ DWORD dwCreationDisposition,
			_In_ DWORD dwFlagsAndAttributes = 0,
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
			return Read(offset, buf.data(), buf.size_bytes(), readMode) / sizeof(T);
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
			return Write(offset, buf.data(), buf.size_bytes(), writeMode) / sizeof(T);
		}

		[[nodiscard]] uint64_t GetFileSize() const;
	};

	class ActivationContext : public Closeable<HANDLE, ReleaseActCtx> {
	public:
		using Closeable::Closeable;
		ActivationContext(ActivationContext&& r) noexcept;
		ActivationContext& operator=(ActivationContext&& r) noexcept;
		explicit ActivationContext(const ACTCTXW& actctx);
		~ActivationContext() override;

		[[nodiscard]] xivres::util::on_dtor With() const;
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
		Thread& operator =(std::nullptr_t) noexcept;
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

	class Semaphore : public Handle {
	public:
		using Handle::Handle;
		~Semaphore() override;

		static Semaphore Create(
			LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
			LONG lInitialCount,
			LONG lMaximumCount,
			LPCWSTR lpName = nullptr,
			DWORD dwDesiredAccess = SEMAPHORE_ALL_ACCESS
		);

		LONG Release(LONG count) const;  // NOLINT(modernize-use-nodiscard)
	};

	class FileMapping : public Handle {
	public:
		using Handle::Handle;

		static FileMapping Create(const Handle& file, LPSECURITY_ATTRIBUTES lpFileMappingAttributes = nullptr, DWORD flProtect = PAGE_READONLY, uint64_t maximumSize = 0, LPCWSTR lpName = nullptr) {
			return FileMapping(CreateFileMappingW(file, lpFileMappingAttributes, flProtect, static_cast<DWORD>(maximumSize >> 32), static_cast<DWORD>(maximumSize), lpName), Null, "CreateFileMappingW");
		}

		class View : public Closeable<void*, UnmapViewOfFile> {
		public:
			using Closeable::Closeable;
			View(const Closeable&) = delete;

			using Closeable::operator=;
			View& operator=(const Closeable&) override = delete;

			static View Create(const FileMapping& mapping, DWORD dwDesiredAccess = FILE_MAP_READ, uint64_t fileOffset = 0, SIZE_T dwNumberOfBytesToMap = 0) {	
				return View(MapViewOfFile(mapping, dwDesiredAccess, static_cast<DWORD>(fileOffset >> 32), static_cast<DWORD>(fileOffset), dwNumberOfBytesToMap), nullptr, "MapViewOfFile");
			}
		};
	};
}
