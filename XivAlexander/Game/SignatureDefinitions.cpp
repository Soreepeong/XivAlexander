#include "pch.h"
#include "Game/SignatureDefinitions.h"

#include <algorithm>
#include <utility>

#include "Game/ResolveContext.h"
#include "Game/Signatures.h"

namespace XivAlexander::Game {
	const Signatures::RegexSignature SqPackIndexEntryCount(R"([\x48\x49\x4C\x4D]\x39[\x80-\xBF](....)\x75\x05\xC1[\xE8-\xEF]\x03\xEB\x03\xC1[\xE8-\xEF]\x04)");
	const Signatures::RegexSignature RipRelativeLea(R"([\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....))");
	const Signatures::RegexSignature StringIndirectionResolver(R"(\x8b\x01\x25\xff\xff\xff\x00\x48\x03\xc1)");
	const Signatures::RegexSignature CutSceneLanguageGetter(R"(\xE8(....)[\x48\x49]\x63[\x50-\x53\x55-\x57]\x1C)");

	const Signatures::RegexSignature OodleInit(R"(\x75.\x48\x8d\x15....\x48\x8d\x0d....\xe8(....)\xc6\x05....\x01.{0,256}\x75.\xb9(....)\xe8(....)(?:\x45\x33\xc0\x33\xd2|\x33\xd2\x45\x33\xc0)\x48\x8b\xc8\xe8.....{0,6}\x41\xb9(....)\xba.....{0,6}\x48\x8b\xc8\xe8(....))");
	const Signatures::RegexSignature OodleSetUpStatesAndTrain(R"(\x75\x04[\x48\x4C]\x89..\xe8(....)[\x4C\x48]..\xe8(....).{0,256}\x01\x75\x0a[\x48\x49]\x8b.\xe8(....)\xeb\x09[\x48\x49]\x8b.\x08\xe8(....))");
	const Signatures::RegexSignature OodleDecode(R"([\x48-\x4F]\x85[\xC0-\xFF]\x74\x0A[\x48-\x4F]\x8B[\xC8-\xCF]\xE8(....)\xEB\x09\x48\x8B\x49\x08\xE8(....))");
	const Signatures::RegexSignature OodleEncode(R"([\x48-\x4F]\x85[\xC0-\xFF]\x74\x0D[\x48-\x4F]\x8B[\xC8-\xCF]\xE8(....)[\x48-\x4F]..\xEB\x0B\x48\x8B\x49\x08\xE8(....))");

	const Signatures::RegexSignature OpcodeActionRequest(R"(\x48\x8D\x54\x24(.)\x45\x33\xC9\xC7\x44\x24\1(..[\x00-\x03]\x00))");
	const Signatures::RegexSignature OpcodeCaller(R"((?:\x48\x8B[\x40-\xBF].{0,5}(?:\x41)?\x66\x89[\x44-\x7C]\x24.|(?:\x41)?\x66\x89[\x44-\x7C]\x24.\x48\x8B[\x40-\xBF].{0,5})\xE8(....))");
	const Signatures::RegexSignature OpcodeGroundTargetedSender(R"(\x66[\x40-\x4F]?\x89[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24.\xF3[\x40-\x4F]?\x0F\x11[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24.\xF3[\x40-\x4F]?\x0F\x11[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24.\xC7\x44\x24.(....))");
	const Signatures::RegexSignature OpcodeSizeFirst(R"(\x48\x83\xEC\x38\x4D\x8B\xC8\x48\xC7\x44\x24\x20(....)\x41\xB8(....))"); // opcode != payload size
	const Signatures::RegexSignature OpcodeSizeFromRegister(R"(\x48\x83\xEC.\x4D\x8B\xC8\x41\xB8(....)\x4C\x89\x44\x24\x20)"); // opcode == payload size
	const Signatures::RegexSignature OpcodeConditional(R"((?:[\x70-\x7F].|\x0F[\x80-\x8F]....)\x41\xB8(....)(\x48\xC7\x44\x24\x20)(....)[\x48\x4C]\x8B.{1,5}\x41?\x8B.{1,5}\x48\x8B.{1,5}\xE8)"); // opcode != payload size, but conditional

	const Signatures::RegexSignature AudioSamplingRateImmediate(R"(\xC7(?:\x45.|\x44\x24.|\x85....|\x84\x24....)(\x80\xBB\x00\x00)\xC7(?:\x45.|\x44\x24.|\x85....|\x84\x24....)\x0F\x00\x00\x00)");
	const Signatures::RegexSignature AudioSamplingRateGetter(R"(\x8B\x05(....)\xC3)");

