#include "pch.h"
#include "LeftoverCleanup.h"

#include "LoaderApp/LoaderUi.h"
#include "LoaderApp/LoginSessionCredentials.h"
#include "resource.h"
#include "XivAlexander.h"

namespace LeftoverCleanup = XivAlexander::LoaderApp::LeftoverCleanup;
namespace Ui = XivAlexander::LoaderApp::Ui;

namespace {
	constexpr auto CachedFolderName = L"Cached";

	DWORD AttributesOf(const std::filesystem::path& path) {
		return GetFileAttributesW(path.c_str());
	}

	bool IsReparsePoint(DWORD attributes) {
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
	}

	// A base must be a full path to a folder named XivAlexander; anything else is refused.
	bool IsAcceptableBase(const std::filesystem::path& base) {
		return base.is_absolute()
			&& base.has_relative_path()
			&& _wcsicmp(base.filename().c_str(), L"XivAlexander") == 0;
	}

	bool IsInsideOrSame(const std::filesystem::path& path, const std::filesystem::path& base) {
		const auto p = path.lexically_normal().native();
		auto b = base.lexically_normal().native();
		while (!b.empty() && (b.back() == L'\\' || b.back() == L'/'))
			b.pop_back();
		if (!Utils::StartsWithI(p, b))
			return false;
		return p.size() == b.size() || p[b.size()] == L'\\' || p[b.size()] == L'/';
	}

	template<typename Fn>
	void ForEachChild(const std::filesystem::path& dir, Fn&& fn) {
		WIN32_FIND_DATAW data{};
		const auto hFind = FindFirstFileExW((dir / L"*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
		if (hFind == INVALID_HANDLE_VALUE)
			return;
		do {
			if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
				continue;
			fn(dir / data.cFileName, data);
		} while (FindNextFileW(hFind, &data));
		FindClose(hFind);
	}

	uint64_t MeasureTree(const std::filesystem::path& path, DWORD attributes, uint64_t fileSize) {
		if (attributes == INVALID_FILE_ATTRIBUTES || IsReparsePoint(attributes))
			return 0;
		if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
			return fileSize;
		uint64_t total = 0;
		ForEachChild(path, [&](const std::filesystem::path& child, const WIN32_FIND_DATAW& data) {
			total += MeasureTree(child, data.dwFileAttributes, (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
		});
		return total;
	}

	uint64_t MeasureTree(const std::filesystem::path& path) {
		WIN32_FILE_ATTRIBUTE_DATA data{};
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
			return 0;
		return MeasureTree(path, data.dwFileAttributes, (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
	}

	// Returns how many entries could not be removed.
	size_t DeleteTree(const std::filesystem::path& path) {
		const auto attributes = AttributesOf(path);
		if (attributes == INVALID_FILE_ATTRIBUTES)
			return 0;
		if (attributes & FILE_ATTRIBUTE_READONLY)
			SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);

		if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
			return DeleteFileW(path.c_str()) ? 0 : 1;

		size_t left = 0;
		// A junction or a directory symlink goes as a link; what it points at stays.
		if (!IsReparsePoint(attributes))
			ForEachChild(path, [&left](const std::filesystem::path& child, const WIN32_FIND_DATAW&) { left += DeleteTree(child); });
		if (!RemoveDirectoryW(path.c_str()))
			left++;
		return left;
	}

	bool RecycleFolder(const std::filesystem::path& path, std::wstring& error) {
		IFileOperation* pOperation{};
		if (const auto hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pOperation)); FAILED(hr)) {
			error = _com_error(hr).ErrorMessage();
			return false;
		}
		const auto releaseOperation = xivres::util::on_dtor([pOperation] { pOperation->Release(); });
		pOperation->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE);

		IShellItem* pItem{};
		if (const auto hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&pItem)); FAILED(hr)) {
			error = _com_error(hr).ErrorMessage();
			return false;
		}
		const auto releaseItem = xivres::util::on_dtor([pItem] { pItem->Release(); });

		auto hr = pOperation->DeleteItem(pItem, nullptr);
		if (SUCCEEDED(hr))
			hr = pOperation->PerformOperations();
		BOOL aborted = FALSE;
		if (SUCCEEDED(hr) && SUCCEEDED(pOperation->GetAnyOperationsAborted(&aborted)) && aborted)
			hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
		if (FAILED(hr)) {
			error = _com_error(hr).ErrorMessage();
			return false;
		}
		return true;
	}

