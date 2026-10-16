#include "pch.h"
#include "AudioResampler.h"

#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <list>
#include <map>
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
		using AudioResamplers::SoxrResampler;
		using AudioResamplers::SampleFormat;

		using Game::SoundVoice;
		using Game::SoundVoiceCallback;
		using Game::SoundVoiceState;
		using Game::VoiceFailure;
		using Game::VoiceFormat;
		using Game::VoiceMaxChannels;
		using Game::VoiceMaxQueuedBuffers;

		constexpr size_t OutputBufferCount = VoiceMaxQueuedBuffers + 1;

		constexpr size_t RecordingCacheBytes = 256 << 20;
		constexpr size_t MaxRecordingBytes = 16 << 20;
		constexpr uint32_t StatsInterval = 512;

		// As of 7.56h.
		constexpr uint64_t MarkContext = 1;

		bool Write(uint32_t* target, uint32_t value) {
			try {
				Misc::Hooks::WriteAtomically(target, value);
				return true;
			} catch (const std::exception&) {
				return false;
			}
		}

		// Only tells submitted buffers apart; not collision resistant.
		uint64_t HashBytes(const void* data, size_t size) {
			auto p = static_cast<const uint8_t*>(data);
			auto h = 0x9E3779B97F4A7C15ULL ^ size;
			for (; size >= sizeof(uint64_t); p += sizeof(uint64_t), size -= sizeof(uint64_t)) {
				uint64_t w;
				std::memcpy(&w, p, sizeof w);
				h = (h ^ w) * 0xBF58476D1CE4E5B9ULL;
				h ^= h >> 31;
			}
			uint64_t w = 0;
			std::memcpy(&w, p, size);
			h = (h ^ w) * 0x94D049BB133111EBULL;
			return h ^ (h >> 29);
		}

		struct RecordingKey {
			int32_t SourceRate{};
			int32_t MixRate{};
			int32_t Channels{};
			VoiceFormat Format{};
			uint64_t FirstBytes{};
			uint64_t FirstHash{};

			auto operator<=>(const RecordingKey&) const = default;
		};

		/// A voice's whole output from the first buffer of a sound to its end, for later voices given the same buffers.
		struct Recording {
			struct Input {
				uint64_t Bytes{};
				uint64_t Hash{};

				bool operator==(const Input&) const = default;
			};

			SoxrResamplerConfig Filter;
			std::vector<Input> Inputs;
			std::vector<uint8_t> Output;

			[[nodiscard]] size_t Bytes() const { return Output.size() + Inputs.size() * sizeof(Input); }
		};

		struct Voice {
			int32_t SourceRate{};
			int32_t MixRate{};
			int32_t Channels{};
			VoiceFormat SourceFormat{};
			double Ratio{};
			SoxrResamplerConfig Filter;

			std::recursive_mutex Mtx;
			std::unique_ptr<SoxrResampler> Resampler;
			const uint8_t* EndOfData{};

			RecordingKey Key;
			std::shared_ptr<const Recording> Replay;
			size_t ReplayInput = 0;
			size_t ReplayOutput = 0;
			std::unique_ptr<Recording> Recorded;

			std::array<std::vector<uint8_t>, OutputBufferCount> Output;
			size_t NextOutput = 0;

			int64_t InFrames = 0;
			int64_t OutFrames = 0;
			int64_t InBase = 0;
			int64_t OutBase = 0;
			bool Drained = false;
			std::deque<int64_t> PendingMarks;

			bool LastMarked = false;
			int32_t HeldBack = 0;

			void ResetPosition() {
				InFrames = OutFrames = InBase = OutBase = 0;
				Drained = false;
				PendingMarks.clear();
				LastMarked = false;
				HeldBack = 0;
				Replay.reset();
				ReplayInput = ReplayOutput = 0;
				Recorded.reset();
			}

			[[nodiscard]] int64_t ToOutputFrame(int64_t inFrame) const {
				return OutBase + std::llround(static_cast<double>(inFrame - InBase) * Ratio);
			}

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

		struct CacheEntry {
			std::shared_ptr<const Recording> Recording;
			std::list<RecordingKey>::iterator Lru;
		};

		std::mutex CacheLock;
		std::map<RecordingKey, CacheEntry> Cache;
		std::list<RecordingKey> CacheLru;
		size_t CacheBytes = 0;

		std::atomic_uint32_t Streams;
		std::atomic_uint32_t ReplayedStreams;
		std::atomic_uint32_t DivergedStreams;

		xivres::util::on_dtor::multi Cleanup;
		xivres::util::on_dtor::multi HookCleanup;

		explicit Implementation(App&)
			: Config(Config::Acquire())
			, Logger(Misc::Logger::Acquire()) {
			ResolveMixRate();
			Cleanup += Config->Runtime.Audio.OutputSamplingRate.OnChange([this] { ApplyMixRate(false); });
			ApplyMixRate(true);

			Cleanup += Config->Runtime.Audio.SoxrResampler.Enabled.OnChange([this] { SetEnabled(Config->Runtime.Audio.SoxrResampler.Enabled); });
			SetEnabled(Config->Runtime.Audio.SoxrResampler.Enabled);

			Cleanup += Config->Runtime.OnVersionSensitiveFeaturesAllowedChange([this] {
				ApplyMixRate(false);
				SetEnabled(Config->Runtime.Audio.SoxrResampler.Enabled);
			});
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
			Game::Resolved::AudioResamplerFunctions functions;
			if (Game::Resolved::AudioResampler.Resolve(functions) != Game::Signatures::ResolveError::Ok) {
				Logger->Format<LogLevel::Warning>(LogCategory::AudioResampler, "mix rate not found; the game's own rate stays");
				Immediate = nullptr;
				Global = nullptr;
				return;
			}
			Immediate = functions.MixRateSetup;
			Global = functions.Render.MixRate;
			NativeRate = *Immediate;
			NativeGlobal = *Global;
		}

		void ApplyMixRate(bool live) {
			if (!Immediate)
				return;

			auto rate = Config->Runtime.Audio.OutputSamplingRate.Value();
			if (rate == MatchDefaultDevice) {
				if ((rate = DefaultDeviceRate()))
					Logger->Format<LogLevel::Info>(LogCategory::AudioResampler, "default output device mixes at {} Hz", rate);
			}

			if (rate == 0 || rate == NativeRate) {
				UnapplyMixRate(live);
				return;
			}

			if (!Config->Runtime.AreVersionSensitiveFeaturesAllowed(LogCategory::AudioResampler, std::format("{} Hz sampling rate", rate))) {
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

		std::unique_ptr<SoxrResampler> MakeResampler(int32_t rate, int32_t mix, int32_t channels, VoiceFormat format, const SoxrResamplerConfig& filter) {
			const auto sampleFormat = format == VoiceFormat::Int16 ? SampleFormat::Int16 : SampleFormat::Float;
			std::string error;
			auto r = std::make_unique<SoxrResampler>(rate, mix, channels, sampleFormat, filter, error);
			if (!error.empty()) {
				Logger->Format<LogLevel::Error>(LogCategory::AudioResampler,
					"soxr ({} -> {} Hz, {} ch) failed: {}", rate, mix, channels, error);
				return nullptr;
			}
			return r;
		}

		std::shared_ptr<const Recording> FindRecording(const RecordingKey& key, const SoxrResamplerConfig& filter) {
			std::lock_guard lock(CacheLock);
			const auto it = Cache.find(key);
			if (it == Cache.end() || it->second.Recording->Filter != filter)
				return nullptr;
			CacheLru.splice(CacheLru.begin(), CacheLru, it->second.Lru);
			return it->second.Recording;
		}

		void StoreRecording(const RecordingKey& key, std::unique_ptr<Recording> recording) {
			recording->Inputs.shrink_to_fit();
			recording->Output.shrink_to_fit();
			const auto bytes = recording->Bytes();

			std::lock_guard lock(CacheLock);
			if (const auto it = Cache.find(key); it != Cache.end()) {
				CacheBytes -= it->second.Recording->Bytes();
				CacheLru.erase(it->second.Lru);
				Cache.erase(it);
			}
			CacheLru.push_front(key);
			Cache.emplace(key, CacheEntry{std::move(recording), CacheLru.begin()});
			CacheBytes += bytes;

			while (CacheBytes > RecordingCacheBytes && CacheLru.size() > 1) {
				const auto it = Cache.find(CacheLru.back());
				CacheBytes -= it->second.Recording->Bytes();
				Cache.erase(it);
				CacheLru.pop_back();
			}
		}

		void CountStream(bool replayed) {
			if (replayed)
				++ReplayedStreams;
			if (++Streams % StatsInterval)
				return;

			size_t count, bytes;
			{
				std::lock_guard lock(CacheLock);
				count = Cache.size();
				bytes = CacheBytes;
			}
			Logger->Format<LogLevel::Info>(LogCategory::AudioResampler,
				"last {} sounds: {} replayed, {} fell back to soxr midway; {} resampled sound(s) cached in {:.1f} MiB",
				StatsInterval, ReplayedStreams.exchange(0), DivergedStreams.exchange(0), count, static_cast<double>(bytes) / (1 << 20));
		}

		void BeginStream(Voice& v, uint64_t bytes, uint64_t hash) {
			v.Replay.reset();
			v.ReplayInput = v.ReplayOutput = 0;
			v.Recorded.reset();
			v.Key = {v.SourceRate, v.MixRate, v.Channels, v.SourceFormat, bytes, hash};
			v.Replay = FindRecording(v.Key, v.Filter);
			if (!v.Replay) {
				v.Recorded = std::make_unique<Recording>();
				v.Recorded->Filter = v.Filter;
			}
			CountStream(v.Replay != nullptr);
		}

		/// \returns Whether the recording held this buffer; if not, the voice resamples live from here on.
		bool ReplayNext(Voice& v, uint64_t bytes, uint64_t hash, bool ending, std::vector<uint8_t>& out) {
			const auto& rec = *v.Replay;
			if (v.ReplayInput >= rec.Inputs.size() || rec.Inputs[v.ReplayInput] != Recording::Input{bytes, hash}) {
				++DivergedStreams;
				v.Replay.reset();
				return false;
			}

			const auto last = ++v.ReplayInput == rec.Inputs.size();
			const auto mapped = static_cast<size_t>(std::llround(static_cast<double>(v.InFrames - v.InBase) * v.Ratio)) * v.SourceFrameBytes();
			const auto end = last ? rec.Output.size() : std::clamp(mapped, v.ReplayOutput, rec.Output.size());
			out.assign(rec.Output.begin() + static_cast<ptrdiff_t>(v.ReplayOutput), rec.Output.begin() + static_cast<ptrdiff_t>(end));
			v.ReplayOutput = end;
			if (last || ending) {
				v.Replay.reset();
				v.Drained = true;
			}
			return true;
		}

		void Record(Voice& v, uint64_t bytes, uint64_t hash, const std::vector<uint8_t>& out, bool ending) {
			auto& rec = *v.Recorded;
			rec.Inputs.push_back({bytes, hash});
			rec.Output.insert(rec.Output.end(), out.begin(), out.end());
			if (rec.Output.size() > MaxRecordingBytes)
				v.Recorded.reset();
			else if (ending)
				StoreRecording(v.Key, std::move(v.Recorded));
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

			const auto result = InitHook->bridge(voice, mix, channels, format, a5, a6, a7, a8, a9, a10, a11);
			if (result != 0)
				return result;

			// soxr is made at the first buffer the cache cannot answer.
			auto v = std::make_shared<Voice>();
			v->SourceRate = rate;
			v->MixRate = mix;
			v->Channels = channels;
			v->SourceFormat = format;
			v->Ratio = static_cast<double>(mix) / rate;
			v->Filter = Config->Runtime.Audio.SoxrResampler.Filter.Snapshot();
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
			const auto& gameVoice = *static_cast<const SoundVoice*>(voice);
			const auto queued = gameVoice.QueuedBuffers(Functions.Layout);
			if ((gameVoice.State(Functions.Layout) & ~SoundVoiceState::Flushed) == SoundVoiceState::None
				|| queued >= VoiceMaxQueuedBuffers
				|| bytes % v->SourceFrameBytes() != 0)
				return VoiceFailure;

			const auto frameBytes = v->SourceFrameBytes();
			const auto framesIn = bytes / frameBytes;
			const auto lock = std::lock_guard(v->Mtx);
			auto& out = v->Output[v->NextOutput];
			out.clear();

			const auto hash = HashBytes(data, bytes);
			if ((v->InFrames == 0 || v->Drained) && framesIn != 0)
				BeginStream(*v, bytes, hash);

			if (v->Drained) {
				v->InBase = v->InFrames;
				v->OutBase = v->OutFrames;
				v->Drained = false;
			}
			const auto marked = context == MarkContext;
			const auto inherited = marked && v->LastMarked && queued + v->HeldBack >= VoiceMaxQueuedBuffers;
			v->LastMarked = marked;
			if (marked && !inherited)
				v->PendingMarks.push_back(v->ToOutputFrame(v->InFrames));
			v->InFrames += static_cast<int64_t>(framesIn);

			const auto ending = v->EndOfData && *v->EndOfData != 0;
			if (!v->Replay || !ReplayNext(*v, bytes, hash, ending, out)) {
				if (!v->Resampler && !(v->Resampler = MakeResampler(v->SourceRate, v->MixRate, v->Channels, v->SourceFormat, v->Filter)))
					return VoiceFailure;

				auto ok = v->Resampler->Process(data, framesIn, out);
				if (ok && ending) {
					ok = v->Resampler->Drain(out);
					v->Drained = true;
				}
				if (!ok) {
					Logger->Format<LogLevel::Error>(LogCategory::AudioResampler,
						"voice {:p}: soxr failed on {} frames", voice, framesIn);
					v->Recorded.reset();
				} else if (v->Recorded) {
					Record(*v, bytes, hash, out, ending);
				}
			}

			if (out.empty()) {
				if (framesIn != 0 && queued > 0) {
					++v->HeldBack;
					return 0;
				}
				if (queued == 0)
					out.assign(frameBytes, 0);
			}

			const auto framesOut = static_cast<int64_t>(out.size() / frameBytes);
			const auto bufferEnd = v->OutFrames + framesOut;
			auto marksHere = 0;
			for (auto it = v->PendingMarks.begin(); it != v->PendingMarks.end() && *it < bufferEnd; ++it)
				++marksHere;
			const auto contextOut = marksHere ? MarkContext : marked ? 0 : context;

			const auto result = SubmitHook->bridge(voice, out.data(), out.size(), contextOut, v->ToMixFrames(startFrame));
			if (result != 0)
				return result;

			v->PendingMarks.erase(v->PendingMarks.begin(), v->PendingMarks.begin() + marksHere);
			v->OutFrames = bufferEnd;
			v->HeldBack = 0;
			v->NextOutput = (v->NextOutput + 1) % OutputBufferCount;
			return result;
		}

		bool InstallVoiceHooks() {
			if (InitHook)
				return true;
			if (HooksFailed)
				return false;

			// The mix rate was found, or there would be no Global, so only the voice parts can be missing.
			Game::Resolved::AudioResamplerFunctions functions;
			if (!Global
				|| Game::Resolved::AudioResampler.Resolve(functions) != Game::Signatures::ResolveError::Ok
				|| !functions.Voice
				|| !functions.BufferEnd) {
				Logger->Format<LogLevel::Error>(LogCategory::AudioResampler, "voice functions not found; voices cannot be resampled");
				HooksFailed = true;
				return false;
			}
			Functions = *functions.Voice;
			BufferEnd = *functions.BufferEnd;

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
						if (v->Resampler)
							v->Resampler->Reset();
						v->ResetPosition();
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
			if (enabled && !Config->Runtime.AreVersionSensitiveFeaturesAllowed(LogCategory::AudioResampler, "soxr resampler"))
				enabled = false;

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