	const Signatures::RegexSignature MssAsiAttribute(R"(\x85\xD2\x74.\x83\xFA\x01\x74\x03\x33\xC0\xC3\xB8\x10\x00\x00\x00\xC3\x8B\x41\x28\xC3)");
	const Signatures::RegexSignature MssAsiOpen(R"([\x44\x45]\x8B[\x40-\x43\x45-\x47]\x20\x48\x8D\x15....[\x48\x49]\x8B[\xC8-\xCF]\xE8(....))");
	const Signatures::RegexSignature MssAsiSetUpDecoder(R"([\x48-\x4F]\x8B[\x00-\x3F][\x40-\x4F]?\x89[\x40-\x7F]\x38[\x48-\x4F]\x8B[\x00-\x3F][\x40-\x4F]?\x89[\x40-\x7F]\x34[\x48-\x4F]\x8B[\x00-\x3F]\xC7[\x40-\x47]\x30\x00\x00\x01\x00)");
	const Signatures::RegexSignature MssAsiProcess(R"(\xFF\x41\x3C[\x40-\x4F]?\x8B[\xC0-\xFF]\x83\x79\x3C\x01)");
	const Signatures::RegexSignature MssAsiResetPair(R"(\xE8(....)(?:[\x48\x49]\x8B[\x40-\x4F]\x08\x33\xD2|\x33\xD2[\x48\x49]\x8B[\x40-\x4F]\x08)\xE8(....))");

	const Signatures::RegexSignature SoundVoiceInit(R"([\x48-\x4F]\x8B[\xC0-\xFF]\x41\x83\xF8\x10(?:\x0F\x8F....|\x7F.|\x7E.\x83\xC8\xFF\xC3|\x7E.\xB8\xFF\xFF\xFF\xFF\xC3)\x45\x85\xC9(?:\x0F\x84....|\x74.)[\x48\x4C]\x8B[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24\x28[\x48-\x4F]\x89[\x80-\xBF])");
	const Signatures::RegexSignature SoundVoiceVtable(R"([\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....)[\x48-\x4F]\x8B[\xC0-\xFF][\x48-\x4F]\x89[\x00-\x3F](?:[\x48\x49]\x8D[\x50-\x57]\x30|[\x48\x49]\x83[\xC0-\xC7]\x30\xFF\x15))");
	const Signatures::RegexSignature SoundVoiceRender(R"([\x40-\x4F]?\x83[\xB8-\xBB\xBD-\xBF](....)\x03.{0,8}?(?:\x0F\x85....|\x75.)[\x40-\x4F]?\x83[\xB8-\xBB\xBD-\xBF](....)\x00(?:\x0F\x8E....|\x7E.)\xE8(....))");
	const Signatures::RegexSignature SoundVoiceSubmit(R"(\xF7[\x80-\x83\x85-\x87](....)\xFB\xFF\xFF\xFF.{0,24}?[\x40-\x4F]?\x8B[\x80-\xBF](....)[\x40-\x4F]?\x83[\xF8-\xFF]\x02(?:\x0F\x8D....|\x7D.))");
	const Signatures::RegexSignature SoundVoiceSetMarker(R"(\xF7[\x80-\x83\x85-\x87](....)\xFB\xFF\xFF\xFF\x74.\x89[\x90-\x93\x95-\x97](....)\x33\xC0\xC3)");
	const Signatures::RegexSignature SoundVoiceStoreImmediate(R"(\xC7[\x80-\x83\x85-\x87](....)(....))");
	const Signatures::RegexSignature RelativeCall(R"(\xE8(....))");
	const Signatures::RegexSignature SoundBufferEndHandler(R"([\x40\x41][\x50-\x57]\x48\x83\xEC.\x83\x79.\x07[\x48-\x4F]\x8B[\xC0-\xFF]\x74.\x48\x83\xC1\xF8\xE8....[\x40-\x4F]?\x80[\xB8-\xBB\xBD-\xBF](....)\x01)");

	const Signatures::RegexSignature MessageLoop(R"(\xE8(....)\x84\xc0\x75\xf7)");
}

namespace XivAlexander::Game::Resolved {
	namespace {
		using Signatures::Address;
		using Signatures::RegexSignature;
		using Signatures::ResolveContext;
		using Signatures::ResolveError;
		using Signatures::ScanResult;

