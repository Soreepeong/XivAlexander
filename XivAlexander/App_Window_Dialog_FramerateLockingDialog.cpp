#include "pch.h"

#include <XivAlexanderCommon/Utils_Win32_LoadedModule.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>

#include "App_ConfigRepository.h"
#include "App_Feature_NetworkTimingHandler.h"
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

static std::wstring GetDlgItemString(HWND hwnd, int nId) {
	std::wstring buf;
	int capacity = 128;
	do {
		capacity *= 2;
		buf.resize(capacity);
		buf.resize(GetDlgItemTextW(hwnd, nId, &buf[0], capacity));
	} while (buf.size() == capacity);
	return buf;
}

static void SetDlgItemTextIfChanged(HWND hwnd, int nId, double val, const std::wstring& repr) {
	const auto buf2 = GetDlgItemString(hwnd, nId);
	if (std::wcstod(&buf2[0], nullptr) == val)
		return;
	SetDlgItemTextW(hwnd, nId, repr.c_str());
}

static void SetDlgItemTextIfChanged(HWND hwnd, int nId, uint64_t val, const std::wstring& repr) {
	const auto buf2 = GetDlgItemString(hwnd, nId);
	if (std::wcstoull(&buf2[0], nullptr, 10) == val)
		return;
	SetDlgItemTextW(hwnd, nId, repr.c_str());
}

