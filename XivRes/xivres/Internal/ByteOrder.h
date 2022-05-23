#ifndef _XIVRES_INTERNAL_BYTEORDER_H_
#define _XIVRES_INTERNAL_BYTEORDER_H_

#include <type_traits>

namespace XivRes::Internal {
	template<typename T>
	union ByteOrderStorage {
		T Value;
		char Bytes[sizeof T];

		static ByteOrderStorage<T> FromWithoutSwap(T value) {
			return ByteOrderStorage<T>(value);
		}

		static T ToWithoutSwap(ByteOrderStorage<T> storage) {
			return storage.Value;
		}

		static ByteOrderStorage<T> FromWithSwap(T value);

		static T ToWithSwap(ByteOrderStorage<T> storage);
	};

	template<typename T>
	inline ByteOrderStorage<T> ByteOrderStorage<T>::FromWithSwap(T value) {
		if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
			return ByteOrderStorage<T>(value);

		else if constexpr (std::is_same_v<T, uint16_t> || std::is_same_v<T, int16_t>)
			return ByteOrderStorage<T>(static_cast<T>(_byteswap_ushort(static_cast<uint16_t>(value))));

		else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>)
			return ByteOrderStorage<T>(static_cast<T>(_byteswap_ulong(static_cast<uint32_t>(value))));

		else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t>)
			return ByteOrderStorage<T>(static_cast<T>(_byteswap_uint64(static_cast<uint64_t>(value))));

		else {
			auto storage = ByteOrderStorage(value);
			std::reverse(storage.Bytes, storage.Bytes + sizeof T);
			return storage;
		}
	}

	template<typename T>
	inline T ByteOrderStorage<T>::ToWithSwap(ByteOrderStorage<T> storage) {
		if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
			return storage.Value;

		else if constexpr (std::is_same_v<T, uint16_t> || std::is_same_v<T, int16_t>)
			return static_cast<T>(_byteswap_ushort(static_cast<uint16_t>(storage.Value)));

		else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>)
			return static_cast<T>(_byteswap_ulong(static_cast<uint32_t>(storage.Value)));

		else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t>)
			return static_cast<T>(_byteswap_uint64(static_cast<uint64_t>(storage.Value)));

		else {
			std::reverse(storage.Bytes, storage.Bytes + sizeof T);
			return storage.Value;
		}
	}

	template<typename T, ByteOrderStorage<T> FromNative(T), T ToNative(ByteOrderStorage<T>)>
	union ByteOrder {
		using Type = T;

		ByteOrderStorage<T> Storage;

		ByteOrder(T defaultValue = static_cast<T>(0))
			: Storage(FromNative(defaultValue)) {
		}

		ByteOrder<T, FromNative, ToNative>& operator= (T newValue) {
			Storage = FromNative(newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator+= (T newValue) {
			Storage = FromNative(ToNative(Storage) + newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator-= (T newValue) {
			Storage = FromNative(ToNative(Storage) - newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator*= (T newValue) {
			Storage = FromNative(ToNative(Storage) * newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator/= (T newValue) {
			Storage = FromNative(ToNative(Storage) / newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator%= (T newValue) {
			Storage = FromNative(ToNative(Storage) % newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator&= (T newValue) {
			Storage = FromNative(ToNative(Storage) & newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator|= (T newValue) {
			Storage = FromNative(ToNative(Storage) | newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator^= (T newValue) {
			Storage = FromNative(ToNative(Storage) ^ newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator<<= (T newValue) {
			Storage = FromNative(ToNative(Storage) << newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator>>= (T newValue) {
			Storage = FromNative(ToNative(Storage) >> newValue);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator++ () {
			Storage = FromNative(ToNative(Storage) + 1);
			return *this;
		}

		ByteOrder<T, FromNative, ToNative>& operator-- () {
			Storage = FromNative(ToNative(Storage) - 1);
			return *this;
		}

		T operator++ (int) {
			const auto v = ToNative(Storage);
			Storage = FromNative(v + 1);
			return v;
		}

		T operator-- (int) {
			const auto v = ToNative(Storage);
			Storage = FromNative(v - 1);
			return v;
		}

		T operator+() const {
			return ToNative(Storage);
		}

		T operator-() const {
			return -ToNative(Storage);
		}

		T operator~() const {
			return ~ToNative(Storage);
		}

		T operator!() const {
			return !ToNative(Storage);
		}

		operator T() const {
			return ToNative(Storage);
		}

		T operator *() const {
			return ToNative(Storage);
		}
	};
}

namespace XivRes {
	template<typename T>
	using LE = Internal::ByteOrder<T, Internal::ByteOrderStorage<T>::FromWithoutSwap, Internal::ByteOrderStorage<T>::ToWithoutSwap>;
	template<typename T>
	using BE = Internal::ByteOrder<T, Internal::ByteOrderStorage<T>::FromWithSwap, Internal::ByteOrderStorage<T>::ToWithSwap>;
	template<typename T>
	using NE = LE<T>;
	template<typename T>
	using RNE = BE<T>;
}

#endif
