#include "pch.h"
#include "AltCodecMusicSupport.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "MainApp/App.h"
#include "Game/SignatureDefinitions.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"

#include <xivres/util.on_dtor.h>


#include "MainApp/AltCodecMusicDecoders/FlacDecoder.h"
#include "MainApp/AltCodecMusicDecoders/WavDecoder.h"

namespace XivAlexander::Apps::MainApp::Features {
	constexpr auto PeekBytes = 0x2000U;

	namespace {
		using AsiProcessHookType = Misc::Hooks::PointerFunctionOf<AsiStreamProcessFn>;
		using AsiAttributeHookType = Misc::Hooks::PointerFunctionOf<AsiStreamAttributeFn>;
		using AsiResetHookType = Misc::Hooks::PointerFunctionOf<AsiStreamResetFn>;

		class BuiltInDecoder final : public Decoder {
			AsiProcessHookType* m_process{};
			AsiAttributeHookType* m_attribute{};
			AsiResetHookType* m_reset{};

		public:
			void Bind(AsiProcessHookType& process, AsiAttributeHookType& attribute, AsiResetHookType& reset) {
				m_process = &process;
				m_attribute = &attribute;
				m_reset = &reset;
			}

			[[nodiscard]] const char* Name() const override { return "built-in"; }

			uint32_t Process(AsiStream& stream, void* buffer, uint32_t bufferSize) override {
				return m_process->bridge(stream, buffer, bufferSize);
			}

			uint32_t Attribute(AsiStream& stream, int attrib) override {
				return m_attribute->bridge(stream, attrib);
			}

			void Reset(AsiStream& stream) override {
				m_reset->bridge(stream);
			}

		protected:
			[[nodiscard]] bool ParseHeaderInternal(std::span<const uint8_t>) override { return false; }
		};

		std::mutex s_stateLock;
		std::unordered_map<const AsiStream*, std::unique_ptr<Decoder>> s_streams;

		Decoder* FindState(const AsiStream& stream) {
			std::lock_guard lock(s_stateLock);
			const auto it = s_streams.find(&stream);
			return it == s_streams.end() ? nullptr : it->second.get();
		}

		Decoder* CreateState(const AsiStream& stream) {
			if (!stream.SourcePtr)
				return nullptr;

			std::unique_ptr<Decoder> result;
			if (WavDecoder::IsWav(std::span(stream.SourcePtr, PeekBytes)))
				result = std::make_unique<WavDecoder>();
			else if (FlacDecoder::IsFlac(std::span(stream.SourcePtr, PeekBytes)))
				result = std::make_unique<FlacDecoder>();
			else
				return nullptr;

			std::lock_guard lock(s_stateLock);
			return (s_streams[&stream] = std::move(result)).get();
		}

		void EraseState(const AsiStream& stream) {
			std::lock_guard lock(s_stateLock);
			s_streams.erase(&stream);
		}

		size_t StateCount() {
			std::lock_guard lock(s_stateLock);
			return s_streams.size();
		}
	}

	struct AltCodecMusicSupport::Implementation {
		std::atomic_bool Enabled{};

		std::optional<Misc::Hooks::PointerFunctionOf<AsiStreamSetUpDecoderFn>> AsiSetUpDecoderHook;
		std::optional<AsiProcessHookType> AsiProcessHook;
		std::optional<AsiAttributeHookType> AsiAttributeHook;
		std::optional<AsiResetHookType> AsiResetHook;
		BuiltInDecoder BuiltIn;
		xivres::util::on_dtor::multi Cleanup;

		App& App;

		Implementation(MainApp::App& app)
			: App(app) {}

		Decoder& DecoderFor(const AsiStream& stream) {
			if (auto* state = FindState(stream))
				return *state;
			return BuiltIn;
		}

