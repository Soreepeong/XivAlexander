#include "pch.h"
#include "PatchCodeRepository.h"

#include "Misc/Logger.h"

XivAlexander::PatchCodeRepository::PatchCodeRepository(std::filesystem::path directory)
	: m_logger(Misc::Logger::Acquire())
	, m_directory(std::move(directory))
	, m_entries(std::make_shared<const std::vector<Entry>>())
	, m_stopWatching(Utils::Win32::Event::Create()) {
}

XivAlexander::PatchCodeRepository::~PatchCodeRepository() {
	m_stopWatching.Set();
	if (m_watcher)
		m_watcher.Wait();
}

std::shared_ptr<const std::vector<XivAlexander::PatchCodeRepository::Entry>> XivAlexander::PatchCodeRepository::GetEntries() const {
	const auto lock = std::lock_guard(m_mtx);
	return m_entries;
}

void XivAlexander::PatchCodeRepository::Reload() {
	std::vector<Entry> entries;

	std::error_code ec;
	for (const auto& item : std::filesystem::directory_iterator(m_directory, ec)) {
		if (!item.is_regular_file(ec) || _wcsicmp(item.path().extension().c_str(), L".json") != 0)
			continue;

		auto fileName = xivres::util::unicode::convert<std::string>(item.path().filename().wstring());
		try {
			const auto json = Utils::ParseJsonFromFile(item.path());
			auto patch = json.get<PatchInstruction>();

			if (json.value("HmacKey", "") != patch.HmacKey)
				Utils::SaveJsonToFile(item.path(), patch);

			auto digest = patch.Digest();
			entries.emplace_back(Entry{
				.Path = item.path(),
				.FileName = std::move(fileName),
				.Patch = std::move(patch),
				.Digest = std::move(digest),
			});
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::PatchCode, "{}: failed to load: {}", fileName, e.what());
		}
	}
	if (ec)
		m_logger->Format<LogLevel::Warning>(LogCategory::PatchCode, "Failed to list {}: {}", xivres::util::unicode::convert<std::string>(m_directory.wstring()), ec.message());

	std::ranges::sort(entries, [](const Entry& l, const Entry& r) {
		return _wcsicmp(l.Path.filename().c_str(), r.Path.filename().c_str()) < 0;
	});

	{
		const auto lock = std::lock_guard(m_mtx);
		if (*m_entries == entries)
			return;
		m_entries = std::make_shared<const std::vector<Entry>>(std::move(entries));
	}
	OnChange();
}

void XivAlexander::PatchCodeRepository::StartWatching() {
	if (m_watcher)
		return;

	m_watcher = Utils::Win32::Thread(L"XivAlexander::PatchCodeRepository::Watcher", [this] {
		const auto hChange = FindFirstChangeNotificationW(m_directory.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
		if (hChange == INVALID_HANDLE_VALUE) {
			m_logger->Format<LogLevel::Warning>(LogCategory::PatchCode, "Cannot watch {} for changes: {}", xivres::util::unicode::convert<std::string>(m_directory.wstring()), GetLastError());
			return;
		}
		const auto closeChange = xivres::util::on_dtor([hChange] { FindCloseChangeNotification(hChange); });

		const HANDLE handles[]{m_stopWatching, hChange};
		while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
			// Editors and the update check write in bursts; wait until the directory settles.
			DWORD waitResult;
			do {
				if (!FindNextChangeNotification(hChange))
					return;
				waitResult = WaitForMultipleObjects(2, handles, FALSE, 300);
			} while (waitResult == WAIT_OBJECT_0 + 1);
			if (waitResult != WAIT_TIMEOUT)
				return;

			try {
				Reload();
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Error>(LogCategory::PatchCode, "Reload failed: {}", e.what());
			}
		}
	});
}
