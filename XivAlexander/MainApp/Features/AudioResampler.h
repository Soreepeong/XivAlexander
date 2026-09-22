#pragma once

#include <cstdint>
#include <memory>

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Features {
	class AudioResampler {
		struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		static constexpr uint32_t Choices[]{ 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };
		static constexpr uint32_t GameDefault = 48000;
		static constexpr uint32_t MatchDefaultDevice = 0;
\
		static uint32_t DefaultDeviceRate();

		explicit AudioResampler(App& app);
		~AudioResampler();
	};
}
