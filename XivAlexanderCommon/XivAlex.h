#pragma once

#include <string>
#include <filesystem>

namespace XivAlex {
	[[nodiscard]]
	std::tuple<std::wstring, std::wstring> ResolveGameReleaseRegion();
	[[nodiscard]]
	std::tuple<std::wstring, std::wstring> ResolveGameReleaseRegion(const std::filesystem::path& path);
}
