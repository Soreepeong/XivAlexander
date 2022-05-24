#ifndef _XIVRES_PIXELFORMATS_H_
#define _XIVRES_PIXELFORMATS_H_

#include <cinttypes>

namespace XivRes {
	union Float {
		float Value;
		uint32_t UintValue;
		struct {
			uint32_t Sign : 1;
			uint32_t Exponent : 8;
			uint32_t Mantissa : 23;
		} Bits;
		struct {
			uint32_t Sign : 1;
			uint32_t Exponent : 8;
			uint32_t Mantissa : 10;
			uint32_t MantissaPad : 13;
		} HalfCompatibleBits;
	};

	union Half {
		uint16_t UintValue;
		struct {
			uint16_t Sign : 1;
			uint16_t Exponent : 5;
			uint16_t Mantissa : 10;
		} Bits;

		operator float() const {
			const auto v1 = Float{ .UintValue = (((UintValue & 0x8000U) << 16)
						| (((UintValue & 0x7c00U) + 0x1C000U) << 13)
						| ((UintValue & 0x03FFU) << 13)) }.Value;
			const auto v2 = Float{ .HalfCompatibleBits = {Bits.Sign, Bits.Exponent - 15U + 127U, Bits.Mantissa, 0} }.Value;
			if (v1 != v2)
				throw std::runtime_error("test");
			return v2;
		}
	};

	struct RGBAF16;
	struct RGBAF32;
	struct RGBAF64;

	template<typename TRGBA, typename T, int RB, int GB, int BB, int AB>
	struct ARGBNNNNCommon {
		static constexpr size_t ChannelCount = 4;
		static constexpr T MaxR = (1 << RB) - 1;
		static constexpr T MaxG = (1 << GB) - 1;
		static constexpr T MaxB = (1 << BB) - 1;
		static constexpr T MaxA = (1 << AB) - 1;

		ARGBNNNNCommon() {
			memset(this, 0, sizeof(*this));
		}

		ARGBNNNNCommon(T value) {
			memcpy(this, &value, sizeof(*this));
		}

		ARGBNNNNCommon(T r, T g, T b, T a = MaxA) {
			SetFrom(r, g, b, a);
		}

		template<typename TR, typename TG, typename TB, typename TA, typename = std::enable_if_t<std::is_integral_v<TR>&& std::is_integral_v<TG>&& std::is_integral_v<TB>&& std::is_integral_v<TA>>>
		void SetFrom(TR r, TG g, TB b, TA a = MaxA) {
			reinterpret_cast<TRGBA*>(this)->R = static_cast<T>(r);
			reinterpret_cast<TRGBA*>(this)->G = static_cast<T>(g);
			reinterpret_cast<TRGBA*>(this)->B = static_cast<T>(b);
			reinterpret_cast<TRGBA*>(this)->A = static_cast<T>(a);
		}

		template<typename TR, typename TG, typename TB, typename TA, typename = std::enable_if_t<std::is_floating_point_v<TR>&& std::is_floating_point_v<TG>&& std::is_floating_point_v<TB>&& std::is_floating_point_v<TA>>>
		void SetFromF(TR r, TG g, TB b, TA a = 1.) {
			SetFrom(
				static_cast<T>(std::round(static_cast<TR>(MaxR) * Internal::Clamp<TR>(r, 0, 1))),
				static_cast<T>(std::round(static_cast<TG>(MaxG) * Internal::Clamp<TG>(g, 0, 1))),
				static_cast<T>(std::round(static_cast<TB>(MaxB) * Internal::Clamp<TB>(b, 0, 1))),
				static_cast<T>(std::round(static_cast<TA>(MaxA) * Internal::Clamp<TA>(a, 0, 1))));
		}

		void SetFrom(const RGBAF16& v);
		void SetFrom(const RGBAF32& v);
		void SetFrom(const RGBAF64& v);
	};

	template<typename T, int RB, int GB, int BB, int AB>
	struct RGBANNNN : ARGBNNNNCommon<RGBANNNN<T, RB, GB, BB, AB>, T, RB, GB, BB, AB> {
		using TRGBA = RGBANNNN<T, RB, GB, BB, AB>;

		T A : AB;  // actually opacity
		T B : BB;
		T G : GB;
		T R : RB;

		using TRGBACommon = ARGBNNNNCommon<RGBANNNN<T, RB, GB, BB, AB>, T,RB, GB, BB, AB>;
		using TRGBACommon::ChannelCount;
		using TRGBACommon::MaxR;
		using TRGBACommon::MaxG;
		using TRGBACommon::MaxB;
		using TRGBACommon::MaxA;
		using TRGBACommon::ARGBNNNNCommon;
		using TRGBACommon::SetFrom;
		using TRGBACommon::SetFromF;
	};