	std::vector<std::filesystem::path> SettingsFiles(const std::filesystem::path& configDir) {
		std::vector<std::filesystem::path> files;
		ForEachChild(configDir, [&files](const std::filesystem::path& child, const WIN32_FIND_DATAW& data) {
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				files.emplace_back(child);
		});
		return files;
	}

	std::vector<std::filesystem::path> UserContentFolders(const std::filesystem::path& configDir) {
		std::vector<std::filesystem::path> folders;
		ForEachChild(configDir, [&folders](const std::filesystem::path& child, const WIN32_FIND_DATAW& data) {
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && _wcsicmp(data.cFileName, CachedFolderName) != 0)
				folders.emplace_back(child);
		});
		return folders;
	}

	bool MatchesScope(const XivAlexander::LoaderApp::LoginSessionCredentials::StoredSession& session, const LeftoverCleanup::Scope& scope) {
		if (session.Publisher == xivres::game_release_publisher::Unspecified)
			return scope.IncludeUnknownSessions;
		return !scope.SessionsPublisher || *scope.SessionsPublisher == session.Publisher;
	}
}

uint32_t XivAlexander::LoaderApp::LeftoverCleanup::Scope::OfferedItems() const {
	return LastInstallation ? (Caches | ProgramFiles | Sessions | Settings | UserContent) : Sessions;
}

size_t XivAlexander::LoaderApp::LeftoverCleanup::CountSessions(const Scope& scope) {
	return static_cast<size_t>(std::ranges::count_if(LoginSessionCredentials::Enumerate(), [&scope](const auto& session) { return MatchesScope(session, scope); }));
}

uint64_t XivAlexander::LoaderApp::LeftoverCleanup::MeasureItem(const Scope& scope, Item item) {
	if (!IsAcceptableBase(scope.ConfigDir) || !IsAcceptableBase(scope.ProgramDir))
		return 0;
	switch (item) {
		case Caches:
			return IsReparsePoint(AttributesOf(scope.ConfigDir)) ? 0 : MeasureTree(scope.ConfigDir / CachedFolderName);
		case ProgramFiles:
			return MeasureTree(scope.ProgramDir);
		case Settings: {
			if (IsReparsePoint(AttributesOf(scope.ConfigDir)))
				return 0;
			uint64_t total = 0;
			for (const auto& file : SettingsFiles(scope.ConfigDir))
				total += MeasureTree(file);
			return total;
		}
		case UserContent: {
			if (IsReparsePoint(AttributesOf(scope.ConfigDir)))
				return 0;
			uint64_t total = 0;
			for (const auto& folder : UserContentFolders(scope.ConfigDir))
				total += MeasureTree(folder);
			return total;
		}
		default:
			return 0;
	}
}

XivAlexander::LoaderApp::LeftoverCleanup::Result XivAlexander::LoaderApp::LeftoverCleanup::Perform(const Scope& scope, uint32_t items) {
	Result result;
	items &= scope.OfferedItems();
	if (scope.TestOnly || !items)
		return result;
	if (!IsAcceptableBase(scope.ConfigDir) || !IsAcceptableBase(scope.ProgramDir)) {
		result.Error = L"Refusing to clean up an unexpected folder.";
		return result;
	}

	// Inside a config folder that is itself a link, every path would lead elsewhere; only the link is ours.
	const auto configIsLink = IsReparsePoint(AttributesOf(scope.ConfigDir));
	const auto checkedDelete = [&](const std::filesystem::path& path, const std::filesystem::path& base) {
		if (!IsInsideOrSame(path, base)) {
			result.EntriesLeft++;
			return;
		}
		result.EntriesLeft += DeleteTree(path);
	};

	if (items & Sessions) {
		for (const auto& session : LoginSessionCredentials::Enumerate()) {
			if (MatchesScope(session, scope) && CredDeleteW(session.TargetName.c_str(), CRED_TYPE_GENERIC, 0))
				result.SessionsRemoved++;
		}
	}

	if ((items & Caches) && !configIsLink)
		checkedDelete(scope.ConfigDir / CachedFolderName, scope.ConfigDir);

	if (items & ProgramFiles)
		checkedDelete(scope.ProgramDir, scope.ProgramDir);

	if ((items & Settings) && !configIsLink) {
		for (const auto& file : SettingsFiles(scope.ConfigDir))
			checkedDelete(file, scope.ConfigDir);
	}

	if ((items & UserContent) && !configIsLink) {
		for (const auto& folder : UserContentFolders(scope.ConfigDir)) {
			if (!IsInsideOrSame(folder, scope.ConfigDir) || folder == scope.ConfigDir) {
				result.EntriesLeft++;
				continue;
			}
			if (IsReparsePoint(AttributesOf(folder))) {
				result.EntriesLeft += RemoveDirectoryW(folder.c_str()) ? 0 : 1;
				continue;
			}
			std::wstring error;
			if (!RecycleFolder(folder, error)) {
				result.EntriesLeft++;
				if (result.Error.empty())
					result.Error = std::move(error);
			}
		}
	}

	// Gone only if nothing is left in it.
	if (!configIsLink)
		RemoveDirectoryW(scope.ConfigDir.c_str());
	return result;
}

