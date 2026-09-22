#include "pch.h"

#include "ArtResampler.h"

XivAlexander::Apps::MainApp::AudioResamplers::ArtResampler::ArtResampler(uint32_t inRate, uint32_t outRate, uint32_t channels, SampleFormat format, const ArtResamplerConfig& config, std::string& error)
	: m_taps(static_cast<int>(config.Taps))
	, m_channels(channels)
	, m_format(format)
	, m_ratio(static_cast<double>(outRate) / inRate) {
	auto flags = 0;
	if (config.SubsampleInterpolate)
		flags |= SUBSAMPLE_INTERPOLATE;
	if (config.FilterWindow == ArtResamplerConfig::Window::BlackmanHarris)
		flags |= BLACKMAN_HARRIS;
	if (config.IncludeLowpass)
		flags |= INCLUDE_LOWPASS;
	if (config.ExtendedPrecision)
		flags |= EXTEND_CONVOLUTION_MATH;
	if (config.Multithreaded)
		flags |= RESAMPLE_MULTITHREADED;

	// NOLINTNEXTLINE(readability-suspicious-call-argument)
	m_cxt = resampleFixedRatioInit(static_cast<int>(channels), m_taps, static_cast<int>(config.Filters), inRate, outRate,
		static_cast<int>(config.LowpassFrequency), flags);
	if (!m_cxt)
		error = "resampleFixedRatioInit returned nothing";
	else
		Start();
}

XivAlexander::Apps::MainApp::AudioResamplers::ArtResampler::~ArtResampler() {
	if (m_cxt)
		resampleFree(m_cxt);
}

const char* XivAlexander::Apps::MainApp::AudioResamplers::ArtResampler::Name() const { return "ART"; }

bool XivAlexander::Apps::MainApp::AudioResamplers::ArtResampler::Process(const void* in, size_t frames, std::vector<float>& out) {
	const float* src;
	if (m_format == SampleFormat::Int16) {
		m_input.resize(frames * m_channels);
		const auto* s = static_cast<const int16_t*>(in);
		for (size_t i = 0; i < m_input.size(); i++)
			m_input[i] = s[i] * (1.f / 32768.f);
		src = m_input.data();
	} else {
		src = static_cast<const float*>(in);
	}
	m_inputFrames += frames;
	return Run(src, static_cast<int>(frames), out);
}

bool XivAlexander::Apps::MainApp::AudioResamplers::ArtResampler::Drain(std::vector<float>& out) {
	const auto ok = Run(nullptr, -1 /* flush */, out);
	resampleReset(m_cxt);
	Start();
	return ok;
}

void XivAlexander::Apps::MainApp::AudioResamplers::ArtResampler::Reset() {
	resampleReset(m_cxt);
	Start();
}

double XivAlexander::Apps::MainApp::AudioResamplers::ArtResampler::HeldBack() const {
	return static_cast<double>(m_inputFrames) * m_ratio - static_cast<double>(m_outputFrames);
}

void XivAlexander::Apps::MainApp::AudioResamplers::ArtResampler::Start() {
	// the output would otherwise run half a filter behind the input
	resampleAdvancePosition(m_cxt, m_taps / 2.0);
	m_inputFrames = 0;
	m_outputFrames = 0;
}

bool XivAlexander::Apps::MainApp::AudioResamplers::ArtResampler::Run(const float* in, int frames, std::vector<float>& out) {
	const auto flush = frames < 0;
	int consumed = 0;
	for (;;) {
		const auto base = out.size() / m_channels;
		const auto room = static_cast<size_t>(static_cast<double>(flush ? m_taps : frames - consumed) * m_ratio) + 256;
		out.resize((base + room) * m_channels);
		const auto res = resampleProcessInterleaved(m_cxt, in ? in + static_cast<size_t>(consumed) * m_channels : nullptr,
			flush ? -1 : frames - consumed, out.data() + base * m_channels, static_cast<int>(room), m_ratio);
		out.resize((base + res.output_generated) * m_channels);
		m_outputFrames += res.output_generated;
		if (flush)
			return true;

		consumed += static_cast<int>(res.input_used);
		if (consumed >= frames)
			return true;
		if (res.input_used == 0 && res.output_generated == 0)
			return false;
	}
}
