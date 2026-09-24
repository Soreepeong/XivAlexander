#include "pch.h"
#include "AudioResampler.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "MainApp/App.h"
#include "Config.h"
#include "Game/SignatureDefinitions.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"

#include <xivres/util.on_dtor.h>

#include "MainApp/AudioResamplers/SoxrResampler.h"
#include "Game/DynamicStruct.h"

namespace XivAlexander::Apps::MainApp::Features {
	namespace {
		using AudioResamplers::Resampler;
		using AudioResamplers::SampleFormat;

		using Game::SoundVoice;
		using Game::SoundVoiceCallback;
		using Game::SoundVoiceState;
		using Game::VoiceFailure;
		using Game::VoiceFormat;
		using Game::VoiceMaxChannels;
		using Game::VoiceMaxQueuedBuffers;

		constexpr size_t OutputBufferCount = VoiceMaxQueuedBuffers + 1;

		bool Write(uint32_t* target, uint32_t value) {
			DWORD oldProtect;
			if (!VirtualProtect(target, sizeof value, PAGE_EXECUTE_READWRITE, &oldProtect))
				return false;

			*target = value;
			VirtualProtect(target, sizeof value, oldProtect, &oldProtect);
			return true;
		}

		struct Voice {
			int32_t SourceRate{};
			int32_t MixRate{};
			int32_t Channels{};
			VoiceFormat SourceFormat{};
			double Ratio{};

			std::recursive_mutex Mtx;
			std::unique_ptr<Resampler> Resampler;
			const uint8_t* EndOfData{};

			std::array<std::vector<float>, OutputBufferCount> Output;
			size_t NextOutput = 0;

			[[nodiscard]] size_t SourceFrameBytes() const {
				return static_cast<size_t>(Channels) * (SourceFormat == VoiceFormat::Int16 ? sizeof(int16_t) : sizeof(float));
			}

			[[nodiscard]] int32_t ToMixFrames(int32_t sourceFrames) const {
				return static_cast<int32_t>(std::llround(static_cast<double>(sourceFrames) * Ratio));
			}
		};
	}

	struct AudioResampler::Implementation {
		const std::shared_ptr<Config> Config;
		const std::shared_ptr<Misc::Logger> Logger;

		uint32_t* Immediate{};
		uint32_t* Global{};
		uint32_t NativeRate{};
		uint32_t NativeGlobal{};
		bool Patched = false;

		std::atomic_bool Enabled{};
		std::optional<Misc::Hooks::PointerFunctionOf<decltype(Game::SoundVoiceFunctions::Init)>> InitHook;
		std::optional<Misc::Hooks::PointerFunctionOf<decltype(Game::SoundVoiceFunctions::Submit)>> SubmitHook;
		std::optional<Misc::Hooks::PointerFunctionOf<decltype(Game::SoundVoiceFunctions::Flush)>> FlushHook;
		std::optional<Misc::Hooks::PointerFunctionOf<decltype(Game::SoundVoiceFunctions::SetMarker)>> SetMarkerHook;
		std::optional<Misc::Hooks::PointerFunctionOf<decltype(Game::SoundVoiceFunctions::Destructor)>> DestructorHook;
		Game::SoundVoiceFunctions Functions;
		Game::SoundBufferEndInfo BufferEnd;
		bool HooksFailed = false;

		std::mutex VoicesLock;
		std::unordered_map<const void*, std::shared_ptr<Voice>> Voices;

		xivres::util::on_dtor::multi Cleanup;
		xivres::util::on_dtor::multi HookCleanup;

		explicit Implementation(App&)
			: Config(Config::Acquire())
			, Logger(Misc::Logger::Acquire()) {
			ResolveMixRate();
			Cleanup += Config->Runtime.AudioOutputSamplingRate.OnChange([this] { ApplyMixRate(false); });
			ApplyMixRate(true);

			Cleanup += Config->Runtime.SoxrResampler.OnChange([this] { SetEnabled(Config->Runtime.SoxrResampler.Value().Enabled); });
			SetEnabled(Config->Runtime.SoxrResampler.Value().Enabled);
		}

