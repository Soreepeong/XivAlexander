#include "Base.h"

// What the C runtime would otherwise provide. Compiled without /GL: the compiler refuses its own helpers in LTCG code.

extern "C" {
#pragma function(memcpy, memset, memcmp, memmove)
	void* __cdecl memcpy(void* dst, const void* src, size_t n) {
		__movsb(static_cast<unsigned char*>(dst), static_cast<const unsigned char*>(src), n);
		return dst;
	}

	void* __cdecl memset(void* dst, int c, size_t n) {
		__stosb(static_cast<unsigned char*>(dst), static_cast<unsigned char>(c), n);
		return dst;
	}

	int __cdecl memcmp(const void* a, const void* b, size_t n) {
		const auto pa = static_cast<const volatile unsigned char*>(a);
		const auto pb = static_cast<const volatile unsigned char*>(b);
		for (size_t i = 0; i < n; ++i) {
			if (pa[i] != pb[i])
				return pa[i] < pb[i] ? -1 : 1;
		}
		return 0;
	}

	void* __cdecl memmove(void* dst, const void* src, size_t n) {
		const auto d = static_cast<volatile unsigned char*>(dst);
		const auto s = static_cast<const volatile unsigned char*>(src);
		if (d < s) {
			for (size_t i = 0; i < n; ++i)
				d[i] = s[i];
		} else {
			for (size_t i = n; i-- > 0;)
				d[i] = s[i];
		}
		return dst;
	}

	// MinHook is built with /GS; its checks need these.
#if XA_LOADER64
	uintptr_t __security_cookie = 0x00002B992DDFA232;

	void __cdecl __security_check_cookie(uintptr_t cookie) {
		if (cookie != __security_cookie)
			__fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
	}

	EXCEPTION_DISPOSITION __GSHandlerCheck(PEXCEPTION_RECORD, void*, PCONTEXT, void*) {
		return ExceptionContinueSearch;
	}
#else
	uintptr_t __security_cookie = 0xBB40E64E;

	void __fastcall __security_check_cookie(uintptr_t cookie) {
		if (cookie != __security_cookie)
			__fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
	}
#endif
}

// Referenced by placement new in code built without exceptions, never called with anything to free.
void __cdecl operator delete(void* p) noexcept {
	Loader::Free(p);
}

void __cdecl operator delete(void* p, size_t) noexcept {
	Loader::Free(p);
}
