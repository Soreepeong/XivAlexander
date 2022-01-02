#include "pch.h"

#include <XivAlexanderCommon/Utils_Win32_LoadedModule.h>

#include "App_ConfigRepository.h"
#include "App_Window_Dialog_FramerateLockingDialog.h"
#include "App_XivAlexApp.h"
#include "DllMain.h"
#include "resource.h"

static double GetDlgItemDouble(HWND hwnd, int nId) {
	std::wstring buf;
	buf.resize(16);
	buf.resize(GetDlgItemTextW(hwnd, nId, &buf[0], static_cast<int>(buf.size())));
	return std::wcstod(buf.c_str(), nullptr);
}

static void SetDlgItemTextIfChanged(HWND hwnd, int nId, const std::wstring& buf) {
	std::wstring buf2;
	buf2.resize(buf2.size() + 1);
	buf2.resize(GetDlgItemTextW(hwnd, nId, &buf2[0], static_cast<int>(buf2.size())));
	if (buf2 == buf)
		return;
	SetDlgItemTextW(hwnd, nId, buf.c_str());
}

void App::Window::Dialog::FramerateLockingDialog::ShowModal(HWND hParentWindow) {
	struct Data {
		uint64_t OriginalValue{};
		std::shared_ptr<App::Config> Config;

		uint64_t IntervalUs;
		double Fps;
		uint64_t FpsDevUs;
		uint64_t Gcd10ms;

		HWND Hwnd;

		mutable size_t PreventEditUpdateCounter = 0;

		auto PreventEditUpdate() const {
			PreventEditUpdateCounter += 1;
			return Utils::CallOnDestruction([this]() { PreventEditUpdateCounter -= 1; });
		}

		void CalcFps() {
			auto prevIntervalValue = IntervalUs;
			if (!prevIntervalValue)
				prevIntervalValue = 1000000 / 60;  // default to 60fps

			// For (Interval + Deviation) * n >= Gcd for minimum n,
			// find minimum interval
			for (auto i = IntervalUs + 1; i <= 1000000; ++i) {
				if ((Gcd10ms * 10000) % (i + FpsDevUs) > (Gcd10ms * 10000) % (prevIntervalValue + FpsDevUs)) {
					prevIntervalValue = i - 1;
					break;
				}
			}

			UpdateInterval(prevIntervalValue);
		}

		void UpdateInterval(uint64_t v) {
			const auto prevent = PreventEditUpdate();
			if (v > 1000000)
				v = 1000000;
			IntervalUs = v;
			if (IntervalUs == 0)
				Fps = 0.;
			else
				Fps = static_cast<double>(1000000 - FpsDevUs) / IntervalUs;
			SetDlgItemTextIfChanged(Hwnd, IDC_TARGETFRAMERATE_EDIT, std::format(L"{:g}", Fps).c_str());
			SetDlgItemTextIfChanged(Hwnd, IDC_INTERVAL_EDIT, std::format(L"{}", IntervalUs).c_str());
			Preview();
		}

		void UpdateFps(double fps) {
			const auto prevent = PreventEditUpdate();
			if (fps <= 0) {
				fps = 0;
				SetDlgItemTextIfChanged(Hwnd, IDC_TARGETFRAMERATE_EDIT, L"0");
			} else if (fps < 1) {
				fps = 1;
				SetDlgItemTextIfChanged(Hwnd, IDC_TARGETFRAMERATE_EDIT, L"0.000001");
			} else if (fps > 1000000) {
				fps = 1000000;
				SetDlgItemTextIfChanged(Hwnd, IDC_TARGETFRAMERATE_EDIT, L"1000000");
			}
			Fps = fps;
			if (fps > 0)
				IntervalUs = static_cast<uint64_t>(std::ceil(static_cast<double>(1000000 - FpsDevUs) / Fps));
			else
				IntervalUs = 0;
			SetDlgItemTextIfChanged(Hwnd, IDC_INTERVAL_EDIT, std::format(L"{}", IntervalUs).c_str());
			Preview();
		}

		void UpdatePreviewParams(uint64_t fpsDevUs, uint64_t gcd10ms) {
			const auto prevent = PreventEditUpdate();
			FpsDevUs = fpsDevUs;
			Gcd10ms = gcd10ms;
			UpdateFps(Fps);
			Preview();
		}

		void Preview() const {
			const auto prevent = PreventEditUpdate();
			if (IntervalUs == 0)
				SetDlgItemTextW(Hwnd, IDC_ESTIMATEDDRIFT_EDIT, L"-");
			else {
				const auto dv1 = Gcd10ms * 10000 % (IntervalUs + FpsDevUs);
				const auto dv2 = Gcd10ms * 10000 % IntervalUs;
				if (dv1 == dv2)
					SetDlgItemTextW(Hwnd, IDC_ESTIMATEDDRIFT_EDIT, std::format(L"{}", Gcd10ms * 10000 % (IntervalUs + FpsDevUs)).c_str());
				else
					SetDlgItemTextW(Hwnd, IDC_ESTIMATEDDRIFT_EDIT, std::format(L"{} ~ {}", std::min(dv1, dv2), std::max(dv1, dv2)).c_str());
			}
			Config->Runtime.LockFramerate = IsDlgButtonChecked(Hwnd, IDC_PREVIEW) ? IntervalUs : OriginalValue;
		}

	} data{
		.Config = App::Config::Acquire(),
	};

	data.OriginalValue = data.Config->Runtime.LockFramerate;
	data.IntervalUs = data.OriginalValue;
	if (const auto app = XivAlexApp::GetCurrentApp())
		data.FpsDevUs = app->GetMessagePumpIntervalTrackerUs().Deviation(1000000);
	else
		data.FpsDevUs = 10000;  // 10ms
	data.Fps = data.IntervalUs ? 1000000. / data.IntervalUs : 0.;
	data.Gcd10ms = 250;  // 2.50s

	DialogBoxParamW(Dll::Module(), MAKEINTRESOURCEW(IDD_DIALOG_FRAMERATELOCKING), hParentWindow, [](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) -> INT_PTR {
		Data& data = *reinterpret_cast<Data*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		switch (message) {
			case WM_INITDIALOG: {
				Data& data = *reinterpret_cast<Data*>(lParam);
				data.Hwnd = hwnd;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, lParam);

				const auto prevent = data.PreventEditUpdate();
				CheckDlgButton(hwnd, IDC_PREVIEW, TRUE);
				SetDlgItemTextW(hwnd, IDC_INTERVAL_EDIT, std::format(L"{}", data.IntervalUs).c_str());
				SetDlgItemTextW(hwnd, IDC_TARGETFRAMERATE_EDIT, std::format(L"{:g}", data.Fps).c_str());
				SetDlgItemTextW(hwnd, IDC_FPSDEV_EDIT, std::format(L"{}", data.FpsDevUs).c_str());
				SetDlgItemTextW(hwnd, IDC_GCD_EDIT, std::format(L"{}.{:02}", data.Gcd10ms / 100, data.Gcd10ms % 100).c_str());
				data.Preview();
				return 0;
			}

			case WM_COMMAND: {
				switch (LOWORD(wParam)) {
					case IDOK:
						data.Config->Runtime.LockFramerate = data.IntervalUs;
						EndDialog(hwnd, 0);
						return 0;

					case IDCANCEL:
						data.Config->Runtime.LockFramerate = data.OriginalValue;
						EndDialog(hwnd, -1);
						return 0;
					
					case IDC_CALCFPS:
						data.CalcFps();
						return 0;
				
					case IDC_PREVIEW:
						data.Preview();
						return 0;

					case IDC_INTERVAL_EDIT:
						if (HIWORD(wParam) == EN_UPDATE && !data.PreventEditUpdateCounter)
							data.UpdateInterval(GetDlgItemInt(hwnd, IDC_INTERVAL_EDIT, nullptr, FALSE));
						return 0;

					case IDC_TARGETFRAMERATE_EDIT:
						if (HIWORD(wParam) == EN_UPDATE && !data.PreventEditUpdateCounter)
							data.UpdateFps(GetDlgItemDouble(hwnd, IDC_TARGETFRAMERATE_EDIT));
						return 0;

					case IDC_FPSDEV_EDIT:
						[[fallthrough]];
					case IDC_GCD_EDIT:
						if (HIWORD(wParam) == EN_UPDATE && !data.PreventEditUpdateCounter)
							data.UpdatePreviewParams(GetDlgItemInt(hwnd, IDC_FPSDEV_EDIT, nullptr, FALSE), static_cast<uint64_t>(std::round(100. * GetDlgItemDouble(hwnd, IDC_GCD_EDIT))));
						return 0;
				}
				return 0;
			}
		}
		return 0;
		}, reinterpret_cast<LPARAM>(&data));
}

void App::Window::Dialog::FramerateLockingDialog::Show(HWND hParentWindow) {
	void(Utils::Win32::Thread(L"FramerateLockingDialog", [hParentWindow]() {
		ShowModal(hParentWindow);
	}));
}
