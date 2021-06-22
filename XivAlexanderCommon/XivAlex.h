#pragma once

#include <string>
#include <filesystem>
#include <chrono>

namespace XivAlex {
	[[nodiscard]]
	std::tuple<std::wstring, std::wstring> ResolveGameReleaseRegion();
	[[nodiscard]]
	std::tuple<std::wstring, std::wstring> ResolveGameReleaseRegion(const std::filesystem::path& path);

	struct VersionInformation {
		std::string Name;
		std::string Body;
		std::chrono::zoned_time<std::chrono::seconds> PublishDate;
		std::string DownloadLink;
		size_t DownloadSize;
	};
	VersionInformation CheckUpdates();
}