		template<typename T>
		std::vector<T> AllMatchStarts(const ResolveContext& ctx, const RegexSignature& signature) {
			std::vector<T> result;
			for (const auto& m : ctx.All(signature, ctx.Text()))
				result.push_back(Address(m.begin(0)));
			return result;
		}

		size_t FieldOffset(const ResolveContext& ctx, const ScanResult& m, size_t group, std::string_view what) {
			return static_cast<size_t>(ctx.InRange<int32_t>(m.Get<int32_t>(group), 1, 0x10000, what));
		}

		bool InOpcodeRange(uint32_t v) {
			return 0x0001 <= v && v <= 0x1000;
		}

		std::optional<uint16_t> FindActionRequest(std::span<const uint8_t> text) {
			for (const auto& m : OpcodeActionRequest.Lookup(text, RegexSignature::FromNextByte)) {
				if (const auto opcode = static_cast<uint16_t>(m.Get<uint32_t>(2)); InOpcodeRange(opcode))
					return opcode;
			}
			return std::nullopt;
		}

		std::optional<uint16_t> FindActionRequestGroundTargeted(const ResolveContext& ctx, std::span<const uint8_t> text) {
			for (const auto& call : OpcodeCaller.Lookup(text, RegexSignature::FromNextByte)) {
				const auto body = ctx.Module().FunctionAt(call.ResolveAddress<const uint8_t*>(1));
				if (body.empty())
					continue;

				if (const auto m = ctx.Find(OpcodeGroundTargetedSender, body)) {
					if (const auto opcode = static_cast<uint16_t>(m->Get<uint32_t>(1)); InOpcodeRange(opcode))
						return opcode;
				}
			}
			return std::nullopt;
		}

		std::vector<IpcTypeCandidates::PayloadWriter> FindDutyRecorderPayloadWriters(const ResolveContext& ctx, std::span<const uint8_t> text) {
			const auto base = ctx.Image().data();
			const auto offsetOf = [base](const ScanResult& m) {
				return static_cast<size_t>(static_cast<const uint8_t*>(m.begin(0)) - base);
			};

			std::vector<IpcTypeCandidates::PayloadWriter> result;
			for (const auto& m : OpcodeSizeFirst.Lookup(text, RegexSignature::FromNextByte)) {
				if (const auto opcode = static_cast<uint16_t>(m.Get<uint32_t>(2)); InOpcodeRange(opcode))
					result.push_back({offsetOf(m), m.Get<uint32_t>(1), opcode});
			}

			for (const auto& m : OpcodeSizeFromRegister.Lookup(text, RegexSignature::FromNextByte)) {
				const auto value = m.Get<uint32_t>(1);
				if (const auto opcode = static_cast<uint16_t>(value); InOpcodeRange(opcode))
					result.push_back({offsetOf(m), value, opcode});
			}

			for (const auto& m : OpcodeConditional.Lookup(text, RegexSignature::FromNextByte)) {
				if (const auto opcode = static_cast<uint16_t>(m.Get<uint32_t>(1)); InOpcodeRange(opcode))
					result.push_back({offsetOf(m), m.Get<uint32_t>(3), opcode});
			}

			std::ranges::sort(result, [](const auto& a, const auto& b) { return a.Offset < b.Offset; });
			return result;
		}
	}

	std::string to_string(const MssAsiFunctions& value) {
		return std::format("attribute {}, reset {}, open {}, set up {}, process {}",
			Signatures::Describe(value.Attribute),
			Signatures::Describe(value.Reset),
			Signatures::Describe(value.Open),
			Signatures::Describe(value.SetUpDecoder),
			Signatures::Describe(value.Process));
	}

	std::string to_string(const IpcTypeCandidates& value) {
		return std::format(
			"ActionEffect01 {}, ActionEffect08 {}, ActionEffect16 {}, ActionEffect24 {}, ActionEffect32 {}, "
			"ActorControl {}, ActorControlSelf {}, ActorCast {}, ActionRequest {}, ActionRequestGroundTargeted {}, "
			"{} payload writers",
			Signatures::Describe(value.S2C_ActionEffects[0]),
			Signatures::Describe(value.S2C_ActionEffects[1]),
			Signatures::Describe(value.S2C_ActionEffects[2]),
			Signatures::Describe(value.S2C_ActionEffects[3]),
			Signatures::Describe(value.S2C_ActionEffects[4]),
			Signatures::Describe(value.S2C_ActorControl),
			Signatures::Describe(value.S2C_ActorControlSelf),
			Signatures::Describe(value.S2C_ActorCast),
			Signatures::Describe(value.C2S_ActionRequest),
			Signatures::Describe(value.C2S_ActionRequestGroundTargeted),
			value.PayloadWriters.size());
	}

