#pragma once

#include "Misc/Logger.h"
#include "Utils/RingBuffer.h"

#include "Decoder.h"

namespace XivAlexander::Apps::MainApp {
	template<size_t QueueLength>
	class BufferedDecoder : public Decoder {
	protected:
		using Queue = Utils::RingBuffer<uint8_t, QueueLength>;

	private:
		Queue m_ring{};

	public:
		uint32_t Process(AsiStream& stream, void* buffer, uint32_t bufferSize) override {
			if (buffer == nullptr || bufferSize == 0)
				return 0;

			std::span out{static_cast<uint8_t*>(buffer), bufferSize};
			while (!out.empty()) {
				if (const auto written = DecodeTo(out); written != 0) {
					out = out.subspan(written);
					continue;
				}

				if (Finished() || EndOfStream(stream) || !ReadFrom(stream, Queue::Capacity()))
					break;
			}

			std::ranges::fill(out, uint8_t{});
			return bufferSize - static_cast<uint32_t>(out.size());
		}

		uint32_t Attribute(AsiStream&, int attrib) override {
			return attrib == 0 ? ChannelCount() : attrib == 1 ? BitDepth() : 0;
		}

		void Reset(AsiStream&) override {
			m_ring.Clear();
			ResetBufferInternal();
		}

		bool ReadFrom(AsiStream& stream, size_t nb) {
			if (stream.FetchCallback == nullptr)
				return false;

			auto pulledAny = false;
			while (m_ring.Readable() < nb) {
				const auto free = m_ring.Tail();
				if (free.empty())
					break;

				const auto pulled = stream.FetchCallback(
					stream.User,
					free.data(),
					static_cast<uint32_t>(free.size()),
					stream.PendingFetchOffset);
				stream.PendingFetchOffset = NoFetchOffset;
				if (pulled == 0)
					break;

				m_ring.Commit(pulled);
				pulledAny = true;
			}

			return pulledAny;
		}

		uint32_t DecodeTo(std::span<uint8_t> out) {
			uint32_t produced = 0;
			while (!out.empty()) {
				const auto [consumed, written] = Decode(m_ring.Head(), out);
				if (consumed == 0 && written == 0)
					break;

				m_ring.Consume(consumed);
				out = out.subspan(written);
				produced += written;
			}

			return produced;
		}

		[[nodiscard]] size_t QueuedBytes() const { return m_ring.Readable(); }

		[[nodiscard]] bool EndOfStream(const AsiStream& stream) const {
			const auto* ctx = stream.User;
			return ctx && (ctx->Flags & AsiStreamFlag::EndOfStream) != AsiStreamFlag::None;
		}

	protected:
		[[nodiscard]] virtual bool Finished() const { return false; }
		[[nodiscard]] const Queue& Queued() const { return m_ring; }
		[[nodiscard]] Queue& Queued() { return m_ring; }

		bool QueueHeader(std::span<const uint8_t> bytes) {
			if (m_ring.Readable() != 0 || bytes.size() > Queue::Capacity())
				return false;

			m_ring.Clear();
			std::memcpy(m_ring.Tail().data(), bytes.data(), bytes.size());
			m_ring.Commit(static_cast<uint32_t>(bytes.size()));
			return true;
		}

		virtual void ResetBufferInternal() {}

		virtual std::pair<uint32_t, uint32_t> Decode(std::span<const uint8_t> data, std::span<uint8_t> out) = 0;

		template<typename... TArgs>
		void Report(const char* format, const TArgs&... args) {
			Misc::Logger::Acquire()->Format<LogLevel::Warning>(LogCategory::AltCodecMusic, "{} {}", Name(), std::vformat(format, std::make_format_args(args...)));
		}
	};
}