namespace {
	enum : UINT {
		WmSizeMeasured = WM_APP + 1,
	};

	struct RowControls {
		LeftoverCleanup::Item Item;
		int CheckId;
		int SizeId;
		int ExtraId;
		bool CheckedByDefault;
	};

	constexpr RowControls Rows[]{
		{LeftoverCleanup::Caches, IDC_CLEANUP_CACHES, IDC_CLEANUP_CACHES_SIZE, 0, true},
		{LeftoverCleanup::ProgramFiles, IDC_CLEANUP_PROGRAM, IDC_CLEANUP_PROGRAM_SIZE, IDC_CLEANUP_PROGRAM_PATH, true},
		{LeftoverCleanup::Sessions, IDC_CLEANUP_SESSIONS, IDC_CLEANUP_SESSIONS_SIZE, 0, true},
		{LeftoverCleanup::Settings, IDC_CLEANUP_SETTINGS, IDC_CLEANUP_SETTINGS_SIZE, 0, false},
		{LeftoverCleanup::UserContent, IDC_CLEANUP_USERCONTENT, IDC_CLEANUP_USERCONTENT_SIZE, 0, false},
	};

	struct CleanupDialog {
		const LeftoverCleanup::Scope& Scope;
		const Ui::Strings Str;
		const bool Dark;
		HWND Hwnd{};
		Ui::DialogTheme Theme;
		uint32_t Chosen = 0;

		std::mutex Mtx;
		std::map<LeftoverCleanup::Item, std::wstring> Measured;
		std::jthread Measurer;

		CleanupDialog(const LeftoverCleanup::Scope& scope, WORD langId, bool dark)
			: Scope(scope)
			, Str(langId)
			, Dark(dark) {
		}

