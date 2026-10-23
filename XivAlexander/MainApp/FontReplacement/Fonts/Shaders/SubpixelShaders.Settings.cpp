#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Shaders/SubpixelShaders.h"

#include "Config/FontReplacementConfigs.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	DWORD GetOrientation(HMONITOR monitor) {
		if (!monitor)
			return DMDO_DEFAULT;
		MONITORINFOEXW info{};
		info.cbSize = sizeof info;
		if (!GetMonitorInfoW(monitor, &info))
			return DMDO_DEFAULT;
		DEVMODEW mode{.dmSize = sizeof(DEVMODEW)};
		return EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) ? mode.dmDisplayOrientation : DMDO_DEFAULT;
	}
}

FontReplacement::SubpixelShaders::SettingsRecord FontReplacement::SubpixelShaders::SettingsRecord::FromWindow(HWND window) {
	return FromMonitor(window ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : nullptr);
}

FontReplacement::SubpixelShaders::SettingsRecord FontReplacement::SubpixelShaders::SettingsRecord::FromMonitor(HMONITOR monitor) {
	SettingsRecord res;
	auto geometry = DWRITE_PIXEL_GEOMETRY_RGB;
	try {
		IDWriteFactory7Ptr factory;
		ThrowOnError(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory7), reinterpret_cast<IUnknown**>(&factory)), "DWriteCreateFactory");
		IDWriteRenderingParamsPtr created;
		if (monitor)
			ThrowOnError(factory->CreateMonitorRenderingParams(monitor, &created), "CreateMonitorRenderingParams");
		else
			ThrowOnError(factory->CreateRenderingParams(&created), "CreateRenderingParams");
		IDWriteRenderingParams3Ptr parameters;
		ThrowOnError(created.QueryInterface(__uuidof(IDWriteRenderingParams3), &parameters), "IDWriteRenderingParams3");
		geometry = parameters->GetPixelGeometry();
		res.Level = std::clamp(parameters->GetClearTypeLevel(), 0.f, 1.f);
		res.Contrast = (std::max)(parameters->GetEnhancedContrast(), 0.f);
		res.Gamma = (std::max)(parameters->GetGamma(), 1.f);
		res.GrayscaleContrast = (std::max)(parameters->GetGrayscaleEnhancedContrast(), 0.f);
	} catch (const std::exception& e) {
		Host::Warning("Windows' ClearType settings couldn't be read; using the defaults: {}", e.what());
	}
	// Windows' pixel geometry is the panel's, left to right, whichever way the picture is turned on it.
	const auto bgr = geometry == DWRITE_PIXEL_GEOMETRY_BGR;
	if (geometry != DWRITE_PIXEL_GEOMETRY_RGB && !bgr) {
		res.Order = SubpixelOrder::None;
		return res;
	}
	switch (GetOrientation(monitor)) {
		case DMDO_90:
			// The picture turned clockwise on a panel turned counterclockwise has its left at the bottom.
			res.Order = bgr ? SubpixelOrder::VerticalRgb : SubpixelOrder::VerticalBgr;
			break;
		case DMDO_180:
			res.Order = bgr ? SubpixelOrder::Rgb : SubpixelOrder::Bgr;
			break;
		case DMDO_270:
			res.Order = bgr ? SubpixelOrder::VerticalBgr : SubpixelOrder::VerticalRgb;
			break;
		default:
			res.Order = bgr ? SubpixelOrder::Bgr : SubpixelOrder::Rgb;
			break;
	}
	return res;
}

FontReplacement::SubpixelShaders::SettingsRecord FontReplacement::SubpixelShaders::SettingsRecord::From(FontReplacementSubpixelMode mode, const FontReplacementClearTypeConfig& clearType, HWND window) {
	switch (mode) {
		case FontReplacementSubpixelMode::System: {
			auto res = FromWindow(window);
			UINT smoothing = 0, type = 0;
			if (!SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0) || !smoothing
				|| !SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0) || type != FE_FONTSMOOTHINGCLEARTYPE)
				res.Order = SubpixelOrder::None;
			return res;
		}

		case FontReplacementSubpixelMode::Rgb:
		case FontReplacementSubpixelMode::Bgr:
		case FontReplacementSubpixelMode::VerticalRgb:
		case FontReplacementSubpixelMode::VerticalBgr: {
			const auto clamped = clearType.Clamped();
			return {
				.Order = mode == FontReplacementSubpixelMode::Rgb ? SubpixelOrder::Rgb
					: mode == FontReplacementSubpixelMode::Bgr ? SubpixelOrder::Bgr
					: mode == FontReplacementSubpixelMode::VerticalRgb ? SubpixelOrder::VerticalRgb
					: SubpixelOrder::VerticalBgr,
				.Level = clamped.Level,
				.Contrast = clamped.Contrast,
				.Gamma = clamped.Gamma,
				.GrayscaleContrast = clamped.GrayscaleContrast,
			};
		}

		default:
			return {};
	}
}
