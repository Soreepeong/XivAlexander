#include "pch.h"
#include "Oodle.h"
#include "Game/SignatureDefinitions.h"

#include <xivres/util.module_relative.h>

#include "XivAlexander.h"

static void* OodleAlignedAlloc(size_t size, size_t align) {
	return _aligned_malloc(size, align);
}

static void OodleAlignedFree(void* ptr){
	return _aligned_free(ptr);
}

std::string XivAlexander::Game::Oodle::to_string(const OodleNetworkFunctions& value) {
	using xivres::util::module_relative;
	return std::format(
		"htbits {}, window {:#x}, set malloc/free {}, shared size {}, shared set window {}, "
		"udp state size {}, udp train {}, udp decode {}, udp encode {}, "
		"tcp state size {}, tcp train {}, tcp decode {}, tcp encode {}",
		value.HtBits, value.WindowSize,
		module_relative(value.SetMallocFree), module_relative(value.SharedSize), module_relative(value.SharedSetWindow),
		module_relative(value.UdpStateSize), module_relative(value.UdpTrain), module_relative(value.UdpDecode), module_relative(value.UdpEncode),
		module_relative(value.TcpStateSize), module_relative(value.TcpTrain), module_relative(value.TcpDecode), module_relative(value.TcpEncode));
}

XivAlexander::Game::Oodle::OodleModule::OodleModule() : ErrorStep("Start") {
	try {
		if (const auto err = Resolved::OodleNetwork.Resolve(*this); err != Signatures::ResolveError::Ok) {
			ErrorStep = err.Detail;
			return;
		}

		ErrorStep.clear();
	} catch (const std::exception& e) {
		ErrorStep = e.what();
	}
}

XivAlexander::Game::Oodle::OodleModule::~OodleModule() = default;

XivAlexander::Game::Oodle::Oodler::Oodler(const OodleModule& funcs, bool udp)
	: m_funcs(funcs)
	, m_udp(udp) {

	if (!m_funcs.ErrorStep.empty())
		return;
	m_state.resize(udp ? m_funcs.UdpStateSize() : m_funcs.TcpStateSize());
	m_shared.resize(m_funcs.SharedSize(m_funcs.HtBits));
	m_window.resize(m_funcs.WindowSize);
	m_buffer.resize(65536);
	m_funcs.SharedSetWindow(m_shared.data(), m_funcs.HtBits, m_window.data(), static_cast<int>(m_window.size()));
	if (udp)
		m_funcs.UdpTrain(m_state.data(), m_shared.data(), nullptr, nullptr, 0);
	else
		m_funcs.TcpTrain(m_state.data(), m_shared.data(), nullptr, nullptr, 0);

}

XivAlexander::Game::Oodle::Oodler::~Oodler() = default;

std::span<uint8_t> XivAlexander::Game::Oodle::Oodler::Decode(std::span<const uint8_t> source, size_t decodedLength) {
	if (!m_funcs.ErrorStep.empty())
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

std::span<uint8_t> XivAlexander::Game::Oodle::Oodler::Encode(std::span<const uint8_t> source) {
	if (!m_funcs.ErrorStep.empty())
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