		void Enable() {
			if (Enabled)
				return;

			const auto logger = Misc::Logger::Acquire();

			if (AsiSetUpDecoderHook) {
				Enabled = true;
				logger->Format<LogLevel::Info>(LogCategory::AltCodecMusic, "re-enabled");
				return;
			}

			Game::Resolved::MssAsiFunctions fns;
			if (Game::Resolved::MssAsiStream.Resolve(fns) != Game::Signatures::ResolveError::Ok) {
				logger->Format<LogLevel::Error>(LogCategory::AltCodecMusic, "stream functions not found; alternative codecs unavailable");
				return;
			}

			try {
				AsiSetUpDecoderHook.emplace("AltCodecMusicSupport::AsiStreamSetUpDecoder", fns.SetUpDecoder);
				AsiProcessHook.emplace("AltCodecMusicSupport::AsiProcess", fns.Process);
				AsiAttributeHook.emplace("AltCodecMusicSupport::AsiAttribute", fns.Attribute);
				AsiResetHook.emplace("AltCodecMusicSupport::AsiReset", fns.Reset);

				BuiltIn.Bind(*AsiProcessHook, *AsiAttributeHook, *AsiResetHook);

				Cleanup += AsiResetHook->SetHook([this](AsiStream& stream) {
					DecoderFor(stream).Reset(stream);
				});

				Cleanup += AsiProcessHook->SetHook([this](AsiStream& stream, void* buffer, uint32_t bufferSize) -> uint32_t {
					return DecoderFor(stream).Process(stream, buffer, bufferSize);
				});

				Cleanup += AsiAttributeHook->SetHook([this](AsiStream& stream, int attrib) -> uint32_t {
					return DecoderFor(stream).Attribute(stream, attrib);
				});

				Cleanup += AsiSetUpDecoderHook->SetHook([this, logger](AsiStream& stream) -> uint32_t {
					EraseState(stream);

					const auto result = AsiSetUpDecoderHook->bridge(stream);
					if (result || !Enabled)
						return result;

					auto* const state = CreateState(stream);
					if (!state) {
						logger->Format<LogLevel::Info>(LogCategory::AltCodecMusic,
							"refusing stream {:p} (payload starts {:02x} {:02x} {:02x} {:02x})",
							static_cast<void*>(&stream),
							stream.SourcePtr[0],
							stream.SourcePtr[1],
							stream.SourcePtr[2],
							stream.SourcePtr[3]);
						return result;
					}

					state->ParseHeader({stream.SourcePtr, PeekBytes});
					stream.Channels = state->ChannelCount();

					auto flags = AsiStreamFlag::None;
					if (auto* ctx = stream.User) {
						flags = ctx->Flags;
						ctx->Flags = (ctx->Flags & ~AsiStreamFlag::EndOfStream) | AsiStreamFlag::NeedsReinit;
					}

					logger->Format<LogLevel::Info>(LogCategory::AltCodecMusic,
						"stream {:p} taken over as {} ({} Hz, {} ch, {} bit, flags {:02x})",
						static_cast<void*>(&stream), state->Name(), state->SamplingRate(), state->ChannelCount(), state->BitDepth(),
						static_cast<uint8_t>(flags));
					return 1;
				});
			} catch (const std::exception& e) {
				logger->Format<LogLevel::Error>(LogCategory::AltCodecMusic, "failed to load: {}", e.what());
				Cleanup.clear();
				AsiSetUpDecoderHook.reset();
				AsiProcessHook.reset();
				AsiAttributeHook.reset();
				AsiResetHook.reset();
				return;
			}

			Enabled = true;
		}

		void Disable() {
			if (!Enabled.exchange(false) && !AsiSetUpDecoderHook)
				return;

			const auto logger = Misc::Logger::Acquire();
			const auto live = StateCount();
			if (live != 0) {
				logger->Format<LogLevel::Info>(LogCategory::AltCodecMusic, "not disabling; {} stream(s) still active", live);
				return;
			}

			Cleanup.clear();
			AsiSetUpDecoderHook.reset();
			AsiProcessHook.reset();
			AsiAttributeHook.reset();
			AsiResetHook.reset();
			logger->Format<LogLevel::Info>(LogCategory::AltCodecMusic, "disabled");
		}
	};

	AltCodecMusicSupport::AltCodecMusicSupport(App& app)
		: m_pImpl(std::make_unique<Implementation>(app)) {}

	AltCodecMusicSupport::~AltCodecMusicSupport() = default;

	void AltCodecMusicSupport::Enable() {
		m_pImpl->Enable();
	}

	void AltCodecMusicSupport::Disable() {
		m_pImpl->Disable();
	}
}