void App::Window::Dialog::FramerateLockingDialog::ShowModal(XivAlexApp* app, HWND hParentWindow, const Utils::Win32::Event& hCancelEvent) {
	struct Data {
		XivAlexApp& App;
		HWND const Hwnd;
		HWND const HwndParent;
		HFONT const ControlFont;
		std::shared_ptr<App::Config> const Config;

		uint64_t IntervalUs;
		bool Automatic;
		double FpsRangeFrom;
		double FpsRangeTo;
		uint64_t FpsDevUs;
		uint64_t Gcd10ms;

		Utils::CallOnDestruction::Multiple Cleanup;
		Utils::CallOnDestruction CleanupNetworkTimingHandlerCooldownCallback;
		Utils::NumericStatisticsTracker LocalTracker{ 256, 0 };

		mutable size_t PreventEditUpdateCounter = 0;

		Data(XivAlexApp& app, HWND hwnd, HWND hParentWindow) 
			: App(app)
			, Hwnd(hwnd)
			, HwndParent(hParentWindow)
			, ControlFont([]() { NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) }; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0); return CreateFontIndirectW(&ncm.lfMessageFont); }())
			, Config(App::Config::Acquire())
			, IntervalUs(Config->Runtime.LockFramerateInterval)
			, Automatic(Config->Runtime.LockFramerateAutomatic)
			, FpsRangeFrom(Config->Runtime.LockFramerateTargetFramerateRangeFrom)
			, FpsRangeTo(Config->Runtime.LockFramerateTargetFramerateRangeTo)
			, FpsDevUs(Config->Runtime.LockFramerateMaximumRenderIntervalDeviation)
			, Gcd10ms(Config->Runtime.LockFramerateGlobalCooldown) {
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
			EnumChildWindows(Hwnd, [](HWND hChild, LPARAM lParam) { SendMessageW(hChild, WM_SETFONT, lParam, 0); return TRUE; }, reinterpret_cast<LPARAM>(ControlFont));

			RECT rcParent{};
			MONITORINFO mi{ .cbSize = sizeof mi };
			if (!HwndParent) {
				POINT pt{};
				GetCursorPos(&pt);
				GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
				rcParent = mi.rcWork;
			} else {
				GetWindowRect(HwndParent, &rcParent);
				GetMonitorInfoW(MonitorFromRect(&rcParent, MONITOR_DEFAULTTONEAREST), &mi);
			}

			RECT rcWindow{};
			GetWindowRect(Hwnd, &rcWindow);
			rcWindow.right -= rcWindow.left;
			rcWindow.bottom -= rcWindow.top;
			rcWindow.left = (rcParent.right + rcParent.left - rcWindow.right) / 2;
			rcWindow.top = (rcParent.bottom + rcParent.top - rcWindow.bottom) / 2;
			if (rcWindow.left < mi.rcWork.left)
				rcWindow.left = mi.rcWork.left;
			if (rcWindow.top < mi.rcWork.top)
				rcWindow.top = mi.rcWork.top;
			if (rcWindow.left + rcWindow.right > mi.rcWork.right)
				rcWindow.left = mi.rcWork.right - rcWindow.right;
			if (rcWindow.top + rcWindow.bottom > mi.rcWork.bottom)
				rcWindow.top = mi.rcWork.bottom - rcWindow.bottom;
			SetWindowPos(Hwnd, nullptr, rcWindow.left, rcWindow.top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);

			const auto prevent = PreventEditUpdate();
			CheckDlgButton(Hwnd, IDC_AUTOMATICFPSLOCKING, Automatic ? TRUE : FALSE);
			SetDlgItemTextW(Hwnd, IDC_INTERVAL_EDIT, std::format(L"{}", IntervalUs).c_str());
			SetDlgItemTextW(Hwnd, IDC_TARGETFRAMERATE_EDIT, std::format(L"{:g}", Fps()).c_str());
			SetDlgItemTextW(Hwnd, IDC_TARGETFPS_FROM_EDIT, std::format(L"{:g}", FpsRangeFrom).c_str());
			SetDlgItemTextW(Hwnd, IDC_TARGETFPS_TO_EDIT, std::format(L"{:g}", FpsRangeTo).c_str());
			SetDlgItemTextW(Hwnd, IDC_FPSDEV_EDIT, std::format(L"{}", FpsDevUs).c_str());
			SetDlgItemTextW(Hwnd, IDC_GCD_EDIT, std::format(L"{}.{:02}", Gcd10ms / 100, Gcd10ms % 100).c_str());
			SetDlgItemTextW(Hwnd, IDC_ESTIMATENUMGCD_DURATION_EDIT, L"5:00");
			SetDlgItemTextW(Hwnd, IDC_ESTIMATEDURATION_NUMGCD_EDIT, L"120");
			Automatic_OnChange();

			Cleanup += Config->Runtime.UseNetworkTimingHandler.OnChangeListener([&](const auto&) {
				if (Config->Runtime.UseNetworkTimingHandler) {
					void(Utils::Win32::Thread(L"UseNetworkTimingHandler/OnCooldownGroupUpdateListener Waiter", [&]() {
						while (!TryRegisterTimingHandler())
							Sleep(1);
						}));
				} else {
					CleanupNetworkTimingHandlerCooldownCallback.Clear();
				}
				});
			TryRegisterTimingHandler();
		}

		~Data() {
			Cleanup.Clear();
			CleanupNetworkTimingHandlerCooldownCallback.Clear();
			DeleteObject(ControlFont);
		}

		bool TryRegisterTimingHandler() {
			if (const auto handler = App.GetNetworkTimingHandler()) {
				CleanupNetworkTimingHandlerCooldownCallback = handler->OnCooldownGroupUpdateListener([&](const Feature::NetworkTimingHandler::CooldownGroup& group, bool newDriftItem) {
					if (group.Id != Feature::NetworkTimingHandler::CooldownGroup::Id_Gcd || !Automatic)
						return;
					PostMessageW(Hwnd, WM_APP + 1, group.DurationUs / 10000, newDriftItem ? static_cast<int32_t>(group.DriftTrackerUs.Latest()) : INT32_MAX);
					});
				if (const auto& group = handler->GetCooldownGroup(Feature::NetworkTimingHandler::CooldownGroup::Id_Gcd); group.DurationUs != UINT64_MAX) {
					Gcd10ms = group.DurationUs / 10000;
					IntervalUs = Config::RuntimeRepository::CalculateLockFramerateIntervalUs(FpsRangeFrom, FpsRangeTo, group.DurationUs, FpsDevUs);
					ReflectDisplay();
				}
				return true;
			}
			return false;
		}

		void Save() {
			const auto _ = PreventEditUpdate();
			Config->Runtime.LockFramerateAutomatic = Automatic;
			if (!Automatic) {
				Config->Runtime.LockFramerateInterval = IntervalUs;
				Config->Runtime.LockFramerateGlobalCooldown = Gcd10ms;
			}
			Config->Runtime.LockFramerateTargetFramerateRangeFrom = FpsRangeFrom;
			Config->Runtime.LockFramerateTargetFramerateRangeTo = FpsRangeTo;
			Config->Runtime.LockFramerateMaximumRenderIntervalDeviation = FpsDevUs;
			ReflectDisplay();
		}

		double Fps() const {
			return IntervalUs ? 1000000. / IntervalUs : 0;
		}

		void Fps(double newValue) {
			IntervalUs = newValue > 0 ? static_cast<uint64_t>(1000000. / newValue) : 0;
		}

		Utils::CallOnDestruction PreventEditUpdate() const {
			PreventEditUpdateCounter += 1;
			return { [this]() { PreventEditUpdateCounter -= 1; } };
		}

		void RenderInterval_OnChange() {
			const auto prevent = PreventEditUpdate();
			IntervalUs = std::min<uint64_t>(1000000, GetDlgItemInt(Hwnd, IDC_INTERVAL_EDIT, nullptr, FALSE));
			ReflectDisplay();
		}

		void Framerate_OnChange() {
			const auto prevent = PreventEditUpdate();
			const auto fps = GetDlgItemDouble(Hwnd, IDC_TARGETFRAMERATE_EDIT);
			if (fps <= 0)
				Fps(fps);
			else if (fps < 1)
				Fps(1);
			else if (fps > 1000000)
				Fps(1000000);
			else
				Fps(fps);
			ReflectDisplay();
		}

		void Automatic_OnChange() {
			Automatic = IsDlgButtonChecked(Hwnd, IDC_AUTOMATICFPSLOCKING);
			if (Automatic) {
				auto& rt = Config->Runtime;
				if (const auto handler = App.GetNetworkTimingHandler()) {
					if (const auto& group = handler->GetCooldownGroup(Feature::NetworkTimingHandler::CooldownGroup::Id_Gcd); group.DurationUs != UINT64_MAX) {
						Gcd10ms = group.DurationUs / 10000;
					}
				}
				IntervalUs = Config::RuntimeRepository::CalculateLockFramerateIntervalUs(FpsRangeFrom, FpsRangeTo, Gcd10ms * 10000, FpsDevUs);
				SendMessageW(GetDlgItem(Hwnd, IDC_INTERVAL_EDIT), EM_SETREADONLY, TRUE, 0);
				SendMessageW(GetDlgItem(Hwnd, IDC_TARGETFRAMERATE_EDIT), EM_SETREADONLY, TRUE, 0);
				SendMessageW(GetDlgItem(Hwnd, IDC_GCD_EDIT), EM_SETREADONLY, TRUE, 0);
				EnableWindow(GetDlgItem(Hwnd, IDC_CALCFPS), FALSE);
			} else {
				SendMessageW(GetDlgItem(Hwnd, IDC_INTERVAL_EDIT), EM_SETREADONLY, FALSE, 0);
				SendMessageW(GetDlgItem(Hwnd, IDC_TARGETFRAMERATE_EDIT), EM_SETREADONLY, FALSE, 0);
				SendMessageW(GetDlgItem(Hwnd, IDC_GCD_EDIT), EM_SETREADONLY, FALSE, 0);
				EnableWindow(GetDlgItem(Hwnd, IDC_CALCFPS), TRUE);
			}
			ReflectDisplay();
		}

		void TargetFramerateRange_OnChange() {
			FpsRangeFrom = std::min(1000000., std::max(1., GetDlgItemDouble(Hwnd, IDC_TARGETFPS_FROM_EDIT)));
			FpsRangeTo = std::min(1000000., std::max(1., GetDlgItemDouble(Hwnd, IDC_TARGETFPS_TO_EDIT)));
			if (Automatic) {
				IntervalUs = Config::RuntimeRepository::CalculateLockFramerateIntervalUs(FpsRangeFrom, FpsRangeTo, Gcd10ms * 10000, FpsDevUs);
				ReflectDisplay();
			}
		}

		void MaxRenderIntervalDeviation_OnChange() {
			FpsDevUs = GetDlgItemInt(Hwnd, IDC_FPSDEV_EDIT, nullptr, FALSE);
			if (Automatic)
				IntervalUs = Config::RuntimeRepository::CalculateLockFramerateIntervalUs(FpsRangeFrom, FpsRangeTo, Gcd10ms * 10000, FpsDevUs);
			ReflectDisplay();
		}

		void Gcd_OnChange() {
			Gcd10ms = static_cast<uint64_t>(std::round(100. * GetDlgItemDouble(Hwnd, IDC_GCD_EDIT)));
			ReflectDisplay();
		}

		void CalcFps_Click() {
			IntervalUs = Config::RuntimeRepository::CalculateLockFramerateIntervalUs(FpsRangeFrom, FpsRangeTo, Gcd10ms * 10000, FpsDevUs);
			ReflectDisplay();
		}

		void ResetLastGcd_Click() {
			ListBox_ResetContent(GetDlgItem(Hwnd, IDC_LASTGCD_LIST));
			LocalTracker.Clear();
			ReflectDisplay();
		}

		void Disable_Click() {
			const auto _ = PreventEditUpdate();
			Config->Runtime.LockFramerateAutomatic = false;
			Config->Runtime.LockFramerateInterval = 0;
			DestroyWindow(Hwnd);
		}

		static std::wstring FormatDuration(double d) {
			if (d < 60)
				return std::format(L"{:g}", d);
			if (d < 60 * 60)
				return std::format(L"{:02}:{:02}.{:06}",
					static_cast<int>(std::floor(d / 60.)),
					static_cast<int>(std::fmod(std::floor(d), 60.)),
					static_cast<int>(std::fmod(d, 1) * 1000000));
			return std::format(L"{:02}:{:02}:{:02}.{:06}",
				static_cast<int>(std::floor(d / 3600.)),
				static_cast<int>(std::fmod(std::floor(d / 60.), 60.)),
				static_cast<int>(std::fmod(std::floor(d), 60.)),
				static_cast<int>(std::fmod(d, 1) * 1000000));
		}

		void ReflectDisplay() const {
			const auto prevent = PreventEditUpdate();
			SetDlgItemTextIfChanged(Hwnd, IDC_TARGETFRAMERATE_EDIT, Fps(), std::format(L"{:g}", Fps()));
			SetDlgItemTextIfChanged(Hwnd, IDC_INTERVAL_EDIT, IntervalUs, std::format(L"{}", IntervalUs));
			SetDlgItemTextIfChanged(Hwnd, IDC_GCD_EDIT, Gcd10ms, std::format(L"{}.{:02}", Gcd10ms / 100, Gcd10ms % 100));
			if (IntervalUs == 0) {
				SetDlgItemTextW(Hwnd, IDC_ESTIMATENUMGCD_RESULT_EDIT, L"-");
				SetDlgItemTextW(Hwnd, IDC_ESTIMATEDGCD_EDIT, L"-");
				SetDlgItemTextW(Hwnd, IDC_ESTIMATEDURATION_RESULT_EDIT, L"-");
			} else {
				auto bestCaseInterval = (Gcd10ms * 10000 + IntervalUs - 1) / IntervalUs * IntervalUs;
				auto worstCaseInterval = (Gcd10ms * 10000 + IntervalUs + FpsDevUs - 1) / (IntervalUs + FpsDevUs) * (IntervalUs + FpsDevUs);
				if (worstCaseInterval < bestCaseInterval) {
					const auto t = bestCaseInterval;
					bestCaseInterval = worstCaseInterval;
					worstCaseInterval = t;
				}

				double duration = 0;
				for (const auto& part : Utils::StringSplit(Utils::StringTrim(GetDlgItemString(Hwnd, IDC_ESTIMATENUMGCD_DURATION_EDIT)), std::wstring(L":")))
					duration = duration * 60. + 1000000. * std::wcstod(Utils::StringTrim(part).c_str(), nullptr);
				SetDlgItemTextW(Hwnd, IDC_ESTIMATENUMGCD_RESULT_EDIT, std::format(L"{:g} ~ {:g}", duration / worstCaseInterval, duration / bestCaseInterval).c_str());

				SetDlgItemTextW(Hwnd, IDC_ESTIMATEDGCD_EDIT, std::format(L"{} ~ {}", FormatDuration(bestCaseInterval / 1000000.), FormatDuration(worstCaseInterval / 1000000.)).c_str());

				const auto numGcd = GetDlgItemInt(Hwnd, IDC_ESTIMATEDURATION_NUMGCD_EDIT, nullptr, FALSE);
				SetDlgItemTextW(Hwnd, IDC_ESTIMATEDURATION_RESULT_EDIT, std::format(L"{} ~ {}", FormatDuration(bestCaseInterval * numGcd / 1000000.), FormatDuration(worstCaseInterval * numGcd / 1000000.)).c_str());
			}

			if (LocalTracker.Empty()) {
				SetDlgItemTextW(Hwnd, IDC_LASTGCD_MEAN_EDIT, L"-");
				SetDlgItemTextW(Hwnd, IDC_LASTGCD_STDDEV_EDIT, L"-");
				SetDlgItemTextW(Hwnd, IDC_LASTGCD_MEDIAN_EDIT, L"-");
				SetDlgItemTextW(Hwnd, IDC_LASTGCD_RANGE_EDIT_MIN, L"-");
				SetDlgItemTextW(Hwnd, IDC_LASTGCD_RANGE_EDIT_MAX, L"-");
			} else {
				const auto [mean, dev] = LocalTracker.MeanAndDeviation();
				SetDlgItemTextW(Hwnd, IDC_LASTGCD_MEAN_EDIT, std::format(L"{}", mean).c_str());
				SetDlgItemTextW(Hwnd, IDC_LASTGCD_STDDEV_EDIT, std::format(L"{}", dev).c_str());
				SetDlgItemTextW(Hwnd, IDC_LASTGCD_MEDIAN_EDIT, std::format(L"{}", LocalTracker.Median()).c_str());
				SetDlgItemTextW(Hwnd, IDC_LASTGCD_RANGE_EDIT_MIN, std::format(L"{}", LocalTracker.Min()).c_str());
				SetDlgItemTextW(Hwnd, IDC_LASTGCD_RANGE_EDIT_MAX, std::format(L"{}", LocalTracker.Max()).c_str());
			}

			auto changed = Automatic != Config->Runtime.LockFramerateAutomatic;
			if (!changed) {
				changed |= FpsRangeFrom != Config->Runtime.LockFramerateTargetFramerateRangeFrom;
				changed |= FpsRangeTo != Config->Runtime.LockFramerateTargetFramerateRangeTo;
				changed |= FpsDevUs != Config->Runtime.LockFramerateMaximumRenderIntervalDeviation;
				if (!Automatic) {
					changed |= IntervalUs != Config->Runtime.LockFramerateInterval;
					changed |= Gcd10ms != Config->Runtime.LockFramerateGlobalCooldown;
				}
			}
			EnableWindow(GetDlgItem(Hwnd, IDOK), changed);
			EnableWindow(GetDlgItem(Hwnd, IDC_APPLY), changed);
		}

	};

	struct InitDialogParams {
		std::unique_ptr<Data> Data;
		XivAlexApp& App;
		HWND HwndParent;
	} params{.App = *app, .HwndParent = hParentWindow};

	Utils::Win32::Thread cancelWatcher;
	if (hCancelEvent) {
		hCancelEvent.Reset();
		cancelWatcher = Utils::Win32::Thread(L"FramerateLockingDialog/CancelWatcher", [&]() {
			hCancelEvent.Wait();
			if (auto pData = params.Data.get()) {
				SendMessage(pData->Hwnd, WM_CLOSE, 0, 0);
			}
			});
	}

	const auto hDlgModalParent = GetWindowThreadProcessId(hParentWindow, nullptr) == GetCurrentThreadId() ? hParentWindow : nullptr;
	DialogBoxIndirectParamW(Dll::Module(), Utils::Win32::GlobalResource(Dll::Module(), RT_DIALOG, MAKEINTRESOURCEW(IDD_DIALOG_FRAMERATELOCKING), Config::Acquire()->Runtime.GetLangId()).GetDataAs<DLGTEMPLATE>(), hDlgModalParent, [](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) -> INT_PTR {
		auto& data = *reinterpret_cast<Data*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		switch (message) {
			case WM_INITDIALOG: {
				auto& params = *reinterpret_cast<InitDialogParams*>(lParam);
				params.Data = std::make_unique<Data>(params.App, hwnd, params.HwndParent);
				return 0;
			}

			case WM_APP + 1: {
				data.Gcd10ms = static_cast<uint32_t>(wParam);
				const auto durationUs = data.Gcd10ms * 10000;
				data.IntervalUs = Config::RuntimeRepository::CalculateLockFramerateIntervalUs(data.FpsRangeFrom, data.FpsRangeTo, durationUs, data.FpsDevUs);
				if (const auto latest = static_cast<int32_t>(lParam); lParam != INT32_MAX) {
					data.LocalTracker.AddValue(lParam);
					ListBox_InsertString(GetDlgItem(hwnd, IDC_LASTGCD_LIST), 0, std::format(L"{}.{:02}s {:+07}us", durationUs / 1000000, durationUs % 1000000 / 10000, lParam).c_str());
					for (auto i = ListBox_GetCount(GetDlgItem(hwnd, IDC_LASTGCD_LIST)) - 1; i >= 1024; --i)
						ListBox_DeleteString(GetDlgItem(hwnd, IDC_LASTGCD_LIST), i);
				}
				data.ReflectDisplay();
				return 0;
			}

			case WM_COMMAND: {
				switch (LOWORD(wParam)) {
					case IDOK:
						data.Save();
						EndDialog(hwnd, 0);
						return 0;

					case IDCANCEL:
						EndDialog(hwnd, -1);
						return 0;

					case IDC_APPLY:
						data.Save();
						return 0;

					case IDC_INTERVAL_EDIT:
						if (HIWORD(wParam) == EN_UPDATE && !data.PreventEditUpdateCounter)
							data.RenderInterval_OnChange();
						return 0;

					case IDC_TARGETFRAMERATE_EDIT:
						if (HIWORD(wParam) == EN_UPDATE && !data.PreventEditUpdateCounter)
							data.Framerate_OnChange();
						return 0;

					case IDC_TARGETFPS_FROM_EDIT:
					case IDC_TARGETFPS_TO_EDIT:
						if (HIWORD(wParam) == EN_UPDATE && !data.PreventEditUpdateCounter)
							data.TargetFramerateRange_OnChange();
						return 0;

					case IDC_FPSDEV_EDIT:
						if (HIWORD(wParam) == EN_UPDATE && !data.PreventEditUpdateCounter)
							data.MaxRenderIntervalDeviation_OnChange();
						return 0;

					case IDC_GCD_EDIT:
						if (HIWORD(wParam) == EN_UPDATE && !data.PreventEditUpdateCounter)
							data.Gcd_OnChange();
						return 0;
					
					case IDC_CALCFPS:
						data.CalcFps_Click();
						return 0;

					case IDC_AUTOMATICFPSLOCKING:
						if (!data.PreventEditUpdateCounter)
							data.Automatic_OnChange();
						return 0;

					case IDC_ESTIMATENUMGCD_DURATION_EDIT:
					case IDC_ESTIMATEDURATION_NUMGCD_EDIT:
						data.ReflectDisplay();
						return 0;

					case IDC_LASTGCD_RESET:
						data.ResetLastGcd_Click();
						return 0;

					case IDC_DISABLE:
						data.Disable_Click();
						return 0;
				}
				return 0;
			}

			case WM_NCHITTEST: {
				return HTCAPTION;
			}

			case WM_DESTROY: {
				return 0;
			}
		}
		return 0;
		}, reinterpret_cast<LPARAM>(&params));
	if (hCancelEvent)
		hCancelEvent.Set();
	if (cancelWatcher)
		cancelWatcher.Wait();
}

Utils::CallOnDestruction App::Window::Dialog::FramerateLockingDialog::Show(XivAlexApp* app, HWND hParentWindow) {
	const auto hEvent = Utils::Win32::Event::Create();
	const auto hThread = Utils::Win32::Thread(L"FramerateLockingDialog", [app, hParentWindow, hEvent]() {
		ShowModal(app, hParentWindow, hEvent);
	});
	return { [=]() {
		hEvent.Set();
		hThread.Wait();
	} };
}
