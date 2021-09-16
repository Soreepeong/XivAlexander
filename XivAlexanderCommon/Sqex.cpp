#include "pch.h"
#include "Sqex.h"

void Sqex::to_json(nlohmann::json& j, const Language& value) {
	switch (value) {
		case Language::Japanese:
			j = "Japanese";
			break;
		case Language::English:
			j = "English";
			break;
		case Language::German:
			j = "German";
			break;
		case Language::French:
			j = "French";
			break;
		case Language::ChineseSimplified:
			j = "ChineseSimplified";
			break;
		case Language::ChineseTraditional:
			j = "ChineseTraditional";
			break;
		case Language::Korean:
			j = "Korean";
			break;
		case Language::Unspecified:
		default:
			j = "Unspecified";  // fallback
	}
}

void Sqex::from_json(const nlohmann::json& j, Language& newValue) {
	auto newValueString = FromUtf8(j.get<std::string>());
	CharLowerW(&newValueString[0]);

	newValue = Language::Unspecified;
	if (newValueString.empty())
		return;

	if (newValueString.substr(0, std::min<size_t>(8, newValueString.size())) == L"japanese")
		newValue = Language::Japanese;
	else if (newValueString.substr(0, std::min<size_t>(7, newValueString.size())) == L"english")
		newValue = Language::English;
	else if (newValueString.substr(0, std::min<size_t>(6, newValueString.size())) == L"german")
		newValue = Language::German;
	else if (newValueString.substr(0, std::min<size_t>(8, newValueString.size())) == L"deutsche")
		newValue = Language::German;
	else if (newValueString.substr(0, std::min<size_t>(6, newValueString.size())) == L"french")
		newValue = Language::French;
	else if (newValueString.substr(0, std::min<size_t>(17, newValueString.size())) == L"chinesesimplified")
		newValue = Language::ChineseSimplified;
	else if (newValueString.substr(0, std::min<size_t>(18, newValueString.size())) == L"chinesetraditional")
		newValue = Language::ChineseTraditional;
	else if (newValueString.substr(0, std::min<size_t>(6, newValueString.size())) == L"korean")
		newValue = Language::Korean;
}

void Sqex::to_json(nlohmann::json& j, const Region& value) {
	switch (value) {
		case Region::Japan:
			j = "Japan";
			break;
		case Region::NorthAmerica:
			j = "NorthAmerica";
			break;
		case Region::Europe:
			j = "Europe";
			break;
		case Region::China:
			j = "China";
			break;
		case Region::Korea:
			j = "Korea";
			break;
		case Region::Unspecified:
		default:
			j = "Unspecified";
	}
}

void Sqex::from_json(const nlohmann::json& j, Region& newValue) {
	auto newValueString = FromUtf8(j.get<std::string>());
	CharLowerW(&newValueString[0]);

	newValue = Region::Unspecified;
	if (!newValueString.empty())
		return;

	if (newValueString.substr(0, std::min<size_t>(5, newValueString.size())) == L"japan")
		newValue = Region::Japan;
	else if (newValueString.substr(0, std::min<size_t>(12, newValueString.size())) == L"northamerica")
		newValue = Region::NorthAmerica;
	else if (newValueString.substr(0, std::min<size_t>(6, newValueString.size())) == L"europe")
		newValue = Region::Europe;
	else if (newValueString.substr(0, std::min<size_t>(5, newValueString.size())) == L"china")
		newValue = Region::China;
	else if (newValueString.substr(0, std::min<size_t>(5, newValueString.size())) == L"korea")
		newValue = Region::Korea;
}

Sqex::RandomAccessStream::RandomAccessStream() = default;

Sqex::RandomAccessStream::~RandomAccessStream() = default;

uint64_t Sqex::RandomAccessStream::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	return ReadStreamPartial(offset, buf, length);
}