		~Implementation() {
			HookCleanup.clear();
			Cleanup.clear();
			{
				std::lock_guard lock(VoicesLock);
				if (!Voices.empty()) {
					Logger->Format<LogLevel::Warning>(LogCategory::AudioResampler,
						"unhooking with {} converted voice(s) alive; they will play their raw input until they end", Voices.size());
				}
			}
			UnapplyMixRate(false);
		}

		void ResolveMixRate() {
			Game::SoundVoiceRenderInfo render;
			const auto setupStatus = Game::Resolved::MixRateSetup.Resolve(Immediate);
			const auto renderStatus = Game::Resolved::VoiceRender.Resolve(render);
			if (setupStatus != Game::Signatures::ResolveError::Ok || renderStatus != Game::Signatures::ResolveError::Ok) {
				Logger->Format<LogLevel::Warning>(LogCategory::AudioResampler, "mix rate not found; the game's own rate stays");
				Immediate = nullptr;
				Global = nullptr;
				return;
			}
			Global = render.MixRate;
			NativeRate = *Immediate;
			NativeGlobal = *Global;
		}

		void ApplyMixRate(bool live) {
			if (!Immediate)
				return;

			auto rate = Config->Runtime.AudioOutputSamplingRate.Value();
			if (rate == MatchDefaultDevice) {
				if ((rate = DefaultDeviceRate()))
					Logger->Format<LogLevel::Info>(LogCategory::AudioResampler, "default output device mixes at {} Hz", rate);
			}

			if (rate == 0 || rate == NativeRate) {
				UnapplyMixRate(live);
				return;
			}

			const auto wroteImmediate = Write(Immediate, rate);
			const auto wroteGlobal = !live || Write(Global, rate);
			Patched = wroteImmediate || wroteGlobal;

			if (!wroteImmediate || !wroteGlobal)
				Logger->Format<LogLevel::Error>(LogCategory::AudioResampler,
					"{} Hz partially used (setup={}, global={}): {}",
					rate, wroteImmediate, wroteGlobal, Utils::Win32::FormatWindowsErrorMessage(GetLastError()));
			else if (live)
				Logger->Format<LogLevel::Info>(LogCategory::AudioResampler, "using sampling rate of {} Hz instead of {} Hz", rate, NativeRate);
			else
				Logger->Format<LogLevel::Info>(LogCategory::AudioResampler, "{} Hz after game restart", rate);
		}

		void UnapplyMixRate(bool live) {
			if (!Patched)
				return;

			Write(Immediate, NativeRate);
			if (live)
				Write(Global, NativeGlobal);
			Patched = false;
			if (live)
				Logger->Format<LogLevel::Info>(LogCategory::AudioResampler, "{} Hz restored", NativeRate);
			else
				Logger->Format<LogLevel::Info>(LogCategory::AudioResampler, "{} Hz after game restart", NativeRate);
		}

		std::shared_ptr<Voice> Find(const void* voice) {
			std::lock_guard lock(VoicesLock);
			const auto it = Voices.find(voice);
			return it == Voices.end() ? nullptr : it->second;
		}

		void Forget(void* voice) {
			std::shared_ptr<Voice> v;
			{
				std::lock_guard lock(VoicesLock);
				const auto it = Voices.find(voice);
				if (it == Voices.end())
					return;
				v = std::move(it->second);
				Voices.erase(it);
			}

			v.reset();
		}

		std::unique_ptr<Resampler> MakeResampler(int32_t rate, int32_t mix, int32_t channels, VoiceFormat format) {
			const auto sampleFormat = format == VoiceFormat::Int16 ? SampleFormat::Int16 : SampleFormat::Float;
			std::string error;
			auto r = std::make_unique<AudioResamplers::SoxrResampler>(rate, mix, channels, sampleFormat, Config->Runtime.SoxrResampler.Value(), error);
			if (!error.empty()) {
				Logger->Format<LogLevel::Error>(LogCategory::AudioResampler,
					"soxr ({} -> {} Hz, {} ch) failed: {}", rate, mix, channels, error);
				return nullptr;
			}
			return r;
		}

