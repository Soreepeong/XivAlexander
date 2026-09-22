#include "pch.h"

#include <r8brain-free-src/CDSPResampler.h>

#include "R8brainResampler.h"

XivAlexander::Apps::MainApp::AudioResamplers::R8brainResampler::R8brainResampler(uint32_t inRate, uint32_t outRate, uint32_t channels, SampleFormat format, const R8brainResamplerConfig& config)
	: m_blockFrames(static_cast<int>(config.BlockFrames))
	, m_format(format)
	, m_inRate(inRate)
	, m_outRate(outRate)
	, m_block(config.BlockFrames)
	, m_outputs(channels) {
	const auto phase = config.Phase == R8brainResamplerConfig::PhaseResponse::Minimum ? r8b::fprMinPhase : r8b::fprLinearPhase;
	for (uint32_t i = 0; i < channels; i++)
		m_channels.emplace_back(std::make_unique<r8b::CDSPResampler>(inRate, outRate, m_blockFrames, config.TransitionBand, config.Attenuation, phase));
}

XivAlexander::Apps::MainApp::AudioResamplers::R8brainResampler::~R8brainResampler() = default;

const char* XivAlexander::Apps::MainApp::AudioResamplers::R8brainResampler::Name() const { return "r8brain"; }

bool XivAlexander::Apps::MainApp::AudioResamplers::R8brainResampler::Process(const void* in, size_t frames, std::vector<float>& out) {
	for (size_t at = 0; at < frames; at += m_blockFrames) {
		const auto length = static_cast<int>((std::min)(static_cast<size_t>(m_blockFrames), frames - at));
		Feed(in, at, length, out, SIZE_MAX);
	}
	m_inputFrames += frames;
	return true;
}

bool XivAlexander::Apps::MainApp::AudioResamplers::R8brainResampler::Drain(std::vector<float>& out) {
	const auto due = (m_inputFrames * m_outRate + m_inRate - 1) / m_inRate;
	for (auto rounds = 0; m_outputFrames < due && rounds < 1024; rounds++)
		Feed(nullptr, 0, m_blockFrames, out, due - m_outputFrames);
	Reset();
	return true;
}

void XivAlexander::Apps::MainApp::AudioResamplers::R8brainResampler::Reset() {
	for (const auto& c : m_channels)
		c->clear();
	m_inputFrames = 0;
	m_outputFrames = 0;
}

double XivAlexander::Apps::MainApp::AudioResamplers::R8brainResampler::HeldBack() const {
	return static_cast<double>(m_inputFrames) * m_outRate / m_inRate - static_cast<double>(m_outputFrames);
}

void XivAlexander::Apps::MainApp::AudioResamplers::R8brainResampler::Feed(const void* in, size_t at, int length, std::vector<float>& out, size_t limit) {
	const auto channels = m_channels.size();
	int made = 0;
	for (size_t c = 0; c < channels; c++) {
		if (!in)
			std::fill_n(m_block.begin(), length, 0.0);
		else if (m_format == SampleFormat::Int16) {
			const auto* src = static_cast<const int16_t*>(in) + at * channels + c;
			for (int i = 0; i < length; i++)
				m_block[i] = src[i * channels] * (1.0 / 32768.0);
		} else {
			const auto* src = static_cast<const float*>(in) + at * channels + c;
			for (int i = 0; i < length; i++)
				m_block[i] = src[i * channels];
		}
		made = m_channels[c]->process(m_block.data(), length, m_outputs[c]);
	}

	const auto take = (std::min)(static_cast<size_t>(made), limit);
	const auto base = out.size();
	out.resize(base + take * channels);
	for (size_t i = 0; i < take; i++)
		for (size_t c = 0; c < channels; c++)
			out[base + i * channels + c] = static_cast<float>(m_outputs[c][i]);
	m_outputFrames += take;
}
