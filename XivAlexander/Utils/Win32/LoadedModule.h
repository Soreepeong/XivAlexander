#pragma once

#include <Psapi.h>

#include <string_view>

#include "Utils/Win32/Closeable.h"

namespace Utils::Win32 {
	class LoadedModule : public Closeable<HMODULE, FreeLibrary> {
	public:
		using Closeable<HMODULE, FreeLibrary>::Closeable;
		explicit LoadedModule(const wchar_t* pwszFileName, DWORD dwFlags = 0, bool bRequire = true);
		explicit LoadedModule(const std::filesystem::path& path, DWORD dwFlags = 0, bool bRequire = true);
		LoadedModule(LoadedModule&& r) noexcept;
		LoadedModule(const LoadedModule& r);
		LoadedModule& operator=(LoadedModule&& r) noexcept;
		LoadedModule& operator=(const LoadedModule& r);
		LoadedModule& operator=(std::nullptr_t) override;
		~LoadedModule() override;

		static LoadedModule LoadMore(const LoadedModule& module);

		static const LoadedModule& MainModule() {
			static const LoadedModule s_module(GetModuleHandle(nullptr), false);
			return s_module;
		}

		template<typename T>
		T GetProcAddress(const char* szName, bool throwIfNotFound = false) const {
#pragma warning(push)
#pragma warning(disable: 4191)
			const auto result = reinterpret_cast<T>(::GetProcAddress(m_object, szName));
#pragma warning(pop)
			if (throwIfNotFound && !result)
				throw std::runtime_error(std::format("Function \"{}\" not found", szName));
			return result;
		}

		template<typename T>
		T GetProcAddress(WORD ordinal, bool throwIfNotFound = false) const {
#pragma warning(push)
#pragma warning(disable: 4191)
			const auto result = reinterpret_cast<T>(::GetProcAddress(m_object, MAKEINTRESOURCEA(ordinal)));
#pragma warning(pop)
			if (throwIfNotFound && !result)
				throw std::runtime_error(std::format("Function #{} not found", ordinal));
			return result;
		}

		[[nodiscard]] std::filesystem::path PathOf() const;
		[[nodiscard]] std::filesystem::path BaseName() const;
		[[nodiscard]] MODULEINFO ModuleInfo() const;

		[[nodiscard]] const IMAGE_DOS_HEADER& DosHeader() const;
		[[nodiscard]] const IMAGE_NT_HEADERS& NtHeaders() const;
		[[nodiscard]] std::span<const IMAGE_SECTION_HEADER> SectionHeaders() const;

		[[nodiscard]] std::span<const uint8_t> DataDirectoryAt(size_t index) const;
		
		[[nodiscard]] std::span<const uint8_t> FunctionAt(const void* ptr) const;

		[[nodiscard]] std::span<const uint8_t> SectionFrom(const IMAGE_SECTION_HEADER& section) const;

		[[nodiscard]] std::span<const uint8_t> SectionFrom(std::string_view name) const;

		[[nodiscard]] std::span<const uint8_t> SectionAt(size_t index) const;

		template<typename T>
		[[nodiscard]] std::span<const T> SectionFrom(const IMAGE_SECTION_HEADER& section) const {
			const auto body = SectionFrom(section);
			return std::span(reinterpret_cast<const T*>(body.data()), body.size() / sizeof(T));
		}
		
		template<typename T>
		[[nodiscard]] std::span<const uint8_t> SectionFrom(std::string_view name) const {
			const auto body = SectionFrom(name);
			return std::span(reinterpret_cast<const T*>(body.data()), body.size() / sizeof(T));
		}

		template<typename T>
		[[nodiscard]] std::span<const T> SectionAt(size_t index) const {
			const auto body = SectionAt(index);
			return std::span(reinterpret_cast<const T*>(body.data()), body.size() / sizeof(T));
		}

		void SetPinned() const;

		bool operator<(const LoadedModule& r) const {
			return m_object < r.m_object;
		}

		bool operator>(const LoadedModule& r) const {
			return m_object > r.m_object;
		}

		bool operator<=(const LoadedModule& r) const {
			return m_object <= r.m_object;
		}

		bool operator>=(const LoadedModule& r) const {
			return m_object >= r.m_object;
		}
	};
}