	const Signatures::ComplexSignature<std::vector<SqPackIndexLookupFn>> SqPackIndexLookupFunctions("SqPackIndexLookupFunctions", [](ResolveContext& ctx) {
		// Each SqPackManager keeps the lookup for its kind of index in a member, which LoadSqPack sets with the
		// address of either function and then compares against one of them.
		const auto count = ctx.Unique(SqPackIndexEntryCount, ctx.Text(), "index entry count");
		const auto member = FieldOffset(ctx, count, 1, "lookup member");
		const auto loadSqPack = ctx.FunctionContaining(count.begin(0), "LoadSqPack");

		// lea reg, [rip+fn], then mov [base+member], reg within the next few instructions.
		std::vector<SqPackIndexLookupFn> result;
		for (const auto& lea : ctx.All(RipRelativeLea, loadSqPack)) {
			const auto leaBytes = static_cast<const uint8_t*>(lea.begin(0));
			const auto leaReg = ((leaBytes[2] >> 3) & 7) | (leaBytes[0] & 4 ? 8 : 0);
			const auto after = static_cast<const uint8_t*>(lea.end(0));
			const auto limit = (std::min)(after + 24, loadSqPack.data() + loadSqPack.size());
			for (auto p = after; p + 7 <= limit; ++p) {
				if ((p[0] & 0xF0) != 0x40 || (p[0] & 0x08) == 0 || p[1] != 0x89 || (p[2] & 0xC0) != 0x80 || (p[2] & 7) == 4)
					continue;
				if (const auto movReg = ((p[2] >> 3) & 7) | (p[0] & 4 ? 8 : 0); movReg != leaReg)
					continue;
				if (static_cast<size_t>(*reinterpret_cast<const int32_t*>(p + 3)) != member)
					continue;

				const auto fn = lea.ResolveAddress<SqPackIndexLookupFn>(1);
				if (std::ranges::find(result, fn) == result.end()) {
					ctx.Require(!ctx.FunctionStartingAt(reinterpret_cast<const void*>(fn)).empty(), ResolveError::Mismatch,
						"index lookup {} is not where a function starts", Signatures::Describe(reinterpret_cast<const void*>(fn)));
					result.push_back(fn);
				}
				break;
			}
		}

		ctx.Require(result.size() == 2, ResolveError::Mismatch, "LoadSqPack sets {} lookup functions instead of 2", result.size());
		return result;
	});

	const Signatures::ComplexSignature<std::vector<StringIndirectionResolverFn>> StringIndirectionResolverFunctions("StringIndirectionResolverFunctions", [](ResolveContext& ctx) {
		return AllMatchStarts<StringIndirectionResolverFn>(ctx, StringIndirectionResolver);
	});

	const Signatures::ComplexSignature<CutSceneLanguageGetterFn> CutSceneLanguageGetterFunction("CutSceneLanguageGetterFunction", [](ResolveContext& ctx) -> CutSceneLanguageGetterFn {
		return Address(ctx.First(CutSceneLanguageGetter, ctx.Text(), "call").ResolveAddress<const void*>(1));
	});

	const Signatures::ComplexSignature<Oodle::OodleNetworkFunctions> OodleNetwork("OodleNetwork", [](ResolveContext& ctx) {
		Oodle::OodleNetworkFunctions r;
		const auto text = ctx.Text();

		const auto init = ctx.First(OodleInit, text, "OodleInit");
		init.ResolveAddressInto(r.SetMallocFree, 1);
		init.GetInto(r.HtBits, 2);
		init.ResolveAddressInto(r.SharedSize, 3);
		init.GetInto(r.WindowSize, 4);
		init.ResolveAddressInto(r.SharedSetWindow, 5);

		const auto train = ctx.First(OodleSetUpStatesAndTrain, text, "OodleSetUpStatesAndTrain");
		train.ResolveAddressInto(r.UdpStateSize, 1);
		train.ResolveAddressInto(r.TcpStateSize, 2);
		train.ResolveAddressInto(r.TcpTrain, 3);
		train.ResolveAddressInto(r.UdpTrain, 4);

		const auto decode = ctx.First(OodleDecode, text, "OodleDecode");
		decode.ResolveAddressInto(r.TcpDecode, 1);
		decode.ResolveAddressInto(r.UdpDecode, 2);

		const auto encode = ctx.First(OodleEncode, text, "OodleEncode");
		encode.ResolveAddressInto(r.TcpEncode, 1);
		encode.ResolveAddressInto(r.UdpEncode, 2);
		return r;
	});

