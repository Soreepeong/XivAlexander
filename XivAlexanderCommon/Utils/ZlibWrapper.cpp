#include "pch.h"
#include "XivAlexanderCommon/Utils/ZlibWrapper.h"

std::string Utils::ZlibError::DescribeReturnCode(int code) {
	switch (code) {
		case Z_OK: return "OK";
		case Z_STREAM_END: return "Stream end";
		case Z_NEED_DICT: return "Need dict";
		case Z_ERRNO: return std::generic_category().message(code);
		case Z_STREAM_ERROR: return "Stream error";
		case Z_DATA_ERROR: return "Data error";
		case Z_MEM_ERROR: return "Memory error";
		case Z_BUF_ERROR: return "Buffer error";
		case Z_VERSION_ERROR: return "Version error";
		default: return std::format("Unknown return code {}", code);
	}
}

Utils::ZlibError::ZlibError(int returnCode)
	: std::runtime_error(DescribeReturnCode(returnCode)) {
}

Utils::Oodler::Oodler(const OodleNetworkFunctions& funcs, bool udp): m_funcs(funcs)
	, m_udp(udp)
	, m_state(!m_funcs.Found ? 0 : (udp ? m_funcs.UdpStateSize() : m_funcs.TcpStateSize()))
	, m_shared(!m_funcs.Found ? 0 : m_funcs.SharedSize(m_funcs.HtBits))
	, m_window(!m_funcs.Found ? 0 : m_funcs.WindowSize)
	, m_buffer(!m_funcs.Found ? 0 : 65536) {

	if (m_funcs.Found) {
		m_funcs.SharedSetWindow(m_shared.data(), m_funcs.HtBits, m_window.data(), static_cast<int>(m_window.size()));
		if (udp)
			m_funcs.UdpTrain(m_state.data(), m_shared.data(), nullptr, nullptr, 0);
		else
			m_funcs.TcpTrain(m_state.data(), m_shared.data(), nullptr, nullptr, 0);
	}
}

std::span<uint8_t> Utils::Oodler::Decode(std::span<const uint8_t> source, size_t decodedLength) {
	if (!m_funcs.Found)
		throw std::runtime_error("Oodle not initialized");
	m_buffer.resize(decodedLength);
	if (m_udp) {
		if (!m_funcs.UdpDecode(m_state.data(), m_shared.data(), source.data(), source.size(), m_buffer.data(), decodedLength))
			throw std::runtime_error("OodleNetwork1UDP_Decode error");
	} else {
		if (!m_funcs.TcpDecode(m_state.data(), m_shared.data(), source.data(), source.size(), m_buffer.data(), decodedLength))
			throw std::runtime_error("OodleNetwork1TCP_Decode error");
	}
	return { m_buffer };
}

std::span<uint8_t> Utils::Oodler::Encode(std::span<const uint8_t> source) {
	if (!m_funcs.Found)
		throw std::runtime_error("Oodle not initialized");
	if (m_buffer.size() < MaxEncodedSize(source.size()))
		m_buffer.resize(MaxEncodedSize(source.size()));
	size_t size;
	if (m_udp) {
		size = m_funcs.UdpEncode(m_state.data(), m_shared.data(), source.data(), source.size(), m_buffer.data());
		if (!size)
			throw std::runtime_error("OodleNetwork1UDP_Encode error");
	} else {
		size = m_funcs.TcpEncode(m_state.data(), m_shared.data(), source.data(), source.size(), m_buffer.data());
		if (!size)
			throw std::runtime_error("OodleNetwork1TCP_Encode error");
	}
	return std::span(m_buffer).subspan(0, size);
}

void Utils::ZlibReusableInflater::Initialize() {
	int res;
	if (!m_initialized) {
		res = inflateInit2(&m_zstream, m_windowBits);
		m_initialized = true;
	} else
		res = inflateReset2(&m_zstream, m_windowBits);
	if (res != Z_OK)
		throw ZlibError(res);
}

