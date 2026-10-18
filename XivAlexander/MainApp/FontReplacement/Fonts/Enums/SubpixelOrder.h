#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	enum class SubpixelOrder : uint8_t {
		None,
		Rgb,
		Bgr,
		VerticalRgb,
		VerticalBgr,
	};

	[[nodiscard]] constexpr bool IsVertical(SubpixelOrder order) { return order == SubpixelOrder::VerticalRgb || order == SubpixelOrder::VerticalBgr; }

	// R, G, B coverage is stored swapped from the stripes' order.
	[[nodiscard]] constexpr bool IsBlueFirst(SubpixelOrder order) { return order == SubpixelOrder::Bgr || order == SubpixelOrder::VerticalBgr; }
}