	template<typename T, int RB, int GB, int BB, int AB>
	struct ARGBNNNN : ARGBNNNNCommon<RGBANNNN<T, RB, GB, BB, AB>, T, RB, GB, BB, AB> {
		using TRGBA = ARGBNNNN<T, RB, GB, BB, AB>;

		T B : BB;
		T G : GB;
		T R : RB;
		T A : AB;  // actually opacity

		using TRGBACommon = ARGBNNNNCommon<RGBANNNN<T, RB, GB, BB, AB>, T, RB, GB, BB, AB>;
		using TRGBACommon::ChannelCount;
		using TRGBACommon::MaxR;
		using TRGBACommon::MaxG;
		using TRGBACommon::MaxB;
		using TRGBACommon::MaxA;
		using TRGBACommon::ARGBNNNNCommon;
		using TRGBACommon::SetFrom;
		using TRGBACommon::SetFromF;
	};

	struct RGBAF16 {
		static constexpr size_t ChannelCount = 4;
		static constexpr float MaxR = 1.f;
		static constexpr float MaxG = 1.f;
		static constexpr float MaxB = 1.f;
		static constexpr float MaxA = 1.f;

		Half R;
		Half G;
		Half B;
		Half A;  // Actually opacity
	};

	struct RGBAF32 {
		static constexpr size_t ChannelCount = 4;
		static constexpr float MaxR = 1.f;
		static constexpr float MaxG = 1.f;
		static constexpr float MaxB = 1.f;
		static constexpr float MaxA = 1.f;

		float R;
		float G;
		float B;
		float A;  // Actually opacity
	};

	struct RGBAF64 {
		static constexpr size_t ChannelCount = 4;
		static constexpr double MaxR = 1.f;
		static constexpr double MaxG = 1.f;
		static constexpr double MaxB = 1.f;
		static constexpr double MaxA = 1.f;

		double R;
		double G;
		double B;
		double A;  // Actually opacity
	};

	template<typename TRGBA, typename T, int RB, int GB, int BB, int AB>
	void ARGBNNNNCommon<TRGBA, T, RB, GB, BB, AB>::SetFrom(const RGBAF16& v) {
		this->SetFrom(
			static_cast<uint32_t>(std::round(Internal::Clamp(255.f * static_cast<float>(v.R), 0.f, 255.f))),
			static_cast<uint32_t>(std::round(Internal::Clamp(255.f * static_cast<float>(v.G), 0.f, 255.f))),
			static_cast<uint32_t>(std::round(Internal::Clamp(255.f * static_cast<float>(v.B), 0.f, 255.f))),
			static_cast<uint32_t>(std::round(Internal::Clamp(255.f * static_cast<float>(v.A), 0.f, 255.f))));
	}

	template<typename TRGBA, typename T, int RB, int GB, int BB, int AB>
	void ARGBNNNNCommon<TRGBA, T, RB, GB, BB, AB>::SetFrom(const RGBAF32& v) {
		this->SetFrom(
			static_cast<uint32_t>(std::round(Internal::Clamp(255.f * v.R, 0.f, 255.f))),
			static_cast<uint32_t>(std::round(Internal::Clamp(255.f * v.G, 0.f, 255.f))),
			static_cast<uint32_t>(std::round(Internal::Clamp(255.f * v.B, 0.f, 255.f))),
			static_cast<uint32_t>(std::round(Internal::Clamp(255.f * v.A, 0.f, 255.f))));
	}

	template<typename TRGBA, typename T, int RB, int GB, int BB, int AB>
	void ARGBNNNNCommon<TRGBA, T, RB, GB, BB, AB>::SetFrom(const RGBAF64& v) {
		this->SetFrom(
			static_cast<uint32_t>(std::round(Internal::Clamp(255. * v.R, 0., 255.))),
			static_cast<uint32_t>(std::round(Internal::Clamp(255. * v.G, 0., 255.))),
			static_cast<uint32_t>(std::round(Internal::Clamp(255. * v.B, 0., 255.))),
			static_cast<uint32_t>(std::round(Internal::Clamp(255. * v.A, 0., 255.))));
	}

	struct RGBA4444 : RGBANNNN<uint16_t, 4, 4, 4, 4> { using TRGBA::RGBANNNN; };
	static_assert(sizeof RGBA4444 == 2);
	struct RGBA5551 : ARGBNNNN<uint16_t, 5, 5, 5, 1> { using TRGBA::ARGBNNNN; };
	static_assert(sizeof RGBA5551 == 2);
	struct RGBA8888 : RGBANNNN<uint32_t, 8, 8, 8, 8> { using TRGBA::RGBANNNN; };
	static_assert(sizeof RGBA8888 == 4);
}

#endif