Utils::ZlibReusableInflater::ZlibReusableInflater(int windowBits, int defaultBufferSize)
	: m_windowBits(windowBits)
	, m_defaultBufferSize(defaultBufferSize) {
}

Utils::ZlibReusableInflater::~ZlibReusableInflater() {
	if (m_initialized)
		inflateEnd(&m_zstream);
}

std::span<uint8_t> Utils::ZlibReusableInflater::operator()(std::span<const uint8_t> source) {
	Initialize();

	m_zstream.next_in = &source[0];
	m_zstream.avail_in = static_cast<uint32_t>(source.size());

	if (m_buffer.size() < m_defaultBufferSize)
		m_buffer.resize(m_defaultBufferSize);
	while (true) {
		m_zstream.next_out = &m_buffer[m_zstream.total_out];
		m_zstream.avail_out = static_cast<uint32_t>(m_buffer.size() - m_zstream.total_out);

		if (const auto res = inflate(&m_zstream, Z_FINISH);
			res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END) {
			throw ZlibError(res);
		} else {
			if (res == Z_STREAM_END)
				break;
			m_buffer.resize(m_buffer.size() + std::min<size_t>(m_buffer.size(), 65536));
		}
	}

	return std::span(m_buffer).subspan(0, m_zstream.total_out);
}

std::span<uint8_t> Utils::ZlibReusableInflater::operator()(std::span<const uint8_t> source, size_t maxSize) {
	if (m_buffer.size() < maxSize)
		m_buffer.resize(maxSize);

	return operator()(source, std::span(m_buffer));
}

std::span<uint8_t> Utils::ZlibReusableInflater::operator()(std::span<const uint8_t> source, std::span<uint8_t> target) {
	Initialize();

	m_zstream.next_in = &source[0];
	m_zstream.avail_in = static_cast<uint32_t>(source.size());
	m_zstream.next_out = &target[0];
	m_zstream.avail_out = static_cast<uint32_t>(target.size());

	if (const auto res = inflate(&m_zstream, Z_FINISH);
		res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END) {
		throw ZlibError(res);
	}

	return target.subspan(0, target.size() - m_zstream.avail_out);
}

void Utils::ZlibReusableDeflater::Initialize() {
	int res;
	if (!m_initialized) {
		res = deflateInit2(&m_zstream, m_level, m_method, m_windowBits, m_memLevel, m_strategy);
		m_initialized = true;
	} else
		res = deflateReset(&m_zstream);
	if (res != Z_OK)
		throw ZlibError(res);
}

Utils::ZlibReusableDeflater::ZlibReusableDeflater(int level, int method, int windowBits, int memLevel, int strategy, size_t defaultBufferSize)
	: m_level(level)
	, m_method(method)
	, m_windowBits(windowBits)
	, m_memLevel(memLevel)
	, m_strategy(strategy)
	, m_defaultBufferSize(defaultBufferSize) {
}

Utils::ZlibReusableDeflater::~ZlibReusableDeflater() {
	if (m_initialized)
		deflateEnd(&m_zstream);
}

std::span<uint8_t> Utils::ZlibReusableDeflater::Deflate(std::span<const uint8_t> source) {
	Initialize();

	m_zstream.next_in = &source[0];
	m_zstream.avail_in = static_cast<uint32_t>(source.size());

	if (m_buffer.size() < m_defaultBufferSize)
		m_buffer.resize(m_defaultBufferSize);
	while (true) {
		m_zstream.next_out = &m_buffer[m_zstream.total_out];
		m_zstream.avail_out = static_cast<uint32_t>(m_buffer.size() - m_zstream.total_out);

		if (const auto res = deflate(&m_zstream, Z_FINISH);
			res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END)
			throw ZlibError(res);
		else {
			if (res == Z_STREAM_END)
				break;
			m_buffer.resize(m_buffer.size() + std::min<size_t>(m_buffer.size(), 65536));
		}
	}

	return m_latestResult = std::span(m_buffer).subspan(0, m_zstream.total_out);
}