	const Signatures::ComplexSignature<IpcTypeCandidates> IpcTypes("IpcTypes", [](ResolveContext& ctx) {
		IpcTypeCandidates r;
		const auto text = ctx.Text();

		r.C2S_ActionRequest = FindActionRequest(text);
		r.C2S_ActionRequestGroundTargeted = FindActionRequestGroundTargeted(ctx, text);

		// (ActionEffect01,) 08, 16, 24, 32, ActorCast, ActorControl, ActorControlTarget, ActorControlSelf
		// (AE01->08: 0x200,) 08->...->32: 0x240
		r.PayloadWriters = FindDutyRecorderPayloadWriters(ctx, text);
		const auto& writers = r.PayloadWriters;
		const auto opcodeAt = [&writers](size_t i) -> std::optional<uint16_t> {
			if (i >= writers.size())
				return std::nullopt;
			return writers[i].Opcode;
		};

		for (size_t i = 2, streak = 0; i < writers.size(); ++i) {
			if (writers[i].PayloadSize == writers[i - 1].PayloadSize * 2 - writers[i - 2].PayloadSize
				&& writers[i].PayloadSize >= 0x200)
				++streak;
			else
				streak = 1;

			if (streak == 3) {
				r.S2C_ActionEffects[0] = opcodeAt(i - 4);
				r.S2C_ActionEffects[1] = opcodeAt(i - 3);
				r.S2C_ActionEffects[2] = opcodeAt(i - 2);
				r.S2C_ActionEffects[3] = opcodeAt(i - 1);
				r.S2C_ActionEffects[4] = opcodeAt(i + 0);
				r.S2C_ActorCast = opcodeAt(i + 1);
				r.S2C_ActorControl = opcodeAt(i + 2);
				// r.S2C_ActorControlTarget = opcodeAt(i + 3);
				r.S2C_ActorControlSelf = opcodeAt(i + 4);
				break;
			}
		}
		return r;
	});

	const Signatures::ComplexSignature<uint32_t*> MixRateSetup("MixRateSetup", [](ResolveContext& ctx) {
		return ctx.Unique(AudioSamplingRateImmediate, ctx.Text(), "setup").begin<uint32_t>(1);
	});

	const Signatures::ComplexSignature<SoundVoiceRenderInfo> VoiceRender("VoiceRender", [](ResolveContext& ctx) {
		const auto m = ctx.Unique(SoundVoiceRender, ctx.Text(), "render");
		const auto getter = ctx.RequireInSection(m.ResolveAddress<const void*>(3), ".text", "mix rate getter");
		const auto g = ctx.MatchAt(AudioSamplingRateGetter, ctx.FunctionStartingAt(getter, "mix rate getter"), "mix rate getter");
		return SoundVoiceRenderInfo{
			.State = FieldOffset(ctx, m, 1, "state offset"),
			.QueuedBuffers = FieldOffset(ctx, m, 2, "queued buffers offset"),
			.MixRate = g.ResolveAddress<uint32_t*>(1),
		};
	});

