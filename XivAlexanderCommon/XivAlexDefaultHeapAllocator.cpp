// ReSharper disable CppParameterNamesMismatch
#include "pch.h"

// Enable this block when trying to figure out stuff about heap allocation from game
#if false

#include "Utils_Win32.h"

#pragma warning(disable: 28251)  // Inconsistent annotation

static auto GetLocalAllocLock() {
	static std::mutex s_allocLock;
	return std::lock_guard(s_allocLock);
}
static size_t s_allocCount = 0;

inline void* AllocateAndIncreaseCounter(size_t size) {
	const auto lock = GetLocalAllocLock();
	if (!Utils::Win32::g_hDefaultHeap) {
		Utils::Win32::g_hDefaultHeap = HeapCreate(HEAP_NO_SERIALIZE, 0, 0);
		if (!Utils::Win32::g_hDefaultHeap)
			ExitProcess(-1);
	}
	const auto p = HeapAlloc(Utils::Win32::g_hDefaultHeap, 0, size);
	if (p)
		s_allocCount++;
	return p;
}

inline void DeallocateAndDecreaseCounter(void* ptr) {
	const auto lock = GetLocalAllocLock();
	if (HeapFree(Utils::Win32::g_hDefaultHeap, 0, ptr))
		s_allocCount--;
	if (!s_allocCount) {
		HeapDestroy(Utils::Win32::g_hDefaultHeap);
		Utils::Win32::g_hDefaultHeap = nullptr;
	}
}

void* operator new(size_t size) {
	const auto p = AllocateAndIncreaseCounter(size);
	if (!p)
		throw std::bad_alloc();
	return p;
}

void* operator new[](size_t size) {
	const auto p = AllocateAndIncreaseCounter(size);
	if (!p)
		throw std::bad_alloc();
	return p;
}

void* operator new(size_t size, const std::nothrow_t&) noexcept {
	return AllocateAndIncreaseCounter(size);
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
	return AllocateAndIncreaseCounter(size);
}

void operator delete(void* ptr) noexcept {
	DeallocateAndDecreaseCounter(ptr);
}

void operator delete[](void* ptr) noexcept {
	DeallocateAndDecreaseCounter(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
	DeallocateAndDecreaseCounter(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
	DeallocateAndDecreaseCounter(ptr);
}

#endif
