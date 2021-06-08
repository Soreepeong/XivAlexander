#pragma once
namespace Utils {
	class WinPath {
		bool m_empty = true;
		std::wstring m_wbuf;
		mutable std::unique_ptr<std::string> m_sbuf;
		
	public:
		WinPath(const std::string& path);
		WinPath(std::wstring path);

		template<typename ... Args>
		WinPath(WinPath path, Args ... args) :
			WinPath(std::move(path.AddComponentInplace(std::forward<Args>(args)...))) {
		}

		WinPath(const WinPath& r);
		WinPath(WinPath&& r) noexcept;
		WinPath& operator=(const WinPath& r);
		WinPath& operator=(WinPath&& r) noexcept;

		explicit WinPath(HMODULE hModule, HANDLE hProcess = INVALID_HANDLE_VALUE);

		~WinPath();

		WinPath& RemoveComponentInplace(size_t numComponents = 1);
		WinPath& AddComponentInplace(const char* component);
		WinPath& AddComponentInplace(const wchar_t* component);
		
		template<typename C, typename ... Args>
		WinPath& AddComponentInplace(const std::basic_string<C>& component) {
			return AddComponentInplace(component.c_str());
		}

		template<typename C, typename ... Args>
		WinPath& AddComponentInplace(const std::basic_string<C>& component, Args... args) {
			return AddComponentInplace(component.c_str()).AddComponentInplace(std::forward<Args>(args)...);
		}

		template<typename C, typename ... Args>
		WinPath& AddComponentInplace(const C* component, Args... args) {
			return AddComponentInplace(component).AddComponentInplace(std::forward<Args>(args)...);
		}

		[[nodiscard]]
		bool operator==(const WinPath& rhs) const;

		[[nodiscard]]
		bool Exists() const;

		[[nodiscard]]
		bool IsDirectory() const;

		[[nodiscard]]
		const std::wstring& wstr() const;

		[[nodiscard]]
		const wchar_t* wbuf() const;

		[[nodiscard]]
		operator const std::wstring& () const;

		[[nodiscard]]
		operator const wchar_t* () const;

	private:

		[[nodiscard]]
		const std::string& str() const;

		[[nodiscard]]
		const char* buf() const;

		[[nodiscard]]
		operator const std::string& () const;

		[[nodiscard]]
		operator const char* () const;
	};
}
