#include "pch.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <numbers>
#include <tuple>
#include <utility>

#include "WindowedSincResampler.h"

XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::WindowedSincResampler(uint32_t inRate, uint32_t outRate, uint32_t channels, SampleFormat format, const WindowedSincResamplerConfig& config)
	: m_inRate(inRate)
	, m_outRate(outRate)
	, m_channels(channels)
	, m_format(format)
	, m_table(TableFor(std::min(1.0, static_cast<double>(outRate) / inRate), config)) {
	Reset();
}

const char* XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::Name() const { return "windowed sinc"; }

bool XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::Process(const void* in, size_t frames, std::vector<float>& out) {
	const auto at = m_history.size();
	m_history.resize(at + frames * m_channels);
	if (m_format == SampleFormat::Int16) {
		const auto* src = static_cast<const int16_t*>(in);
		std::transform(src, src + frames * m_channels, m_history.begin() + at,
			[](int16_t v) { return static_cast<float>(v) * (1.f / 32768.f); });
	} else {
		std::copy_n(static_cast<const float*>(in), frames * m_channels, m_history.begin() + at);
	}
	m_inputFrames += frames;

	Produce(out);
	return true;
}

bool XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::Drain(std::vector<float>& out) {
	m_history.resize(m_history.size() + static_cast<size_t>(m_table->HalfTaps) * m_channels);
	Produce(out);
	Reset();
	return true;
}

void XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::Reset() {
	m_history.assign(static_cast<size_t>(m_table->HalfTaps - 1) * m_channels, 0.f);
	m_historyStart = -(m_table->HalfTaps - 1);
	m_inputFrames = 0;
	m_outputFrame = 0;
}

double XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::HeldBack() const {
	const auto due = (m_inputFrames * m_outRate + m_inRate - 1) / m_inRate;
	return static_cast<double>(due - m_outputFrame);
}

void XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::Produce(std::vector<float>& out) {
	const auto& table = *m_table;
	const auto available = m_historyStart + static_cast<int64_t>(m_history.size() / m_channels);
	std::vector<float> weights(static_cast<size_t>(table.Taps));

	for (;; m_outputFrame++) {
		const auto position = m_outputFrame * m_inRate;
		const auto whole = static_cast<int64_t>(position / m_outRate);
		// past the end of the input, or short of the frames this output reaches
		if (std::cmp_greater_equal(whole, m_inputFrames) || whole + table.HalfTaps >= available)
			break;

		const auto phase = static_cast<double>(position % m_outRate) * table.Phases / m_outRate;
		const auto row = static_cast<int>(phase);
		const auto blend = static_cast<float>(phase - row);
		const auto* lo = &table.Weights[static_cast<size_t>(row) * table.Taps];
		const auto* hi = lo + table.Taps;
		for (int j = 0; j < table.Taps; j++)
			weights[j] = lo[j] + (hi[j] - lo[j]) * blend;

		const auto* src = &m_history[static_cast<size_t>(whole - table.HalfTaps + 1 - m_historyStart) * m_channels];
		const auto at = out.size();
		out.resize(at + m_channels);
		for (size_t c = 0; c < m_channels; c++) {
			float sum = 0;
			for (int j = 0; j < table.Taps; j++)
				sum += src[j * m_channels + c] * weights[j];
			out[at + c] = sum;
		}
	}

	// drop what no later output reaches
	const auto next = static_cast<int64_t>(m_outputFrame * m_inRate / m_outRate);
	if (const auto drop = std::clamp<int64_t>(next - table.HalfTaps + 1 - m_historyStart, 0, available - m_historyStart); drop > 0) {
		m_history.erase(m_history.begin(), m_history.begin() + static_cast<ptrdiff_t>(drop * m_channels));
		m_historyStart += drop;
	}
}

std::shared_ptr<const XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::Table>
XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::TableFor(double scale, const WindowedSincResamplerConfig& config) {
	static std::mutex s_lock;
	static std::map<std::tuple<double, uint32_t, uint32_t, double, double>, std::shared_ptr<const Table>> s_tables;

	std::lock_guard lock(s_lock);
	auto& table = s_tables[{scale, config.HalfTaps, config.Phases, config.KaiserBeta, config.Cutoff}];
	if (!table)
		table = MakeTable(scale, config);
	return table;
}

std::shared_ptr<const XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::Table>
XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::MakeTable(double scale, const WindowedSincResamplerConfig& config) {
	auto t = std::make_shared<Table>();
	t->HalfTaps = static_cast<int>(std::ceil(config.HalfTaps / scale));
	t->Taps = 2 * t->HalfTaps;
	t->Phases = static_cast<int>(config.Phases);
	t->Weights.resize(static_cast<size_t>(t->Phases + 1) * t->Taps);

	const auto cutoff = config.Cutoff * scale;
	const auto i0Beta = BesselI0(config.KaiserBeta);
	for (int p = 0; p <= t->Phases; p++) {
		auto* row = &t->Weights[static_cast<size_t>(p) * t->Taps];
		double sum = 0;
		for (int j = 0; j < t->Taps; j++) {
			// how far this input frame is from the output, in input frames
			const auto d = static_cast<double>(p) / t->Phases + t->HalfTaps - 1 - j;
			const auto x = d / t->HalfTaps;
			const auto window = std::abs(x) >= 1 ? 0 : BesselI0(config.KaiserBeta * std::sqrt(1 - x * x)) / i0Beta;
			const auto arg = std::numbers::pi * cutoff * d;
			const auto sinc = arg == 0 ? 1 : std::sin(arg) / arg;
			row[j] = static_cast<float>(cutoff * sinc * window);
			sum += row[j];
		}
		for (int j = 0; j < t->Taps; j++)
			row[j] = static_cast<float>(row[j] / sum);
	}
	return t;
}

double XivAlexander::Apps::MainApp::AudioResamplers::WindowedSincResampler::BesselI0(double x) {
	double sum = 1;
	double term = 1;
	for (int k = 1; k < 32; k++) {
		const auto half = x / (2 * k);
		term *= half * half;
		sum += term;
	}
	return sum;
}