		void OnInit() {
			SetWindowTextW(GetDlgItem(Hwnd, IDC_CLEANUP_INTRO), Str(Scope.LastInstallation ? IDS_LOADER_CLEANUP_INTRO_ALL : IDS_LOADER_CLEANUP_INTRO_REGION));
			SetWindowTextW(GetDlgItem(Hwnd, IDC_CLEANUP_PROGRAM_PATH), Scope.ProgramDir.c_str());

			// Rows not offered are hidden, and the rest move up to close the gaps.
			const auto offered = Scope.OfferedItems();
			auto shift = 0;
			for (const auto& row : Rows) {
				RECT rc{};
				GetWindowRect(GetDlgItem(Hwnd, row.CheckId), &rc);
				MapWindowPoints(nullptr, Hwnd, reinterpret_cast<POINT*>(&rc), 2);
				auto rowBottom = rc.bottom;
				if (row.ExtraId) {
					RECT rcExtra{};
					GetWindowRect(GetDlgItem(Hwnd, row.ExtraId), &rcExtra);
					MapWindowPoints(nullptr, Hwnd, reinterpret_cast<POINT*>(&rcExtra), 2);
					rowBottom = rcExtra.bottom;
				}
				const auto nextTop = rowBottom + (rc.bottom - rc.top) / 3;
				if (!(offered & row.Item)) {
					for (const auto id : {row.CheckId, row.SizeId, row.ExtraId}) {
						if (id)
							ShowWindow(GetDlgItem(Hwnd, id), SW_HIDE);
					}
					shift += nextTop - rc.top;
					continue;
				}
				for (const auto id : {row.CheckId, row.SizeId, row.ExtraId}) {
					if (!id)
						continue;
					const auto hControl = GetDlgItem(Hwnd, id);
					RECT rcControl{};
					GetWindowRect(hControl, &rcControl);
					MapWindowPoints(nullptr, Hwnd, reinterpret_cast<POINT*>(&rcControl), 2);
					SetWindowPos(hControl, nullptr, rcControl.left, rcControl.top - shift, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
				}
				CheckDlgButton(Hwnd, row.CheckId, row.CheckedByDefault ? BST_CHECKED : BST_UNCHECKED);
				SetWindowTextW(GetDlgItem(Hwnd, row.SizeId), Str(IDS_LOADER_CLEANUP_CALCULATING));
			}
			if (shift) {
				for (const auto id : {IDC_CLEANUP_NOTE, IDOK, IDCANCEL}) {
					const auto hControl = GetDlgItem(Hwnd, id);
					RECT rcControl{};
					GetWindowRect(hControl, &rcControl);
					MapWindowPoints(nullptr, Hwnd, reinterpret_cast<POINT*>(&rcControl), 2);
					SetWindowPos(hControl, nullptr, rcControl.left, rcControl.top - shift, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
				}
				RECT rcWindow{};
				GetWindowRect(Hwnd, &rcWindow);
				SetWindowPos(Hwnd, nullptr, 0, 0, rcWindow.right - rcWindow.left, rcWindow.bottom - rcWindow.top - shift, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			}

			const auto hIcon = static_cast<HICON>(LoadImageW(Dll::Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
			SendMessageW(Hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
			Ui::CenterOnMonitorOf(Hwnd, GetWindow(Hwnd, GW_OWNER));
			Theme.Apply(Hwnd, Dark);
			UpdateOk();

			Measurer = std::jthread([this, offered](std::stop_token stop) {
				for (const auto& row : Rows) {
					if (stop.stop_requested())
						return;
					if (!(offered & row.Item))
						continue;
					std::wstring text;
					if (row.Item == LeftoverCleanup::Sessions)
						text = Str.Format(IDS_LOADER_CLEANUP_SESSIONS_COUNT, LeftoverCleanup::CountSessions(Scope));
					else
						text = Ui::FormatSize(LeftoverCleanup::MeasureItem(Scope, row.Item));
					{
						const auto lock = std::lock_guard(Mtx);
						Measured[row.Item] = std::move(text);
					}
					PostMessageW(Hwnd, WmSizeMeasured, 0, 0);
				}
			});
		}

		void OnMeasured() {
			const auto lock = std::lock_guard(Mtx);
			for (const auto& [item, text] : Measured) {
				for (const auto& row : Rows) {
					if (row.Item == item)
						SetWindowTextW(GetDlgItem(Hwnd, row.SizeId), text.c_str());
				}
			}
		}

		void UpdateOk() {
			auto any = false;
			for (const auto& row : Rows)
				any |= (Scope.OfferedItems() & row.Item) && IsDlgButtonChecked(Hwnd, row.CheckId) == BST_CHECKED;
			EnableWindow(GetDlgItem(Hwnd, IDOK), any);
		}

		static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
			auto self = reinterpret_cast<CleanupDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			if (uMsg == WM_INITDIALOG) {
				self = reinterpret_cast<CleanupDialog*>(lParam);
				self->Hwnd = hwnd;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, lParam);
				self->OnInit();
				return TRUE;
			}
			if (!self)
				return FALSE;
			if (const auto result = self->Theme.HandleMessage(hwnd, uMsg, wParam, lParam))
				return *result;

			switch (uMsg) {
				case WmSizeMeasured:
					self->OnMeasured();
					return TRUE;

				case WM_COMMAND:
					switch (LOWORD(wParam)) {
						case IDOK:
							self->Chosen = 0;
							for (const auto& row : Rows) {
								if ((self->Scope.OfferedItems() & row.Item) && IsDlgButtonChecked(hwnd, row.CheckId) == BST_CHECKED)
									self->Chosen |= row.Item;
							}
							EndDialog(hwnd, IDOK);
							return TRUE;
						case IDCANCEL:
							self->Chosen = 0;
							EndDialog(hwnd, IDCANCEL);
							return TRUE;
						default:
							if (HIWORD(wParam) == BN_CLICKED)
								self->UpdateOk();
							return FALSE;
					}

				case WM_DESTROY:
					self->Measurer.request_stop();
					return FALSE;
			}
			return FALSE;
		}
	};
}

uint32_t XivAlexander::LoaderApp::LeftoverCleanup::ShowDialog(HWND hParent, WORD langId, bool dark, const Scope& scope) {
	CleanupDialog dialog(scope, langId, dark);
	DialogBoxIndirectParamW(Dll::Module(), Ui::LoadDialogTemplate(IDD_LOADER_CLEANUP, langId), hParent, &CleanupDialog::DialogProc, reinterpret_cast<LPARAM>(&dialog));
	if (dialog.Measurer.joinable()) {
		dialog.Measurer.request_stop();
		dialog.Measurer.join();
	}
	return dialog.Chosen;
}
