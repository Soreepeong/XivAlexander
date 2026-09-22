#pragma once

#include <span>

#include <xivres/util.on_dtor.h>

#include "Utils/Win32/LoadedModule.h"

namespace Utils::Win32 {
	/// A copy of a loaded module's image, read from its file and relocated to wherever the copy is placed, so that
	/// it holds the module's code as shipped, without whatever has since been hooked or patched into the original.
	class RelocatedImageCopy {
		std::span<char> m_mem{};
		xivres::util::on_dtor m_memRelease{};
		HMODULE m_original{};
		LoadedModule m_copy{static_cast<HMODULE>(nullptr), false};

	public:
		explicit RelocatedImageCopy(const LoadedModule& original);
		RelocatedImageCopy(const RelocatedImageCopy&) = delete;
		RelocatedImageCopy(RelocatedImageCopy&&) = delete;
		RelocatedImageCopy& operator=(const RelocatedImageCopy&) = delete;
		RelocatedImageCopy& operator=(RelocatedImageCopy&&) = delete;
		~RelocatedImageCopy();

		/// \returns The copy, as a module that signatures can be resolved against.
		[[nodiscard]] const LoadedModule& Copy() const { return m_copy; }

		/// \returns Where \p p in the copy is in the original.
		template<typename T>
		[[nodiscard]] T* ToOriginal(T* p) const {
			return Rebase(p, reinterpret_cast<const char*>(m_mem.data()), reinterpret_cast<const char*>(m_original));
		}

		/// \returns Where \p p in the original is in the copy.
		template<typename T>
		[[nodiscard]] T* ToCopy(T* p) const {
			return Rebase(p, reinterpret_cast<const char*>(m_original), reinterpret_cast<const char*>(m_mem.data()));
		}

	private:
		template<typename T>
		[[nodiscard]] T* Rebase(T* p, const char* from, const char* to) const {
			const auto offset = reinterpret_cast<const char*>(p) - from;
			if (offset < 0 || static_cast<size_t>(offset) >= m_mem.size())
				return nullptr;
			return reinterpret_cast<T*>(const_cast<char*>(to + offset));
		}
	};
}
