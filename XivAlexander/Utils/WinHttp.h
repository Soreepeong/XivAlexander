#pragma once

#include <string>
#include <functional>

namespace Utils::Win32::WinHttp {

	struct Response {
		int StatusCode;
		std::string Body;
	};

	Response Get(const std::string& url, const std::function<void(const void*, size_t)>& onData = {});
}
