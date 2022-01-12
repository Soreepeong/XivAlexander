#pragma once

#include "XivAlexanderCommon/Utils/Win32.h"
#include "XivAlexanderCommon/Utils/Utils.h"
#include "XivAlexanderCommon/Utils/StringUtils.h"

namespace Utils::Win32 {
	template <typename T, auto CloserFunction, T NullValue = (T)0>
	class Closeable {
	public:
		static constexpr T Null = NullValue;

	protected:
		T m_object;
		bool m_bOwnership;

	public:
		Closeable() : m_object(Null), m_bOwnership(false) {}
		Closeable(std::nullptr_t) : m_object(Null), m_bOwnership(false) {}

		Closeable(T object, bool ownership)
			: m_object(object)
			, m_bOwnership(object != Null && ownership) {
		}

		Closeable(T object, T invalidValue)
			: m_object(object == invalidValue ? nullptr : object)
			, m_bOwnership(object != invalidValue) {
		}

		Closeable(T object, T invalidValue, const std::string& errorMessage)
			: m_object(object == invalidValue ? nullptr : object)
			, m_bOwnership(object != invalidValue) {
			if (object == invalidValue)
				throw Error(errorMessage);
		}

		template <typename ... Args>
		Closeable(T object, T invalidValue, const char* errorMessageFormat, Args ... args)
			: m_object(object == invalidValue ? nullptr : object)
			, m_bOwnership(object != invalidValue) {
			if (object == invalidValue)
				throw Error(std::format(errorMessageFormat, std::forward<Args>(args)...));
		}

		template <typename ... Args>
		Closeable(T object, T invalidValue, const wchar_t* errorMessageFormat, Args ... args)
			: Closeable(object, invalidValue, Utils::ToUtf8(std::format(errorMessageFormat, std::forward<Args>(args)...))) {
		}

		Closeable(Closeable&& r) noexcept
			: m_object(std::move(r.m_object))
			, m_bOwnership(std::move(r.m_bOwnership)) {
			r.m_object = Null;
			r.m_bOwnership = false;
		}

		Closeable& operator=(Closeable&& r) noexcept {
			m_object = r.m_object;
			m_bOwnership = r.m_bOwnership;
			r.m_object = Null;
			r.m_bOwnership = false;
			return *this;
		}

		virtual Closeable& operator=(nullptr_t) {
			Clear();
			return *this;
		}

		Closeable(const Closeable&) = delete;
		virtual Closeable& operator=(const Closeable&) = delete;

		virtual ~Closeable() {
			ClearInternal();
		}

		Closeable& Attach(T r, T invalidValue, bool ownership, const std::string& errorMessage) {
			if (r == invalidValue)
				throw Error(errorMessage);
			
			Clear();
			m_object = r;
			m_bOwnership = ownership;
			return *this;
		}

		virtual T Detach() {
			const auto prev = m_object;
			m_object = nullptr;
			m_bOwnership = true;
			return prev;
		}

		virtual void Clear() {
			ClearInternal();
		}
		
		T operator*() const { return m_object; }

		operator bool() const { return !!m_object; }

		virtual operator T() const { return m_object; }
		
		template<typename = std::enable_if_t<std::is_pointer_v<T>>>
		[[nodiscard]] auto Value64() const { return static_cast<uint64_t>(Value()); }

		template<typename = std::enable_if_t<std::is_pointer_v<T>>>
		[[nodiscard]] auto Value32() const { return static_cast<uint32_t>(Value()); }
		
		template<typename = std::enable_if_t<std::is_pointer_v<T>>>
		[[nodiscard]] auto Value() const { return reinterpret_cast<size_t>(m_object); }

		[[nodiscard]] bool HasOwnership() const { return m_object && m_bOwnership; }

	private:
		void ClearInternal() {
			if (m_object && m_bOwnership)
				CloserFunction(m_object);
			m_object = nullptr;
			m_bOwnership = true;
		}
	};

	using Icon = Closeable<HICON, DestroyIcon>;
	using CreatedDC = Closeable<HDC, DeleteDC>;
	using FindFile = Closeable<HANDLE, FindClose>;
}