		const uint8_t* EndOfDataFor(const SoundVoiceCallback* callback) {
			if (!callback || !BufferEnd.Handler)
				return nullptr;

			const auto vtbl = std::span(callback->Vtbl, callback->Vtbl + SoundVoiceCallback::MaxVtblSlots);
			if (std::ranges::find(vtbl, reinterpret_cast<const void*>(BufferEnd.Handler)) != std::ranges::end(vtbl))
				return callback->EndOfData(BufferEnd.CallbackLayout);

			return nullptr;
		}

		uint32_t OnInit(void* voice, int32_t rate, int32_t channels, VoiceFormat format,
			const SoundVoiceCallback* a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10, uint64_t a11) {
			Forget(voice);

			const auto mix = static_cast<int32_t>(*Global);
			if (!Enabled || rate <= 0 || mix <= 0 || rate == mix
				|| channels <= 0 || channels > VoiceMaxChannels || format == VoiceFormat::None)
				return InitHook->bridge(voice, rate, channels, format, a5, a6, a7, a8, a9, a10, a11);

			auto resampler = MakeResampler(rate, mix, channels, format);
			if (!resampler)
				return InitHook->bridge(voice, rate, channels, format, a5, a6, a7, a8, a9, a10, a11);

			const auto result = InitHook->bridge(voice, mix, channels, VoiceFormat::Float, a5, a6, a7, a8, a9, a10, a11);
			if (result != 0)
				return result;

			auto v = std::make_shared<Voice>();
			v->SourceRate = rate;
			v->MixRate = mix;
			v->Channels = channels;
			v->SourceFormat = format;
			v->Ratio = static_cast<double>(mix) / rate;
			v->Resampler = std::move(resampler);
			v->EndOfData = EndOfDataFor(a5);

			std::lock_guard lock(VoicesLock);
			Voices[voice] = std::move(v);
			return result;
		}

		uint32_t OnSubmit(void* voice, const void* data, uint64_t bytes, uint64_t context, int32_t startFrame) {
			const auto v = Find(voice);
			if (!v)
				return SubmitHook->bridge(voice, data, bytes, context, startFrame);

			// same check with the game
			if (const auto& gameVoice = *static_cast<const SoundVoice*>(voice);
				(gameVoice.State(Functions.Layout) & ~SoundVoiceState::Flushed) == SoundVoiceState::None
				|| gameVoice.QueuedBuffers(Functions.Layout) >= VoiceMaxQueuedBuffers
				|| bytes % v->SourceFrameBytes() != 0)
				return VoiceFailure;

			const auto framesIn = bytes / v->SourceFrameBytes();
			const auto channels = static_cast<size_t>(v->Channels);
			const auto lock = std::lock_guard(v->Mtx);
			auto& out = v->Output[v->NextOutput];
			out.clear();

			auto ok = v->Resampler->Process(data, framesIn, out);
			if (ok && v->EndOfData && *v->EndOfData != 0)
				ok = v->Resampler->Drain(out);
			if (!ok) {
				Logger->Format<LogLevel::Error>(LogCategory::AudioResampler,
					"voice {:p}: {} failed on {} frames", voice, v->Resampler->Name(), framesIn);
			}

			const auto produced = out.size() / channels;
			if (produced == 0)
				out.assign(channels, 0.f);

			const auto result = SubmitHook->bridge(voice, out.data(), out.size() * sizeof(float), context, v->ToMixFrames(startFrame));
			if (result != 0)
				return result;

			v->NextOutput = (v->NextOutput + 1) % OutputBufferCount;
			return result;
		}

