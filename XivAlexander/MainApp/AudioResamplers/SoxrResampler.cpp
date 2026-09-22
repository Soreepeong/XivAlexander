#include "pch.h"

#include "SoxrResampler.h"

XivAlexander::Apps::MainApp::AudioResamplers::SoxrResampler::SoxrResampler(uint32_t inRate, uint32_t outRate, uint32_t channels, SampleFormat format, const SoxrResamplerConfig& config, std::string& error)
	: m_channels(channels)
	, m_inFrameBytes(channels * (format == SampleFormat::Int16 ? sizeof(int16_t) : sizeof(float)))
	, m_ratio(static_cast<double>(outRate) / inRate) {
	const auto io = soxr_io_spec(format == SampleFormat::Int16 ? SOXR_INT16_I : SOXR_FLOAT32_I, SOXR_FLOAT32_I);
	using Q = SoxrResamplerConfig::QualityPreset;
	using P = SoxrResamplerConfig::PhaseResponse;
	unsigned long recipe;
	switch (config.Quality) {
		case Q::Quick: recipe = SOXR_QQ; break;
		case Q::Low: recipe = SOXR_LQ; break;
		case Q::Medium: recipe = SOXR_MQ; break;
		case Q::Bits16: recipe = SOXR_16_BITQ; break;
		case Q::Bits24: recipe = SOXR_24_BITQ; break;
		case Q::Bits28: recipe = SOXR_28_BITQ; break;
		case Q::Bits32: recipe = SOXR_32_BITQ; break;
		default: recipe = SOXR_20_BITQ; break;
	}
	recipe |= config.Phase == P::Minimum ? SOXR_MINIMUM_PHASE : config.Phase == P::Intermediate ? SOXR_INTERMEDIATE_PHASE : SOXR_LINEAR_PHASE;
	if (config.SteepFilter)
		recipe |= SOXR_STEEP_FILTER;

	auto flags = static_cast<unsigned long>(config.PassbandRolloff);  // SOXR_ROLLOFF_SMALL, _MEDIUM, _NONE in order
	if (config.HighPrecisionClock)
		flags |= SOXR_HI_PREC_CLOCK;
	if (config.DoublePrecision)
		flags |= SOXR_DOUBLE_PRECISION;

	auto quality = soxr_quality_spec(recipe, flags);
	if (config.PassbandEnd > 0)
		quality.passband_end = config.PassbandEnd;
	if (config.StopbandBegin > 0)
		quality.stopband_begin = config.StopbandBegin;

	auto runtime = soxr_runtime_spec(1);
	if (config.Log2MinDftSize)
		runtime.log2_min_dft_size = config.Log2MinDftSize;
	if (config.Log2LargeDftSize)
		runtime.log2_large_dft_size = config.Log2LargeDftSize;

	soxr_error_t e{};
	m_soxr = soxr_create(inRate, outRate, channels, &e, &io, &quality, &runtime);
	if (e || !m_soxr)
		error = e ? e : "soxr_create failed";
}

XivAlexander::Apps::MainApp::AudioResamplers::SoxrResampler::~SoxrResampler() {
	if (m_soxr)
		soxr_delete(m_soxr);
}

const char* XivAlexander::Apps::MainApp::AudioResamplers::SoxrResampler::Name() const { return "soxr"; }

bool XivAlexander::Apps::MainApp::AudioResamplers::SoxrResampler::Process(const void* in, size_t frames, std::vector<float>& out) {
	if (m_drained) {
		soxr_clear(m_soxr);
		m_drained = false;
	}
	return Run(static_cast<const uint8_t*>(in), frames, out);
}

bool XivAlexander::Apps::MainApp::AudioResamplers::SoxrResampler::Drain(std::vector<float>& out) {
	if (m_drained)
		return true;
	m_drained = true;
	return Run(nullptr, 0, out);
}

void XivAlexander::Apps::MainApp::AudioResamplers::SoxrResampler::Reset() {
	soxr_clear(m_soxr);
	m_drained = false;
}

double XivAlexander::Apps::MainApp::AudioResamplers::SoxrResampler::HeldBack() const {
	return soxr_delay(m_soxr);
}

bool XivAlexander::Apps::MainApp::AudioResamplers::SoxrResampler::Run(const uint8_t* in, size_t frames, std::vector<float>& out) {
	const auto base = out.size() / m_channels;
	out.resize((base + static_cast<size_t>(static_cast<double>(frames) * m_ratio) + 64) * m_channels);

	size_t consumed = 0;
	size_t produced = 0;
	for (;;) {
		size_t done = 0;
		size_t made = 0;
		if (soxr_process(m_soxr,
			in ? in + consumed * m_inFrameBytes : nullptr, frames - consumed, &done,
			out.data() + (base + produced) * m_channels, out.size() / m_channels - base - produced, &made)) {
			out.resize((base + produced) * m_channels);
			return false;
		}
		consumed += done;
		produced += made;

		if (base + produced == out.size() / m_channels)
			out.resize(out.size() * 2);
		else if (in ? consumed == frames : made == 0)
			break;
		else if (in && done == 0 && made == 0) {
			out.resize((base + produced) * m_channels);
			return false;
		}
	}

	out.resize((base + produced) * m_channels);
	return true;
}