void Sqex::RandomAccessStream::ReadStream(uint64_t offset, void* buf, uint64_t length) const {
	if (ReadStreamPartial(offset, buf, length) != length)
		throw std::runtime_error("Reached end of stream before reading all of the requested data.");
}

Sqex::BufferedRandomAccessStream::~BufferedRandomAccessStream() {
	for (const auto addr : m_buffers)
		if (addr)
			VirtualFree(addr, 0, MEM_RELEASE);
}

uint64_t Sqex::BufferedRandomAccessStream::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	const auto streamSize = StreamSize();

	if (offset >= streamSize)
		return 0;
	if (offset + length > streamSize)
		length = streamSize - offset;
	
	auto out = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length));
	auto relativeOffset = static_cast<size_t>(offset - offset / m_bufferSize * m_bufferSize);
	for (auto i = offset / m_bufferSize * m_bufferSize; i < offset + length; i += m_bufferSize) {
		auto& buffer = m_buffers[i / m_bufferSize];
		if (!buffer) {
			buffer = VirtualAlloc(nullptr, m_bufferSize, MEM_COMMIT, PAGE_READWRITE);
			m_stream->ReadStreamPartial(i, buffer, m_bufferSize);
		} else {
			if (!VirtualAlloc(buffer, m_bufferSize, MEM_RESET_UNDO, PAGE_READWRITE))
				m_stream->ReadStreamPartial(i, buffer, m_bufferSize);
		}

		const auto src = std::span(static_cast<uint8_t*>(buffer), std::min(m_bufferSize, static_cast<size_t>(offset + length - i))).subspan(relativeOffset);
		const auto available = std::min(src.size_bytes(), out.size_bytes());
		std::copy_n(&src[0], available, &out[0]);
		out = out.subspan(available);
		relativeOffset = 0;

		VirtualAlloc(buffer, m_bufferSize, MEM_RESET, PAGE_READWRITE);
	}
	return length - out.size_bytes();
}

Sqex::FileRandomAccessStream::FileRandomAccessStream(Win32::File file, uint64_t offset, uint64_t length)
	: m_file(std::move(file))
	, m_offset(offset)
	, m_size(length == UINT64_MAX ? m_file.GetLength() - m_offset : length) {
	if (const auto filelen = m_file.GetLength(); m_offset + m_size > filelen) {
		throw std::invalid_argument(std::format("offset({}) + size({}) > file size({} from {})", m_offset, m_size, filelen, m_file.ResolveName()));
	}
}

Sqex::FileRandomAccessStream::FileRandomAccessStream(std::filesystem::path path, uint64_t offset, uint64_t length, bool openImmediately)
	: m_path(std::move(path))
	, m_initializationMutex(openImmediately ? nullptr : std::make_shared<std::mutex>())
	, m_file(openImmediately ? Win32::File::Create(m_path, GENERIC_READ, FILE_SHARE_READ, nullptr, GENERIC_READ, 0) : Win32::File())
	, m_offset(offset)
	, m_size(length == UINT64_MAX ? file_size(m_path) - m_offset : length) {
	if (const auto filelen = file_size(m_path); m_offset + m_size > filelen) {
		throw std::invalid_argument(std::format("offset({}) + size({}) > file size({} from {}!)", m_offset, m_size, filelen, m_path));
	}
}

Sqex::FileRandomAccessStream::~FileRandomAccessStream() = default;

uint64_t Sqex::FileRandomAccessStream::StreamSize() const {
	return m_size;
}

uint64_t Sqex::FileRandomAccessStream::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	if (offset >= m_size)
		return 0;

	if (m_initializationMutex) {
		if (const auto mtx = m_initializationMutex) {
			const auto lock = std::lock_guard(*mtx);
			if (m_initializationMutex) {
				m_file = Win32::File::Create(m_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
				m_initializationMutex = nullptr;
			}
		}
	}

	const auto available = static_cast<size_t>(std::min(length, m_size - offset));
	return m_file.Read(m_offset + offset, buf, available, Win32::File::PartialIoMode::AllowPartial);
}