	const Signatures::ComplexSignature<SoundVoiceFunctions> VoiceFunctions("VoiceFunctions", [](ResolveContext& ctx) {
		SoundVoiceFunctions r;
		const auto text = ctx.Text();
		r.Init = Address(ctx.Unique(SoundVoiceInit, text, "initialiser").begin(0));

		const auto vt = ctx.Unique(SoundVoiceVtable, text, "vtable");
		const auto slots = ctx.Vtable(vt.ResolveAddress<const void* const*>(1), SoundVoice::MaxVtblSlots, "vtable");
		const auto destructorBody = ctx.FunctionContaining(vt.begin(0), "destructor").data();

		// every slot is told apart by what it does, so that a reordered vtable is still understood
		const auto submit = ctx.UniqueSlot(slots, "submit", [&ctx](size_t, std::span<const uint8_t> fn) -> std::optional<SoundVoice::Layout> {
			const auto m = ctx.Find(SoundVoiceSubmit, fn);
			if (!m)
				return std::nullopt;
			return SoundVoice::Layout{
				.State = FieldOffset(ctx, *m, 1, "submit state offset"),
				.QueuedBuffers = FieldOffset(ctx, *m, 2, "submit queued buffers offset"),
			};
		});
		r.Layout = submit.second;
		r.Layout.SubmitSlot = submit.first;

		const auto setMarker = ctx.UniqueSlot(slots, "set marker", [&ctx](size_t, std::span<const uint8_t> fn) -> std::optional<size_t> {
			const auto m = ctx.TryMatchAt(SoundVoiceSetMarker, fn);
			if (!m)
				return std::nullopt;
			return static_cast<size_t>(m->Get<int32_t>(1));
		});
		ctx.Require(setMarker.second == r.Layout.State, ResolveError::Mismatch,
			"submit state +{:#x} != set marker state +{:#x}", r.Layout.State, setMarker.second);
		r.Layout.SetMarkerSlot = setMarker.first;

		if (const auto render = ctx.TryGet(VoiceRender)) {
			ctx.Require(render->State == r.Layout.State && render->QueuedBuffers == r.Layout.QueuedBuffers, ResolveError::Mismatch,
				"submit state +{:#x} and queued buffers +{:#x} != render state +{:#x} and queued buffers +{:#x}",
				r.Layout.State, r.Layout.QueuedBuffers, render->State, render->QueuedBuffers);
		}

		// the deleting destructor calls the destructor proper within its first few instructions
		r.Layout.DestructorSlot = ctx.UniqueSlotIndex(slots, "destructor", [destructorBody](size_t, std::span<const uint8_t> fn) {
			for (const auto& m : RelativeCall.Lookup(fn.first((std::min)(fn.size(), static_cast<size_t>(48))), RegexSignature::FromNextByte)) {
				if (m.ResolveAddress<const uint8_t*>(1) == destructorBody)
					return true;
			}
			return false;
		});

		// the flush is what puts the voice in the flushed state
		const auto state = r.Layout.State;
		r.Layout.FlushSlot = ctx.UniqueSlotIndex(slots, "flush", [state](size_t, std::span<const uint8_t> fn) {
			for (const auto& m : SoundVoiceStoreImmediate.Lookup(fn)) {
				if (std::cmp_equal(m.Get<int32_t>(1), state) && m.Get<int32_t>(2) == static_cast<int32_t>(SoundVoiceState::Flushed))
					return true;
			}
			return false;
		});

		r.Submit = Address(slots[r.Layout.SubmitSlot]);
		r.SetMarker = Address(slots[r.Layout.SetMarkerSlot]);
		r.Destructor = Address(slots[r.Layout.DestructorSlot]);
		r.Flush = Address(slots[r.Layout.FlushSlot]);
		return r;
	});

	const Signatures::ComplexSignature<SoundBufferEndInfo> BufferEndHandler("BufferEndHandler", [](ResolveContext& ctx) {
		const auto m = ctx.Unique(SoundBufferEndHandler, ctx.Text(), "handler");
		return SoundBufferEndInfo{
			.Handler = Address(m.begin(0)),
			.CallbackLayout = {.EndOfData = FieldOffset(ctx, m, 1, "end of data offset")},
		};
	});

	const Signatures::ComplexSignature<MssAsiFunctions> MssAsiStream("MssAsiStream", [](ResolveContext& ctx) {
		MssAsiFunctions r;
		const auto text = ctx.Text();

		const Address attribute = ctx.First(MssAsiAttribute, text, "attribute").begin(0);
		r.Attribute = attribute;

		for (const auto& m : MssAsiResetPair.Lookup(text, RegexSignature::FromNextByte)) {
			if (m.ResolveAddress<const void*>(2) == attribute.Get()) {
				r.Reset = Address(m.ResolveAddress<const void*>(1));
				break;
			}
		}
		ctx.Require(r.Reset != nullptr, ResolveError::NotFound, "reset not found");

		r.Open = Address(ctx.First(MssAsiOpen, text, "open").ResolveAddress<const void*>(1));
		r.SetUpDecoder = Address(ctx.FunctionContaining(ctx.First(MssAsiSetUpDecoder, text, "decoder set-up").begin(0), "decoder set-up").data());
		r.Process = Address(ctx.FunctionContaining(ctx.First(MssAsiProcess, text, "process").begin(0), "process").data());
		return r;
	});

	const Signatures::ComplexSignature<MessageLoopFn> MessageLoopFunction("MessageLoopFunction", [](ResolveContext& ctx) -> MessageLoopFn {
		return Address(ctx.First(MessageLoop, ctx.Image(), "message loop").ResolveAddress<const void*>(1));
	});
}
