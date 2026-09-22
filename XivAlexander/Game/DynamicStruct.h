// ReSharper disable CppUninitializedNonStaticDataMember,CppClassNeedsConstructorBecauseOfUninitializedMember
#pragma once

template<size_t Size>
struct DynamicStruct {
private:
	[[maybe_unused]] uint8_t Data[Size];
	
protected:
	template<typename T>
	[[nodiscard]] const T& Field(size_t offset) const {
		return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(this) + offset);
	}
};

template<size_t TMaxVtblSlots, size_t Size>
struct DynamicVirtualStruct {
	static constexpr auto MaxVtblSlots = TMaxVtblSlots;
	void* const* Vtbl;

private:
	[[maybe_unused]] uint8_t Data[Size - sizeof Vtbl];
	
protected:
	template<typename T>
	[[nodiscard]] const T& Field(size_t offset) const {
		return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(this) + offset);
	}
};