		bool InstallVoiceHooks() {
			if (InitHook)
				return true;
			if (HooksFailed)
				return false;

			if (!Global
				|| Game::Resolved::VoiceFunctions.Resolve(Functions) != Game::Signatures::ResolveError::Ok
				|| Game::Resolved::BufferEndHandler.Resolve(BufferEnd) != Game::Signatures::ResolveError::Ok) {
				Logger->Format<LogLevel::Error>(LogCategory::AudioResampler, "voice functions not found; voices cannot be resampled");
				HooksFailed = true;
				return false;
			}

			try {
				InitHook.emplace("AudioResampler::Init", Functions.Init);
				SubmitHook.emplace("AudioResampler::Submit", Functions.Submit);
				FlushHook.emplace("AudioResampler::Flush", Functions.Flush);
				SetMarkerHook.emplace("AudioResampler::SetMarker", Functions.SetMarker);
				DestructorHook.emplace("AudioResampler::Destructor", Functions.Destructor);

				HookCleanup += DestructorHook->SetHook([this](void* voice, uint64_t flags) -> void* {
					Forget(voice);
					return DestructorHook->bridge(voice, flags);
				});

				HookCleanup += FlushHook->SetHook([this](void* voice) -> uint32_t {
					const auto result = FlushHook->bridge(voice);
					if (const auto v = Find(voice)) {
						const auto lock = std::lock_guard(v->Mtx);
						v->Resampler->Reset();
					}
					return result;
				});

				HookCleanup += SetMarkerHook->SetHook([this](void* voice, int32_t frame) -> uint32_t {
					if (const auto v = Find(voice); v && frame >= 0)
						frame = v->ToMixFrames(frame);
					return SetMarkerHook->bridge(voice, frame);
				});

				HookCleanup += SubmitHook->SetHook([this](void* voice, const void* data, uint64_t bytes, uint64_t context, int32_t startFrame) -> uint32_t {
					return OnSubmit(voice, data, bytes, context, startFrame);
				});

				HookCleanup += InitHook->SetHook([this](void* voice, int32_t rate, int32_t channels, VoiceFormat format, const SoundVoiceCallback* callback, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10, uint64_t a11) -> uint32_t {
					return OnInit(voice, rate, channels, format, callback, a6, a7, a8, a9, a10, a11);
				});
			} catch (const std::exception& e) {
				Logger->Format<LogLevel::Error>(LogCategory::AudioResampler, "failed to hook voices: {}", e.what());
				HookCleanup.clear();
				InitHook.reset();
				SubmitHook.reset();
				FlushHook.reset();
				SetMarkerHook.reset();
				DestructorHook.reset();
				HooksFailed = true;
				return false;
			}

			Logger->Format<LogLevel::Info>(LogCategory::AudioResampler, "voices hooked (mix rate {} Hz, {})", *Global, soxr_version());
			return true;
		}

		void SetEnabled(bool enabled) {
			if (enabled && !InstallVoiceHooks())
				return;

			if (Enabled.exchange(enabled) == enabled)
				return;

			std::lock_guard lock(VoicesLock);
			Logger->Format<LogLevel::Info>(LogCategory::AudioResampler,
				"new voices: {}; {} converted voice(s) keep their resampler", enabled ? "soxr" : "built-in", Voices.size());
		}
	};

	uint32_t AudioResampler::DefaultDeviceRate() {
		const auto hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const auto uninit = xivres::util::on_dtor([hrInit] {
			if (SUCCEEDED(hrInit))
				CoUninitialize();
		});

		IMMDeviceEnumeratorPtr enumerator{};
		if (FAILED(enumerator.CreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL)))
			return 0;

		IMMDevicePtr device{};
		if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
			return 0;

		IAudioClientPtr client{};
		if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client))))
			return 0;

		WAVEFORMATEX* p{};
		if (; FAILED(client->GetMixFormat(&p))) {
			return 0;
		} else {
			const auto format = std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)>(p, &CoTaskMemFree);
			return format->nSamplesPerSec;
		}
	}

	AudioResampler::AudioResampler(App& app)
		: m_pImpl(std::make_unique<Implementation>(app)) {}

	AudioResampler::~AudioResampler() = default;
}
