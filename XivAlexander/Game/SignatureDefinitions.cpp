#include "pch.h"
#include "Game/SignatureDefinitions.h"

#include <algorithm>
#include <map>
#include <utility>

#include <xivres/pe_image.h>

#include "Game/ResolveContext.h"
#include "Game/Signatures.h"
#include "Game/Structs.h"
#include "Misc/Logger.h"
#include "Utils/Win32/Process.h"

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

	// The stream attribute query; attribute 0 is the channel count, read from the stream.
	const Signatures::RegexSignature MssAsiAttribute(R"(\x85\xD2\x74.\x83\xFA\x01\x74\x03\x33\xC0\xC3\xB8\x10\x00\x00\x00\xC3\x8B\x41(.)\xC3)");
	// The game opening a stream with its fetch callback.
	const Signatures::RegexSignature MssAsiOpen(R"([\x44\x45]\x8B[\x40-\x43\x45-\x47]\x20\x48\x8D\x15(....)[\x48\x49]\x8B[\xC8-\xCF]\xE8(....))");
	// The open allocating the stream.
	const Signatures::RegexSignature MssAsiOpenAllocation(R"(\xB9(....)(?:\xFF\xD0|\xFF\x15....))");
	// The process fetching what is pending, then adding to the fetched bytes.
	const Signatures::RegexSignature MssAsiProcessFetch(R"(\x44\x8B[\x48-\x4B\x4D-\x4F](.).{0,12}?[\x48\x49]\x8B[\x48-\x4B\x4D-\x4F](.).{0,12}?\x41?\xFF[\x50-\x53\x55-\x57](.)\x41?\x01[\x40-\x43\x45-\x47](.).{0,4}?\x41?\xC7[\x40-\x43\x45-\x47]\1\xFF\xFF\xFF\xFF)");
	// The decoder set-up storing the source buffer, then fetching its first bytes into it: callback(user, source, bytes, 0).
	const Signatures::RegexSignature MssAsiSetUpFetch(R"([\x48\x49]\x89[\x40-\x43\x45-\x47](.)[\x48\x4D]\x85\xC0(?:\x75.|\x0F\x85....).{0,96}?[\x48\x49]\x8B[\x48-\x4B\x4D-\x4F](.)\x45\x33\xC9\x41\xB8(....).{0,8}?[\x48\x49]\x8B[\xD0-\xD7]\x41?\xFF[\x50-\x53\x55-\x57](.))");
	// The game's fetch callback testing its flags: end of stream, then marking the stream for reinitialisation.
	const Signatures::RegexSignature FfxivAsiFetchFlags(R"(\x0F\xB6\x41(.)\xA8(.)(?:\x0F\x85....|\x75.)\xA8.\x74.\x0C(.)\x88\x41\1)");
	const Signatures::RegexSignature MssAsiSetUpDecoder(R"([\x48-\x4F]\x8B[\x00-\x3F][\x40-\x4F]?\x89[\x40-\x7F]\x38[\x48-\x4F]\x8B[\x00-\x3F][\x40-\x4F]?\x89[\x40-\x7F]\x34[\x48-\x4F]\x8B[\x00-\x3F]\xC7[\x40-\x47]\x30\x00\x00\x01\x00)");
	const Signatures::RegexSignature MssAsiProcess(R"(\xFF\x41\x3C[\x40-\x4F]?\x8B[\xC0-\xFF]\x83\x79\x3C\x01)");
	const Signatures::RegexSignature MssAsiResetPair(R"(\xE8(....)(?:[\x48\x49]\x8B[\x40-\x4F]\x08\x33\xD2|\x33\xD2[\x48\x49]\x8B[\x40-\x4F]\x08)\xE8(....))");

	const Signatures::RegexSignature SoundVoiceInit(R"([\x48-\x4F]\x8B[\xC0-\xFF]\x41\x83\xF8(.)(?:\x0F\x8F....|\x7F.|\x7E.\x83\xC8\xFF\xC3|\x7E.\xB8\xFF\xFF\xFF\xFF\xC3)\x45\x85\xC9(?:\x0F\x84....|\x74.)[\x48\x4C]\x8B[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24\x28[\x48-\x4F]\x89[\x80-\xBF])");
	const Signatures::RegexSignature SoundVoiceVtable(R"([\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....)[\x48-\x4F]\x8B[\xC0-\xFF][\x48-\x4F]\x89[\x00-\x3F](?:[\x48\x49]\x8D[\x50-\x57]\x30|[\x48\x49]\x83[\xC0-\xC7]\x30\xFF\x15))");
	const Signatures::RegexSignature SoundVoiceRender(R"([\x40-\x4F]?\x83[\xB8-\xBB\xBD-\xBF](....)\x03.{0,8}?(?:\x0F\x85....|\x75.)[\x40-\x4F]?\x83[\xB8-\xBB\xBD-\xBF](....)\x00(?:\x0F\x8E....|\x7E.)\xE8(....))");
	// The submit's state test, then the test of the queued buffers against the most it queues.
	const Signatures::RegexSignature SoundVoiceSubmit(R"(\xF7[\x80-\x83\x85-\x87](....)(....).{0,24}?[\x40-\x4F]?\x8B[\x80-\xBF](....)[\x40-\x4F]?\x83[\xF8-\xFF](.)(?:\x0F\x8D....|\x7D.))");
	const Signatures::RegexSignature SoundVoiceSetMarker(R"(\xF7[\x80-\x83\x85-\x87](....)(....)\x74.\x89[\x90-\x93\x95-\x97](....)\x33\xC0\xC3)");
	const Signatures::RegexSignature SoundVoiceStoreImmediate(R"(\xC7[\x80-\x83\x85-\x87](....)(....))");
	const Signatures::RegexSignature RelativeCall(R"(\xE8(....))");
	const Signatures::RegexSignature SoundBufferEndHandler(R"([\x40\x41][\x50-\x57]\x48\x83\xEC.\x83\x79.\x07[\x48-\x4F]\x8B[\xC0-\xFF]\x74.\x48\x83\xC1\xF8\xE8....[\x40-\x4F]?\x80[\xB8-\xBB\xBD-\xBF](....)\x01)");

	const Signatures::RegexSignature MessageLoop(R"(\xE8(....)\x84\xc0\x75\xf7)");

	// Callers of the login, which check lobby state 4 or 0x3B first; the login's own code changes too often to match.
	const Signatures::RegexSignature LobbyLoginCaller(R"(\x8B[\x80-\xBF]....\x83\xF8\x04(?:\x74.|\x0F\x84....)\x83\xF8\x3B.{0,80}?\xE8(....))");
	// Near its start, the login tests its last argument to choose between two ways of logging in.
	const Signatures::RegexSignature LobbyLoginLastArgumentTest(R"(\x80\xBC\x24....\x00)");

	// AgentLobby's OK handler of a lobby error dialog, which reads the error code from the dialog result right away.
	const Signatures::RegexSignature LobbyErrorDialog(R"(\x40\x53\x48\x83\xEC\x30\x48\x8B\xD9\x49\x8B\xC8\xE8(....)\x8B\xD0)");
	// What it reads the code with: zero unless the AtkValue has a type, else the u32 value.
	const Signatures::RegexSignature AtkValueUIntGetter(R"(\xF6\x01(.)\x75.\x33\xC0\xC3\x8B\x41(.)\xC3)");
	// The Utf8String copy constructor reading the source's size, length and empty flag.
	const Signatures::RegexSignature Utf8StringCopy(R"([\x48\x49]\x8B[\x50-\x57](.)\xE8....[\x4C\x4D]\x8B[\x40-\x47](.)[\x48\x49]\x8B[\x10-\x17][\x48\x49]\x8B[\x08-\x0F]\xE8....[\x48\x49]\x8B[\x40-\x47](.)[\x48\x49]\x89[\x40-\x47]\3[\x40-\x4F]?\x0F\xB6[\x40-\x47](.))");

	// The IME mode getter's ImmGetOpenStatus, then ImmGetConversionStatus, on a global HIMC, with either form of its early return.
	const Signatures::RegexSignature ImeStatusQueries(R"(\x48\x8B\x0D(....)(?:\xE8|\xFF\x15)(....)\x85\xC0(?:\x74.|\x0F\x84....|\x75.{1,16}?)\x48\x8B\x0D(....)(?:\x4C\x8D\x44\x24.\x48\x8D\x54\x24.|\x48\x8D\x54\x24.\x4C\x8D\x44\x24.)(?:\xE8|\xFF\x15)(....))");
	// bt eax, 8: tests IME_CMODE_NOCONVERSION, which only the IME mode getter does after those queries.
	const Signatures::RegexSignature ImeNoConversionTest(R"(\x0F\xBA\xE0\x08)");

	// The call of Kernel::SwapChain::Present in DeviceDX11::PostTick's render thread path, which then jumps over the other path's call.
	const Signatures::RegexSignature SwapChainPresentCall(R"([\x48\x49]\x8B[\x48-\x4B\x4D-\x4F]\x70\xE8(....)\x41?\xC6[\x40-\x47]\x79\x00\xEB)");
	// AtkServer::SetDrawState staging the blend state (masked to its 27 bits) and clearing a flag after it, then the texture's width over the divisor.
	const Signatures::RegexSignature AtkServerSetDrawStateBody(R"(\x25\xFF\xFF\xFF\x07\x89[\x80-\xBF]....\xC6[\x80-\xBF]....\x00.{0,400}?\x48\x85\xC0(?:\x74.|\x0F\x84....)\x8B[\x40-\x7F](.)\x33\xD2\xF7[\xF0-\xF7])");
	// AtkServer::SetRenderTexture's begin: the command's flags, then the AtkServer flag it sets while drawing into the texture.
	const Signatures::RegexSignature AtkServerBeginRenderTexture(R"(\x81[\xE0-\xE7]\x01\x00\x00\xFF.{0,16}?\x83[\xC8-\xCF]\x01\x89[\x40-\x7F]\x08.{0,8}?\xC6[\x80-\xBF](....)\x01)");

	// The pre-present notifier walk before that call: the game's, or SkipIdleNotifiers' rewrite of it.
	const Signatures::RegexSignature PrePresentNotifierWalk(R"(\x48\x8D\x0D....\xFF\x15....(?:\x48\x8B\x1D....\x48\x85\xDB\x74.\x48\x8B\x03\x48\x8B\xCB\xFF\x50\x10|\x48\xB9.{8}\x48\xB8.{8}\xFF\xD0\xEB))");

	// ExcelModuleInterface::GetLanguage's end: the module's language minus one into a static, and its address returned.
	const Signatures::RegexSignature ExcelLanguageGetterBody(R"(\xE8....\xFF\xC8\x89\x05(....)\x48\x8D\x05(....))");
	// Localize::ProcessNoun loading the Attributive sheet once: the interface, the name, then where it keeps the sheet.
	const Signatures::RegexSignature LocalizeAttributiveLoad(R"([\x48\x49]\x8B[\x48-\x4F](.)\x48\x8D\x15(....)\x48\x8B\x49\x08\xE8....[\x48\x49]\x89[\x40-\x47](.)\x48\x85\xC0)");
	// Then the language it picks the handler by: the interface, the call, and the switch over eight languages.
	const Signatures::RegexSignature LocalizeLanguageSwitch(R"([\x48\x49]\x8B[\x48-\x4F](.).{0,24}?\xE8(....).{0,4}?[\x48\x4C]\x63[\x00-\x3F]\x83[\xF8-\xFF]\x07\x0F\x87)");
	// Its map lookup at the start: the head from the map, then the map itself.
	const Signatures::RegexSignature LocalizeMapLoad(R"([\x48\x4C]\x8B[\x41\x49\x51\x59\x61\x69\x71\x79](.)[\x48\x4C]\x8D[\x41\x49\x51\x59\x61\x69\x71\x79]\1)");
	const Signatures::RegexSignature MapNodeIsNilTest(R"(\x80[\x78-\x7F](.)\x00)");
	// A found node's handler formatting the noun: its vtable slot 1.
	const Signatures::RegexSignature LocalizeNodeHandlerCall(R"(\x48\x8B[\x88-\x8F](....)(?:[\x48\x49\x4C\x4D]\x8B[\xC0-\xFF]){0,3}\x48\x8B\x01\xFF\x50\x08)");
	// A new node: allocated, then its colour and nil flag cleared together.
	const Signatures::RegexSignature LocalizeNodeAllocation(R"(\xB9(....)\x41\xB8....\xE8)");
	const Signatures::RegexSignature LocalizeNodeColourClear(R"(\x66\x41?\xC7[\x40-\x47](.)\x00\x00)");
	// The noun macro handler comparing the macro's language with the sheets'.
	const Signatures::RegexSignature NounMacroLanguageTest(R"([\x48\x49]\x8B(?:[\x48-\x4B\x4D-\x4F](.)|\x4C\x24(.))\xE8(....)\x8B([\x85\x8D\x95\x9D\xA5\xAD\xB5\xBD])(....)\x39([\x00-\x3F])\x74(.))");
	// The wrapper setting its CJK flag from the language (chs and later).
	const Signatures::RegexSignature TextWrapCjkFlag(R"([\x48\x49]\x8B[\x48-\x4F](.).{0,24}?\xE8(....).{0,16}?\x83\x38\x04\x0F\x9D\xC0\x88[\x80-\x83\x85-\x87](....))");
	// Its test of what the set-up stores, first thing.
	const Signatures::RegexSignature TextWrapFontsTest(R"(\x48\x83\x79(.)\x00)");
	// The wrapper set-up setting the Korean flag (ko only), and storing its argument first.
	const Signatures::RegexSignature TextWrapKoreanFlag(R"([\x48\x49]\x8B[\x48-\x4F](.)\xE8(....).{0,8}?\x83\x38\x06\x0F\x94\xC0\x88[\x80-\x83\x85-\x87](....))");
	const Signatures::RegexSignature TextWrapSetupStore(R"(\x48\x89\x51(.))");
	// AtkModule::Initialize calling the set-up for its wrapper with what it stores, both inside AtkModule.
	const Signatures::RegexSignature TextWrapSetupCall(R"(\x48\x8D([\x88-\x8F])(....).{0,16}?\x48\x8D([\x90-\x97])(....)\xE8(....))");
	// AddonTalk::OnRefresh: 3 into a register, then the language from the number array tested for en, de and fr.
	const Signatures::RegexSignature TalkLanguageBranch(R"(\x41([\xB8-\xBF])\x03\x00\x00\x00.{0,12}?\x8B([\x40-\x7F])\x0C(\x8B[\xC8-\xCF])\x83\xE9\x01\x74(.)\x83\xE9\x01\x74(.)\x83\xF9\x01\x74(.))");
	// Their side: 4 lines into the line count, font size 14, line spacing 20.
	const Signatures::RegexSignature TalkFourLines(R"([\x48\x49]\x8B[\x88-\x8F]....\xB8\x04\x00\x00\x00(?:\x45\x33\xC0)?\x66\x89[\x80-\x87](....)\xB2\x0E\xE8(....)\xB2\x14)");
	// The other side: the register's 3 into the line count, then the ULD's font size.
	const Signatures::RegexSignature TalkThreeLines(R"(\x66\x44\x89([\x80-\xBF])(....)\xE8(....))");
	// AddonTalkSubtitle: scale 1, then for ja, chs, cht and tc the other scale.
	const Signatures::RegexSignature SubtitleLanguageScale(R"(\xF3\x0F\x10([\x05\x0D\x15\x1D\x25\x2D\x35\x3D])(....).{0,24}?\x8B[\x40-\x7F]\x0C(\x85[\xC0-\xFF])\x74(.)(?:\x2B[\xC0-\xFF]|\x83[\xE8-\xEF]\x04)\x74(.)\x83[\xE8-\xEF]\x01\x74(.)\x83[\xF8-\xFF]\x02\x75(.)(\xF3\x0F\x10\1(....))\xF3\x0F\x11)");

	// PronounModule::ProcessShellCommandMessage's byte limit for the text checker.
	const Signatures::RegexSignature ShellCommandMessageLimit(R"(\xC7\x44\x24.\xFF\x03\x00\x00\x48\x8D[\x81\x89\x91\x99\xA1\xA9\xB1\xB9](....)\x4C\x89\x44\x24.)");

	// The signatures below are from CrowdFix (SheepGoMeh).

	// The call of TaskManager::ExecuteAllTasks in Framework::Tick.
	const Signatures::RegexSignature TaskManagerExecuteAllTasksCall(R"(\xE8(....)\x48\x8B\x8B....\x48\x85\xC9\x74.\xF3\x0F\x10\x8B)");
	// A call to the culling manager getter, which starts with mov rax, [g_CullingManager].
	const Signatures::RegexSignature CullingManagerGetterCall(R"(\xE8(....)\x48\x8B\x05....\x83\x60..\xE8)");
	const Signatures::RegexSignature CullingManagerGetter(R"(\x48\x8B\x05(....))");

	// The start of the Kernel::Notifier list walk: the lock, then the head.
	const Signatures::RegexSignature NotifierListWalk(R"(\x48\x8D\x0D(....)\xFF\x15....\x48\x8B\x1D(....)\x48\x85\xDB\x74.\x48\x8B\x03\x48\x8B\xCB\xFF\x50\x10)");
	// The two PostTick loops over the notifiers, each calling its vtable slot and following the next notifier.
	const Signatures::RegexSignature NotifierPrePresentLoop(R"(\x48\x8B\x1D(....)\x48\x85\xDB\x74\x12\x48\x8B\x03\x48\x8B\xCB\xFF\x50(.)\x48\x8B\x5B(.)\x48\x85\xDB\x75\xEE)");
	const Signatures::RegexSignature NotifierPostKickLoop(R"(\x48\x8B\x1D(....)\x48\x85\xDB\x74\x14\x66\x90\x48\x8B\x03\x48\x8B\xCB\xFF\x50(.)\x48\x8B\x5B(.)\x48\x85\xDB\x75\xEE)");
	// Link and Unlink, which take the notifier lock; then Link reads the head, Unlink [rbx+8].
	const Signatures::RegexSignature NotifierLinkBody(R"(\x48\x8B\xD9\x48\x8D\x0D(....)\xFF\x15....\x48\x8B\x05(....)\x33\xC9)");
	const Signatures::RegexSignature NotifierUnlinkBody(R"(\x48\x8B\xD9\x48\x8D\x0D(....)\xFF\x15....\x48\x8B\x4B\x08)");
	// Notifier callbacks (buffer, vertex/index buffer, texture, constant buffer) up to their flag test.
	// The texture pre-present one also does work for another flag, which it tests at the jne target.
	const Signatures::RegexSignature BufferNotifierPostKick(R"(\x40\x53\x48\x83\xEC\x40\x8B\x05....\x48\x8B\xD9\x48\x8B\x54\xC1\x20\x48\x85\xD2\x74.\x8B\x49(.)\xF6\xC1(.)\x74)");
	const Signatures::RegexSignature BufferNotifierPrePresent(R"(\x40\x53\x48\x83\xEC\x20\x8B\x05....\x48\x8B\xD9\x48\x8B\x54\xC1\x20\x48\x85\xD2\x74.\xF6\x41(.)(.)\x74)");
	const Signatures::RegexSignature IndexBufferNotifierPostKick(R"(\x40\x53\x48\x83\xEC\x40\x8B\x05....\x48\x8B\xD9\x4C\x8B\x54\xC1\x28\x4D\x85\xD2\x74.\x8B\x51(.)\xF6\xC2(.)\x74.\xF6\xC2(.)\x75)");
	const Signatures::RegexSignature IndexBufferNotifierPrePresent(R"(\x40\x53\x48\x83\xEC\x20\x8B\x05....\x48\x8B\xD9\x48\x8B\x54\xC1\x28\x48\x85\xD2\x74.\x8B\x41(.)\xA8(.)\x74.\xA8(.)\x75)");
	const Signatures::RegexSignature TextureNotifierPostKick(R"(\x40\x53\x48\x83\xEC\x40\x8B\x41(.)\x48\x8B\xD9\x25(....)\x3D\2\x0F\x85)");
	const Signatures::RegexSignature TextureNotifierPrePresent(R"((?:\x40|\x48\x8B\xC4)\x53\x48\x83\xEC\x60\x48\x83\x79\x48\x00\x48\x8B\xD9\x0F\x84....\x8B[\x49\x51](.).{0,32}?\x25(....).{0,24}?\x3D\2(?:\x0F\x85(....)|\x75(.)))");
	const Signatures::RegexSignature TextureNotifierUploadTest(R"(\x0F\xBA[\xE0-\xE7](.))");
	const Signatures::RegexSignature ConstantBufferNotifierPrePresent(R"(\x48\x89\x5C\x24\x10\x57\x48\x83\xEC\x40\x8B\x05....\x48\x8B\xD9\x48\x8B\x7C\xC1\x18\x48\x85\xFF\x74.\x48\x83\x79\x30\x00\x74.\xF7\x41(.)(....)\x74)");

	// The increments of the 128-entry job ring's write index by the enqueues, and of its read index by the fewer dequeues.
	const Signatures::RegexSignature JobRingIndexIncrement(R"(\x8B\x05(....)\xFF\xC0\x83\xE0\x7F\x89\x05(....))");
	// The call of JobPool::WakeAll in the kick.
	const Signatures::RegexSignature JobPoolWakeAllCall(R"(\x48\x8D[\x48-\x4B\x4D-\x4F](.)\xE8(....)\x48\x8B\x4C\x24.\xBA....\xFF\x15)");
	// The wake-all's loop: the pool's thread count and threads, then each thread's skip flag, wake count, wake limit and event.
	const Signatures::RegexSignature JobPoolWakeAllBody(R"(\x39[\x40-\x7F](.)\x76.(?:\x0F\x1F[\x00-\x84].{0,6}?|\x66?\x90)*\x48\x8B[\x40-\x7F](.)\x48\x8B\x0C[\xC0-\xFF]\x0F\xB6\x41(.)\x84\xC0\x75.\x33\xC0\xF0\x0F\xC1\x41(.)\x83\xF8(.)\x73.\xB8\x01\x00\x00\x00\xF0\x0F\xC1\x41\4\x85\xC0\x75.\x48\x8B\x49(.)\xFF\x15(....))");

	const Signatures::RegexSignature SkeletonPoseSyncWalkCall(R"(\xE8(....)\xE8....\xE8....\x41\x8B\xFF)");
	// The partial skeleton count, array, pose and stride, in that order in the pose sync walk.
	const Signatures::RegexSignature PoseSyncPartialCount(R"(\x0F\xB7[\x40-\x7F](.))");
	const Signatures::RegexSignature PoseSyncPartialArray(R"(\x48\x8B[\x40-\x7F](.)[\x48\x49]\x03[\xC0-\xFF])");
	const Signatures::RegexSignature PoseSyncPartialPose(R"(\x48\x8B[\x80-\xBF](....)[\x48\x4C]\x85[\xC0-\xFF]\x74.\xE8)");
	const Signatures::RegexSignature PoseSyncPartialStride(R"([\x48\x49]\x81[\xC0-\xC7](....)[\x48\x49]\x83[\xE8-\xEF]\x01)");

	// The count of the loop that clears the visibility table, one 16 byte store per object slot.
	const Signatures::RegexSignature CullingVisibilityClearCount(R"(\xB9(....)(?:[\x48\x4C]\x8B[\x84-\xBC]\x24....|\x0F\x28[\x84-\xBC]\x24....|\x90|\x0F\x1F[\x00-\x84].{0,6}?){0,10}\xF3\x0F\x7F\x00\x48\x8D\x40\x10\x48\x83\xE9\x01\x75\xF2)");
	// The visibility table load right before that count.
	const Signatures::RegexSignature CullingVisibilityTableLoad(R"([\x48\x49]\x8B[\x40-\x47](.)\x0F\x57\xC0.{0,12}?\xB9....)");
	// The culling manager's object slot allocator: the mask, the words it scans, the entry shift and the objects.
	const Signatures::RegexSignature CullingSlotAlloc(R"([\x48\x4C]\x8B[\x41\x49\x51\x59\x61\x69\x71\x79](.).{0,16}?\x8B[\x00-\x3F]\x83\xF8\xFF\x74..{0,40}?\x81[\xF8-\xFF](....)\x72..{0,24}?\x0B[\xC0-\xFF][\x40-\x4F]?\x89[\x04-\x3C][\x80-\xBF][\x41]?\xC1[\xE0-\xE7]\x05.{0,8}?[\x48\x49]\xC1[\xE0-\xE7](.)[\x48\x49\x4C\x4D]\x03[\x40-\x7F](.))");
	// The object slot release: the objects, the mask and the entry shift.
	const Signatures::RegexSignature CullingSlotRelease(R"([\x48\x49\x4C\x4D]\x2B[\x40-\x7F](.)[\x48\x49\x4C\x4D]\x8B[\x40-\x7F](.)[\x48\x49]\xC1[\xF8-\xFF](.).{0,24}?\xF7[\xD0-\xD7][\x40-\x4F]?\x21)");
	// A call with g_CullingManager as this.
	const Signatures::RegexSignature CullingManagerCall(R"(\x48\x8B\x0D(....)\xE8(....))");

	const Signatures::RegexSignature GraphicsAllocatorFreeStart(R"(\x48\x85\xD2\x0F\x84....\x48\x89\x74\x24.\x57\x48\x83\xEC.\x48\x8B\xF1\x48\x89\x5C\x24.\x48\x81\xC1)");
	// Free's slab check: the lock, the page mask and index, then the chunk count, table, base and span.
	const Signatures::RegexSignature GraphicsAllocatorFreeLayout(R"(\x48\x81\xC1(....)[\x48\x4C]\x8B[\xC0-\xFF]\xFF\x15(....)[\x48\x4C]\x8B[\xC0-\xFF][\x48\x49]\x81[\xE0-\xE7](....)[\x40-\x4F]?\x8B[\x40-\x7F](.)\x39[\x80-\xBF](....)\x0F\x86....([\x48\x4C]\x8D[\x00-\x3F][\x40-\x7F])[\x48\x4C]\x8B[\x80-\xBF](....)([\x48\x4C]\x03[\xC0-\xFF])[\x48\x4C]\x8B[\x40-\x7F]([\xC0-\xFF])(.)[\x48\x4C]\x85[\xC0-\xFF]\x0F\x84....[\x48\x4C]\x8B[\xC0-\xFF][\x48\x4C]\x2B[\xC0-\xFF](?:\x48\x3D|\x48\x81[\xF8-\xFF])(....))");
	// The frees through the backing allocator, and the vtable slot they call.
	const Signatures::RegexSignature GraphicsAllocatorBackingFree(R"(\x48\x8B[\x88-\x8F](....)(?:[\x48\x49]\x8B[\xD0-\xD7])?\x48\x8B\x01\xFF\x50(.))");
	// The unlock at the end of Free.
	const Signatures::RegexSignature GraphicsAllocatorUnlock(R"(\x48\x8D[\x88-\x8F](....)\xFF\x15(....))");
	// The allocator manager load in the buffer write lock, the allocator in it and the slot called on it.
	const Signatures::RegexSignature GraphicsAllocatorManagerLoad(R"(\x48\x8B\x05(....)\x41\xB8....\x8B\xD7\x48\x8B\x48(.)\x48\x8B\x01\xFF\x50(.)\x48\x89\x83)");
	// The graphics allocator constructor or destructor storing both vtables, and where the inner one goes.
	const Signatures::RegexSignature GraphicsAllocatorVtables(R"(\x48\x8D\x05(....)(?:[\x48\x4C]\x8B[\xC0-\xFF])?\x48\x89\x01(?:\x8B[\xC0-\xFF])?\x48\x8D\x05(....)\x48\x89\x81(....))");
	// Its alloc counts, then forwards to the inner allocator (most others only forward).
	const Signatures::RegexSignature GraphicsAllocatorCountingWrapper(R"(\xB8\x01\x00\x00\x00\xF0\x0F\xC1\x81(....)\x48\x81\xC1(....)\x48\x8B\x01\x48\xFF\x60(.))");
	const Signatures::RegexSignature GraphicsAllocatorForwardingWrapper(R"(\x48\x81\xC1(....)\x48\x8B\x01\x48\xFF\x60(.))");
	// The terminate releases the inner allocator, then calls the base's.
	const Signatures::RegexSignature GraphicsAllocatorTerminateWrapper(R"(\x40\x53\x48\x83\xEC.\x48\x8B\xD9\x48\x81\xC1(....)\x48\x8B\x01\xFF\x50(.)\x48\x8B\x43(.)\x48\x8D\x4B(.))");
	// The inner allocator's size query: the page mask, the page index and the chunk count.
	const Signatures::RegexSignature SmallObjectAllocatorSize(R"([\x48\x4C]\x8B[\xC8-\xCF][\x48\x49]\x81[\xE0-\xE7](....)[\x40-\x4F]?\x8B[\x40-\x7F](.)\x39[\x80-\xBF](....))");

	// The tail jump to the follow AI at the end of Companion::Update.
	const Signatures::RegexSignature CompanionFollowJump(R"(\xE9(....)\x48\x8B\xCF\xE8....\xF3\x0F\x58\x87....\x0F\x2F\x05)");
	// Elsewhere in Companion::Update: the render flags test right after the draw-ready check.
	const Signatures::RegexSignature CompanionRenderFlagsTest(R"(\xFF\x90\x08\x01\x00\x00(?:\x3C\x01\x0F\x85....|\x84\xC0\x0F\x84....|\x3C\x01\x75.|\x84\xC0\x74.)\x48\x83[\xB8-\xBF](....)\x00)");

	// JobList::Prepare, up to its second WaitForSingleObject call, after waiting for the previous run and resetting counters.
	const Signatures::RegexSignature JobListArrayPrepare(R"(\x40\x53\x48\x83\xEC\x20\x48\x8B\x01\x48\x8B\xD9\xFF\x50\x18\x33\xD2\x8B\xC2\x87\x43\x7C\x87\x93\xA0\x00\x00\x00\x48\x8B\x4B\x10\xBA\xFF\xFF\xFF\xFF(\xFF\x15)(....))");
	const Signatures::RegexSignature JobListSingleItemPrepare(R"(\x40\x53\x48\x83\xEC\x20\x48\x8B\x01\x48\x8B\xD9\xFF\x50\x18\x33\xC0\xBA\xFF\xFF\xFF\xFF\x87\x43\x74\x48\x8B\x4B\x10(\xFF\x15)(....))");

	// Callers of the job list kick, with g_TaskManager as this.
	const Signatures::RegexSignature JobListKickCaller(R"(\x48\x8B[\x50-\x57]\x18\x48\x8B\x0D(....)\xE8(....))");
	const Signatures::RegexSignature RenderManagerLoad(R"(\x48\x8B\x0D(....)\xE8....\x84\xC0\x74.\x48\x8B\x0D....\xE8....\x33\xC9)");
	// Manager::RenderView kicking its prep list on Framework's TaskManager.
	const Signatures::RegexSignature BgInstancingPrepKickCall(R"(\x48\x8D\x93(....)\x48\x81\xC1(....)\xE8(....))");
	// The kick's calls on the job list: item count, prepare, describe, item count.
	const Signatures::RegexSignature JobListKickCalls(R"(\xFF\x50(.)\x85\xC0(?:\x0F\x84....|\x74.)[\x48\x49]\x8B[\x00-\x3F][\x48\x49]\x8B[\xC8-\xCF]\xFF\x50(.)[\x48\x49]\x8B[\x00-\x3F](?:\x48\x8D\x54\x24.|[\x48\x49]\x8B[\xC8-\xCF]){2}\xFF\x50(.)[\x48\x49]\x8B[\x00-\x3F][\x48\x49]\x8B[\xC8-\xCF]\xFF\x50(.))");
	// InnerThread::Run running a claimed task: the pool, its context, and the task slots called with and without an argument.
	const Signatures::RegexSignature JobRunTask(R"(\x48\x8B[\x50-\x57](.)[\x48\x49]\x8B[\xC8-\xCF][\x4C\x48]\x8B[\x44\x4C]\x24.[\x48\x49]\x83\xC2(.)[\x48\x49]\x8B[\x00-\x07][\x4D\x48]\x85[\xC0-\xFF]\x75.\xFF\x50(.)\xE9....[\x4D\x49]\x8B[\x00-\x3F]\xFF\x50(.))");
	// And claiming it first, from the queue entry: the claim function, the state and the owner, in either order.
	const Signatures::RegexSignature JobRunClaim(R"((?:\x48\x8B[\x40-\x47](.)\x48\x8D[\x50-\x57](.)\x48\x8B[\x48-\x4F](.)\x4C\x8D\x4C\x24.\x4C\x8D\x44\x24..{0,8}?\xFF\xD0|\x48\x8B[\x48-\x4F](.)\x48\x8D[\x50-\x57](.)\x4C\x8D\x4C\x24..{0,8}?\x4C\x8D\x44\x24.\xFF[\x50-\x57](.)))");

	// A hotbar slot's prepare: the temporary's constructor, then Prepare; then the slot index increment and slot id step.
	const Signatures::RegexSignature HotbarPrepare(R"((\x48\x8D\x4C\x24(.)\xE8(....)[\x48\x49]\x8B[\x88-\x8F]....\x4C\x8D\x44\x24\2[\x48\x49]\x8B[\xD0-\xD7]\xE8(....))\xFF[\xC0-\xC7]\x83[\xC0-\xC7]\x11)");

	const Signatures::RegexSignature AnimationUpdateCall(R"(\xE8(....)\x48\x8B\x0D....\x48\x8B\x6C\x24\x58)");
	const Signatures::RegexSignature AnimationTailStart(R"(\x48\x89\x5C\x24\x18\x55\x48\x83\xEC\x30\x48\x8B\xE9)");
	// The animation update storing the tail entry count, then the entries.
	const Signatures::RegexSignature AnimationTailEntries(R"(\x89[\x05\x0D\x15\x1D\x2D\x35\x3D](....)\x48\x8D[\x05\x0D\x15\x1D\x2D\x35\x3D](....)\x83[\xF8-\xFF]\x01)");
	const Signatures::RegexSignature AnimationSubmit(R"(\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x20\x48\x8B\x05(....)\x48\x8B\xD9\xF3\x0F\x11\x88\xF0\x00\x00\x00)");
	// The animation submit, in this order. Its group: the submit base, the writer count and the group.
	const Signatures::RegexSignature AnimationSubmitGroup(R"([\x48\x4C]\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....)(?:[\x40-\x4F]?\x33[\xC0-\xFF])?[\x40-\x4F]?\x8B[\x40-\x7F](.)[\x48\x49\x4C\x4D]\x8D[\x40-\x7F](.))");
	// Publishing every writer's item count to its block: the writers, the block and item count in a writer, and the writer size.
	const Signatures::RegexSignature ParallelForWriterFlush(R"([\x48\x49]\x8B[\x40-\x7F](.)[\x48\x4C]\x8B[\x44\x4C\x54\x5C\x64\x6C\x74\x7C].(.)[\x48\x4D]\x85[\xC0-\xFF]\x74.\x8B[\x44\x4C\x54\x5C\x64\x6C\x74\x7C].(.)[\x40-\x4F]?\x89[\x00-\x3F][\x48\x49]\x83[\xC0-\xC7](.)[\x48\x49]\x83[\xE8-\xEF]\x01\x75.)");
	// Arming: the job, the context, the two claim counters and the blocks claimed counter, then the test for nothing appended.
	const Signatures::RegexSignature ParallelForArm(R"(\x48\x8D\x05(....)[\x48\x4C]\x89[\x40-\x7F](.)\x48\x89[\x40-\x7F](.)\x8B[\xC0-\xFF]\x87[\x80-\xBF](....)\x8B[\xC0-\xFF]\x87[\x80-\xBF](....)\x8B[\xC0-\xFF]\xF0\x0F\xC1[\x80-\xBF](....)\x85\xC0\x74.)");
	// The kick of the group's job list, the per-item or block help, then the wait on the job list.
	const Signatures::RegexSignature AnimationSubmitKick(R"(\x48\x8B[\x50-\x57](.)\x48\x8B\x0D(....)\xE8(....)[\x48\x49]\x8B[\xC8-\xCF][\x40-\x47]?\x38[\x80-\xBF](....)\x74\x07\xE8(....)\xEB\x05\xE8(....)[\x48\x49]\x8B[\x48-\x4F](.)\x48\x8B\x01\xFF\x50(.))");
	// Disarming, then emptying every writer: the writers, writer size, items per block and block.
	const Signatures::RegexSignature ParallelForDisarm(R"([\x48\x4C]\x89[\x40-\x7F](.)[\x48\x4C]\x89[\x40-\x7F](.).{0,40}?[\x48\x49]\x8B[\x40-\x7F](.)[\x48\x49]\x8D[\x40-\x7F](.)\xC7\x44[\x00-\x3F](.)(....)[\x48\x4C]\x89[\x44\x4C\x54\x5C\x64\x6C\x74\x7C][\x00-\x3F](.))");
	// Emptying the chunks, from the submit base: the chunks, the chunk count, then the blocks claimed counter.
	const Signatures::RegexSignature ParallelForChunkReset(R"([\x48\x49]\x8D[\x40-\x47](.)\xBA(....)[\x48\x49]\x8B[\x00-\x3F][\x48\x49]\x85[\xC0-\xFF]\x74\x02\x89[\x00-\x3F][\x48\x49]\x83[\xC0-\xC7]\x08[\x48\x49]\x83[\xE8-\xEF]\x01\x75..{0,8}?[\x40-\x4F]?\x89[\x80-\xBF](....))");
	// The append's prologue, up to the TLS slot of the thread's parallel-for writer.
	const Signatures::RegexSignature AnimationTailAppend(R"(\x40\x53\x55\x41\x54\x48\x83\xEC\x20\x8B\x0D....\x4C\x8B\xE2\x65\x48\x8B\x04\x25\x58\x00\x00\x00\xBD(....))");
	// Then claiming a block when the writer's is full; past the last chunk, the append stores the item through null.
	const Signatures::RegexSignature ParallelForBlockClaim(R"(\x8B[\x40-\x7F](.)\x83[\xF8-\xFF](.)(?:\x0F\x86....|\x76.)[\x48\x4C]\x8B[\x40-\x7F](.)[\x48\x4D]\x85[\xC0-\xFF]\x74\x02\x89[\x00-\x3F][\x48\x4C]\x8B[\x40-\x7F](.).{0,8}?\xF0[\x40-\x4F]?\x0F\xC1[\x80-\xBF](....).{0,8}?[\x41]?\xC1[\xE8-\xEF](.)[\x41]?\x83[\xF8-\xFF](.)\x72..{0,64}?\xF0[\x48\x49\x4C\x4D]\x0F\xC1[\x40-\x7F](.))");
	// The tail's ground ray: the skeleton's ground state, then the call of the test whether it is active.
	const Signatures::RegexSignature AnimationTailGround(R"([\x48\x49]\x8B[\x88-\x8F](....)[\x48\x4D]\x85\xC9\x74.\xE8(....)\x84\xC0\x74.)");
	// That test, which only reads the ground state.
	const Signatures::RegexSignature GroundRayActiveBody(R"(\xF6\x41.\x01\x74.\x48\x83\x79.\x00\x74.\x48\x83\x79.\x00\x74.\xB0\x01\xC3)");
	// Then the update of every partial skeleton with a pose.
	const Signatures::RegexSignature AnimationTailPartialUpdate(R"([\x48\x49]\x83[\xB8-\xBF](....)\x00\x74.(?:\x0F\x28[\xC8-\xCF]|[\x48\x49]\x8B[\xC8-\xCF]){1,3}\xE8(....))");
	// The partial skeleton update ends by applying pending animation control removals: the list, then its count.
	const Signatures::RegexSignature PartialPendingRemovals(R"([\x48\x49\x4C\x4D]\x8D[\x88-\x8F](....)\xE8....[\x48\x49]\x83[\xB8-\xBF](....)\x00\x75.)");

	// A parallel-for group being armed: the context, then the job.
	const Signatures::RegexSignature ParallelForJobStore(R"(\x48\x8D\x05(....)[\x4C\x48]\x89[\x40-\x7F]\x20\x48\x89[\x40-\x7F]\x28)");
	// What the camera culling job reads of its item first.
	const Signatures::RegexSignature CameraCullItemType(R"([\x40-\x4F]?\x0F\xB6[\x02\x0A\x12\x1A\x32\x3A])");
	const Signatures::RegexSignature CameraCullItemField(R"(([\x40\x44]?)\x8B([\x42\x4A\x52\x5A\x62\x6A\x72\x7A])(.))");
	// Then the end of its range, and the start compared with it.
	const Signatures::RegexSignature CameraCullRangeEnd(R"(([\x40-\x4F]?)\x8D([\x04\x0C\x14\x1C\x24\x2C\x34\x3C])([\x00-\x3F]))");
	const Signatures::RegexSignature CameraCullRangeCompare(R"(([\x40-\x4F]?)([\x39\x3B])([\xC0-\xFF]))");
	// Where the culling builds its items: the object type tests that sort visible objects into lists,
	// then every list but the BG objects' becoming one item with its type byte.
	const Signatures::RegexSignature CameraCullObjectTypeTest(R"(\x41\x80[\xF8-\xFF](.)\x75)");
	const Signatures::RegexSignature CameraCullSingleItem(R"(\xE8(....).{0,32}?\x48\x89[\x40-\x7F]\x08\xC6[\x00-\x03\x06\x07](.))");
	// The item allocator returns the item at the writer's cursor: the block header, the cursor and the item shift.
	const Signatures::RegexSignature CameraCullItemAddress(R"([\x48\x49]\x8D[\x40-\x47](.)[\x48\x4C]\x89[\x40-\x7F](.).{0,24}?[\x48\x49]\xC1[\xE0-\xE7](.)[\x48\x49]\x03[\x40-\x7F](.))");

	// The cell group's parallel-for call (instructions in any order, then register restores).
	const Signatures::RegexSignature CullingCellParallelFor(R"([\x48\x49](?:\x81[\xC0-\xC7]|\x8D[\x80-\xBF])(....)(?:\x45\x0F\xB6[\xC8-\xCF]|\x4C\x8D\x05....|[\x48\x49]\x8B[\xD0-\xD7]){3}(?:[\x48\x4C]\x8B[\x40-\x7F]\x24.|\x48\x83\xC4.|\x41?[\x58-\x5F])*[\xE8\xE9](....))");
	// The setup group's parallel-for call.
	const Signatures::RegexSignature CullingSetupParallelFor(R"([\x48\x49]\x8D[\x80-\xBF](....)\x41\xB1\x01\x4C\x8D\x05....[\x48\x49]\x8B[\xD0-\xD7]\xE8(....))");
	// The choice between the per-item help (the group's +0xBC flag set) and the block help.
	const Signatures::RegexSignature ParallelForHelpChoice(R"(\x40\x38[\x80-\xBF]\xBC\x00\x00\x00\x74\x07\xE8(....)\xEB\x05\xE8(....))");

	// The gather is compiled without optimizations, which keeps it stable.
	const Signatures::RegexSignature CommandListGatherDriver(R"(\x4C\x89\x4C\x24\x20\x4C\x89\x44\x24\x18\x89\x54\x24\x10\x48\x89\x4C\x24\x08\x48\x83\xEC\x48\x48\x8B\x44\x24\x50)");
	// The context count, read in the gather.
	const Signatures::RegexSignature CommandListContextCount(R"(\x8B\x40(.)\x89\x44\x24.)");
	// The context size and the contexts, in the device.
	const Signatures::RegexSignature CommandListContextArray(R"([\x48\x4C]\x69[\xC0-\xFF](....)[\x48\x4C]\x8B[\x44\x4C\x54\x5C]\x24.[\x48\x4C]\x03[\x40-\x7F](.))");
	// The start of the gather of one context's blocks.
	const Signatures::RegexSignature CommandListBlocks(R"(\x48\xFF\xC0.{0,12}?\x48\x8D\x04([\x40\x80\xC0]).{0,8}?\x4C\x8D([\x04\x0C\x14\x1C\x24\x2C\x34\x3C])([\x01\x41\x81\xC1])\x8B\x44(.)(.)\x8B[\xE8-\xEF]\xC1[\xE0-\xE7](.))");
	// Then, before copying, it loads the first block from that list, through whichever register holds it.
	const Signatures::RegexSignature CommandListFirstBlock(R"([\x48-\x4F]\x8B)");
	// Its copy of every full block: the next block link and the block size.
	const Signatures::RegexSignature CommandListBlockCopy(R"(\xE8....[\x48\x4C]\x8B[\x80-\xBF](....)[\x48\x49]\x81[\xC0-\xC7](....)[\x48\x49]\x83[\xE8-\xEF]\x01\x75.)");
	// Then the entry count: the entry shift and the free slots.
	const Signatures::RegexSignature CommandListEntryCount(R"(\xC1[\xE8-\xEF](.)[\x40-\x4F]?\x2B(?:[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24|[\x40-\x7F])(.))");
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

		template<typename TFn>
		auto OptionalPart(std::string_view feature, std::string_view part, TFn&& resolve) -> std::optional<std::invoke_result_t<TFn&>> {
			try {
				return resolve();
			} catch (const std::exception& e) {
				Misc::Logger::Acquire()->Format<LogLevel::Warning>(LogCategory::Signatures, "{}: {}: {}", feature, part, e.what());
				return std::nullopt;
			}
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

		std::vector<OpcodeGuesserCandidates::PayloadWriter> FindDutyRecorderPayloadWriters(const ResolveContext& ctx, std::span<const uint8_t> text) {
			const auto base = ctx.Image().data();
			const auto offsetOf = [base](const ScanResult& m) {
				return static_cast<size_t>(static_cast<const uint8_t*>(m.begin(0)) - base);
			};

			std::vector<OpcodeGuesserCandidates::PayloadWriter> result;
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

		const void* ImportSlot(const ResolveContext& ctx, const char* dllName, const char* functionName) {
			const auto slot = Utils::Win32::Process::Current().FindImportedFunction(*ctx.Module(), dllName, functionName).first;
			ctx.Require(slot != nullptr, ResolveError::NotFound, "{}!{} is not imported", dllName, functionName);
			return slot;
		}

		// The import slot a call reads its target from: call [rip+slot], or call to a jmp [rip+slot] thunk.
		const void* ImportSlotCalledAt(const ResolveContext& ctx, const ScanResult& m, size_t group) {
			const auto target = m.ResolveAddress<const uint8_t*>(group);
			if (static_cast<const uint8_t*>(m.begin(group))[-1] != 0xE8)
				return target;
			if (!ctx.InSection(target, ".text"))
				return nullptr;

			const auto jump = target[0] == 0x48 ? target + 1 : target;
			if (jump[0] != 0xFF || jump[1] != 0x25)
				return nullptr;
			return jump + 6 + *reinterpret_cast<const int32_t*>(jump + 2);
		}

		const void* UniqueCallTarget(const ResolveContext& ctx, const RegexSignature& signature, std::string_view what) {
			return ctx.RequireInSection(ctx.Unique(signature, ctx.Text(), what).ResolveAddress<const void*>(1), ".text", what);
		}

		const void* UniqueFunctionStart(const ResolveContext& ctx, const RegexSignature& signature, std::string_view what) {
			return ctx.FunctionStartingAt(ctx.Unique(signature, ctx.Text(), what).begin(0), what).data();
		}

		bool Calls(std::span<const uint8_t> fn, const void* target) {
			for (const auto& m : RelativeCall.Lookup(fn, RegexSignature::FromNextByte)) {
				if (m.ResolveAddress<const void*>(1) == target)
					return true;
			}
			return false;
		}

		std::span<const uint8_t> CodeFrom(const ResolveContext& ctx, const void* p, size_t length) {
			const auto text = ctx.Text();
			const auto begin = static_cast<const uint8_t*>(ctx.RequireInSection(p, ".text", "code"));
			return {begin, (std::min)(length, static_cast<size_t>(text.data() + text.size() - begin))};
		}

		std::span<const uint8_t> After(std::span<const uint8_t> data, const ScanResult& m) {
			return data.subspan(static_cast<size_t>(static_cast<const uint8_t*>(m.end(0)) - data.data()));
		}

		size_t ByteAt(const ScanResult& m, size_t group) {
			return m.Get<uint8_t>(group);
		}

		// Parts within 16 KB of primary whose unwind info chains back to it; the compiler moves a function's rarely run code into such parts.
		std::vector<std::span<const uint8_t>> FunctionParts(const ResolveContext& ctx, const void* primary) {
			const auto base = reinterpret_cast<const uint8_t*>(*ctx.Module());
			const auto image = xivres::pe_image::from_loaded(base);
			const auto rva = static_cast<uint32_t>(static_cast<const uint8_t*>(primary) - base);
			const auto table = image.function_table();

			std::vector<std::span<const uint8_t>> parts;
			for (auto it = std::ranges::lower_bound(table, rva, {}, &xivres::pe_image::runtime_function::BeginAddress);
				it != table.end() && it->BeginAddress < rva + 0x4000; ++it) {
				if (image.primary_of(*it).BeginAddress == rva)
					parts.emplace_back(base + it->BeginAddress, it->EndAddress - it->BeginAddress);
			}

			if (const auto leaf = ctx.FunctionStartingAt(primary); parts.empty() && !leaf.empty())
				parts.push_back(leaf);
			ctx.Require(!parts.empty(), ResolveError::NotFound, "no function starts at {}", Signatures::Describe(primary));
			return parts;
		}

		uint32_t* FindMixRateSetup(const ResolveContext& ctx) {
			return ctx.Unique(AudioSamplingRateImmediate, ctx.Text(), "setup").begin<uint32_t>(1);
		}

		SoundVoiceRenderInfo FindVoiceRender(const ResolveContext& ctx) {
			const auto m = ctx.Unique(SoundVoiceRender, ctx.Text(), "render");
			const auto getter = ctx.RequireInSection(m.ResolveAddress<const void*>(3), ".text", "mix rate getter");
			const auto g = ctx.MatchAt(AudioSamplingRateGetter, ctx.FunctionStartingAt(getter, "mix rate getter"), "mix rate getter");
			return SoundVoiceRenderInfo{
				.State = FieldOffset(ctx, m, 1, "state offset"),
				.QueuedBuffers = FieldOffset(ctx, m, 2, "queued buffers offset"),
				.MixRate = g.ResolveAddress<uint32_t*>(1),
			};
		}

		SoundVoiceFunctions FindVoiceFunctions(const ResolveContext& ctx, const SoundVoiceRenderInfo& render) {
			SoundVoiceFunctions r;
			const auto text = ctx.Text();
			const auto init = ctx.Unique(SoundVoiceInit, text, "initialiser");
			r.Init = Address(init.begin(0));
			// The init hook leaves voices with more channels than this to the game, which refuses them.
			ctx.Require(init.Get<uint8_t>(1) == VoiceMaxChannels, ResolveError::Mismatch,
				"the initialiser takes up to {} channels instead of {}", init.Get<uint8_t>(1), VoiceMaxChannels);

			const auto vt = ctx.Unique(SoundVoiceVtable, text, "vtable");
			const auto slots = ctx.Vtable(vt.ResolveAddress<const void* const*>(1), SoundVoice::MaxVtblSlots, "vtable");
			const auto destructorBody = ctx.FunctionContaining(vt.begin(0), "destructor").data();

			// every slot is told apart by what it does, so that a reordered vtable is still understood
			struct SubmitTests {
				SoundVoice::Layout Layout;
				uint32_t StateMask{};
				uint8_t MaxQueuedBuffers{};
			};
			const auto submit = ctx.UniqueSlot(slots, "submit", [&ctx](size_t, std::span<const uint8_t> fn) -> std::optional<SubmitTests> {
				const auto m = ctx.Find(SoundVoiceSubmit, fn);
				if (!m)
					return std::nullopt;
				return SubmitTests{
					.Layout = {
						.State = FieldOffset(ctx, *m, 1, "submit state offset"),
						.QueuedBuffers = FieldOffset(ctx, *m, 3, "submit queued buffers offset"),
					},
					.StateMask = m->Get<uint32_t>(2),
					.MaxQueuedBuffers = m->Get<uint8_t>(4),
				};
			});
			r.Layout = submit.second.Layout;
			r.Layout.SubmitSlot = submit.first;

			const auto setMarker = ctx.UniqueSlot(slots, "set marker", [&ctx](size_t, std::span<const uint8_t> fn) -> std::optional<std::pair<size_t, uint32_t>> {
				const auto m = ctx.TryMatchAt(SoundVoiceSetMarker, fn);
				if (!m)
					return std::nullopt;
				return std::make_pair(static_cast<size_t>(m->Get<int32_t>(1)), m->Get<uint32_t>(2));
			});
			ctx.Require(setMarker.second.first == r.Layout.State, ResolveError::Mismatch,
				"submit state +0x{:X} != set marker state +0x{:X}", r.Layout.State, setMarker.second.first);
			r.Layout.SetMarkerSlot = setMarker.first;

			// The submit hook repeats the game's checks: a voice takes buffers in any state but these, and up to this many at once.
			const auto submittable = ~static_cast<uint32_t>(SoundVoiceState::Flushed);
			ctx.Require(submit.second.StateMask == submittable && setMarker.second.second == submittable, ResolveError::Mismatch,
				"submit and set marker test the state with 0x{:X} and 0x{:X} instead of 0x{:X}", submit.second.StateMask, setMarker.second.second, submittable);
			ctx.Require(submit.second.MaxQueuedBuffers == VoiceMaxQueuedBuffers, ResolveError::Mismatch,
				"submit queues up to {} buffers instead of {}", submit.second.MaxQueuedBuffers, VoiceMaxQueuedBuffers);

			ctx.Require(render.State == r.Layout.State && render.QueuedBuffers == r.Layout.QueuedBuffers, ResolveError::Mismatch,
				"submit state +0x{:X} and queued buffers +0x{:X} != render state +0x{:X} and queued buffers +0x{:X}",
				r.Layout.State, r.Layout.QueuedBuffers, render.State, render.QueuedBuffers);

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
		}

		SoundBufferEndInfo FindBufferEndHandler(const ResolveContext& ctx) {
			const auto m = ctx.Unique(SoundBufferEndHandler, ctx.Text(), "handler");
			return SoundBufferEndInfo{
				.Handler = Address(m.begin(0)),
				.CallbackLayout = {.EndOfData = FieldOffset(ctx, m, 1, "end of data offset")},
			};
		}

		LobbyLoginFn FindLobbyLogin(const ResolveContext& ctx) {
			const void* target = nullptr;
			for (const auto& call : ctx.All(LobbyLoginCaller, ctx.Text())) {
				const auto p = call.ResolveAddress<const void*>(1);
				ctx.Require(!target || target == p, ResolveError::Ambiguous,
					"lobby login callers call both {} and {}", Signatures::Describe(target), Signatures::Describe(p));
				target = p;
			}
			ctx.Require(target != nullptr, ResolveError::NotFound, "lobby login caller not found");
			const auto fn = ctx.FunctionStartingAt(target, "lobby login");

			// The hook relies on 8 arguments, the last picking one of two branches that each copy the three Utf8String arguments with the same function.
			ctx.Require(ctx.Find(LobbyLoginLastArgumentTest, fn.first((std::min)(fn.size(), static_cast<size_t>(0x60)))).has_value(), ResolveError::Mismatch,
				"lobby login {} does not test its last argument", Signatures::Describe(target));
			std::map<const void*, size_t> callees;
			for (const auto& call : RelativeCall.Lookup(fn))
				++callees[call.ResolveAddress<const void*>(1)];
			ctx.Require(std::ranges::any_of(callees, [](const auto& callee) { return callee.second == 6; }), ResolveError::Mismatch,
				"lobby login {} does not copy three strings on both branches", Signatures::Describe(target));

			// The hook passes its own Utf8String, which the login only copies: the copy must read the fields where the hook fills them.
			std::optional<ScanResult> copy;
			for (const auto& [callee, count] : callees) {
				if (count != 6 || !ctx.InSection(callee, ".text"))
					continue;
				const auto calleeFn = ctx.FunctionStartingAt(callee);
				if (const auto m = calleeFn.empty() ? std::nullopt : ctx.Find(Utf8StringCopy, calleeFn)) {
					ctx.Require(!copy, ResolveError::Ambiguous, "lobby login {} calls more than one string copy", Signatures::Describe(target));
					copy = m;
				}
			}
			ctx.Require(copy.has_value(), ResolveError::NotFound, "string copy of lobby login {} not found", Signatures::Describe(target));
			const auto reserved = ByteAt(*copy, 1);
			const auto used = ByteAt(*copy, 2);
			const auto length = ByteAt(*copy, 3);
			const auto empty = ByteAt(*copy, 4);
			ctx.Require(reserved == used && used == offsetof(Utf8String, BufUsed) && length == offsetof(Utf8String, StringLength) && empty == offsetof(Utf8String, IsEmpty), ResolveError::Mismatch,
				"the string copy reads used size +0x{:X}/+0x{:X}, length +0x{:X} and empty flag +0x{:X}, not where Utf8String has them", reserved, used, length, empty);
			return Address(fn.data());
		}

		LobbyErrorDialogFn FindLobbyErrorDialog(const ResolveContext& ctx) {
			const auto match = ctx.Unique(LobbyErrorDialog, ctx.Text(), "lobby error dialog");

			// The hook reads and rewrites the code as the u32 of an AtkValue, as the getter the handler calls reads it.
			const auto getter = ctx.RequireInSection(match.ResolveAddress<const void*>(1), ".text", "AtkValue u32 getter");
			const auto read = ctx.TryMatchAt(AtkValueUIntGetter, CodeFrom(ctx, getter, 0x10));
			ctx.Require(read.has_value(), ResolveError::Mismatch, "the lobby error dialog reads its code with {}, which is not the AtkValue u32 getter", Signatures::Describe(getter));
			ctx.Require(read->Get<uint8_t>(1) == static_cast<uint8_t>(AtkValueType::TypeMask) && ByteAt(*read, 2) == offsetof(AtkValue, UInt), ResolveError::Mismatch,
				"the AtkValue getter tests the type with 0x{:X} and reads +0x{:X}, not as AtkValue has them", read->Get<uint8_t>(1), ByteAt(*read, 2));
			return Address(ctx.FunctionStartingAt(&match.Get<const uint8_t>(0), "lobby error dialog").data());
		}

		ExcelLanguageGetterFn FindExcelLanguageGetter(const ResolveContext& ctx) {
			const void* found = nullptr;
			for (const auto& m : ctx.All(ExcelLanguageGetterBody, ctx.Text())) {
				// It returns where it stored the language.
				if (m.ResolveAddress<const void*>(1) != m.ResolveAddress<const void*>(2) || !ctx.InSection(m.ResolveAddress<const void*>(1), ".data"))
					continue;
				const auto fn = ctx.FunctionContaining(m.begin(0), "language getter").data();
				ctx.Require(!found || found == fn, ResolveError::Ambiguous, "language getters at {} and {}", Signatures::Describe(found), Signatures::Describe(fn));
				found = fn;
			}
			ctx.Require(found != nullptr, ResolveError::NotFound, "language getter not found");
			return Address(found);
		}

		void RequireNoEarlier(const ResolveContext& ctx, const std::optional<ScanResult>& earlier, const ScanResult& m, std::string_view what) {
			if (earlier)
				ctx.Fail(ResolveError::Ambiguous, std::format("{} at {} and {}", what, Signatures::Describe(earlier->begin(0)), Signatures::Describe(m.begin(0))));
		}

		const uint8_t* CallTarget(const ScanResult& m, size_t group) {
			return m.ResolveAddress<const uint8_t*>(group);
		}

		uint8_t* MatchStart(const ScanResult& m, size_t group) {
			return static_cast<uint8_t*>(m.begin(group));
		}

		// Where a jcc rel8 whose displacement is the group goes.
		const uint8_t* ShortJumpTarget(const ScanResult& m, size_t group) {
			return MatchStart(m, group) + 1 + m.Get<int8_t>(group);
		}

		uint16_t ShortJumpFrom(const ResolveContext& ctx, const uint8_t* instruction, const uint8_t* target) {
			const auto displacement = target - (instruction + 2);
			ctx.Require(-128 <= displacement && displacement < 128, ResolveError::Invalid, "a short jmp at {} cannot reach {}", Signatures::Describe(instruction), Signatures::Describe(target));
			return static_cast<uint16_t>(0xEB | static_cast<uint8_t>(displacement) << 8);
		}

		std::optional<ScanResult> FindInParts(const ResolveContext& ctx, const RegexSignature& signature, const std::vector<std::span<const uint8_t>>& parts) {
			for (const auto& part : parts) {
				if (auto m = ctx.Find(signature, part))
					return m;
			}
			return std::nullopt;
		}

		// The stack argument number index (from 0 for rcx) as [rbp+displacement] in a function that pushes registers and then sets rbp with lea rbp, [rsp+disp].
		std::optional<int32_t> FramePointerArgumentDisplacement(std::span<const uint8_t> fn, size_t index) {
			size_t i = 0, pushes = 0;
			while (i + 5 <= fn.size() && fn[i] == 0x48 && fn[i + 1] == 0x89 && (fn[i + 2] & 0xC7) == 0x44 && fn[i + 3] == 0x24)
				i += 5;  // saves into the home space
			for (;; ++pushes) {
				if (i < fn.size() && 0x50 <= fn[i] && fn[i] <= 0x57)
					i += 1;
				else if (i + 1 < fn.size() && (fn[i] == 0x40 || fn[i] == 0x41) && 0x50 <= fn[i + 1] && fn[i + 1] <= 0x57)
					i += 2;
				else
					break;
			}
			int32_t lea;
			if (i + 8 <= fn.size() && fn[i] == 0x48 && fn[i + 1] == 0x8D && fn[i + 2] == 0xAC && fn[i + 3] == 0x24)
				lea = *reinterpret_cast<const int32_t*>(&fn[i + 4]);
			else if (i + 5 <= fn.size() && fn[i] == 0x48 && fn[i + 1] == 0x8D && fn[i + 2] == 0x6C && fn[i + 3] == 0x24)
				lea = static_cast<int8_t>(fn[i + 4]);
			else
				return std::nullopt;
			return static_cast<int32_t>(8 + 8 * index + 8 * pushes) - lea;
		}

		DisplayLanguageNouns FindDisplayLanguageNouns(const ResolveContext& ctx, ExcelLanguageGetterFn getter) {
			DisplayLanguageNouns r{.GetLanguage = getter};

			std::optional<ScanResult> load;
			for (const auto& m : ctx.All(LocalizeAttributiveLoad, ctx.Text())) {
				const auto name = m.ResolveAddress<const char*>(2);
				if (!ctx.InSection(name, ".rdata") || std::string_view(name, 12) != std::string_view("Attributive", 12))
					continue;
				RequireNoEarlier(ctx, load, m, "Attributive is loaded");
				load = m;
			}
			ctx.Require(load.has_value(), ResolveError::NotFound, "Localize::ProcessNoun not found");
			r.Interface = ByteAt(*load, 1);

			// The handler is picked by the language right after.
			const auto language = ctx.Find(LocalizeLanguageSwitch, CodeFrom(ctx, load->end(0), 0x40));
			ctx.Require(language.has_value(), ResolveError::NotFound, "Localize::ProcessNoun does not switch over the language after loading Attributive");
			ctx.Require(ByteAt(*language, 1) == r.Interface && CallTarget(*language, 2) == reinterpret_cast<const uint8_t*>(getter), ResolveError::Mismatch,
				"Localize::ProcessNoun picks its handler by {} on +0x{:X}, not the language getter on its interface at +0x{:X}",
				Signatures::Describe(CallTarget(*language, 2)), ByteAt(*language, 1), r.Interface);
			r.LanguageCall = MatchStart(*language, 2) - 1;

			const auto fn = ctx.FunctionContaining(load->begin(0), "Localize::ProcessNoun");
			r.ProcessNoun = Address(fn.data());
			const auto parts = FunctionParts(ctx, fn.data());

			// The shadow handler maps imitate the game's: a std::map node with its colour before the nil flag, the key, then the handler as its last member.
			const auto map = ctx.Find(LocalizeMapLoad, fn.first((std::min)(fn.size(), static_cast<size_t>(0x40))));
			ctx.Require(map.has_value(), ResolveError::NotFound, "Localize::ProcessNoun does not start by loading its map");
			r.Map = ByteAt(*map, 1);
			ctx.Require(r.Map > r.Interface, ResolveError::Mismatch, "the map at +0x{:X} is not after the interface at +0x{:X}", r.Map, r.Interface);
			const auto isNil = ctx.Find(MapNodeIsNilTest, fn.first((std::min)(fn.size(), static_cast<size_t>(0x60))));
			ctx.Require(isNil.has_value(), ResolveError::NotFound, "Localize::ProcessNoun does not test a node's nil flag");
			r.NodeIsNil = ByteAt(*isNil, 1);
			const auto handler = FindInParts(ctx, LocalizeNodeHandlerCall, parts);
			ctx.Require(handler.has_value(), ResolveError::NotFound, "Localize::ProcessNoun does not call a found node's handler");
			r.NodeValue = FieldOffset(ctx, *handler, 1, "node handler offset");
			for (const auto& part : parts) {
				for (const auto& m : ctx.All(LocalizeNodeAllocation, part)) {
					if (std::cmp_equal(m.Get<int32_t>(1), r.NodeValue + sizeof(void*)))
						r.NodeSize = r.NodeValue + sizeof(void*);
				}
			}
			ctx.Require(r.NodeSize != 0, ResolveError::Mismatch, "Localize::ProcessNoun allocates no 0x{:X} byte node ending with the handler", r.NodeValue + sizeof(void*));
			const auto colour = FindInParts(ctx, LocalizeNodeColourClear, parts);
			ctx.Require(colour.has_value() && ByteAt(*colour, 1) + 1 == r.NodeIsNil && r.NodeIsNil < r.NodeValue, ResolveError::Mismatch,
				"Localize::ProcessNoun's new nodes do not clear the colour right before the nil flag at +0x{:X}", r.NodeIsNil);

			// The macro handlers that take Localize for their own language; their language argument is the sixth.
			for (const auto& m : ctx.All(NounMacroLanguageTest, ctx.Text())) {
				if (CallTarget(m, 3) != reinterpret_cast<const uint8_t*>(getter))
					continue;
				const auto loaded = m.Get<uint8_t>(4);
				const auto compared = m.Get<uint8_t>(6);
				if ((compared & 0xC7) != 0 || (compared >> 3 & 7) != (loaded >> 3 & 7))
					continue;
				uint8_t* localizeCall = nullptr;
				for (const auto& call : RelativeCall.Lookup(CodeFrom(ctx, ShortJumpTarget(m, 7), 0x40), RegexSignature::FromNextByte)) {
					if (CallTarget(call, 1) == fn.data()) {
						localizeCall = MatchStart(call, 1) - 1;
						break;
					}
				}
				if (!localizeCall)
					continue;
				const auto handlerFn = ctx.FunctionContaining(m.begin(0), "noun macro handler");
				const auto argument = FramePointerArgumentDisplacement(handlerFn, 5);
				ctx.Require(argument.has_value() && *argument == m.Get<int32_t>(5), ResolveError::Mismatch,
					"the noun macro handler {} compares [rbp+0x{:X}], not its language argument", Signatures::Describe(handlerFn.data()), m.Get<int32_t>(5));
				r.MacroHandlers.push_back({
					.Function = Address(handlerFn.data()),
					.LocalizeCall = localizeCall,
					.Interface = static_cast<size_t>(m.Match()[1].matched ? ByteAt(m, 1) : ByteAt(m, 2)),
				});
			}
			ctx.Require(!r.MacroHandlers.empty(), ResolveError::NotFound, "no noun macro handler compares the language before calling Localize::ProcessNoun");
			return r;
		}

		DisplayLanguageWrap FindDisplayLanguageWrap(const ResolveContext& ctx, ExcelLanguageGetterFn getter) {
			DisplayLanguageWrap r{.GetLanguage = getter};
			const auto getterAddress = reinterpret_cast<const uint8_t*>(getter);

			std::optional<ScanResult> cjk;
			for (const auto& m : ctx.All(TextWrapCjkFlag, ctx.Text())) {
				if (CallTarget(m, 2) != getterAddress)
					continue;
				RequireNoEarlier(ctx, cjk, m, "CJK flags set");
				cjk = m;
			}
			ctx.Require(cjk.has_value(), ResolveError::NotFound, "the wrapper's CJK flag not found");
			r.Interface = ByteAt(*cjk, 1);
			r.LanguageCall = MatchStart(*cjk, 2) - 1;
			r.CjkFlag = FieldOffset(ctx, *cjk, 3, "CJK flag offset");
			const auto wrap = ctx.FunctionContaining(cjk->begin(0), "wrapper");
			r.Wrap = Address(wrap.data());
			const auto fonts = ctx.Find(TextWrapFontsTest, wrap.first((std::min)(wrap.size(), static_cast<size_t>(0x20))));
			ctx.Require(fonts.has_value(), ResolveError::Mismatch, "the wrapper {} does not test its set-up first", Signatures::Describe(wrap.data()));
			r.WrapperFonts = ByteAt(*fonts, 1);

			std::optional<ScanResult> korean;
			for (const auto& m : ctx.All(TextWrapKoreanFlag, ctx.Text())) {
				if (CallTarget(m, 2) != getterAddress || ByteAt(m, 1) != r.Interface)
					continue;
				RequireNoEarlier(ctx, korean, m, "Korean flags set");
				korean = m;
			}
			ctx.Require(korean.has_value(), ResolveError::NotFound, "the wrapper's Korean flag not found");
			r.KoreanFlag = FieldOffset(ctx, *korean, 3, "Korean flag offset");
			ctx.Require(r.KoreanFlag == r.CjkFlag + 1, ResolveError::Mismatch, "the Korean flag +0x{:X} does not follow the CJK flag +0x{:X}", r.KoreanFlag, r.CjkFlag);
			const auto setup = ctx.FunctionContaining(korean->begin(0), "wrapper set-up");
			const auto store = ctx.Find(TextWrapSetupStore, setup.first((std::min)(setup.size(), static_cast<size_t>(0x10))));
			ctx.Require(store.has_value() && ByteAt(*store, 1) == r.WrapperFonts, ResolveError::Mismatch,
				"the wrapper set-up {} does not store its argument at +0x{:X} first", Signatures::Describe(setup.data()), r.WrapperFonts);

			// Only AtkModule's wrapper is set up, which tells it apart from the others.
			std::optional<ScanResult> call;
			for (const auto& m : ctx.All(TextWrapSetupCall, ctx.Text())) {
				if (CallTarget(m, 5) != setup.data() || (m.Get<uint8_t>(1) & 7) != (m.Get<uint8_t>(3) & 7))
					continue;
				RequireNoEarlier(ctx, call, m, "the wrapper set-up is called");
				call = m;
			}
			ctx.Require(call.has_value(), ResolveError::NotFound, "the call of the wrapper set-up not found");
			r.ModuleWrapper = static_cast<size_t>(ctx.InRange<int32_t>(call->Get<int32_t>(2), 1, 0x100000, "wrapper offset"));
			r.ModuleFonts = static_cast<size_t>(ctx.InRange<int32_t>(call->Get<int32_t>(4), 1, 0x100000, "set-up argument offset"));
			return r;
		}

		LanguageBranchPatch FindTalkLines(const ResolveContext& ctx) {
			const auto m = ctx.Unique(TalkLanguageBranch, ctx.Text(), "Talk language branch");
			const auto threeRegister = ByteAt(m, 1) - 0xB8;
			const auto languageLoad = ByteAt(m, 2);
			const auto instruction = MatchStart(m, 3);
			ctx.Require((languageLoad >> 3 & 7) == (instruction[1] & 7), ResolveError::Mismatch, "the Talk language branch tests another register than the language it loads");

			const auto four = ShortJumpTarget(m, 4);
			ctx.Require(ShortJumpTarget(m, 5) == four && ShortJumpTarget(m, 6) == four, ResolveError::Mismatch, "the Talk language branch does not take en, de and fr to one side");
			const auto three = static_cast<const uint8_t*>(m.end(0));

			const auto fourLines = ctx.TryMatchAt(TalkFourLines, CodeFrom(ctx, four, 0x40));
			ctx.Require(fourLines.has_value(), ResolveError::Mismatch, "the en/de/fr side at {} does not set 4 lines, size 14 and spacing 20", Signatures::Describe(four));
			const auto threeLines = ctx.Find(TalkThreeLines, CodeFrom(ctx, three, 0x30));
			ctx.Require(threeLines.has_value() && (ByteAt(*threeLines, 1) >> 3 & 7) == threeRegister, ResolveError::Mismatch,
				"the other side at {} does not set the 3 lines", Signatures::Describe(three));
			ctx.Require(threeLines->Get<int32_t>(2) == fourLines->Get<int32_t>(1) && CallTarget(*threeLines, 3) == CallTarget(*fourLines, 2), ResolveError::Mismatch,
				"the sides set the line count at +0x{:X} and +0x{:X}, or the font size through {} and {}",
				threeLines->Get<int32_t>(2), fourLines->Get<int32_t>(1), Signatures::Describe(CallTarget(*threeLines, 3)), Signatures::Describe(CallTarget(*fourLines, 2)));

			return {
				.Instruction = instruction,
				.Original = *reinterpret_cast<const uint16_t*>(instruction),
				.JumpToFirst = ShortJumpFrom(ctx, instruction, four),
				.JumpToSecond = ShortJumpFrom(ctx, instruction, three),
			};
		}

		LanguageBranchPatch FindSubtitleScale(const ResolveContext& ctx) {
			const auto m = ctx.Unique(SubtitleLanguageScale, ctx.Text(), "TalkSubtitle scale");
			const auto enlarged = MatchStart(m, 8);
			const auto normal = static_cast<const uint8_t*>(m.end(8));
			for (const auto group : {4, 5, 6})
				ctx.Require(ShortJumpTarget(m, group) == enlarged, ResolveError::Mismatch, "the TalkSubtitle language test does not take ja, chs, cht and tc to one side");
			ctx.Require(ShortJumpTarget(m, 7) == normal, ResolveError::Mismatch, "the TalkSubtitle language test does not skip the other scale");

			const auto scale = [&ctx, &m](size_t group) {
				return *static_cast<const float*>(ctx.RequireInSection(m.ResolveAddress<const float*>(group), ".rdata", "scale"));
			};
			ctx.Require(scale(2) == 1.f && scale(9) == 1.5f, ResolveError::Mismatch, "the TalkSubtitle scales are {} and {} instead of 1 and 1.5", scale(2), scale(9));

			const auto instruction = MatchStart(m, 3);
			return {
				.Instruction = instruction,
				.Original = *reinterpret_cast<const uint16_t*>(instruction),
				.JumpToFirst = ShortJumpFrom(ctx, instruction, enlarged),
				.JumpToSecond = ShortJumpFrom(ctx, instruction, normal),
			};
		}

		ProcessShellCommandMessageFn FindProcessShellCommandMessage(const ResolveContext& ctx) {
			const auto m = ctx.Unique(ShellCommandMessageLimit, ctx.Text(), "shell command message limit");
			const auto fn = ctx.FunctionContaining(m.begin(0), "ProcessShellCommandMessage");
			ctx.Require(fn.size() <= 0x100, ResolveError::Mismatch, "ProcessShellCommandMessage {} is 0x{:X} bytes long", Signatures::Describe(fn.data()), fn.size());

			// The hook hands callers a string of its own, which works only while every caller copies the result right away.
			const auto text = ctx.Text();
			size_t calls = 0;
			for (size_t i = 0; i + 8 <= text.size(); i++) {
				if (text[i] != 0xE8 || text.data() + i + 5 + *reinterpret_cast<const int32_t*>(&text[i + 1]) != fn.data())
					continue;
				ctx.Require(text[i + 5] == 0x48 && text[i + 6] == 0x8B && text[i + 7] == 0xD0, ResolveError::Mismatch,
					"the caller at {} does not pass the result on right away", Signatures::Describe(&text[i]));
				calls++;
			}
			ctx.Require(calls >= 12, ResolveError::Mismatch, "ProcessShellCommandMessage has {} callers instead of every chat channel's", calls);
			return Address(fn.data());
		}
	}

	std::string to_string(const OutgoingTextFunctions& value) {
		return std::format("ProcessShellCommandMessage {}", Signatures::Describe(value.ProcessShellCommandMessage));
	}

	const Signatures::ComplexSignature<OutgoingTextFunctions> OutgoingText("OutgoingText", [](ResolveContext& ctx) {
		return OutgoingTextFunctions{.ProcessShellCommandMessage = FindProcessShellCommandMessage(ctx)};
	});

	std::string to_string(const SqpackLookupHooksFunctions& value) {
		return std::format("index lookups {}", Signatures::Describe(value.IndexLookups));
	}

	std::string to_string(const TextHooksFunctions& value) {
		return std::format("cut scene language getter {}, string indirection resolvers {}",
			Signatures::Describe(value.CutSceneLanguageGetter),
			Signatures::Describe(value.StringIndirectionResolvers));
	}

	std::string to_string(const AudioResamplerFunctions& value) {
		return std::format("mix rate setup {}, render: {}, voice: {}, buffer end: {}",
			Signatures::Describe(value.MixRateSetup),
			Signatures::Describe(value.Render),
			Signatures::Describe(value.Voice),
			Signatures::Describe(value.BufferEnd));
	}

	std::string to_string(const MainThreadTimingHandlerFunctions& value) {
		return std::format("single message loop {}", Signatures::Describe(value.SingleMessageLoop));
	}

	std::string to_string(const LoginSessionsFunctions& value) {
		return std::format("lobby login {}, lobby error dialog {}",
			Signatures::Describe(value.LobbyLogin),
			Signatures::Describe(value.LobbyErrorDialog));
	}

	std::string to_string(const AltCodecMusicSupportFunctions& value) {
		return std::format("attribute {}, reset {}, open {}, set up {}, process {}, peek 0x{:X} bytes",
			Signatures::Describe(value.Attribute),
			Signatures::Describe(value.Reset),
			Signatures::Describe(value.Open),
			Signatures::Describe(value.SetUpDecoder),
			Signatures::Describe(value.Process),
			value.PeekBytes);
	}

	std::string to_string(const OpcodeGuesserCandidates& value) {
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

	std::string to_string(const ImeModeIndicatorFunctions& value) {
		return std::format("IME mode getter {}, input context {}",
			Signatures::Describe(value.GetImeMode),
			Signatures::Describe(value.InputContext));
	}

	std::string to_string(const FontReplacementFunctions& value) {
		return std::format("Present call {}, Present {}",
			Signatures::Describe(value.PresentCall),
			Signatures::Describe(value.Present));
	}

	std::string to_string(const SubpixelTextFunctions& value) {
		return std::format("SetDrawState {}, texture width +0x{:X}, rendering to texture +0x{:X}",
			Signatures::Describe(value.SetDrawState), value.TextureWidth, value.RenderingToTexture);
	}

	std::string to_string(const DisplayLanguageNouns::MacroHandler& value) {
		return std::format("{} (Localize call {}, interface +0x{:X})", Signatures::Describe(value.Function), Signatures::Describe(value.LocalizeCall), value.Interface);
	}

	std::string to_string(const DisplayLanguageNouns& value) {
		return std::format("getter {}, ProcessNoun {}, language call {}, interface +0x{:X}, map +0x{:X}, node nil +0x{:X}, value +0x{:X}, size 0x{:X}, macro handlers {}",
			Signatures::Describe(value.GetLanguage), Signatures::Describe(value.ProcessNoun), Signatures::Describe(value.LanguageCall),
			value.Interface, value.Map, value.NodeIsNil, value.NodeValue, value.NodeSize, Signatures::Describe(value.MacroHandlers));
	}

	std::string to_string(const DisplayLanguageWrap& value) {
		return std::format("getter {}, wrap {}, language call {}, interface +0x{:X}, CJK flag +0x{:X}, Korean flag +0x{:X}, module wrapper +0x{:X}, module fonts +0x{:X}, wrapper fonts +0x{:X}",
			Signatures::Describe(value.GetLanguage), Signatures::Describe(value.Wrap), Signatures::Describe(value.LanguageCall),
			value.Interface, value.CjkFlag, value.KoreanFlag, value.ModuleWrapper, value.ModuleFonts, value.WrapperFonts);
	}

	std::string to_string(const LanguageBranchPatch& value) {
		return std::format("{} {:04X} -> {:04X} / {:04X}", Signatures::Describe(value.Instruction), value.Original, value.JumpToFirst, value.JumpToSecond);
	}

	std::string to_string(const DisplayLanguageFunctions& value) {
		return std::format("nouns: {}; wrap: {}; talk lines: {}; subtitle scale: {}",
			Signatures::Describe(value.Nouns), Signatures::Describe(value.Wrap), Signatures::Describe(value.TalkLines), Signatures::Describe(value.SubtitleScale));
	}

	const Signatures::ComplexSignature<DisplayLanguageFunctions> DisplayLanguage("DisplayLanguage", [](ResolveContext& ctx) {
		const auto getter = OptionalPart("DisplayLanguage", "language getter", [&ctx] { return FindExcelLanguageGetter(ctx); });
		DisplayLanguageFunctions r;
		if (getter) {
			r.Nouns = OptionalPart("DisplayLanguage", "nouns", [&] { return FindDisplayLanguageNouns(ctx, *getter); });
			r.Wrap = OptionalPart("DisplayLanguage", "wrap", [&] { return FindDisplayLanguageWrap(ctx, *getter); });
		}
		r.TalkLines = OptionalPart("DisplayLanguage", "talk lines", [&ctx] { return FindTalkLines(ctx); });
		r.SubtitleScale = OptionalPart("DisplayLanguage", "subtitle scale", [&ctx] { return FindSubtitleScale(ctx); });
		ctx.Require(r.Nouns || r.Wrap || r.TalkLines || r.SubtitleScale, ResolveError::NotFound, "none of the language rules found");
		return r;
	});

	const Signatures::ComplexSignature<SqpackLookupHooksFunctions> SqpackLookupHooks("SqpackLookupHooks", [](ResolveContext& ctx) {
		// Each SqPackManager keeps its index kind's lookup in a member; LoadSqPack sets it to either function, then compares it against one of them.
		const auto count = ctx.Unique(SqPackIndexEntryCount, ctx.Text(), "index entry count");
		const auto member = FieldOffset(ctx, count, 1, "lookup member");
		const auto loadSqPack = ctx.FunctionContaining(count.begin(0), "LoadSqPack");

		// The lookup function stored into a member.
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
		return SqpackLookupHooksFunctions{.IndexLookups = std::move(result)};
	});

	const Signatures::ComplexSignature<TextHooksFunctions> TextHooks("TextHooks", [](ResolveContext& ctx) {
		return TextHooksFunctions{
			.CutSceneLanguageGetter = OptionalPart("TextHooks", "cut scene language getter", [&ctx]() -> CutSceneLanguageGetterFn {
				return Address(ctx.First(CutSceneLanguageGetter, ctx.Text(), "call").ResolveAddress<const void*>(1));
			}),
			.StringIndirectionResolvers = AllMatchStarts<StringIndirectionResolverFn>(ctx, StringIndirectionResolver),
		};
	});

	const Signatures::ComplexSignature<Oodle::OodleNetworkFunctions> OodleNetwork("OodleNetwork", [](ResolveContext& ctx) {
		Oodle::OodleNetworkFunctions r;
		const auto text = ctx.Text();

		const auto init = ctx.First(OodleInit, text, "OodleInit");
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

	const Signatures::ComplexSignature<OpcodeGuesserCandidates> OpcodeGuesser("OpcodeGuesser", [](ResolveContext& ctx) {
		OpcodeGuesserCandidates r;
		const auto text = ctx.Text();

		r.C2S_ActionRequest = FindActionRequest(text);
		r.C2S_ActionRequestGroundTargeted = FindActionRequestGroundTargeted(ctx, text);

		// In order: (ActionEffect01,) 08, 16, 24, 32 with sizes growing by one step, then ActorCast, ActorControl, ActorControlTarget, ActorControlSelf.
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

	const Signatures::ComplexSignature<AudioResamplerFunctions> AudioResampler("AudioResampler", [](ResolveContext& ctx) {
		// The voices are resampled to the mix rate, so nothing works without it.
		AudioResamplerFunctions r{
			.MixRateSetup = FindMixRateSetup(ctx),
			.Render = FindVoiceRender(ctx),
		};
		r.Voice = OptionalPart("AudioResampler", "voice functions", [&] { return FindVoiceFunctions(ctx, r.Render); });
		r.BufferEnd = OptionalPart("AudioResampler", "buffer end handler", [&] { return FindBufferEndHandler(ctx); });
		return r;
	});

	const Signatures::ComplexSignature<AltCodecMusicSupportFunctions> AltCodecMusicSupport("AltCodecMusicSupport", [](ResolveContext& ctx) {
		AltCodecMusicSupportFunctions r;
		const auto text = ctx.Text();

		const auto attributeMatch = ctx.First(MssAsiAttribute, text, "attribute");
		const Address attribute = attributeMatch.begin(0);
		r.Attribute = attribute;

		for (const auto& m : MssAsiResetPair.Lookup(text, RegexSignature::FromNextByte)) {
			if (m.ResolveAddress<const void*>(2) == attribute.Get()) {
				r.Reset = Address(m.ResolveAddress<const void*>(1));
				break;
			}
		}
		ctx.Require(r.Reset != nullptr, ResolveError::NotFound, "reset not found");

		const auto open = ctx.First(MssAsiOpen, text, "open");
		r.Open = Address(open.ResolveAddress<const void*>(2));
		const auto setUp = ctx.FunctionContaining(ctx.First(MssAsiSetUpDecoder, text, "decoder set-up").begin(0), "decoder set-up");
		const auto process = ctx.FunctionContaining(ctx.First(MssAsiProcess, text, "process").begin(0), "process");
		r.SetUpDecoder = Address(setUp.data());
		r.Process = Address(process.data());

		// The decoders use AsiStream and AsiStreamUserFfxiv as they are declared; refuse unless the stream code reads them there (past shrink-wrapped prologues).
		const auto allocation = ctx.First(MssAsiOpenAllocation, CodeFrom(ctx, reinterpret_cast<const void*>(r.Open), 0x40), "stream allocation");
		ctx.Require(std::cmp_equal(allocation.Get<int32_t>(1), sizeof(AsiStream)), ResolveError::Mismatch,
			"the open allocates 0x{:X} bytes for a stream instead of 0x{:X}", allocation.Get<int32_t>(1), sizeof(AsiStream));
		ctx.Require(ByteAt(attributeMatch, 1) == offsetof(AsiStream, Channels), ResolveError::Mismatch,
			"the attribute query reads the channels at +0x{:X} instead of +0x{:X}", ByteAt(attributeMatch, 1), offsetof(AsiStream, Channels));

		std::optional<ScanResult> fetchFound = ctx.Find(MssAsiProcessFetch, CodeFrom(ctx, process.data(), 0x200));
		for (const auto& call : RelativeCall.Lookup(CodeFrom(ctx, process.data(), 0x200), RegexSignature::FromNextByte)) {
			if (const auto callee = call.ResolveAddress<const void*>(1); !fetchFound && ctx.InSection(callee, ".text") && !ctx.FunctionStartingAt(callee).empty())
				fetchFound = ctx.Find(MssAsiProcessFetch, CodeFrom(ctx, callee, 0x200));
		}
		ctx.Require(fetchFound.has_value(), ResolveError::NotFound, "process fetch not found");
		const auto& fetch = *fetchFound;
		const auto pending = ByteAt(fetch, 1);
		const auto user = ByteAt(fetch, 2);
		const auto callback = ByteAt(fetch, 3);
		const auto fetched = ByteAt(fetch, 4);
		ctx.Require(pending == offsetof(AsiStream, PendingFetchOffset) && user == offsetof(AsiStream, User)
			&& callback == offsetof(AsiStream, FetchCallback) && fetched == offsetof(AsiStream, FetchedBytes), ResolveError::Mismatch,
			"the process fetches with pending offset +0x{:X}, user +0x{:X}, callback +0x{:X} and fetched bytes +0x{:X}, not as AsiStream has them", pending, user, callback, fetched);

		const auto peek = ctx.First(MssAsiSetUpFetch, CodeFrom(ctx, setUp.data(), 0x200), "decoder set-up fetch");
		ctx.Require(ByteAt(peek, 1) == offsetof(AsiStream, SourcePtr), ResolveError::Mismatch,
			"the decoder set-up stores the source at +0x{:X} instead of +0x{:X}", ByteAt(peek, 1), offsetof(AsiStream, SourcePtr));
		ctx.Require(ByteAt(peek, 2) == user && ByteAt(peek, 4) == callback, ResolveError::Mismatch,
			"the decoder set-up fetches with user +0x{:X} and callback +0x{:X}, the process with +0x{:X} and +0x{:X}", ByteAt(peek, 2), ByteAt(peek, 4), user, callback);
		r.PeekBytes = ctx.InRange<uint32_t>(peek.Get<uint32_t>(3), 4, 0x10001, "decoder set-up fetch size");

		// The game's fetch callback, which keeps the stream flags the take-over changes.
		const auto fetchCallback = ctx.RequireInSection(open.ResolveAddress<const void*>(1), ".text", "fetch callback");
		ctx.Require(!ctx.FunctionStartingAt(fetchCallback).empty(), ResolveError::Mismatch, "the fetch callback {} is not where a function starts", Signatures::Describe(fetchCallback));
		const auto fetchCallbackFn = CodeFrom(ctx, fetchCallback, 0x100);
		const auto flags = ctx.Find(FfxivAsiFetchFlags, fetchCallbackFn);
		ctx.Require(flags.has_value(), ResolveError::NotFound, "the fetch callback {} does not test its flags", Signatures::Describe(fetchCallback));
		const auto flagsOffset = flags->Get<uint8_t>(1);
		const auto endOfStream = flags->Get<uint8_t>(2);
		const uint8_t setsEndOfStream[]{0x80, 0x49, flagsOffset, endOfStream};  // or byte ptr [rcx+flags], end of stream
		ctx.Require(!std::ranges::search(fetchCallbackFn, setsEndOfStream).empty(), ResolveError::Mismatch, "the fetch callback does not set the end of stream flag it tests");
		ctx.Require(flagsOffset == offsetof(AsiStreamUserFfxiv, Flags) && endOfStream == static_cast<uint8_t>(AsiStreamFlag::EndOfStream)
			&& flags->Get<uint8_t>(3) == static_cast<uint8_t>(AsiStreamFlag::NeedsReinit), ResolveError::Mismatch,
			"the fetch callback keeps its flags at +0x{:X} (end of stream 0x{:X}, reinitialise 0x{:X}), not as AsiStreamUserFfxiv has them", flagsOffset, endOfStream, flags->Get<uint8_t>(3));
		return r;
	});

	const Signatures::ComplexSignature<MainThreadTimingHandlerFunctions> MainThreadTimingHandler("MainThreadTimingHandler", [](ResolveContext& ctx) {
		return MainThreadTimingHandlerFunctions{
			.SingleMessageLoop = Address(ctx.First(MessageLoop, ctx.Image(), "message loop").ResolveAddress<const void*>(1)),
		};
	});

	const Signatures::ComplexSignature<LoginSessionsFunctions> LoginSessions("LoginSessions", [](ResolveContext& ctx) {
		return LoginSessionsFunctions{
			.LobbyLogin = OptionalPart("LoginSessions", "lobby login", [&ctx] { return FindLobbyLogin(ctx); }),
			.LobbyErrorDialog = OptionalPart("LoginSessions", "lobby error dialog", [&ctx] { return FindLobbyErrorDialog(ctx); }),
		};
	});

	const Signatures::ComplexSignature<ImeModeIndicatorFunctions> ImeModeIndicator("ImeModeIndicator", [](ResolveContext& ctx) {
		const auto openStatus = ImportSlot(ctx, "imm32.dll", "ImmGetOpenStatus");
		const auto conversionStatus = ImportSlot(ctx, "imm32.dll", "ImmGetConversionStatus");

		std::optional<ImeModeIndicatorFunctions> found;
		for (const auto& m : ctx.All(ImeStatusQueries, ctx.Text())) {
			const auto inputContext = m.ResolveAddress<void* const*>(1);
			if (inputContext != m.ResolveAddress<void* const*>(3)
				|| ImportSlotCalledAt(ctx, m, 2) != openStatus
				|| ImportSlotCalledAt(ctx, m, 4) != conversionStatus)
				continue;

			// the queries are the first thing it does, right after reserving its stack
			const auto fn = ctx.FunctionContaining(m.begin(0), "IME mode getter");
			if (static_cast<const uint8_t*>(m.begin(0)) - fn.data() > 0x10 || !ctx.Find(ImeNoConversionTest, fn))
				continue;

			ctx.Require(!found, ResolveError::Ambiguous, "IME mode getters at {} and {}",
				Signatures::Describe(found ? found->GetImeMode : nullptr), Signatures::Describe(fn.data()));
			found = ImeModeIndicatorFunctions{
				.GetImeMode = Address(fn.data()),
				.InputContext = static_cast<void* const*>(ctx.RequireInSection(inputContext, ".data", "input context")),
			};
		}
		ctx.Require(found.has_value(), ResolveError::NotFound, "IME mode getter not found");
		return *found;
	});

	const Signatures::ComplexSignature<FontReplacementFunctions> FontReplacement("FontReplacement", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto call = ctx.Unique(SwapChainPresentCall, text, "Present call");
		const auto callInstruction = static_cast<const uint8_t*>(call.begin(1)) - 1;

		// Only PostTick walks the notifiers right before calling Present.
		const auto before = (std::min)(static_cast<size_t>(0x100), static_cast<size_t>(callInstruction - text.data()));
		ctx.Require(ctx.Find(PrePresentNotifierWalk, std::span(callInstruction - before, before)).has_value(), ResolveError::Mismatch,
			"the Present call at {} does not follow the pre-present notifier walk", Signatures::Describe(callInstruction));

		const auto present = call.ResolveAddress<const void*>(1);
		ctx.Require(!ctx.FunctionStartingAt(present).empty(), ResolveError::Mismatch,
			"Present {} is not where a function starts", Signatures::Describe(present));
		return FontReplacementFunctions{
			.PresentCall = Address(callInstruction),
			.Present = Address(present),
		};
	});

	const Signatures::ComplexSignature<SubpixelTextFunctions> SubpixelText("SubpixelText", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto body = ctx.Unique(AtkServerSetDrawStateBody, text, "SetDrawState");
		const auto renderTexture = ctx.Unique(AtkServerBeginRenderTexture, text, "render texture begin");
		return SubpixelTextFunctions{
			.SetDrawState = Address(ctx.FunctionContaining(body.begin(0), "SetDrawState").data()),
			.TextureWidth = body.Get<uint8_t>(1),
			.RenderingToTexture = static_cast<size_t>(ctx.InRange<int32_t>(renderTexture.Get<int32_t>(1), 0x100, 0x10000, "rendering to texture flag")),
		};
	});
}

namespace XivAlexander::Game::Resolved::CrowdFix {
	namespace {
		// Per-item claims when the group's +0xBC flag is set, blocks of items otherwise.
		std::pair<ParallelForHelpFn, ParallelForHelpFn> ParallelForHelps(const ResolveContext& ctx, const void* forkJoin, std::string_view what) {
			const auto m = ctx.Unique(ParallelForHelpChoice, ctx.FunctionStartingAt(forkJoin, what), what);
			return {
				Address(ctx.RequireInSection(m.ResolveAddress<const void*>(1), ".text", what)),
				Address(ctx.RequireInSection(m.ResolveAddress<const void*>(2), ".text", what)),
			};
		}

		struct JobListKickCallers {
			void* const* TaskManager{};
			const void* Kick{};
		};

		// The kick that most JobListKickCaller matches call.
		JobListKickCallers FindJobListKick(const ResolveContext& ctx) {
			std::map<std::pair<const void*, const void*>, size_t> counts;
			for (const auto& m : ctx.All(JobListKickCaller, ctx.Text()))
				++counts[{m.ResolveAddress<const void*>(1), m.ResolveAddress<const void*>(2)}];
			ctx.Require(!counts.empty(), ResolveError::NotFound, "job list kick callers not found");

			const auto top = std::ranges::max_element(counts, {}, [](const auto& item) { return item.second; });
			size_t runnerUp = 0;
			for (const auto& [key, count] : counts) {
				if (key != top->first)
					runnerUp = (std::max)(runnerUp, count);
			}
			ctx.Require(top->second >= 3 && top->second > runnerUp * 2, ResolveError::Ambiguous,
				"job list kick callers disagree: {} call one function and {} another", top->second, runnerUp);

			const auto [taskManager, kick] = top->first;
			ctx.Require(!ctx.FunctionStartingAt(kick).empty(), ResolveError::Invalid, "no function starts at job list kick {}", Signatures::Describe(kick));
			return {static_cast<void* const*>(taskManager), kick};
		}

		void* const* FindCullingManager(const ResolveContext& ctx) {
			const auto getter = UniqueCallTarget(ctx, CullingManagerGetterCall, "culling manager getter call");
			const auto load = ctx.MatchAt(CullingManagerGetter, ctx.FunctionStartingAt(getter, "culling manager getter"), "culling manager getter");
			return static_cast<void* const*>(ctx.RequireInSection(load.ResolveAddress<const void*>(1), ".data", "culling manager"));
		}

		SkeletonPoseSyncWalkFn FindSkeletonPoseSyncWalk(const ResolveContext& ctx) {
			return Address(UniqueCallTarget(ctx, SkeletonPoseSyncWalkCall, "pose sync walk call"));
		}

		PartialSkeletonLayout FindPartialSkeletonLayout(const ResolveContext& ctx) {
			auto code = CodeFrom(ctx, reinterpret_cast<const void*>(FindSkeletonPoseSyncWalk(ctx)), 0x180);
			const auto count = ctx.First(PoseSyncPartialCount, code, "partial skeleton count");
			code = After(code, count);
			const auto array = ctx.First(PoseSyncPartialArray, code, "partial skeleton array");
			code = After(code, array);
			const auto pose = ctx.First(PoseSyncPartialPose, code, "partial skeleton pose");
			code = After(code, pose);
			const auto stride = ctx.First(PoseSyncPartialStride, code, "partial skeleton stride");

			return PartialSkeletonLayout{
				.Count = ByteAt(count, 1),
				.Array = ByteAt(array, 1),
				.Stride = FieldOffset(ctx, stride, 1, "partial skeleton stride"),
				.Pose = FieldOffset(ctx, pose, 1, "partial skeleton pose"),
			};
		}

		struct JobPoolWakeAll {
			const void* WakeAll{};
			JobPoolLayout Layout;
		};

		JobPoolWakeAll FindJobPoolWakeAll(const ResolveContext& ctx) {
			const auto call = ctx.Unique(JobPoolWakeAllCall, ctx.Text(), "job pool wake-all call");
			const auto wakeAll = ctx.RequireInSection(call.ResolveAddress<const void*>(2), ".text", "job pool wake-all");
			const auto body = ctx.Find(JobPoolWakeAllBody, ctx.FunctionStartingAt(wakeAll, "job pool wake-all"));
			ctx.Require(body.has_value(), ResolveError::Mismatch, "job pool wake-all {} does not match", Signatures::Describe(wakeAll));
			ctx.Require(ImportSlotCalledAt(ctx, *body, 7) == ImportSlot(ctx, "kernel32.dll", "SetEvent"), ResolveError::Mismatch,
				"job pool wake-all {} does not call SetEvent", Signatures::Describe(wakeAll));

			return {
				.WakeAll = wakeAll,
				.Layout = {
					.TaskManagerJobPool = ByteAt(call, 1),
					.Threads = ByteAt(*body, 2),
					.ThreadCount = ByteAt(*body, 1),
					.ThreadSkip = ByteAt(*body, 3),
					.ThreadWakeCount = ByteAt(*body, 4),
					.ThreadEvent = ByteAt(*body, 6),
					.ThreadWakeLimit = ctx.InRange<int32_t>(body->Get<int8_t>(5), 1, 0x40, "thread wake limit"),
				},
			};
		}

		ptrdiff_t SignedByteAt(const ScanResult& m, size_t group) {
			return m.Get<int8_t>(group);
		}

		// First part's start to last part's end: shrink-wrapped register saves and cold code get parts of their own, which patterns may span.
		std::span<const uint8_t> WholeFunctionContaining(const ResolveContext& ctx, const void* p, std::string_view what) {
			const auto base = reinterpret_cast<const uint8_t*>(*ctx.Module());
			const auto image = xivres::pe_image::from_loaded(base);
			const auto rva = static_cast<uint32_t>(static_cast<const uint8_t*>(p) - base);
			const auto table = image.function_table();
			auto it = std::ranges::upper_bound(table, rva, {}, &xivres::pe_image::runtime_function::BeginAddress);
			ctx.Require(it != table.begin() && rva < (it - 1)->EndAddress, ResolveError::NotFound, "{}: no function contains {}", what, Signatures::Describe(p));

			const auto parts = FunctionParts(ctx, base + image.primary_of(*(it - 1)).BeginAddress);
			const auto begin = std::ranges::min(parts, {}, [](const auto& part) { return part.data(); }).data();
			const auto& last = std::ranges::max(parts, {}, [](const auto& part) { return part.data(); });
			return {begin, last.data() + last.size()};
		}

		// The camera culling job's item range fields: the two it adds for the end, and which of them it compares with that end.
		std::optional<std::pair<size_t, size_t>> FindCullItemRange(const ResolveContext& ctx, const void* job) {
			std::map<uint8_t, size_t> fields;  // register -> field it was loaded from
			for (const auto& m : ctx.All(CameraCullItemField, CodeFrom(ctx, job, 0x60))) {
				const auto reg = static_cast<uint8_t>(((m.Get<uint8_t>(2) >> 3) & 7) | (m.Match()[1].matched && (m.Get<uint8_t>(1) & 4) ? 8 : 0));
				fields.try_emplace(reg, m.Get<uint8_t>(3));
			}

			const auto code = CodeFrom(ctx, job, 0x200);
			for (const auto& lea : ctx.All(CameraCullRangeEnd, code)) {
				const auto rex = lea.Match()[1].matched ? lea.Get<uint8_t>(1) : uint8_t{};
				const auto sib = lea.Get<uint8_t>(3);
				const auto end = static_cast<uint8_t>(((lea.Get<uint8_t>(2) >> 3) & 7) | (rex & 4 ? 8 : 0));
				const auto base = static_cast<uint8_t>((sib & 7) | (rex & 1 ? 8 : 0));
				const auto index = static_cast<uint8_t>(((sib >> 3) & 7) | (rex & 2 ? 8 : 0));
				if ((sib & 7) == 5 || base == index || !fields.contains(base) || !fields.contains(index))
					continue;

				for (const auto& cmp : ctx.All(CameraCullRangeCompare, After(code, lea).first((std::min)(After(code, lea).size(), size_t{0x20})))) {
					const auto cmpRex = cmp.Match()[1].matched ? cmp.Get<uint8_t>(1) : uint8_t{};
					const auto modrm = cmp.Get<uint8_t>(3);
					auto start = static_cast<uint8_t>(((modrm >> 3) & 7) | (cmpRex & 4 ? 8 : 0));
					auto other = static_cast<uint8_t>((modrm & 7) | (cmpRex & 1 ? 8 : 0));
					if (start == end)
						std::swap(start, other);
					if (other != end || (start != base && start != index))
						continue;
					return std::make_pair(fields.at(start), fields.at(start == base ? index : base));
				}
			}
			return std::nullopt;
		}

		// The base register and displacement of mov r64, [base+disp] (no index, not rip relative) at the start of code.
		std::optional<std::pair<uint8_t, int32_t>> LoadFrom(std::span<const uint8_t> code) {
			if (code.size() < 3 || (code[0] & 0xF8) != 0x48 || (code[0] & 0x02) || code[1] != 0x8B)
				return std::nullopt;

			const auto mod = code[2] >> 6;
			auto rm = code[2] & 7;
			size_t next = 3;
			if (mod == 3)
				return std::nullopt;
			if (rm == 4) {
				// SIB without an index
				if (code.size() < 4 || ((code[3] >> 3) & 7) != 4)
					return std::nullopt;
				rm = code[3] & 7;
				next = 4;
			}
			if (mod == 0 && rm == 5)
				return std::nullopt;

			const auto baseRegister = static_cast<uint8_t>(rm | (code[0] & 1 ? 8 : 0));
			if (mod == 0)
				return std::make_pair(baseRegister, 0);
			if (mod == 1)
				return code.size() > next ? std::optional(std::make_pair(baseRegister, static_cast<int32_t>(static_cast<int8_t>(code[next])))) : std::nullopt;
			return code.size() >= next + 4 ? std::optional(std::make_pair(baseRegister, *reinterpret_cast<const int32_t*>(&code[next]))) : std::nullopt;
		}
	}

	std::string to_string(const FixDriverFunctions& value) {
		return std::format("execute all tasks {}", Signatures::Describe(value.ExecuteAllTasks));
	}

	std::string to_string(const SkipIdleNotifiersFunctions& value) {
		std::set<std::string> tests;
		for (const auto& t : value.CallbackTests)
			tests.insert(std::format("{}:{:+#x}/{:#x}/{:#x}", static_cast<int>(t.Test), t.FlagsOffset, t.Mask, t.SecondMask));
		std::string testList;
		for (const auto& t : tests)
			testList += (testList.empty() ? "" : " ") + t;
		return std::format("lock {}, head {} (next +0x{:X}), link {}, unlink {}, pre-present loop {} (slot {}), post-kick loop {} (slot {}), {} callback tests [{}]",
			Signatures::Describe(value.Lock),
			Signatures::Describe(value.Head),
			value.NextOffset,
			Signatures::Describe(value.Link),
			Signatures::Describe(value.Unlink),
			Signatures::Describe(value.PrePresentLoop),
			value.PrePresentSlot,
			Signatures::Describe(value.PostKickLoop),
			value.PostKickSlot,
			value.CallbackTests.size(),
			testList);
	}

	std::string to_string(const ChainWorkerWakeupsFunctions& value) {
		return std::format("queue indices {}, wake all {}, job pool +{} (threads +{}, count +{}), thread skip +{}, wake count +{} (limit {}), event +{}",
			Signatures::Describe(value.QueueIndices),
			Signatures::Describe(value.WakeAll),
			Signatures::Describe(value.Layout.TaskManagerJobPool),
			Signatures::Describe(value.Layout.Threads),
			Signatures::Describe(value.Layout.ThreadCount),
			Signatures::Describe(value.Layout.ThreadSkip),
			Signatures::Describe(value.Layout.ThreadWakeCount),
			Signatures::Describe(value.Layout.ThreadWakeLimit),
			Signatures::Describe(value.Layout.ThreadEvent));
	}

	std::string to_string(const PartialSkeletonLayout& value) {
		return std::format("count +{}, array +{}, stride {}, pose +{}",
			Signatures::Describe(value.Count),
			Signatures::Describe(value.Array),
			Signatures::Describe(value.Stride),
			Signatures::Describe(value.Pose));
	}

	std::string to_string(const ParallelForGroupLayout& value) {
		return std::format("writers +{} (count +{}, size {}, items +{}, block +{}), job list +{} (wait slot {}), context +{}, job +{}, chunks +{} ({} x {} blocks x {} items), blocks claimed +{}, claim counters +{} +{}, per-item claims +{}",
			Signatures::Describe(value.Writers),
			Signatures::Describe(value.WriterCount),
			Signatures::Describe(value.WriterSize),
			Signatures::Describe(value.WriterItems),
			Signatures::Describe(value.WriterBlock),
			Signatures::Describe(value.JobList),
			Signatures::Describe(value.JobListWaitSlot),
			Signatures::Describe(value.JobContext),
			Signatures::Describe(value.Job),
			Signatures::Describe(value.Chunks),
			Signatures::Describe(value.ChunkCount),
			Signatures::Describe(value.ChunkBlocks),
			Signatures::Describe(value.BlockItems),
			Signatures::Describe(value.BlocksClaimed),
			Signatures::Describe(value.ClaimCounters[0]),
			Signatures::Describe(value.ClaimCounters[1]),
			Signatures::Describe(value.PerItemClaims));
	}

	std::string to_string(const DedupeSkeletonSyncsFunctions& value) {
		return std::format("pose sync walk {}", Signatures::Describe(value.SyncWalk));
	}

	std::string to_string(const TrimCullingClearFunctions& value) {
		return std::format("clear count {} (0x{:X}), table +{}, object mask +{} ({} words), culling manager {}",
			Signatures::Describe(value.ClearCount),
			value.FullCount,
			Signatures::Describe(value.TableOffset),
			Signatures::Describe(value.ObjectMask),
			Signatures::Describe(value.ObjectMaskWords),
			Signatures::Describe(value.CullingManager));
	}

	std::string to_string(const ShortenAllocatorLockFunctions& value) {
		const auto& l = value.Layout;
		return std::format("free {}, lock +{}, chunk table +{} (count +{}, stride {}, base +{}, span {}), page mask {} (index +{}), backing +{} (free slot {})",
			Signatures::Describe(value.Free),
			Signatures::Describe(l.Lock),
			Signatures::Describe(l.ChunkTable),
			Signatures::Describe(l.ChunkCount),
			Signatures::Describe(l.ChunkStride),
			Signatures::Describe(l.ChunkBase),
			Signatures::Describe(l.ChunkSpan),
			Signatures::Describe(l.PageMask),
			Signatures::Describe(l.PageIndex),
			Signatures::Describe(l.Backing),
			Signatures::Describe(l.BackingFreeSlot));
	}

	std::string to_string(const PoolStagingBlocksFunctions& value) {
		return std::format("allocator manager {}, allocator +{} (vtable {}, slots: terminate {}, alloc {}, free {}, size {}; alloc counter +{})",
			Signatures::Describe(value.AllocatorManager),
			Signatures::Describe(value.AllocatorOffset),
			Signatures::Describe(value.Vtable),
			Signatures::Describe(value.TerminateSlot),
			Signatures::Describe(value.AllocSlot),
			Signatures::Describe(value.FreeSlot),
			Signatures::Describe(value.SizeSlot),
			Signatures::Describe(value.AllocCounter));
	}

	std::string to_string(const FreezeHiddenMinionsFunctions& value) {
		return std::format("follow {}, render flags +{}",
			Signatures::Describe(value.Follow),
			Signatures::Describe(value.RenderFlagsOffset));
	}

	std::string to_string(const SkipPrepareWaitFunctions& value) {
		return std::format("array list {}, single-item list {}",
			Signatures::Describe(value.ArrayList),
			Signatures::Describe(value.SingleItemList));
	}

	std::string to_string(const InlineBgPrepFunctions& value) {
		return std::format("kick {}, render manager {}, prep list +{}, framework task manager +{}, list slots: count {}, prepare {}, describe {}, job pool +{} (threads +{}, count +{}), thread pool +{}, pool context +{}, task slots {} and {}",
			Signatures::Describe(value.Kick),
			Signatures::Describe(value.RenderManager),
			Signatures::Describe(value.PrepListOffset),
			Signatures::Describe(value.FrameworkTaskManagerOffset),
			Signatures::Describe(value.ListCountSlot),
			Signatures::Describe(value.ListPrepareSlot),
			Signatures::Describe(value.ListDescribeSlot),
			Signatures::Describe(value.Pool.TaskManagerJobPool),
			Signatures::Describe(value.Pool.Threads),
			Signatures::Describe(value.Pool.ThreadCount),
			Signatures::Describe(value.Run.ThreadPool),
			Signatures::Describe(value.Run.PoolContext),
			Signatures::Describe(value.Run.TaskRunSlot),
			Signatures::Describe(value.Run.TaskRunWithArgumentSlot));
	}

	std::string to_string(const SkipHiddenHotbarsFunctions& value) {
		return std::format("bar {}, cross bar {}, length {}",
			Signatures::Describe(value.Bar),
			Signatures::Describe(value.CrossBar),
			Signatures::Describe(value.Length));
	}

	std::string to_string(const ParallelAnimTailFunctions& value) {
		return std::format("update {}, tail {}, entry count {}, entries {}, submit base {} (group +{}: {}), task manager {}, kick {}, help per item {}, help blocks {}, append {} (TLS +{}), partial skeletons: {} (pending removals +{}), ground +{} (active {})",
			Signatures::Describe(value.Update),
			Signatures::Describe(value.Tail),
			Signatures::Describe(value.EntryCount),
			Signatures::Describe(value.Entries),
			Signatures::Describe(value.SubmitBase),
			Signatures::Describe(value.GroupOffset),
			to_string(value.Group),
			Signatures::Describe(value.TaskManager),
			Signatures::Describe(value.Kick),
			Signatures::Describe(value.HelpPerItem),
			Signatures::Describe(value.HelpBlocks),
			Signatures::Describe(value.Append),
			Signatures::Describe(value.AppendTlsSlot),
			to_string(value.Partials),
			Signatures::Describe(value.PartialPendingRemovals),
			Signatures::Describe(value.SkeletonGround),
			Signatures::Describe(value.GroundRayActive));
	}

	std::string to_string(const SplitCharacterCullingFunctions& value) {
		return std::format("camera cull job {}, character item type {}, item size {} (start +{}, count +{})",
			Signatures::Describe(value.CullJob),
			Signatures::Describe(static_cast<size_t>(value.CharacterItemType)),
			Signatures::Describe(value.ItemSize),
			Signatures::Describe(value.StartOffset),
			Signatures::Describe(value.CountOffset));
	}

	std::string to_string(const PerItemCullingClaimsFunctions& value) {
		return std::format("culling manager {}, cell group +{} (per item {}, blocks {}), setup group +{} (per item {}, blocks {})",
			Signatures::Describe(value.CullingManager),
			Signatures::Describe(value.CellGroupOffset),
			Signatures::Describe(value.CellHelpPerItem),
			Signatures::Describe(value.CellHelpBlocks),
			Signatures::Describe(value.SetupGroupOffset),
			Signatures::Describe(value.SetupHelpPerItem),
			Signatures::Describe(value.SetupHelpBlocks));
	}

	std::string to_string(const GatherUsedCommandsFunctions& value) {
		const auto& l = value.Layout;
		return std::format("gather {}, sort {}, contexts +{} (count +{}, size {}), lists +{} (size {}, first block +{}, free slots +{}, blocks +{}), block size {} (next +{}), entry size {}",
			Signatures::Describe(value.Gather),
			Signatures::Describe(value.Sort),
			Signatures::Describe(l.ContextArray),
			Signatures::Describe(l.ContextCount),
			Signatures::Describe(l.ContextSize),
			Signatures::Describe(l.Lists),
			Signatures::Describe(l.ListSize),
			Signatures::Describe(l.ListFirstBlock),
			Signatures::Describe(l.ListFreeSlots),
			Signatures::Describe(l.ListBlocks),
			Signatures::Describe(l.BlockSize),
			Signatures::Describe(l.NextBlock),
			Signatures::Describe(l.EntrySize));
	}

	const Signatures::ComplexSignature<FixDriverFunctions> FixDriver("CrowdFix::FixDriver", [](ResolveContext& ctx) {
		return FixDriverFunctions{
			.ExecuteAllTasks = Address(UniqueCallTarget(ctx, TaskManagerExecuteAllTasksCall, "ExecuteAllTasks call")),
		};
	});

	const Signatures::ComplexSignature<SkipIdleNotifiersFunctions> SkipIdleNotifiers("CrowdFix::SkipIdleNotifiers", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto walk = ctx.Unique(NotifierListWalk, text, "notifier list walk");
		SkipIdleNotifiersFunctions r{
			.Lock = walk.ResolveAddress<CRITICAL_SECTION*>(1),
			.Head = walk.ResolveAddress<void* const*>(2),
		};
		ctx.RequireInSection(r.Lock, ".data", "notifier lock");
		ctx.RequireInSection(r.Head, ".data", "notifier list head");

		// The patched loops call the same slots and follow the same link as the originals.
		const auto loop = [&](const RegexSignature& signature, std::string_view what, uint8_t*& at, size_t& length, size_t& slot) {
			const auto m = ctx.Unique(signature, text, what);
			ctx.Require(m.ResolveAddress<void* const*>(1) == r.Head, ResolveError::Mismatch, "{} walks another list", what);
			ctx.Require(ByteAt(m, 2) % 8 == 0, ResolveError::Invalid, "{} calls vtable offset 0x{:X}", what, ByteAt(m, 2));
			ctx.Require(!r.NextOffset || r.NextOffset == ByteAt(m, 3), ResolveError::Mismatch, "the notifier loops follow +0x{:X} and +0x{:X}", r.NextOffset, ByteAt(m, 3));
			at = m.begin<uint8_t>(0);
			length = static_cast<size_t>(static_cast<uint8_t*>(m.end(0)) - at);
			slot = ByteAt(m, 2) / 8;
			r.NextOffset = ByteAt(m, 3);
		};
		loop(NotifierPrePresentLoop, "pre-present loop", r.PrePresentLoop, r.PrePresentLoopLength, r.PrePresentSlot);
		loop(NotifierPostKickLoop, "post-kick loop", r.PostKickLoop, r.PostKickLoopLength, r.PostKickSlot);
		ctx.Require(r.PrePresentSlot != r.PostKickSlot && r.PrePresentSlot && r.PostKickSlot, ResolveError::Mismatch,
			"the notifier loops call vtable slots {} and {}", r.PrePresentSlot, r.PostKickSlot);

		// Of the functions that take the same lock, only Link and Unlink read these right after.
		const auto lockUser = [&](const RegexSignature& signature, bool readsHead, std::string_view what) {
			const void* found = nullptr;
			for (const auto& m : ctx.All(signature, text)) {
				if (m.ResolveAddress<const void*>(1) != r.Lock || (readsHead && m.ResolveAddress<const void*>(2) != r.Head))
					continue;
				ctx.Require(!found, ResolveError::Ambiguous, "{} found more than once", what);
				found = ctx.FunctionContaining(m.begin(0), what).data();
			}
			ctx.Require(found != nullptr, ResolveError::NotFound, "{} not found", what);
			return found;
		};
		r.Link = Address(lockUser(NotifierLinkBody, true, "notifier link"));
		r.Unlink = Address(lockUser(NotifierUnlinkBody, false, "notifier unlink"));

		// Callbacks are shared across base/derived vtables; one without a test is assumed to always do work, so a missed test only keeps more notifiers.
		const std::pair<const RegexSignature*, NotifierWorkTest> tests[]{
			{&BufferNotifierPostKick, NotifierWorkTest::BufferFlags},
			{&BufferNotifierPrePresent, NotifierWorkTest::BufferFlags},
			{&IndexBufferNotifierPostKick, NotifierWorkTest::IndexBufferFlags},
			{&IndexBufferNotifierPrePresent, NotifierWorkTest::IndexBufferFlags},
			{&TextureNotifierPostKick, NotifierWorkTest::TextureMappedFlags},
			{&TextureNotifierPrePresent, NotifierWorkTest::TextureMappedOrUploadFlags},
			{&ConstantBufferNotifierPrePresent, NotifierWorkTest::ConstantBufferFlags},
		};
		// Every callback of the same kind of notifier must test the same flags: a wrong test would skip a notifier that has work.
		std::map<int, std::pair<ptrdiff_t, uint32_t>> kinds;
		for (const auto& [signature, test] : tests) {
			for (const auto& m : ctx.All(*signature, text)) {
				if (ctx.FunctionStartingAt(m.begin(0)).empty())
					continue;

				NotifierCallbackTest t{.Function = m.begin(0), .Test = test, .FlagsOffset = m.Get<int8_t>(1)};
				int kind = static_cast<int>(test);
				switch (test) {
					case NotifierWorkTest::BufferFlags:
					case NotifierWorkTest::IndexBufferFlags:
						t.Mask = m.Get<uint8_t>(2);
						if (test == NotifierWorkTest::IndexBufferFlags)
							t.SecondMask = m.Get<uint8_t>(3);
						break;

					case NotifierWorkTest::TextureMappedFlags:
						t.Mask = m.Get<uint32_t>(2);
						break;

					case NotifierWorkTest::TextureMappedOrUploadFlags: {
						t.Mask = m.Get<uint32_t>(2);
						kind = static_cast<int>(NotifierWorkTest::TextureMappedFlags);
						// where the jne goes: jne rel32 or jne rel8
						const auto target = m.Match()[3].matched
							? m.ResolveAddress<const uint8_t*>(3)
							: m.begin<uint8_t>(4) + 1 + m.Get<int8_t>(4);
						const auto upload = ctx.InSection(target, ".text") ? ctx.TryMatchAt(TextureNotifierUploadTest, CodeFrom(ctx, target, 5)) : std::nullopt;
						if (!upload || upload->Get<uint8_t>(1) >= 32)
							continue;
						t.SecondMask = uint32_t{1} << upload->Get<uint8_t>(1);
						break;
					}

					case NotifierWorkTest::ConstantBufferFlags:
						t.Mask = m.Get<uint32_t>(2);
						break;
				}

				ctx.Require(t.Mask != 0, ResolveError::Invalid, "notifier callback {} tests no flags", Signatures::Describe(t.Function));
				const auto [it, inserted] = kinds.try_emplace(kind, t.FlagsOffset, t.Mask);
				ctx.Require(inserted || it->second == std::make_pair(t.FlagsOffset, t.Mask), ResolveError::Mismatch,
					"notifier callbacks of one kind test +0x{:X} with 0x{:X} and +0x{:X} with 0x{:X}", it->second.first, it->second.second, t.FlagsOffset, t.Mask);
				r.CallbackTests.push_back(t);
			}
		}
		return r;
	});

	const Signatures::ComplexSignature<ChainWorkerWakeupsFunctions> ChainWorkerWakeups("CrowdFix::ChainWorkerWakeups", [](ResolveContext& ctx) {
		const auto text = ctx.Text();

		// The write index is the global the enqueues increment; the read index follows it.
		std::map<const uint32_t*, size_t> increments;
		for (const auto& m : ctx.All(JobRingIndexIncrement, text)) {
			if (const auto index = m.ResolveAddress<const uint32_t*>(1); index == m.ResolveAddress<const uint32_t*>(2))
				++increments[index];
		}
		ctx.Require(increments.size() == 2, ResolveError::Mismatch, "job ring index increments write {} globals instead of 2", increments.size());
		const auto [writeIndex, writes] = *increments.begin();
		const auto [readIndex, reads] = *std::next(increments.begin());
		ctx.Require(readIndex == writeIndex + 1 && writes > reads, ResolveError::Mismatch,
			"job ring indices {} ({} increments) and {} ({} increments) are not a write and a read index",
			Signatures::Describe(writeIndex), writes, Signatures::Describe(readIndex), reads);

		const auto [wakeAll, layout] = FindJobPoolWakeAll(ctx);
		return ChainWorkerWakeupsFunctions{
			.QueueIndices = static_cast<const uint32_t*>(ctx.RequireInSection(writeIndex, ".data", "job queue indices")),
			.WakeAll = Address(wakeAll),
			.Layout = layout,
		};
	});

	const Signatures::ComplexSignature<DedupeSkeletonSyncsFunctions> DedupeSkeletonSyncs("CrowdFix::DedupeSkeletonSyncs", [](ResolveContext& ctx) {
		return DedupeSkeletonSyncsFunctions{.SyncWalk = FindSkeletonPoseSyncWalk(ctx)};
	});

	const Signatures::ComplexSignature<TrimCullingClearFunctions> TrimCullingClear("CrowdFix::TrimCullingClear", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto loop = ctx.Unique(CullingVisibilityClearCount, text, "visibility clear loop");
		const auto site = loop.begin<const uint8_t>(0);
		ctx.Require(site - text.data() >= 0x30, ResolveError::Invalid, "visibility clear loop at the start of .text");

		// the table pointer is loaded into rax, which the loop stores through, right before the count
		const auto load = ctx.Find(CullingVisibilityTableLoad, std::span(site - 0x30, 0x35));
		ctx.Require(load && load->end(0) == site + 5, ResolveError::NotFound, "visibility table load not found");
		TrimCullingClearFunctions r{
			.ClearCount = loop.begin<uint32_t>(1),
			.FullCount = loop.Get<uint32_t>(1),
			.TableOffset = ByteAt(*load, 1),
			.CullingManager = FindCullingManager(ctx),
		};

		// The camera culling indexes the visibility table with the same slots the culling manager's slot allocator and release use.
		std::set<const void*> callees;
		for (const auto& m : ctx.All(CullingManagerCall, text)) {
			if (m.ResolveAddress<void* const*>(1) == r.CullingManager)
				callees.insert(m.ResolveAddress<const void*>(2));
		}

		// mask, objects, entry shift; and the words the allocator scans
		std::optional<std::tuple<size_t, size_t, size_t>> allocator, release;
		uint32_t words = 0;
		for (const auto callee : callees) {
			const auto fn = ctx.InSection(callee, ".text") ? ctx.FunctionStartingAt(callee) : std::span<const uint8_t>();
			if (fn.empty())
				continue;

			if (const auto m = ctx.Find(CullingSlotAlloc, fn)) {
				const auto current = std::make_tuple(ByteAt(*m, 1), ByteAt(*m, 4), ByteAt(*m, 3));
				ctx.Require(!allocator || *allocator == current, ResolveError::Ambiguous, "culling slot allocators disagree");
				allocator = current;
				words = m->Get<uint32_t>(2);
			}
			if (const auto m = ctx.Find(CullingSlotRelease, fn)) {
				const auto current = std::make_tuple(ByteAt(*m, 2), ByteAt(*m, 1), ByteAt(*m, 3));
				ctx.Require(!release || *release == current, ResolveError::Ambiguous, "culling slot releases disagree");
				release = current;
			}
		}
		ctx.Require(allocator.has_value(), ResolveError::NotFound, "culling slot allocator not found");
		ctx.Require(release.has_value(), ResolveError::NotFound, "culling slot release not found");

		const auto [mask, objects, shift] = *allocator;
		ctx.Require(*release == *allocator, ResolveError::Mismatch,
			"the culling slot allocator (mask +0x{:X}, objects +0x{:X}) and release (mask +0x{:X}, objects +0x{:X}) disagree",
			mask, objects, std::get<0>(*release), std::get<1>(*release));
		ctx.Require(std::cmp_equal(static_cast<uint64_t>(words) * 32, r.FullCount), ResolveError::Mismatch,
			"the culling slot allocator scans {} mask words for 0x{:X} visibility slots", words, r.FullCount);
		ctx.Require(mask != r.TableOffset && mask != objects, ResolveError::Invalid, "the object mask +0x{:X} overlaps the visibility table or the objects", mask);
		r.ObjectMask = mask;
		r.ObjectMaskWords = words;
		return r;
	});

	const Signatures::ComplexSignature<ShortenAllocatorLockFunctions> ShortenAllocatorLock("CrowdFix::ShortenAllocatorLock", [](ResolveContext& ctx) {
		const auto function = UniqueFunctionStart(ctx, GraphicsAllocatorFreeStart, "graphics allocator free");
		const auto code = CodeFrom(ctx, function, 0x200);
		const auto m = ctx.Find(GraphicsAllocatorFreeLayout, code);
		ctx.Require(m.has_value(), ResolveError::Mismatch, "graphics allocator free {} does not match", Signatures::Describe(function));

		GraphicsAllocatorLayout r{
			.Lock = FieldOffset(ctx, *m, 1, "lock"),
			.ChunkTable = FieldOffset(ctx, *m, 7, "chunk table"),
			.ChunkCount = FieldOffset(ctx, *m, 5, "chunk count"),
			.ChunkBase = ByteAt(*m, 10),
			.ChunkSpan = static_cast<size_t>(ctx.InRange<int32_t>(m->Get<int32_t>(11), 1, 0x1000000, "chunk span")),
			.PageMask = ~static_cast<uint64_t>(static_cast<int64_t>(m->Get<int32_t>(3))),
			.PageIndex = ByteAt(*m, 4),
		};
		ctx.Require(std::has_single_bit(r.PageMask + 1) && r.PageMask >= 0xFF && r.PageMask < 0x100000, ResolveError::Invalid,
			"page mask 0x{:X} is not a power of two minus one", r.PageMask);

		// The chunk entry address computation.
		const auto lea = m->begin<const uint8_t>(6);
		const auto add = m->begin<const uint8_t>(8);
		const auto sib = m->Get<uint8_t>(9);
		const auto leaTriples = (lea[2] & 0xC7) == 0x04 && (lea[3] >> 6) == 1 && ((lea[3] >> 3) & 7) == (lea[3] & 7) && !!(lea[0] & 2) == !!(lea[0] & 1);
		const auto addDoubles = ((add[2] >> 3) & 7) == (add[2] & 7) && !!(add[0] & 4) == !!(add[0] & 1);
		ctx.Require(leaTriples && addDoubles, ResolveError::Mismatch, "chunk entry address is not computed as expected");
		r.ChunkStride = size_t{6} << (sib >> 6);

		// It frees blocks of other sizes through the backing allocator, and unlocks the same lock it took.
		const auto rest = After(code, *m);
		std::optional<std::pair<size_t, size_t>> backing;
		for (const auto& f : ctx.All(GraphicsAllocatorBackingFree, rest)) {
			const auto current = std::make_pair(FieldOffset(ctx, f, 1, "backing allocator"), ByteAt(f, 2));
			ctx.Require(!backing || *backing == current, ResolveError::Mismatch, "backing frees disagree");
			backing = current;
		}
		ctx.Require(backing.has_value(), ResolveError::NotFound, "backing free not found");
		ctx.Require(backing->second % 8 == 0, ResolveError::Invalid, "backing free slot offset 0x{:X}", backing->second);
		r.Backing = backing->first;
		r.BackingFreeSlot = backing->second / 8;

		const auto enter = ImportSlotCalledAt(ctx, *m, 2);
		const auto leave = ImportSlot(ctx, "kernel32.dll", "LeaveCriticalSection");
		ctx.Require(enter == ImportSlot(ctx, "kernel32.dll", "EnterCriticalSection"), ResolveError::Mismatch, "graphics allocator free does not lock with EnterCriticalSection");
		ctx.Require(std::ranges::any_of(ctx.All(GraphicsAllocatorUnlock, rest), [&](const ScanResult& u) {
			return std::cmp_equal(u.Get<int32_t>(1), r.Lock) && ImportSlotCalledAt(ctx, u, 2) == leave;
		}), ResolveError::Mismatch, "graphics allocator free does not unlock +0x{:X}", r.Lock);

		return ShortenAllocatorLockFunctions{.Free = Address(function), .Layout = r};
	});

	const Signatures::ComplexSignature<PoolStagingBlocksFunctions> PoolStagingBlocks("CrowdFix::PoolStagingBlocks", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto load = ctx.Unique(GraphicsAllocatorManagerLoad, text, "allocator manager load");
		PoolStagingBlocksFunctions r{
			.AllocatorManager = static_cast<void* const*>(ctx.RequireInSection(load.ResolveAddress<const void*>(1), ".data", "allocator manager")),
			.AllocatorOffset = ByteAt(load, 2),
		};

		const auto slotsOf = [&ctx](const void* const* vtable) {
			std::vector<const void*> slots;
			for (size_t i = 0; i < 32 && ctx.InSection(vtable[i], ".text"); i++)
				slots.push_back(vtable[i]);
			return slots;
		};
		const auto matchingSlots = [&ctx](const std::vector<const void*>& slots, const RegexSignature& signature, auto&& predicate) {
			std::vector<std::pair<size_t, ScanResult>> found;
			for (size_t i = 0; i < slots.size(); i++) {
				if (const auto m = ctx.TryMatchAt(signature, CodeFrom(ctx, slots[i], 0x40)); m && predicate(*m))
					found.emplace_back(i, *m);
			}
			return found;
		};

		// The class whose counting alloc forwards to the small-object allocator beside it; other slots are told apart by the inner slot they forward to.
		// A wrong guess corrupts the heap: pooled blocks need the real size query for their size class, and the real free once the pool is off.
		std::optional<std::tuple<const void* const*, const void* const*, int32_t>> classes;
		for (const auto& m : ctx.All(GraphicsAllocatorVtables, text)) {
			const auto vtable = m.ResolveAddress<const void* const*>(1);
			const auto inner = m.ResolveAddress<const void* const*>(2);
			const auto innerOffset = m.Get<int32_t>(3);
			if (!ctx.InSection(vtable, ".rdata") || !ctx.InSection(inner, ".rdata") || (classes && std::get<0>(*classes) == vtable))
				continue;
			if (matchingSlots(slotsOf(vtable), GraphicsAllocatorCountingWrapper, [innerOffset](const ScanResult& w) { return w.Get<int32_t>(2) == innerOffset; }).empty())
				continue;

			ctx.Require(!classes, ResolveError::Ambiguous, "graphics allocator vtables at {} and {}", Signatures::Describe(std::get<0>(*classes)), Signatures::Describe(vtable));
			classes.emplace(vtable, inner, innerOffset);
		}
		ctx.Require(classes.has_value(), ResolveError::NotFound, "graphics allocator vtable not found");
		const auto [vtable, innerVtable, innerOffset] = *classes;
		const auto slots = slotsOf(vtable);
		const auto innerSlots = slotsOf(innerVtable);

		const auto unique = [&ctx](const auto& found, std::string_view what) {
			ctx.Require(found.size() == 1, ResolveError::Mismatch, "{} graphics allocator {} slots instead of 1", found.size(), what);
			return found.front();
		};
		const auto [innerFree, innerFreeMatch] = unique(matchingSlots(innerSlots, GraphicsAllocatorFreeStart, [](const ScanResult&) { return true; }), "inner free");
		const auto [innerSize, innerSizeMatch] = unique(matchingSlots(innerSlots, SmallObjectAllocatorSize, [](const ScanResult&) { return true; }), "inner size");
		const auto forwardingTo = [&](size_t innerSlot) {
			return [innerOffset, innerSlot](const ScanResult& w) {
				return w.Get<int32_t>(1) == innerOffset && w.Get<uint8_t>(2) == innerSlot * 8;
			};
		};

		const auto [allocSlot, alloc] = unique(matchingSlots(slots, GraphicsAllocatorCountingWrapper, [innerOffset](const ScanResult& w) { return w.Get<int32_t>(2) == innerOffset; }), "alloc");
		r.Vtable = vtable;
		r.AllocSlot = allocSlot;
		r.FreeSlot = unique(matchingSlots(slots, GraphicsAllocatorForwardingWrapper, forwardingTo(innerFree)), "free").first;
		r.SizeSlot = unique(matchingSlots(slots, GraphicsAllocatorForwardingWrapper, forwardingTo(innerSize)), "size").first;
		r.TerminateSlot = unique(matchingSlots(slots, GraphicsAllocatorTerminateWrapper, [innerOffset](const ScanResult& w) {
			return w.Get<int32_t>(1) == innerOffset && w.Get<uint8_t>(3) == w.Get<uint8_t>(4);
		}), "terminate").first;
		r.AllocCounter = FieldOffset(ctx, alloc, 1, "alloc counter");

		// what the game itself calls to allocate through the manager
		ctx.Require(ByteAt(load, 3) == r.AllocSlot * 8, ResolveError::Mismatch,
			"the game allocates through slot +0x{:X}, but the counting alloc is slot {}", ByteAt(load, 3), r.AllocSlot);
		return r;
	});

	const Signatures::ComplexSignature<FreezeHiddenMinionsFunctions> FreezeHiddenMinions("CrowdFix::FreezeHiddenMinions", [](ResolveContext& ctx) {
		const auto jump = ctx.Unique(CompanionFollowJump, ctx.Text(), "companion follow jump");

		// Companion::Update tests the render flags itself after the draw-ready check; the tests and the jump may be in any of its parts.
		std::optional<size_t> renderFlags;
		for (const auto part : FunctionParts(ctx, ctx.FunctionContaining(jump.begin(0), "Companion::Update").data())) {
			for (const auto& m : ctx.All(CompanionRenderFlagsTest, part)) {
				const auto offset = FieldOffset(ctx, m, 1, "render flags");
				ctx.Require(!renderFlags || *renderFlags == offset, ResolveError::Mismatch, "render flag tests disagree: +0x{:X} and +0x{:X}", renderFlags.value_or(0), offset);
				renderFlags = offset;
			}
		}
		ctx.Require(renderFlags.has_value(), ResolveError::NotFound, "render flags test not found");

		return FreezeHiddenMinionsFunctions{
			.Follow = Address(ctx.RequireInSection(jump.ResolveAddress<const void*>(1), ".text", "companion follow")),
			.RenderFlagsOffset = *renderFlags,
		};
	});

	const Signatures::ComplexSignature<SkipPrepareWaitFunctions> SkipPrepareWait("CrowdFix::SkipPrepareWait", [](ResolveContext& ctx) {
		const auto wait = ImportSlot(ctx, "kernel32.dll", "WaitForSingleObject");
		const auto site = [&](const RegexSignature& signature, std::string_view what) {
			const auto m = ctx.Unique(signature, ctx.Text(), what);
			ctx.Require(ImportSlotCalledAt(ctx, m, 2) == wait, ResolveError::Mismatch, "{} does not call WaitForSingleObject", what);
			return m.begin<uint8_t>(1);
		};
		return SkipPrepareWaitFunctions{
			.ArrayList = site(JobListArrayPrepare, "array job list prepare"),
			.SingleItemList = site(JobListSingleItemPrepare, "single-item job list prepare"),
		};
	});

	const Signatures::ComplexSignature<InlineBgPrepFunctions> InlineBgPrep("CrowdFix::InlineBgPrep", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto kick = FindJobListKick(ctx).Kick;

		// RenderView may kick it from more than one place, but always the same list on the same task manager.
		std::optional<std::pair<int32_t, int32_t>> offsets;
		for (const auto& m : ctx.All(BgInstancingPrepKickCall, text)) {
			if (m.ResolveAddress<const void*>(3) != kick)
				continue;
			const auto current = std::make_pair(m.Get<int32_t>(1), m.Get<int32_t>(2));
			ctx.Require(!offsets || *offsets == current, ResolveError::Ambiguous, "BG instancing prep kicks disagree");
			offsets = current;
		}
		ctx.Require(offsets.has_value(), ResolveError::NotFound, "BG instancing prep kick not found");

		const auto load = ctx.Unique(RenderManagerLoad, text, "render manager load");
		InlineBgPrepFunctions r{
			.Kick = Address(kick),
			.RenderManager = static_cast<void* const*>(ctx.RequireInSection(load.ResolveAddress<const void*>(1), ".data", "render manager")),
			.PrepListOffset = static_cast<size_t>(ctx.InRange<int32_t>(offsets->first, 1, 0x100000, "prep list offset")),
			.FrameworkTaskManagerOffset = static_cast<size_t>(ctx.InRange<int32_t>(offsets->second, 1, 0x100000, "framework task manager offset")),
		};

		// The inline run does what the kick and a worker would: the kick's job list calls, then the worker's claim and task calls with its context.
		const auto calls = ctx.Find(JobListKickCalls, CodeFrom(ctx, kick, 0x80));
		ctx.Require(calls.has_value(), ResolveError::Mismatch, "job list kick {} does not call the job list as expected", Signatures::Describe(kick));
		ctx.Require(ByteAt(*calls, 1) == ByteAt(*calls, 4), ResolveError::Mismatch, "the job list kick counts the items with two different slots");
		for (const auto group : {1, 2, 3})
			ctx.Require(ByteAt(*calls, group) % 8 == 0, ResolveError::Invalid, "job list slot offset 0x{:X}", ByteAt(*calls, group));
		r.ListCountSlot = ByteAt(*calls, 1) / 8;
		r.ListPrepareSlot = ByteAt(*calls, 2) / 8;
		r.ListDescribeSlot = ByteAt(*calls, 3) / 8;

		const auto task = ctx.Unique(JobRunTask, text, "job run task call");
		const auto claim = ctx.Find(JobRunClaim, WholeFunctionContaining(ctx, task.begin(0), "job run"));
		ctx.Require(claim.has_value(), ResolveError::NotFound, "job run claim call not found");
		// The descriptor the describe fills is copied as it is into the queue entry, which the worker reads as these.
		const auto throughRax = claim->Match()[1].matched;
		const auto claimFunction = ByteAt(*claim, throughRax ? 1 : 6);
		const auto claimState = ByteAt(*claim, throughRax ? 2 : 5);
		const auto claimOwner = ByteAt(*claim, throughRax ? 3 : 4);
		ctx.Require(claimOwner == claimFunction + 8 && claimState == claimFunction + 0x10, ResolveError::Mismatch,
			"the job run claims with the function at +0x{:X}, its object at +0x{:X} and its state at +0x{:X}", claimFunction, claimOwner, claimState);
		ctx.Require(ByteAt(task, 3) % 8 == 0 && ByteAt(task, 4) % 8 == 0, ResolveError::Invalid, "task slot offsets 0x{:X} and 0x{:X}", ByteAt(task, 3), ByteAt(task, 4));
		r.Run = {
			.ThreadPool = ByteAt(task, 1),
			.PoolContext = ByteAt(task, 2),
			.TaskRunSlot = ByteAt(task, 3) / 8,
			.TaskRunWithArgumentSlot = ByteAt(task, 4) / 8,
		};
		r.Pool = FindJobPoolWakeAll(ctx).Layout;
		return r;
	});

	const Signatures::ComplexSignature<SkipHiddenHotbarsFunctions> SkipHiddenHotbars("CrowdFix::SkipHiddenHotbars", [](ResolveContext& ctx) {
		// The bar and the cross bar site: the same constructor and Prepare, the same temporary, the same length.
		const auto sites = ctx.All(HotbarPrepare, ctx.Text());
		ctx.Require(sites.size() == 2, ResolveError::Mismatch, "{} hidden hotbar prepare sites instead of 2", sites.size());
		const auto length = [](const ScanResult& m) {
			return static_cast<size_t>(static_cast<uint8_t*>(m.end(1)) - m.begin<uint8_t>(1));
		};
		const auto& bar = sites[0];
		const auto& crossBar = sites[1];
		ctx.Require(bar.ResolveAddress<const void*>(3) == crossBar.ResolveAddress<const void*>(3)
			&& bar.ResolveAddress<const void*>(4) == crossBar.ResolveAddress<const void*>(4), ResolveError::Mismatch,
			"the hidden hotbar prepare sites call different functions");
		ctx.Require(bar.Get<uint8_t>(2) == crossBar.Get<uint8_t>(2), ResolveError::Mismatch, "the hidden hotbar prepare sites use different temporaries");
		ctx.Require(length(bar) == length(crossBar), ResolveError::Mismatch, "the hidden hotbar prepare sites differ in length");
		return SkipHiddenHotbarsFunctions{
			.Bar = bar.begin<uint8_t>(1),
			.CrossBar = crossBar.begin<uint8_t>(1),
			.Length = length(bar),
		};
	});

	const Signatures::ComplexSignature<ParallelAnimTailFunctions> ParallelAnimTail("CrowdFix::ParallelAnimTail", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto update = UniqueCallTarget(ctx, AnimationUpdateCall, "animation update call");
		const auto updateFn = ctx.FunctionStartingAt(update, "animation update");

		// The tail's prologue is not unique in every build; the tail is the one of them the update calls.
		const void* tail = nullptr;
		for (const auto& m : ctx.All(AnimationTailStart, text)) {
			if (ctx.FunctionStartingAt(m.begin(0)).empty() || !Calls(updateFn, m.begin(0)))
				continue;
			ctx.Require(!tail, ResolveError::Ambiguous, "the animation update calls more than one tail candidate");
			tail = m.begin(0);
		}
		ctx.Require(tail != nullptr, ResolveError::NotFound, "animation tail not found");
		const auto entries = ctx.Unique(AnimationTailEntries, updateFn, "tail entries");

		const auto submit = ctx.Unique(AnimationSubmit, text, "animation submit");
		const auto submitFn = ctx.FunctionStartingAt(submit.begin(0), "animation submit");
		const auto jobListKick = FindJobListKick(ctx);
		const auto kick = ctx.Unique(AnimationSubmitKick, submitFn, "animation submit kick");
		ctx.Require(kick.ResolveAddress<const void*>(3) == jobListKick.Kick, ResolveError::Mismatch, "the animation submit kicks with another function");
		ctx.Require(kick.ResolveAddress<void* const*>(2) == jobListKick.TaskManager, ResolveError::Mismatch, "the animation submit kicks on another task manager");

		// The append is the function with this prologue that the submit calls; its TLS slot moves from build to build.
		const ScanResult* append = nullptr;
		const auto appends = ctx.All(AnimationTailAppend, text);
		for (const auto& m : appends) {
			if (!Calls(submitFn, m.begin(0)))
				continue;
			ctx.Require(!append, ResolveError::Ambiguous, "the animation submit calls more than one append candidate");
			append = &m;
		}
		ctx.Require(append != nullptr && !ctx.FunctionStartingAt(append->begin(0)).empty(), ResolveError::NotFound, "animation append not found");

		ParallelAnimTailFunctions r{
			.Update = Address(update),
			.Tail = Address(tail),
			.EntryCount = entries.ResolveAddress<int32_t*>(1),
			.Entries = entries.ResolveAddress<void*>(2),
			.SubmitBase = submit.ResolveAddress<void* const*>(1),
			.TaskManager = kick.ResolveAddress<void* const*>(2),
			.Kick = Address(kick.ResolveAddress<const void*>(3)),
			.HelpPerItem = Address(ctx.RequireInSection(kick.ResolveAddress<const void*>(5), ".text", "per-item help")),
			.HelpBlocks = Address(ctx.RequireInSection(kick.ResolveAddress<const void*>(6), ".text", "block help")),
			.Append = Address(append->begin(0)),
			.AppendTlsSlot = static_cast<size_t>(ctx.InRange<int32_t>(append->Get<int32_t>(1), 1, 0x10000, "append TLS slot")),
			.Partials = FindPartialSkeletonLayout(ctx),
		};
		ctx.RequireInSection(r.EntryCount, ".data", "tail entry count");
		ctx.RequireInSection(r.Entries, ".data", "tail entries");
		ctx.RequireInSection(r.SubmitBase, ".data", "submit base");
		ctx.RequireInSection(r.TaskManager, ".data", "task manager");

		// The fix repeats the submit's arm/kick/help/wait/reset on the same group, so every field the submit touches more than once must agree;
		// writer and block fields must also agree with the append, which crashes once the claimed blocks pass the last chunk.
		const auto group = ctx.First(AnimationSubmitGroup, submitFn, "animation submit group");
		ctx.Require(group.ResolveAddress<void* const*>(1) == r.SubmitBase, ResolveError::Mismatch, "the animation submit group is not in the submit base");
		const auto flush = ctx.First(ParallelForWriterFlush, After(submitFn, group), "parallel-for writer flush");
		const auto arm = ctx.First(ParallelForArm, After(submitFn, flush), "parallel-for arming");
		ctx.Require(static_cast<const uint8_t*>(arm.end(0)) <= kick.begin<const uint8_t>(0), ResolveError::Mismatch, "the animation submit arms its group after kicking it");
		const auto disarm = ctx.First(ParallelForDisarm, After(submitFn, kick), "parallel-for disarming");
		const auto chunkReset = ctx.First(ParallelForChunkReset, After(submitFn, disarm), "parallel-for chunk reset");
		const auto claim = ctx.First(ParallelForBlockClaim, WholeFunctionContaining(ctx, append->begin(0), "animation append"), "parallel-for block claim");

		r.GroupOffset = ByteAt(group, 3);
		auto& g = r.Group;
		ctx.Require(ByteAt(group, 2) > r.GroupOffset, ResolveError::Invalid, "the writer count +0x{:X} is before the group +0x{:X}", ByteAt(group, 2), r.GroupOffset);
		g.WriterCount = ByteAt(group, 2) - r.GroupOffset;
		g.Writers = ByteAt(flush, 1);
		g.WriterBlock = ByteAt(flush, 2);
		g.WriterItems = ByteAt(flush, 3);
		g.WriterSize = ByteAt(flush, 4);
		g.JobContext = ByteAt(arm, 2);
		g.Job = ByteAt(arm, 3);
		g.ClaimCounters[0] = FieldOffset(ctx, arm, 4, "claim counter");
		g.ClaimCounters[1] = FieldOffset(ctx, arm, 5, "claim counter");
		g.BlocksClaimed = FieldOffset(ctx, arm, 6, "blocks claimed");
		g.JobList = ByteAt(kick, 1);
		g.PerItemClaims = FieldOffset(ctx, kick, 4, "per-item claims");
		g.BlockItems = static_cast<size_t>(ctx.InRange<int32_t>(disarm.Get<int32_t>(6), 1, 0x100, "items per block"));
		g.ChunkCount = static_cast<size_t>(ctx.InRange<int32_t>(chunkReset.Get<int32_t>(2), 1, 0x100, "chunk count"));
		g.ChunkBlocks = size_t{1} << ctx.InRange<uint8_t>(claim.Get<uint8_t>(6), 1, 16, "chunk shift");
		ctx.Require(ByteAt(chunkReset, 1) > r.GroupOffset, ResolveError::Invalid, "the chunks +0x{:X} are before the group +0x{:X}", ByteAt(chunkReset, 1), r.GroupOffset);
		g.Chunks = ByteAt(chunkReset, 1) - r.GroupOffset;
		ctx.Require(ByteAt(kick, 8) % 8 == 0, ResolveError::Invalid, "job list wait slot offset 0x{:X}", ByteAt(kick, 8));
		g.JobListWaitSlot = ByteAt(kick, 8) / 8;

		ctx.Require(ByteAt(kick, 7) == g.JobList, ResolveError::Mismatch, "the animation submit waits on +0x{:X}, but kicks +0x{:X}", ByteAt(kick, 7), g.JobList);
		ctx.Require(ByteAt(disarm, 1) == g.JobContext && ByteAt(disarm, 2) == g.Job, ResolveError::Mismatch,
			"the animation submit arms +0x{:X} and +0x{:X}, but disarms +0x{:X} and +0x{:X}", g.JobContext, g.Job, ByteAt(disarm, 1), ByteAt(disarm, 2));
		ctx.Require(ByteAt(disarm, 3) == g.Writers && ByteAt(disarm, 4) == g.WriterSize
			&& std::cmp_equal(SignedByteAt(disarm, 5) + static_cast<ptrdiff_t>(g.WriterSize), g.WriterItems)
			&& std::cmp_equal(SignedByteAt(disarm, 7) + static_cast<ptrdiff_t>(g.WriterSize), g.WriterBlock), ResolveError::Mismatch,
			"the animation submit publishes and empties its writers differently");
		ctx.Require(FieldOffset(ctx, chunkReset, 3, "blocks claimed") == r.GroupOffset + g.BlocksClaimed, ResolveError::Mismatch,
			"the animation submit counts claimed blocks at +0x{:X}, but resets +0x{:X}", r.GroupOffset + g.BlocksClaimed, FieldOffset(ctx, chunkReset, 3, "blocks claimed"));
		ctx.Require(ByteAt(claim, 1) == g.WriterItems && ByteAt(claim, 2) + 1 == g.BlockItems && ByteAt(claim, 3) == g.WriterBlock, ResolveError::Mismatch,
			"the animation append fills its writer differently from the submit");
		ctx.Require(FieldOffset(ctx, claim, 5, "blocks claimed") == g.BlocksClaimed && ByteAt(claim, 7) == g.ChunkCount && ByteAt(claim, 8) == g.Chunks, ResolveError::Mismatch,
			"the animation append claims blocks differently from the submit");
		ctx.Require(g.WriterItems + 4 <= g.WriterSize && g.WriterBlock + 8 <= g.WriterSize, ResolveError::Invalid, "writer fields outside a writer of {} bytes", g.WriterSize);

		// The tail only casts a ground ray for a skeleton whose ground state passes this test.
		const auto tailFn = WholeFunctionContaining(ctx, tail, "animation tail");
		const auto ground = ctx.First(AnimationTailGround, tailFn, "animation tail ground ray");
		const auto groundRayActive = ctx.RequireInSection(ground.ResolveAddress<const void*>(2), ".text", "ground ray test");
		ctx.Require(ctx.TryMatchAt(GroundRayActiveBody, CodeFrom(ctx, groundRayActive, 0x20)).has_value(), ResolveError::Mismatch,
			"the ground ray test {} does more than read the ground state", Signatures::Describe(groundRayActive));
		r.SkeletonGround = FieldOffset(ctx, ground, 1, "skeleton ground");
		r.GroundRayActive = Address(groundRayActive);

		// Then it updates every partial skeleton with a pose, which ends by applying its pending animation control removals.
		const auto partialUpdate = ctx.First(AnimationTailPartialUpdate, After(tailFn, ground), "animation tail partial skeleton update");
		ctx.Require(FieldOffset(ctx, partialUpdate, 1, "partial skeleton pose") == r.Partials.Pose, ResolveError::Mismatch,
			"the animation tail tests the pose at +0x{:X}, the pose sync walk at +0x{:X}", FieldOffset(ctx, partialUpdate, 1, "partial skeleton pose"), r.Partials.Pose);
		std::optional<size_t> removals;
		const auto partialUpdateFn = WholeFunctionContaining(ctx, ctx.RequireInSection(partialUpdate.ResolveAddress<const void*>(2), ".text", "partial skeleton update"), "partial skeleton update");
		for (const auto& m : ctx.All(PartialPendingRemovals, partialUpdateFn)) {
			// a list: its head, then its count
			const auto count = FieldOffset(ctx, m, 2, "pending removals");
			if (count != FieldOffset(ctx, m, 1, "pending removals list") + 8)
				continue;
			ctx.Require(!removals || *removals == count, ResolveError::Ambiguous, "pending removals at +0x{:X} and +0x{:X}", removals.value_or(0), count);
			removals = count;
		}
		ctx.Require(removals.has_value(), ResolveError::NotFound, "pending animation control removals not found");
		ctx.Require(*removals + 8 <= r.Partials.Stride, ResolveError::Invalid, "pending removals +0x{:X} outside a partial skeleton of 0x{:X} bytes", *removals, r.Partials.Stride);
		r.PartialPendingRemovals = *removals;
		return r;
	});

	const Signatures::ComplexSignature<SplitCharacterCullingFunctions> SplitCharacterCulling("CrowdFix::SplitCharacterCulling", [](ResolveContext& ctx) {
		// Of the jobs stored into parallel-for groups, the camera culling job is the one that starts by reading its item's type, start and count.
		const void* found = nullptr;
		std::pair<size_t, size_t> range;
		std::set<const void*> builders;
		for (const auto& m : ctx.All(ParallelForJobStore, ctx.Text())) {
			const auto job = m.ResolveAddress<const void*>(1);
			if (job != found) {
				if (!ctx.InSection(job, ".text") || ctx.FunctionStartingAt(job).empty())
					continue;

				if (!ctx.Find(CameraCullItemType, CodeFrom(ctx, job, 0x60)))
					continue;
				const auto jobRange = FindCullItemRange(ctx, job);
				if (!jobRange)
					continue;

				ctx.Require(!found, ResolveError::Ambiguous, "camera cull jobs at {} and {}", Signatures::Describe(found), Signatures::Describe(job));
				found = job;
				range = *jobRange;
			}
			builders.insert(WholeFunctionContaining(ctx, m.begin(0), "camera culling").data());
		}
		ctx.Require(found != nullptr, ResolveError::NotFound, "camera cull job not found");
		ctx.Require(builders.size() == 1, ResolveError::Mismatch, "{} functions arm the camera cull job instead of 1", builders.size());
		const auto builder = WholeFunctionContaining(ctx, *builders.begin(), "camera culling");

		// The arming function builds the items: visible objects sorted by type, every list but the BG objects' put into a single item of that type.
		// Characters must get one of those single items; the item size is the item allocator's stride.
		std::set<uint8_t> objectTypes;
		for (const auto& m : ctx.All(CameraCullObjectTypeTest, builder))
			objectTypes.insert(m.Get<uint8_t>(1));
		std::set<uint8_t> singleItemTypes;
		const void* allocator = nullptr;
		for (const auto& m : ctx.All(CameraCullSingleItem, builder)) {
			const auto target = m.ResolveAddress<const void*>(1);
			ctx.Require(!allocator || allocator == target, ResolveError::Mismatch, "the camera culling allocates its items with {} and {}", Signatures::Describe(allocator), Signatures::Describe(target));
			allocator = target;
			singleItemTypes.insert(m.Get<uint8_t>(2));
		}
		ctx.Require(allocator != nullptr, ResolveError::NotFound, "camera culling item allocation not found");
		constexpr uint8_t characterType = 2;  // The object type of characters; no single instruction names it. As of 7.56h.
		ctx.Require(objectTypes.contains(characterType) && singleItemTypes.contains(characterType), ResolveError::Mismatch,
			"the camera culling does not put objects of type {} into a single item of their own", characterType);

		const auto address = ctx.First(CameraCullItemAddress, WholeFunctionContaining(ctx, ctx.RequireInSection(allocator, ".text", "camera culling item allocator"), "camera culling item allocator"), "camera culling item address");
		ctx.Require(ByteAt(address, 2) == ByteAt(address, 4), ResolveError::Mismatch, "the camera culling item allocator returns items from another cursor");
		const auto itemSize = size_t{1} << ctx.InRange<uint8_t>(address.Get<uint8_t>(3), 6, 8, "camera culling item shift");
		const auto [start, count] = range;
		ctx.Require(start != count && start + 4 <= itemSize && count + 4 <= itemSize, ResolveError::Mismatch,
			"the camera cull job reads its range at +0x{:X} and +0x{:X}, outside items of 0x{:X} bytes", start, count, itemSize);
		return SplitCharacterCullingFunctions{
			.CullJob = Address(found),
			.CharacterItemType = characterType,
			.ItemSize = itemSize,
			.StartOffset = start,
			.CountOffset = count,
		};
	});

	const Signatures::ComplexSignature<PerItemCullingClaimsFunctions> PerItemCullingClaims("CrowdFix::PerItemCullingClaims", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto offsetAndTarget = [](const ScanResult& m) {
			return std::make_pair(m.Get<int32_t>(1), m.ResolveAddress<const void*>(2));
		};

		std::set<std::pair<int32_t, const void*>> setups;
		for (const auto& m : ctx.All(CullingSetupParallelFor, text))
			setups.insert(offsetAndTarget(m));
		ctx.Require(setups.size() == 1, ResolveError::Mismatch, "{} culling setup parallel-fors instead of 1", setups.size());

		// The cell group is the other group handed to a parallel-for this way.
		std::set<std::pair<int32_t, const void*>> cells;
		for (const auto& m : ctx.All(CullingCellParallelFor, text)) {
			if (const auto site = offsetAndTarget(m); 0 < site.first && site.first < 0x10000 && !setups.contains(site))
				cells.insert(site);
		}
		ctx.Require(cells.size() == 1, ResolveError::Mismatch, "{} cell culling parallel-for candidates instead of 1", cells.size());

		const auto [cellOffset, cellForkJoin] = *cells.begin();
		const auto [setupOffset, setupForkJoin] = *setups.begin();
		const auto [cellPerItem, cellBlocks] = ParallelForHelps(ctx, ctx.RequireInSection(cellForkJoin, ".text", "cell culling parallel-for"), "cell culling parallel-for");
		const auto [setupPerItem, setupBlocks] = ParallelForHelps(ctx, ctx.RequireInSection(setupForkJoin, ".text", "culling setup parallel-for"), "culling setup parallel-for");
		return PerItemCullingClaimsFunctions{
			.CullingManager = FindCullingManager(ctx),
			.CellGroupOffset = static_cast<size_t>(ctx.InRange<int32_t>(cellOffset, 1, 0x10000, "cell group offset")),
			.CellHelpPerItem = cellPerItem,
			.CellHelpBlocks = cellBlocks,
			.SetupGroupOffset = static_cast<size_t>(ctx.InRange<int32_t>(setupOffset, 1, 0x10000, "setup group offset")),
			.SetupHelpPerItem = setupPerItem,
			.SetupHelpBlocks = setupBlocks,
		};
	});

	const Signatures::ComplexSignature<GatherUsedCommandsFunctions> GatherUsedCommands("CrowdFix::GatherUsedCommands", [](ResolveContext& ctx) {
		const auto gather = UniqueFunctionStart(ctx, CommandListGatherDriver, "command list gather");
		const auto gatherFn = ctx.FunctionStartingAt(gather, "command list gather");

		// The gather of one context's blocks is the only function the gather calls.
		std::set<const void*> callees;
		for (const auto& m : RelativeCall.Lookup(gatherFn)) {
			if (const auto target = m.ResolveAddress<const void*>(1); ctx.InSection(target, ".text") && !ctx.FunctionStartingAt(target).empty())
				callees.insert(target);
		}
		ctx.Require(callees.size() == 1, ResolveError::Mismatch, "the command list gather calls {} functions instead of 1", callees.size());
		const auto blocks = *callees.begin();
		const auto blocksFn = ctx.FunctionStartingAt(blocks, "command list gather of blocks");

		// It inlines the top level of a merge sort, calling the sort for both halves; the sort calls itself for its halves.
		std::map<const void*, size_t> blocksCallees;
		for (const auto& m : RelativeCall.Lookup(blocksFn))
			++blocksCallees[m.ResolveAddress<const void*>(1)];
		const void* sort = nullptr;
		for (const auto& [target, count] : blocksCallees) {
			if (count != 2 || !ctx.InSection(target, ".text") || ctx.FunctionStartingAt(target).empty())
				continue;

			size_t selfCalls = 0;
			for (const auto part : FunctionParts(ctx, target)) {
				for (const auto& m : RelativeCall.Lookup(part))
					selfCalls += m.ResolveAddress<const void*>(1) == target;
			}
			if (selfCalls < 2)
				continue;

			ctx.Require(!sort, ResolveError::Ambiguous, "merge sorts at {} and {}", Signatures::Describe(sort), Signatures::Describe(target));
			sort = target;
		}
		ctx.Require(sort != nullptr, ResolveError::NotFound, "merge sort not found");

		const auto contextCount = ctx.First(CommandListContextCount, gatherFn, "context count");
		const auto contextArray = ctx.First(CommandListContextArray, gatherFn, "context array");
		const auto prologue = ctx.Find(CommandListBlocks, blocksFn.first((std::min)(blocksFn.size(), static_cast<size_t>(0x40))));
		ctx.Require(prologue.has_value(), ResolveError::Mismatch, "command list gather of blocks {} does not start as expected", Signatures::Describe(blocks));
		const auto copy = ctx.First(CommandListBlockCopy, After(blocksFn, *prologue), "block copy");
		const auto entryCount = ctx.First(CommandListEntryCount, After(blocksFn, copy), "entry count");
		// The lists start one list in.
		const auto tripling = prologue->Get<uint8_t>(1);
		const auto listSib = prologue->Get<uint8_t>(3);
		ctx.Require((tripling & 0x3F) == 0 && (listSib & 0x38) == 0 && listSib == prologue->Get<uint8_t>(4), ResolveError::Mismatch,
			"the command list gather of blocks {} does not index its lists as expected", Signatures::Describe(blocks));
		const auto listSize = (size_t{1} + (size_t{1} << (tripling >> 6))) << (listSib >> 6);

		// The first block, loaded from the list before the copy.
		const auto list = static_cast<uint8_t>(((prologue->Get<uint8_t>(2) >> 3) & 7) | 8);
		const auto beforeCopy = After(blocksFn, *prologue).first(static_cast<size_t>(static_cast<const uint8_t*>(copy.begin(0)) - static_cast<const uint8_t*>(prologue->end(0))));
		std::optional<int32_t> firstBlock;
		for (const auto& m : ctx.All(CommandListFirstBlock, beforeCopy)) {
			if (const auto load = LoadFrom(std::span(m.begin<const uint8_t>(0), beforeCopy.data() + beforeCopy.size())); load && load->first == list) {
				firstBlock = load->second;
				break;
			}
		}
		ctx.Require(firstBlock.has_value() && *firstBlock >= 0, ResolveError::NotFound, "the command list gather of blocks does not load the first block");

		CommandListLayout r{
			.ContextArray = ByteAt(contextArray, 2),
			.ContextCount = ByteAt(contextCount, 1),
			.ContextSize = static_cast<size_t>(ctx.InRange<int32_t>(contextArray.Get<int32_t>(1), 0x100, 0x100000, "context size")),
			.Lists = listSize,
			.ListSize = listSize,
			.ListFirstBlock = static_cast<size_t>(*firstBlock),
			.ListFreeSlots = ByteAt(entryCount, 2),
			.ListBlocks = ByteAt(*prologue, 5),
			.BlockSize = size_t{1} << ctx.InRange<uint8_t>(prologue->Get<uint8_t>(6), 8, 24, "block shift"),
			.NextBlock = FieldOffset(ctx, copy, 1, "next block"),
			.EntrySize = size_t{1} << ctx.InRange<uint8_t>(entryCount.Get<uint8_t>(1), 2, 8, "entry shift"),
		};
		ctx.Require(std::cmp_equal(copy.Get<int32_t>(2), r.BlockSize) && r.NextBlock == r.BlockSize - r.EntrySize, ResolveError::Mismatch,
			"blocks of 0x{:X} bytes are copied 0x{:X} bytes at a time and linked at +0x{:X}", r.BlockSize, copy.Get<int32_t>(2), r.NextBlock);
		ctx.Require(r.ListFreeSlots + 4 <= r.ListSize && r.ListBlocks + 4 <= r.ListSize && r.ListFirstBlock + 8 <= r.ListSize, ResolveError::Invalid,
			"list fields +0x{:X}, +0x{:X} and +0x{:X} are outside a list", r.ListFirstBlock, r.ListFreeSlots, r.ListBlocks);

		return GatherUsedCommandsFunctions{
			.Gather = Address(gather),
			.Sort = Address(sort),
			.Layout = r,
		};
	});
}

// Font replacement's signatures, each matching once in .text.
namespace XivAlexander::Game::FontCode {
	// GameFontSet::PickFont, from its start: picks the font for the requested size.
	const Signatures::RegexSignature PickFont(R"([\x81\x83]\xB9(....)\x00.{0,11}?\xF3[\x40-\x4F]?\x0F\x10[\x80-\xBF](....)(?:[\x70-\x7F].|\x0F[\x80-\x8F]....)\xF3[\x40-\x4F]?\x0F\x10[\x80-\xBF](....)(?:(?:\xEB.|\xE9....)\xF3[\x40-\x4F]?\x0F\x10[\x80-\xBF](....))?.{0,40}?\x84\xD2(?:[\x70-\x7F].|\x0F[\x80-\x8F]....)[\x48-\x4F]\x8B[\x80-\xBF](....)[\x48-\x4F]\x85[\xC0-\xFF](?:[\x70-\x7F].|\x0F[\x80-\x8F]....)\xF3[\x40-\x4F]?\x0F\x10[\x80-\xBF](....).{0,80}?[\x48-\x4F]\x03[\xC0\xC9\xD2\xDB\xE4\xED\xF6\xFF][\x48-\x4F]\x83\x3C[\xC0-\xFF]\x00(?:\x74.|\x0F\x84....)\xF3[\x40-\x4F]?\x0F\x10[\x44\x4C\x54\x5C\x64\x6C\x74\x7C][\xC0-\xFF](.).{0,40}?\x66[\x40-\x4F]?\x83[\xF8-\xFF]\x08(?:[\x70-\x7F].|\x0F[\x80-\x8F]....).{0,24}?[\x40-\x4F]?\xF6[\x80-\x87](....)\x08)");
	// Inside GetGlyph: a glyph by direct index, else from the font's glyph map.
	const Signatures::RegexSignature GetGlyph(R"(\xF6[\x80-\x87](....)\x04.{0,12}?(?:[\x48-\x4F]\x8B[\x80-\xBF](....).{0,8}?)?(?:\x74.|\x0F\x84....).{0,8}?[\x40-\x4F]?\x0F\xB6[\x80-\xBF](....).{0,12}?\x81[\xF8-\xFF](?:\xFF\x00|\x00\x01)\x00\x00(?:[\x70-\x7F].|\x0F[\x80-\x8F]....).{0,12}?(?:[\x48-\x4F]\x8B[\x80-\xBF](....).{0,8}?)?[\x48-\x4F]\x8D[\x04-\x3C][\x40-\x7F].{0,8}?[\x48-\x4F]\x8D[\x04-\x3C][\x80-\xBF](?:.{0,16}?[\x48-\x4F]\x8B[\x80-\xBF](....).{0,16}?\xE8(....).{0,8}?[\x48-\x4F]\x8B[\x00-\x3F][\x48-\x4F]\x8B(?:[\x44\x4C\x54\x5C\x64\x6C\x74\x7C].|[\x40-\x43\x45-\x4B\x4D-\x53\x55-\x5B\x5D-\x63\x65-\x6B\x6D-\x73\x75-\x7B\x7D-\x7F])(.)|.{0,24}?[\x40-\x4F]?\x8B[\x80-\xBF](....).{0,24}?\x99.{0,4}?\xD1[\xF8-\xFF].{0,16}?[\x48-\x4F]\x8D[\x04-\x3C][\x40-\x7F].{0,8}?[\x48-\x4F]\x8D[\x04-\x3C][\x80-\xBF]))");
	// Inside LayOutCharacter: a character's glyph (GetGlyph's call), the secondary font, kerning and CJK spacing.
	const Signatures::RegexSignature LayOutCharacter(R"((?:\x80[\xF8-\xFF]\xE0|\x3C\xE0|\x81[\xF8-\xFF]\xE0\x00\x00\x00|\x3D\xE0\x00\x00\x00).{0,40}?(?:\x80[\xF8-\xFF]\xF0|\x3C\xF0|\x81[\xF8-\xFF]\xF0\x00\x00\x00|\x3D\xF0\x00\x00\x00).{0,60}?(?:\x80[\xF8-\xFF]\xF8|\x3C\xF8|\x81[\xF8-\xFF]\xF8\x00\x00\x00|\x3D\xF8\x00\x00\x00).{0,80}?\xE8(....)(?:.{0,40}?(?:[\x48-\x4F]\x8B[\x80-\xBF](....).{0,24}?\x93\x80\xE3\x00|\x93\x80\xE3\x00.{0,24}?[\x48-\x4F]\x8B[\x80-\xBF](....)).{0,24}?\xE8(....).{0,8}?[\x48-\x4F]\x8B[\x00-\x3F][\x48-\x4F]\x8B(?:[\x44\x4C\x54\x5C\x64\x6C\x74\x7C].|[\x40-\x43\x45-\x4B\x4D-\x53\x55-\x5B\x5D-\x63\x65-\x6B\x6D-\x73\x75-\x7B\x7D-\x7F])(.)(?:.{0,24}?[\x40-\x4F]?\xF6[\x80-\x87](....)\x04.{0,40}?[\x40-\x4F]?\x0F\xB6[\x80-\xBF](....).{0,24}?[\x48-\x4F]\x8B[\x80-\xBF](....)|.{0,24}?\xBA\x2D\x00\x00\x00.{0,16}?\xE8....).{0,80}?\x80\x90\xE3\x00|.{0,40}?\xE8(....)(?:[\x48-\x4F]\x8B[\xC0-\xFF])?[\x48-\x4F]\x85\xC0(?:\x74.|\x0F\x84....))(?:.{0,200}?[\x48-\x4F]\x8B[\x80-\xBF](....).{0,16}?[\x48-\x4F]\x85[\xC0-\xFF](?:[\x70-\x7F].|\x0F[\x80-\x8F]....).{0,24}?(?:\xAB\xAA\xAA\xAA\xAA\xAA\xAA\xAA.{0,16}?[\x48-\x4F]\x2B[\x80-\xBF](....)|[\x48-\x4F]\x2B[\x80-\xBF](....).{0,16}?\xAB\xAA\xAA\xAA\xAA\xAA\xAA\xAA).{0,40}?[\x48-\x4F]\x8D[\x04-\x3C][\x40-\x7F].{0,8}?[\x48-\x4F]\x8D[\x04-\x3C][\x80-\xBF].{0,24}?[\x48-\x4F]\x8B[\x80-\xBF](....).{0,40}?[\x48-\x4F]\x89[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24\x20.{0,16}?\xE8.....{0,24}?(?:[\x40-\x4F]?\x0F\xBE(?:[\x44\x4C\x54\x5C\x64\x6C\x74\x7C].|[\x40-\x43\x45-\x4B\x4D-\x53\x55-\x5B\x5D-\x63\x65-\x6B\x6D-\x73\x75-\x7B\x7D-\x7F])(.).{0,8}?[\x40-\x4F]?\x0F\xB6(?:[\x44\x4C\x54\x5C\x64\x6C\x74\x7C].|[\x40-\x43\x45-\x4B\x4D-\x53\x55-\x5B\x5D-\x63\x65-\x6B\x6D-\x73\x75-\x7B\x7D-\x7F])(.)|[\x40-\x4F]?\x0F\xB6(?:[\x44\x4C\x54\x5C\x64\x6C\x74\x7C].|[\x40-\x43\x45-\x4B\x4D-\x53\x55-\x5B\x5D-\x63\x65-\x6B\x6D-\x73\x75-\x7B\x7D-\x7F])(.).{0,8}?[\x40-\x4F]?\x0F\xBE(?:[\x44\x4C\x54\x5C\x64\x6C\x74\x7C].|[\x40-\x43\x45-\x4B\x4D-\x53\x55-\x5B\x5D-\x63\x65-\x6B\x6D-\x73\x75-\x7B\x7D-\x7F])(.)).{0,40}?\xF3[\x40-\x4F]?\x0F\x10[\x80-\xBF](....)|.{0,200}?[\x48-\x4F]\x89[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24\x28.{0,8}?[\x48-\x4F]\x89[\x44\x4C\x54\x5C\x64\x6C\x74\x7C]\x24\x20\xE8(....)).{0,200}?[\x40-\x4F]?\x0F[\xB6\xBE][\x80-\xBF](....).{0,24}?\x50\x7B\x1C\xFF)");
	// The text wrapper's SplitWord, from its start: splits a word that doesn't fit a line (FitUnits' call).
	const Signatures::RegexSignature SplitWord(R"(\x48\x89\x5C\x24.(?:[\x40\x41]?[\x50-\x57]){1,6}\x48\x83\xEC..{0,16}?[\x44\x45]\x8B[\x88-\x8B\x8D-\x8F](....).{0,16}?[\x44\x45]\x2B[\x88-\x8B\x8D-\x8F](....).{0,24}?\xE8(....)(?:[\x40-\x4F]?\x8B[\xC0-\xFF]\x85\xC0|\x85\xC0[\x40-\x4F]?\x8B[\xC0-\xFF])(?:\x79.|\x0F\x89....).{0,400}?(?:[\x48-\x4F]\x63|[\x40-\x4F]?\x8B)[\x80-\xBF](....).{0,24}?\x83\x3C[\x80-\xBF]\x20)");
	// AtkTextNode::FreeFontCache, from its start.
	const Signatures::RegexSignature FreeFontCache(R"(\x48\x85\xC9(?:\x74.|\x0F\x84....).{2,24}?\x0F\xB7[\x80-\xBF](....).{0,12}?\x66\x85[\xC0-\xFF](?:\x74.|\x0F\x84....).{0,12}?[\x48\x4C]\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....).{0,12}?[\x48-\x4F]\x8B[\x40-\x7F](.).{0,12}?(?:\xB8\x00\x01\x00\x00\x66[\x39\x3B][\xC0-\xFF]|\x66\x41?\x81[\xF8-\xFF]\x00\x01)(?:\x73.|\x0F\x83....).{0,12}?[\x48\x49]\xC1[\xE0-\xE7]\x04[\x48-\x4F]\x03[\x80-\xBF](....).{0,64}?\x66\x41?\x89[\x80-\xBF]\1(?:[\x48\x4C]\x8B[\x44-\x7C]\x24.|\x48\x83\xC4.|\x41?[\x58-\x5F])*?\xC3)");
	// AtkTextNode::BuildFontCache, from its start.
	const Signatures::RegexSignature BuildFontCache(R"(\x48\x85\xC9(?:\x74.|\x0F\x84....).{2,24}?\x0F\xB7[\x80-\xBF](....).{0,12}?\x66\x85[\xC0-\xFF](?:\x74.|\x0F\x84....).{0,12}?[\x48\x4C]\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....).{0,12}?[\x48-\x4F]\x8B[\x40-\x7F](.).{0,12}?(?:\xB8\x00\x01\x00\x00\x66[\x39\x3B][\xC0-\xFF]|\x66\x41?\x81[\xF8-\xFF]\x00\x01)(?:\x73.|\x0F\x83....).{0,12}?[\x48\x49]\xC1[\xE0-\xE7]\x04[\x48-\x4F]\x03[\x80-\xBF](....).{0,64}?\x66\x41?\x89[\x80-\xBF]\1[\x48\x4C]\x8B[\x80-\xBF](....)(?:[\x40-\x4F]?\x38[\x00-\x3F]|\x80[\x38-\x3F]\x00)(?:\x74.|\x0F\x84....))");
	// AtkTextNode::ToggleFontCache: the node's cache flag, then BuildFontCache's or FreeFontCache's call.
	const Signatures::RegexSignature ToggleFontCache(R"(\x48\x85\xC9(?:\x74.|\x0F\x84....).{0,20}?[\x40-\x4F]?\x0F\xB6[\x80-\xBF](....).{0,16}?\x80[\xE0-\xE7]\xBF.{0,16}?[\x40-\x4F]?\x88[\x80-\xBF]\1.{0,8}?[\x40-\x4F]?\xF6[\xC0-\xC7]\x40(?:\x74.|\x0F\x84....)\xE8(....)(?:\xEB.|\xE9....)\xE8(....)\x80[\xA0-\xA7\x60-\x67]\1\xEF)");
	// AtkFontAnalyzer's constructor: the vtables of its renderer and its render counter.
	const Signatures::RegexSignature FontAnalyzerVtables(R"(\x48\x8D\x05(....)[\x48\x49]\x89[\x80-\x83\x85-\x87](....).{0,16}?\x48\x8D\x05(....).{0,120}?[\x48\x49]\x89[\x80-\x83\x85-\x87](....).{0,60}?\x41\xB8\xA0\x00\x00\x00.{0,40}?\xE8....\xC7[\x80-\x83\x85-\x87](....)\x03\x00\x00\x00)");
	// Loading the fonts from the game's table or one of the two lobby ones.
	const Signatures::RegexSignature FontTables(R"(\x83[\xF8-\xFF]\x01(?:\x75.|\x0F\x85....)\x41?(?:\x83[\xB8-\xBF](....)\x01|\x39[\x80-\xBF](....))[\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....)[\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....)[\x48-\x4F]\x0F\x45[\xC0-\xFF](?:\xEB.|\xE9....)[\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....).{0,400}?[\x4C\x4D]\x8B(?:\x4C.|[\x48-\x4B\x4D-\x4F])(.).{0,400}?(?:[\x48\x49]\x83[\xC0-\xC7](.).{0,8}?)?\x41?\x83[\xF8-\xFF](.)(?:\x7C.|\x0F\x8C....))");
	// Inside BakePlateRenderer::AllocateBake: a plate's region of the render texture, by its text size.
	const Signatures::RegexSignature NamePlateAllocateBake(R"([\x48\x4C]\x8B[\x81\x89\x91\x99\xA1\xA9\xB1\xB9](....).{0,8}?[\x48\x4C]\x8B[\x42\x4A\x52\x5A\x62\x6A\x72\x7A](.).{0,160}?[\x48\x49]\x8D[\x88-\x8F](....).{0,8}?\xE8.{4,24}?[\x40-\x4F]?\x0F\xB7[\x40-\x7F](.).{0,8}?[\x40-\x4F]?\x0F\xB7[\x40-\x7F](.).{0,8}?\x66[\x40-\x4F]?\x83[\xC0-\xC7]\x08.{0,8}?\x66[\x40-\x4F]?\x83[\xC0-\xC7]\x04.{0,16}?\x66[\x40-\x4F]?\x03[\xC0-\xFF].{0,8}?\x66[\x40-\x4F]?\x03[\xC0-\xFF])");
	// AddonNamePlate's update calling AllocateBake for the plates to bake.
	const Signatures::RegexSignature NamePlateAllocateBakeCaller(R"((?:[\x48-\x4F]\x8B[\x80-\xBF](....)[\x48-\x4F]\x03[\xC0-\xFF].{0,80}?[\x48\x49]\x8D[\x88-\x8F](....)[\x40-\x4F]?\xC6[\x40-\x47](.)\x00[\x48-\x4F]\x8B[\xD0-\xD7]\xE8(....)[\x48-\x4F]\x83[\xC0-\xC7](.)[\x48-\x4F]\x81[\xF8-\xFF](....)|[\x48\x49]\x8D[\x88-\x8F](....).{0,16}?(?:[\x48-\x4F]\x6B[\xC0-\xFF](.)|[\x48-\x4F]\x69[\xC0-\xFF](....)).{0,8}?[\x48-\x4F]\x03[\x80-\xBF](....).{0,8}?\xE8(....)\x84\xC0(?:\x74.|\x0F\x84....)(?:[\x40-\x4F]?\xC6[\x40-\x47](.)\x00)?))");
	// AddonNamePlate's update looping over every plate's text node.
	const Signatures::RegexSignature NamePlateObjectsLoop(R"([\x48-\x4F]\x8B[\x80-\xBF](....)\x33\xD2[\x48-\x4F]\x8B[\x4C\x54].(.)\xE8....[\x48-\x4F]\x8B[\x80-\xBF]\1.{0,8}?\xE8....[\x48\x49]\x83[\xC0-\xC7](.)[\x48\x49]\x81[\xF8-\xFF](....)(?:\x7C.|\x0F\x8C....)(?:\xEB.|\xE9....))");
	// Inside BakePlateRenderer's per-node setup: the current bake data it reads from the renderer.
	const Signatures::RegexSignature NamePlateBakePrepare(R"(\x48(?:\x83\xEC.|\x81\xEC....).{0,16}?\xE8....[\x48\x49]\x83[\xB8-\xBB\xBD-\xBF](....)\x00(?:\x74.|\x0F\x84....).{0,8}?\xE8.....{0,12}?\xC7[\x40-\x47].\x00\x00\x80\x3F\xC7[\x40-\x47].\x00\x00\x80\x3F.{0,8}?[\x48\x4C]\x8B[\x80-\xBF]\1)");
	// Inside BakePlateRenderer::Draw: the plate's map lookup, then the renderer's flag that draws it live instead of baked.
	const Signatures::RegexSignature NamePlateDraw(R"(\x80[\x78-\x7F]\x19\x00(?:\x75.|\x0F\x85....).{0,16}?\x80[\xB8-\xBB\xBD-\xBF](....)\x00.{0,16}?(?:\x75.|\x0F\x85....)[\x48-\x4F]\x3B[\xC0-\xFF](?:\x74.|\x0F\x84....))");
	// The constructor storing AtkTextNodeRenderer's vtable at +0x28, then its scale; the next renderer's vtable is stored before or after it.
	const Signatures::RegexSignature TextNodeRendererVtable(R"(\x48\x8D\x05(....).{0,8}?[\x48\x4C]\x89[\x40-\x7F]\x28.{0,40}?\xC7[\x40-\x47]\x50\x00\x00\x80\x3F)");
	// Inside BakePlateRenderer::DrawBaked: a baked region at the node's transform.
	const Signatures::RegexSignature NamePlateDrawBaked(R"(\xF3[\x40-\x4F]?\x0F\x10[\x42\x4A\x52\x5A\x62\x6A\x72\x7A](.).{0,8}?\xF3[\x40-\x4F]?\x0F\x10[\x42\x4A\x52\x5A\x62\x6A\x72\x7A](.).{0,300}?[\x40-\x4F]?\x0F\xB7[\x80-\xBF](....).{0,48}?\xE8....[\x40-\x4F]?\x0F\xB7[\x80-\xBF](....).{0,8}?[\x40-\x4F]?\x0F\xBF[\x40-\x7F](.).{0,8}?\x83[\xC0-\xC7]\x08.{0,400}?[\x40-\x4F]?\x0F\xB6[\x40-\x7F](.)(?:[\x40-\x4F]?\x88[\x40-\x7F].){4}.{0,200}?[\x40-\x4F]?\x0F\x2E.{1,6}?(?:\x7A.|\x0F\x8A....)(?:\x75.|\x0F\x85....)[\x40-\x4F]?[\xB8-\xBF]\x00\x00\x18\x00)");
	// AtkServer::LoadShaders' loop over the shader pairs.
	const Signatures::RegexSignature AtkServerLoadShadersLoop(R"([\x48\x49]\x81[\xC0-\xC7](....)[\x40\x41]?[\xB8-\xBF](\x09\x00\x00\x00).{0,16}?[\x48\x49]\x8B[\x80-\xBF](....)[\x48\x49]\x8B[\x88-\x8F](....)[\x48\x49]\x85\xC9(?:\x74.|\x0F\x84....)\x48\x8B\x01\xFF\x50..{0,32}?[\x48\x49]\x8B(?:[\x80-\xBF](....)|[\x40-\x7F](.))[\x48\x49]\x89[\x08-\x0F\x48-\x4F\x88-\x8F].{0,32}?[\x48\x49]\x8B[\x48-\x4F](.)[\x48\x49]\x85\xC9)");
	// Inside AtkServer::BindShaders: a pass's vertex and pixel shaders.
	const Signatures::RegexSignature AtkServerBindFontShaders(R"([\x48-\x4F]\x8B[\x84\x8C\x94\x9C\xA4\xAC\xB4\xBC]([\xC0-\xFF])(....).{0,16}?[\x48-\x4F]\x89[\x40-\x47\x80-\x87].{0,24}?[\x40-\x4F]?\x84[\xC0-\xFF](?:\x74.|\x0F\x84....)[\x48-\x4F]\x8B[\x84\x8C\x94\x9C\xA4\xAC\xB4\xBC]\1(....)(?:[\x48-\x4F]\x89[\x40-\x47\x80-\x87].{0,16}?)?(?:\xEB.|\xE9....|\xC3)[\x48-\x4F]\x8B[\x84\x8C\x94\x9C\xA4\xAC\xB4\xBC]\1(....))");
	// Inside Kernel::ImmediateContext::SetPixelShader.
	const Signatures::RegexSignature KernelSetPixelShader(R"([\x48\x4C]\x8B[\x81\x89\x91\x99\xA1\xA9\xB1\xB9](....)[\x48\x4C]\x8D[\x81\x89\x91\x99\xA1\xA9\xB1\xB9]....[\x40-\x4F]?\x8B[\x42\x4A\x52\x5A\x6A\x72\x7A](.).{0,400}?[\x48\x49]\x8B[\x50-\x53\x55-\x57](.).{0,16}?\x48\x8B\x01\xFF\x50\x48[\x4C\x4D\x48\x49]\x89(?:[\x80-\x83\x85-\xBF]|[\x84\x8C\x94\x9C\xA4\xAC\xB4\xBC]\x24)\1)");
	// Inside AtkFontManager::BuildFont: a font's textures, header and kerning from its FDT.
	const Signatures::RegexSignature BuildFont(R"(\x66[\x3B\x39][\x40-\x7F](.)(?:\x73.|\x0F\x83....).{0,40}?[\x48-\x4F]\x69[\xC0-\xFF](....)[\x48-\x4F]\x03[\x40-\x7F](.).{0,8}?[\x40-\x4F]?\xF6[\x80-\xBF](....)\x02.{0,80}?\x66[\x40-\x4F]?[\x3B\x39][\x80-\xBF](....)(?:\x73.|\x0F\x83....).{0,24}?[\x48-\x4F]\x8B[\x04\x0C\x14\x1C\x24\x2C\x34\x3C][\xC0-\xC4\xC6-\xCC\xCE-\xD4\xD6-\xDC\xDE-\xE4\xE6-\xEC\xEE-\xF4\xF6-\xFC\xFE\xFF][\x48-\x4F]\x8B[\x80-\xBF](....)[\x48-\x4F]\x89[\x44\x4C\x54\x5C\x64\x6C\x74\x7C][\xC0-\xFF](.).{0,40}?[\x48-\x4F]\x8B[\x40-\x7F](.)[\x48-\x4F]\x8B[\x00-\x3F]\xFF[\x90-\x97].....{0,800}?[\x40-\x4F]?\x0F\x10[\x40-\x7F]\x04[\x40-\x4F]?\x0F\x11[\x80-\xBF](....)\xF2[\x40-\x4F]?\x0F\x10[\x40-\x7F]\x14\xF2[\x40-\x4F]?\x0F\x11[\x80-\xBF](....)[\x40-\x4F]?\x8B[\x40-\x7F]\x1C[\x40-\x4F]?\x89[\x80-\xBF](....).{0,60}?[\x48-\x4F]\x89[\x80-\xBF](....).{0,400}?[\x40-\x4F]?\x8B[\x40-\x7F]\x04[\x40-\x4F]?\x89[\x80-\xBF](....).{0,60}?[\x48-\x4F]\x89[\x80-\xBF](....).{0,60}?[\x48-\x4F]\x8D[\x40-\x7F]\x10.{0,60}?[\x40-\x4F]?\x8B[\x00-\x03\x06-\x0B\x0E-\x13\x16-\x1B\x1E-\x23\x26-\x2B\x2E-\x33\x36-\x3B\x3E\x3F][\x48-\x4F]\x8D[\x40-\x7F]\x10[\x40-\x4F]?\x89[\x00-\x03\x06-\x0B\x0E-\x13\x16-\x1B\x1E-\x23\x26-\x2B\x2E-\x33\x36-\x3B\x3E\x3F][\x48-\x4F]\x8D[\x40-\x7F](.).{0,200}?[\x40-\x4F]?\x0F\xB6[\x80-\xBF](....).{0,16}?[\x40-\x4F]?\x80[\xE0-\xE7]\xFB(?:[\x40-\x4F]?\x80[\xC8-\xCF]\x01)?(?:[\x40-\x4F]?\x88[\x80-\xBF]\16)?[\x40-\x4F]?\x88[\x80-\xBF](....).{0,60}?[\x40-\x4F]?\x8B[\x00-\x03\x06-\x0B\x0E-\x13\x16-\x1B\x1E-\x23\x26-\x2B\x2E-\x33\x36-\x3B\x3E\x3F]\x81[\xF8-\xFF]\xFF\x00\x00\x00(?:\x77.|\x0F\x87....).{0,24}?[\x48-\x4F]\x83[\xC0-\xC7](.))");
	// Inside the font renderer's quad of a glyph: its box and the font's texture width.
	const Signatures::RegexSignature GlyphQuad(R"(\x0F\xB6[\x40-\x7F](.).{0,40}?[\x40-\x4F]?\x0F\xB6[\x40-\x7F](.).{0,40}?[\x40-\x4F]?\x0F\xBE[\x40-\x7F](.).{0,40}?[\x40-\x4F]?\x0F\xBE[\x40-\x7F](.).{0,400}?[\x40-\x4F]?\x84[\xC0-\xFF](?:\x75.|\x0F\x85....)[\x40-\x4F]?\x0F\xB6[\x40-\x7F](.).{0,8}?[\x40-\x4F]?\x0F\xB7[\x80-\xBF](....).{0,40}?[\x40-\x4F]?\x80[\xE0-\xE7]\x03.{0,40}?[\x40-\x4F]?\xC1[\xE8-\xEF]\x02.{0,400}?[\x40-\x4F]?[\xB8-\xBF]\xFF\x0F\x00\x00.{0,200}?(?:\x66[\x40-\x4F]?\x23[\x40-\x7F](.).{0,8}?[\x40-\x4F]?\x8B[\x40-\x7F]\7|[\x40-\x4F]?\x8B[\x40-\x7F](.).{0,8}?[\x40-\x4F]?\x0F\xB7[\x40-\x7F]\8).{0,80}?[\x40-\x4F]?\xC1[\xE8-\xEF]\x0C)");
	// AtkTextNodeRenderer::Draw taking the node's font set from AtkStage's font manager.
	const Signatures::RegexSignature TextNodeFontSet(R"(\x0F\xB6[\x80-\xBF](....).{0,8}?[\x48-\x4F]?\xC1[\xE8-\xEF]\x04.{0,8}?[\x48-\x4F]\x69[\xC0-\xFF](....)[\x48\x4C]\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....).{0,16}?[\x48-\x4F]\x8B[\x40-\x7F](.).{0,8}?[\x48-\x4F]\x03[\x00-\x03\x06-\x0B\x0E-\x13\x16-\x1B\x1E-\x23\x26-\x2B\x2E-\x33\x36-\x3B\x3E\x3F].{0,8}?\xFF[\x50-\x57]\x18)");
	// AtkTextNodeRenderer's per-node setup: the requested size and the node's scale.
	const Signatures::RegexSignature TextNodeSetUp(R"(\x0F\xB6[\x80-\xBF](....).{0,40}?\x0F\x5B[\xC0-\xFF]\xF3[\x40-\x4F]?\x0F\x11[\x80-\xBF](....)[\x40-\x4F]?\x0F\xB6[\x80-\xBF]\1.{0,12}?\x0F\x5B[\xC0-\xFF]\xF3[\x40-\x4F]?\x0F\x11[\x80-\xBF](....).{0,40}?\xF3[\x40-\x4F]?\x0F\x11[\x80-\xBF](....).{0,24}?\xF3[\x40-\x4F]?\x0F\x11[\x80-\xBF](....))");
	// Inside AtkFontManager's initialization: the fonts and the font sets.
	const Signatures::RegexSignature InitFontManager(R"(\x66[\x40-\x4F]?\x89[\x40-\x7F](.).{0,24}?[\x48-\x4F]\x69[\xC0-\xFF](....).{0,40}?[\x48-\x4F]\x89[\x40-\x7F](.).{0,40}?\xB9(....)\xE8.....{0,24}?\x41?[\xB8-\xBF]([\x01-\x3F]\x00\x00\x00).{0,300}?[\x48-\x4F]\x81[\xC0-\xC7](....).{0,40}?[\x48-\x4F]\x89[\x00-\x03\x06-\x0B\x0E-\x13\x16-\x1B\x1E-\x23\x26-\x2B\x2E-\x33\x36-\x3B\x3E\x3F])");
	// Filling the font sets' slots, and each font's italic correction.
	const Signatures::RegexSignature SetUpFontSets(R"(\x83[\x78-\x7F].\x02(?:\x75.|\x0F\x85....).{0,24}?[\x48-\x4F]\x8B[\x00-\x03\x06-\x0B\x0E-\x13\x16-\x1B\x1E-\x23\x26-\x2B\x2E-\x33\x36-\x3B\x3E\x3F].{0,200}?[\x40-\x4F]?\x8D[\x40-\x7F]([\x04-\x10]).{0,60}?[\x48-\x4F]\x81[\xC0-\xC7].....{0,24}?[\x48-\x4F]\x89[\x00-\x03\x06-\x0B\x0E-\x13\x16-\x1B\x1E-\x23\x26-\x2B\x2E-\x33\x36-\x3B\x3E\x3F].{0,8}?[\x40-\x4F]?\xC6[\x80-\x87](....)[\x01-\x08].{0,8}?[\x48-\x4F]\x89[\x80-\xBF](....).{0,8}?\xC7[\x40-\x47](.)....\xC7[\x40-\x47](.)\x00\x00\x80\x3F.{0,60}?[\x48-\x4F]\x8D[\x40-\x7F](.).{0,40}?\x80[\xA0-\xA7](....)\xFD)");
	// AtkModule reloading the UI shaders (AtkServer::LoadShaders) and the fonts.
	const Signatures::RegexSignature AtkModuleReloadFontShaders(R"([\x48\x49]\x8B[\x88-\x8B\x8D-\x8F](....)[\x48\x49]\x85\xC9(?:\x74.|\x0F\x84....)\xE8(....).{0,16}?\xBA([\x01-\x7F]\x00\x00\x00).{0,8}?[\x48\x49]\x8D[\x88-\x8B\x8D-\x8F](....).{0,8}?[\x4C\x4D]\x8D[\x80-\x83\x85-\x87]\1.{0,8}?\xE8(....))");
	// Loading a font of the font manager: its texture count, against the most it can have.
	const Signatures::RegexSignature LoadFont(R"(\x66[\x3B\x39][\x40-\x7F](.)(?:\x73.|\x0F\x83....|\x72.|\x0F\x82....).{0,40}?[\x48-\x4F]\x69[\xC0-\xFF](....)[\x48-\x4F]\x03[\x40-\x7F](.)[\x40-\x4F]?\xF6[\x80-\xBF](....)\x01.{0,40}?\x41?\x83[\x38-\x3F](.)(?:\x7C.|\x0F\x8C....).{0,24}?\x83[\xB8-\xBF](....)\x00(?:\x74.|\x0F\x84....).{0,200}?\x66[\x40-\x4F]?\x89[\x80-\xBF](....).{0,16}?[\x40-\x4F]?\x88[\x80-\xBF](....))");
	// Inside AtkUnitBase::CountCollisionNodes: the node list, recursing into components.
	const Signatures::RegexSignature CountCollisionNodes(R"(\x0F\xB7[\x40-\x7F](.).{0,12}?[\x48-\x4F]\x8B[\x40-\x7F](.).{0,60}?(?:\xE8\x03\x00\x00.{0,60}?\x66[\x40-\x4F]?\x39[\x40-\x7F](.)|\x66[\x40-\x4F]?\x81[\x78-\x7F](.)\xE8\x03)(?:\x72.|\x0F\x82....)[\x48-\x4F]\x8B[\x80-\xBF](....)(?:\xEB.[\x31\x33][\xC0-\xFF])?[\x48-\x4F]\x85[\xC0-\xFF](?:\x74.|\x0F\x84....)(?:[\x48-\x4F]\x83[\xC0-\xC7](.)|[\x48-\x4F]\x8D[\x40-\x7F](.)).{0,8}?\xE8(....))");
	// Inside AtkUnitBase::UpdateCollisionNodeList: the unit's own ULD manager.
	const Signatures::RegexSignature UpdateCollisionNodeList(R"([\x48\x49]\x8D[\x50-\x57](.).{0,16}?\xE8(....)\x89[\x80-\xBF](....)\x85\xC0(?:\x74.|\x0F\x84....).{0,12}?\xE8.....{0,8}?\x8B[\x80-\xBF]\3)");
	// AtkUnitBase::SetScale, from its start: the loaded units, through AtkStage's unit manager.
	const Signatures::RegexSignature SetScale(R"([\x48-\x4F]\x85\xC9(?:\x74.|\x0F\x84....).{0,60}?[\x48\x4C]\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....).{0,30}?[\x48-\x4F]\x8B[\x40-\x7F](.).{0,40}?\x0F\xB7[\x80-\xBF](....)(?:\xEB.)\x0F\xB7[\x80-\xBF](....).{0,40}?(?:\x0F[\xB7\xBF]|\x66[\x40-\x4F]?[\x39\x3B])[\x80-\xBF](....).{0,40}?(?:\x00\x01\x00\x00|\x81[\xF8-\xFF]\x00\x01).{0,30}?[\x48-\x4F]\x8B[\x84-\xBC][\xC0-\xFF](....).{0,40}?\x66[\x40-\x4F]?\x39[\x80-\xBF]\4)");
	// Inside ResourceHandle's destructor: its file name, an MSVC std::string.
	const Signatures::RegexSignature ResourceHandleDtor(R"([\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....)[\x48-\x4F]\x8B[\xC0-\xFF][\x48-\x4F]\x89[\x00-\x3F].{0,8}?\xB8\x01\x00\x00\x00\x87[\x80-\xBF](....).{0,80}?\xF7[\x40-\x7F](.)\x00\x00\x00\x0C.{0,200}?[\x48-\x4F]\x8B[\x40-\x7F](.)[\x48-\x4F]\x83[\xF8-\xFF]\x10(?:\x72.|\x0F\x82....)[\x48-\x4F]\x8B[\x40-\x7F](.).{0,24}?\xE8....(?:[\x48-\x4F]\x89[\x40-\x7F](.)[\x48-\x4F]\xC7[\x40-\x47]\4\x0F\x00\x00\x00|[\x48-\x4F]\xC7[\x40-\x47]\4\x0F\x00\x00\x00.{0,8}?[\x48-\x4F]\x89[\x40-\x7F](.)).{0,24}?[\x40-\x4F]?\x88[\x40-\x7F]\5)");
	// Inside Kernel::Texture's creation of its views: the device and the D3D11 texture.
	const Signatures::RegexSignature TextureCreateViews(R"((?:[\x48\x4C]\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....).{0,24}?(?:\x4C\x8D[\x48-\x4F](.)[\x48\x49]\x8B[\x50-\x57](.)|[\x48\x49]\x8B[\x50-\x57](.)\x4C\x8D[\x48-\x4F](.))|(?:\x4C\x8D[\x48-\x4F](.)[\x48\x49]\x8B[\x50-\x57](.)|[\x48\x49]\x8B[\x50-\x57](.)\x4C\x8D[\x48-\x4F](.)).{0,10}?[\x48\x4C]\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D](....)).{0,8}?[\x48-\x4F]\x8B[\x88-\x8F](....)[\x48-\x4F]\x8B[\x00-\x3F]\xFF[\x50-\x57]\x38)");
	// Inside Device::CreateTexture2D: allocating and filling a Kernel::Texture.
	const Signatures::RegexSignature CreateTexture2D(R"(\xBA(....).{0,30}?\xFF[\x50-\x57]\x10.{0,40}?\xE8(....).{0,120}?[\x48-\x4F]\x89[\x40-\x7F](.).{0,4}?[\x48-\x4F]\x8B[\x00-\x3F][\x48-\x4F]\x89[\x40-\x7F](.)[\x48-\x4F]\x89[\x40-\x7F](.).{0,16}?\xC7[\x40-\x47](.)\x01\x00\x00\x00.{0,8}?[\x40-\x4F]?\x88[\x40-\x7F](.).{0,8}?[\x40-\x4F]?\x89[\x40-\x7F](.)[\x40-\x4F]?\x89[\x40-\x7F](.)\xC6[\x40-\x47](.)\x01[\x40-\x4F]?\xF7[\xC0-\xC7]\x00\x18\x00\x00(?:\x75.|\x0F\x85....).{0,8}?\xE8(....))");
	// AtkResNode::SetScale's body: both scales stored.
	const Signatures::RegexSignature AtkResNodeSetScale(R"([\x40-\x4F]?\x8B[\x81\x89\x91\x99\xA9\xB1\xB9](....)(?:[\x40-\x4F]?\x8B[\xC0-\xFF])?(?:\xA8\x01|\xF6[\xC0-\xC7]\x01)(?:\x75.|\x0F\x85....)\xF3[\x40-\x4F]?\x0F\x10[\x40-\x7F](.)[\x40-\x4F]?\x0F\x2E[\xC0-\xFF](?:\x7A.|\x0F\x8A....)(?:\x75.|\x0F\x85....)\xF3[\x40-\x4F]?\x0F\x10[\x40-\x7F](.)[\x40-\x4F]?\x0F\x2E[\xC0-\xFF](?:\x7A.|\x0F\x8A....)(?:\x74.|\x0F\x84....).{0,40}?(?:\xF3[\x40-\x4F]?\x0F\x11[\x40-\x7F]\2.{0,8}?\xF3[\x40-\x4F]?\x0F\x11[\x40-\x7F]\3|\xF3[\x40-\x4F]?\x0F\x11[\x40-\x7F]\3.{0,8}?\xF3[\x40-\x4F]?\x0F\x11[\x40-\x7F]\2).{0,24}?\xF3[\x40-\x4F]?\x0F\x10[\x40-\x7F](.)[\x40-\x4F]?\x0F\x57[\xC0-\xFF][\x40-\x4F]?\x0F\x2E[\xC0-\xFF](?:\x7A.|\x0F\x8A....)(?:\x75.|\x0F\x85....)(?:\x83\xA1\1\xFB|[\x40-\x4F]?\x83[\xE0-\xE7]\xFB[\x40-\x4F]?\x89[\x81\x89\x91\x99\xA9\xB1\xB9]\1))");
	// Inside AtkResNode::DispatchEvent: the walk up the parent nodes.
	const Signatures::RegexSignature AtkResNodeDispatchEvent(R"(\x48\x85\xC9(?:[\x74\x75].|\x0F[\x84\x85]....).{0,16}?\xF6\x42\x02\x01.{0,8}?(?:\x74.|\x0F\x84....).{0,16}?[\x48-\x4F]\x8B[\x40-\x7F](.)[\x48-\x4F]\x85[\xC0-\xFF](?:\x74.|\x0F\x84....).{0,16}?\x0F\xB6[\x40-\x7F](.)\x3B[\xC0-\xFF](?:\x74.|\x0F\x84....)[\x48-\x4F]\x8B[\x40-\x7F](.)[\x48-\x4F]\x85[\xC0-\xFF](?:\x75.|\x0F\x85....).{0,16}?[\x48\x49]\x8D[\x48-\x4F]\1.{0,8}?\xE8(....)\x84\xC0(?:\x75.|\x0F\x85....)[\x48-\x4F]\x8B[\x40-\x7F](.)[\x48-\x4F]\x85[\xC0-\xFF](?:\x75.|\x0F\x85....))");
	// The font renderer shearing an italic glyph's quad: the analyzer state's flags.
	const Signatures::RegexSignature ItalicQuad(R"(\xF7[\x40-\x47](.)\x00\x00\x00\x08.{0,40}?\x74.\xF3[\x40-\x4F]?\x0F\x10[\x80-\xBF](....))");
	// The <italic> macro setting the analyzer state's italic flag.
	const Signatures::RegexSignature ItalicMacro(R"(\x0F\xBA[\xF0-\xF7]\x1B.{0,32}?\x0F\xBA[\xE0-\xE7]\x1B(?:\x72.|\x0F\x82....)\x0F\xBA[\xE8-\xEF]\x1E[\x40-\x4F]?\x89[\x40-\x7F](.))");
}

namespace XivAlexander::Game::Resolved::Fonts {
	namespace {
		// A capture as a little-endian signed integer, from the first of its groups that matched (they are alternatives of the pattern).
		std::optional<int32_t> TryCaptured(const ScanResult& m, std::initializer_list<size_t> groups) {
			for (const auto group : groups) {
				const auto& g = m.Match()[group];
				if (!g.matched)
					continue;
				switch (g.length()) {
					case 1: return m.Get<int8_t>(group);
					case 2: return m.Get<int16_t>(group);
					case 4: return m.Get<int32_t>(group);
					default: return std::nullopt;
				}
			}
			return std::nullopt;
		}

		int32_t Captured(const ScanResult& m, std::initializer_list<size_t> groups, std::string_view what) {
			if (const auto value = TryCaptured(m, groups))
				return *value;
			ResolveContext::Fail(ResolveError::Invalid, std::format("{} not captured", what));
		}

		// Where a captured rel32 points.
		template<typename T = void*>
		T CapturedTarget(const ScanResult& m, std::initializer_list<size_t> groups, std::string_view what) {
			for (const auto group : groups) {
				if (m.Match()[group].matched)
					return m.ResolveAddress<T>(group);
			}
			ResolveContext::Fail(ResolveError::Invalid, std::format("{} not captured", what));
		}

		void* FunctionOf(const ResolveContext& ctx, const ScanResult& m, std::string_view what) {
			return Address(ctx.FunctionContaining(m.begin(0), what).data());
		}

		// A signature that may be missing, or no longer unique, without failing the part.
		std::optional<ScanResult> TryUnique(const ResolveContext& ctx, const RegexSignature& signature) {
			auto all = ctx.All(signature, ctx.Text());
			if (all.size() != 1)
				return std::nullopt;
			return std::move(all.front());
		}

		void* CalledFunction(const ResolveContext& ctx, const ScanResult& m, std::initializer_list<size_t> groups, std::string_view what) {
			return Address(ctx.FunctionStartingAt(CapturedTarget<const void*>(m, groups, what), what).data());
		}

		std::vector<std::span<const uint8_t>> FunctionRuns(const ResolveContext& ctx, const void* function, std::string_view what) {
			auto parts = FunctionParts(ctx, ctx.FunctionStartingAt(function, what).data());
			std::ranges::sort(parts, {}, [](const auto& part) { return part.data(); });

			std::vector<std::span<const uint8_t>> runs;
			for (const auto part : parts) {
				if (!runs.empty() && runs.back().data() + runs.back().size() == part.data())
					runs.back() = {runs.back().data(), runs.back().size() + part.size()};
				else
					runs.push_back(part);
			}
			return runs;
		}

		std::vector<const void*> CalleesOf(const ResolveContext& ctx, const void* function, std::string_view what) {
			std::vector<const void*> callees;
			for (const auto run : FunctionRuns(ctx, function, what)) {
				for (const auto& call : RelativeCall.Lookup(run, RegexSignature::FromNextByte)) {
					if (const auto target = call.ResolveAddress<const void*>(1); ctx.InSection(target, ".text") && !ctx.FunctionStartingAt(target).empty())
						callees.push_back(target);
				}
			}
			std::ranges::sort(callees);
			callees.erase(std::ranges::unique(callees).begin(), callees.end());
			return callees;
		}

		std::vector<const void*> FunctionsAfter(const ResolveContext& ctx, const void* function, size_t count) {
			const auto base = reinterpret_cast<const uint8_t*>(*ctx.Module());
			const auto image = xivres::pe_image::from_loaded(base);
			const auto table = image.function_table();

			std::vector<const void*> functions;
			for (auto it = std::ranges::upper_bound(table, static_cast<uint32_t>(static_cast<const uint8_t*>(function) - base), {}, &xivres::pe_image::runtime_function::BeginAddress);
				it != table.end() && functions.size() < count; ++it) {
				if (image.primary_of(*it).BeginAddress == it->BeginAddress)
					functions.push_back(base + it->BeginAddress);
			}
			return functions;
		}

		std::vector<const void*> FunctionsBefore(const ResolveContext& ctx, const void* function, size_t count) {
			const auto base = reinterpret_cast<const uint8_t*>(*ctx.Module());
			const auto image = xivres::pe_image::from_loaded(base);
			const auto table = image.function_table();

			std::vector<const void*> functions;
			for (auto it = std::ranges::lower_bound(table, static_cast<uint32_t>(static_cast<const uint8_t*>(function) - base), {}, &xivres::pe_image::runtime_function::BeginAddress);
				it != table.begin() && functions.size() < count;) {
				--it;
				if (image.primary_of(*it).BeginAddress == it->BeginAddress)
					functions.push_back(base + it->BeginAddress);
			}
			return functions;
		}

		ScanResult UniqueIn(const ResolveContext& ctx, const RegexSignature& signature, std::span<const void* const> functions, std::string_view what) {
			std::optional<ScanResult> found;
			for (const auto function : functions) {
				for (const auto run : FunctionRuns(ctx, function, what)) {
					for (auto& m : ctx.All(signature, run)) {
						ctx.Require(!found, ResolveError::Ambiguous, "{} found more than once in the code searched", what);
						found = std::move(m);
					}
				}
			}
			ctx.Require(found.has_value(), ResolveError::NotFound, "{} not found in the code searched", what);
			return std::move(*found);
		}

		ScanResult UniqueIn(const ResolveContext& ctx, const RegexSignature& signature, const void* function, std::string_view what) {
			return UniqueIn(ctx, signature, std::span<const void* const>(&function, 1), what);
		}

		std::optional<ScanResult> TryUniqueIn(const ResolveContext& ctx, const RegexSignature& signature, const void* function) {
			if (ctx.FunctionStartingAt(function).empty())
				return std::nullopt;
			std::vector<ScanResult> all;
			for (const auto run : FunctionRuns(ctx, function, "function")) {
				for (auto& m : ctx.All(signature, run))
					all.push_back(std::move(m));
			}
			if (all.size() != 1)
				return std::nullopt;
			return std::move(all.front());
		}

		ScanResult UniqueInSlots(const ResolveContext& ctx, void* const* vtable, const RegexSignature& signature, std::string_view what) {
			const auto slots = ctx.Vtable(vtable, 8, what);
			return ctx.UniqueSlot(slots, what, [&](size_t, std::span<const uint8_t> fn) { return ctx.Find(signature, fn); }).second;
		}
	}

	std::string to_string(const ModuleInitFunctions& value) {
		return std::format("LoadShaders {}, InitFontManager {}, AtkServer +0x{:X}, font manager +0x{:X}",
			Signatures::Describe(value.LoadShaders), Signatures::Describe(value.InitFontManager), value.Server, value.FontManager);
	}

	std::string to_string(const StageLayout& value) {
		return std::format("instance {}, font manager +0x{:X}", Signatures::Describe(value.Instance), value.FontManager);
	}

	std::string to_string(const FontManagerLayout& value) {
		return std::format("fonts +0x{:X} (0x{:X} bytes each), font count +0x{:X}, {} font sets of 0x{:X} bytes",
			value.Fonts, value.FontSize, value.FontCount, value.FontSetCount, value.FontSetSize);
	}

	std::string to_string(const FontSetLayout& value) {
		return std::format("PickFont {}, scaled size +0x{:X}, node scale +0x{:X} +0x{:X}, current font +0x{:X} size +0x{:X}, draw flags {}",
			Signatures::Describe(value.PickFont), value.ScaledSizeY, value.NodeScaleX, value.NodeScaleY, value.CurrentFont, value.CurrentFontSize, Signatures::Describe(value.DrawFlags));
	}

	std::string to_string(const FontLayout& value) {
		return std::format("GetGlyph {}, LayOutCharacter {}, BuildFont {}, {} textures +0x{:X}, kerning count +0x{:X}, texture width +0x{:X}, size +0x{:X}, ascent +0x{:X}, "
			"glyph map +0x{:X}, secondary +0x{:X} ratio +0x{:X}, texture count +0x{:X}, flags +0x{:X}, italic correction {}",
			Signatures::Describe(value.GetGlyph), Signatures::Describe(value.LayOutCharacter), Signatures::Describe(value.BuildFont), value.MaxTextures, value.Textures,
			value.KerningCount, value.TextureWidth, value.Size, value.Ascent, value.GlyphMap, value.Secondary, value.SecondaryRatio, value.TextureCount, value.Flags,
			Signatures::Describe(value.XShift));
	}

	std::string to_string(const UiLayout& value) {
		return std::format("unit manager +0x{:X}, loaded units +0x{:X} count +0x{:X}, unit ULD +0x{:X}, node list +0x{:X} count +0x{:X}, node type +0x{:X}, component +0x{:X} ULD +0x{:X}",
			value.StageUnitManager, value.LoadedUnitsEntries, value.LoadedUnitsCount, value.UnitUld, value.UldNodeList, value.UldNodeListCount, value.NodeType,
			value.ComponentNodeComponent, value.ComponentUld);
	}

	std::string to_string(const ResourceHandleLayout& value) {
		return std::format("file name +0x{:X}, length +0x{:X}, capacity +0x{:X}", value.FileName, value.FileNameLength, value.FileNameCapacity);
	}

	std::string to_string(const TextureFunctions& value) {
		return std::format("device {}, CreateTexture2D {}, D3D11 texture +0x{:X}",
			Signatures::Describe(value.Device), Signatures::Describe(value.CreateTexture2D), value.D3D11Texture2D);
	}

	std::string to_string(const ReplacerFunctions& value) {
		return std::format("BuildFontCache {}, FreeFontCache {}, renderer vtable {}, render count vtable {}, font cache slot +0x{:X} flags +0x{:X}, state flags {}",
			Signatures::Describe(value.BuildFontCache), Signatures::Describe(value.FreeFontCache), Signatures::Describe(value.RendererVtable),
			Signatures::Describe(value.RenderCountVtable), value.FontCacheSlot, value.FontCacheFlags, Signatures::Describe(value.StateFlags));
	}

	std::string to_string(const FontTablesLayout& value) {
		return std::format("game {}, lobby {} {}, {} entries of 0x{:X} bytes, FDT name +0x{:X}",
			Signatures::Describe(value.Game), Signatures::Describe(value.LobbyA), Signatures::Describe(value.LobbyB), value.Count, value.EntrySize, value.FdtName);
	}

	std::string to_string(const NamePlateFunctions& value) {
		return std::format("AllocateBake {}, BakePrepare {}, DrawBaked {}, bake plate +0x{:X}, {} objects of 0x{:X} bytes +0x{:X} (name text +0x{:X}, text size +0x{:X} +0x{:X}, "
			"needs baking +0x{:X}), bake text Y +0x{:X} alpha +0x{:X}, current bake +0x{:X}, node transform +0x{:X} size +0x{:X} +0x{:X} parent +0x{:X} scale +0x{:X}, "
			"Draw {}, fixed font resolution +0x{:X}",
			Signatures::Describe(value.AllocateBake), Signatures::Describe(value.BakePrepare), Signatures::Describe(value.DrawBaked), value.AddonBakePlate,
			value.ObjectCount, value.ObjectSize, value.AddonObjects, value.ObjectNameText, value.ObjectTextW, value.ObjectTextH, value.ObjectNeedsToBeBaked,
			value.BakeTextYOffset, value.BakeAlpha, value.RendererCurrentBakeData, value.NodeTransform, value.NodeWidth, value.NodeHeight, value.NodeParent, value.NodeScaleX,
			Signatures::Describe(value.Draw), value.RendererFixedFontResolution);
	}

	std::string to_string(const ShaderTablesLayout& value) {
		return std::format("BindShaders {}, pixel shader files +0x{:X}, pixel shaders +0x{:X}, D3D shader +0x{:X}",
			Signatures::Describe(value.BindFontShaders), value.PixelShaderFiles, value.PixelShaders, value.D3DShader);
	}

	std::string to_string(const EdgeShaderFunctions& value) {
		return std::format("LoadShaders {}, AtkServer +0x{:X}, font manager +0x{:X}", Signatures::Describe(value.LoadShaders), value.ModuleServer, value.ModuleFontManager);
	}

	std::string to_string(const LineBreakerFunctions& value) {
		return std::format("SplitWord {}, FitUnits {}, units consumed +0x{:X} max +0x{:X} used +0x{:X}",
			Signatures::Describe(value.SplitWord), Signatures::Describe(value.FitUnits), value.ConsumedUnits, value.MaxUnits, value.UsedUnits);
	}

	const Signatures::ComplexSignature<ModuleInitFunctions> ModuleInit("Fonts.ModuleInit", [](ResolveContext& ctx) {
		const auto m = ctx.Unique(FontCode::AtkModuleReloadFontShaders, ctx.Text(), "shader reload");
		return ModuleInitFunctions{
			.LoadShaders = CalledFunction(ctx, m, {2}, "LoadShaders"),
			.InitFontManager = CalledFunction(ctx, m, {5}, "InitFontManager"),
			.Server = Captured(m, {1}, "AtkServer"),
			.FontManager = Captured(m, {4}, "font manager"),
		};
	});

	const Signatures::ComplexSignature<void* const*> TextNodeRenderer("Fonts.TextNodeRenderer", [](ResolveContext& ctx) {
		return CapturedTarget<void* const*>(ctx.Unique(FontCode::TextNodeRendererVtable, ctx.Text(), "AtkTextNodeRenderer vtable"), {1}, "AtkTextNodeRenderer vtable");
	});

	const Signatures::ComplexSignature<void*> NamePlateUpdate("Fonts.NamePlateUpdate", [](ResolveContext& ctx) {
		return FunctionOf(ctx, ctx.Unique(FontCode::NamePlateAllocateBakeCaller, ctx.Text(), "AllocateBake's caller"), "AddonNamePlate's update");
	});

	const Signatures::ComplexSignature<StageLayout> Stage("Fonts.Stage", [](ResolveContext& ctx) {
		const auto m = UniqueInSlots(ctx, ctx.Get(TextNodeRenderer), FontCode::TextNodeFontSet, "TextNodeFontSet");
		return StageLayout{
			.Instance = CapturedTarget<void* const*>(m, {3}, "AtkStage instance"),
			.FontManager = Captured(m, {4}, "AtkStage font manager"),
		};
	});

	const Signatures::ComplexSignature<FontManagerLayout> FontManager("Fonts.FontManager", [](ResolveContext& ctx) {
		// Builds without the shader reload (before 2017.09) are scanned whole; InitFontManager's pattern is unique there.
		ModuleInitFunctions init;
		const auto m = ModuleInit.Resolve(init, ctx.Module()) == ResolveError::Ok
			? UniqueIn(ctx, FontCode::InitFontManager, init.InitFontManager, "InitFontManager")
			: ctx.Unique(FontCode::InitFontManager, ctx.Text(), "InitFontManager");
		return FontManagerLayout{
			.Fonts = Captured(m, {3}, "fonts"),
			.FontCount = Captured(m, {1}, "font count"),
			.FontSetCount = Captured(m, {5}, "font set count"),
			.FontSetSize = Captured(m, {6}, "font set size"),
			.FontSize = Captured(m, {2}, "font size"),
		};
	});

	const Signatures::ComplexSignature<FontSetLayout> FontSet("Fonts.FontSet", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto pick = ctx.Unique(FontCode::PickFont, text, "PickFont");
		const auto setUp = UniqueInSlots(ctx, ctx.Get(TextNodeRenderer), FontCode::TextNodeSetUp, "TextNodeSetUp");
		return FontSetLayout{
			.PickFont = Address(pick.begin(0)),
			.ScaledSizeY = Captured(pick, {2}, "scaled size"),
			.NodeScaleX = Captured(setUp, {4}, "node scale X"),
			.NodeScaleY = Captured(setUp, {5}, "node scale Y"),
			.CurrentFont = Captured(pick, {5}, "current font"),
			.CurrentFontSize = Captured(pick, {6}, "current font size"),
			.DrawFlags = TryCaptured(pick, {1}),
		};
	});

	const Signatures::ComplexSignature<FontLayout> Font("Fonts.Font", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto layOut = ctx.Unique(FontCode::LayOutCharacter, text, "LayOutCharacter");
		const auto getGlyphFunction = CalledFunction(ctx, layOut, {1}, "GetGlyph");
		const auto getGlyph = UniqueIn(ctx, FontCode::GetGlyph, getGlyphFunction, "GetGlyph");
		const auto fontManagerFunctions = FunctionsAfter(ctx, ctx.Get(ModuleInit).InitFontManager, 16);
		const auto build = UniqueIn(ctx, FontCode::BuildFont, fontManagerFunctions, "BuildFont");
		const auto quad = ctx.Unique(FontCode::GlyphQuad, text, "GlyphQuad");
		const auto load = UniqueIn(ctx, FontCode::LoadFont, fontManagerFunctions, "LoadFont");
		FontLayout res{
			.GetGlyph = getGlyphFunction,
			.LayOutCharacter = FunctionOf(ctx, layOut, "LayOutCharacter"),
			.BuildFont = FunctionOf(ctx, build, "BuildFont"),
			.MaxTextures = Captured(load, {5}, "max textures"),
			.Textures = Captured(build, {7}, "textures"),
			.KerningCount = Captured(build, {13}, "kerning count"),
			.TextureWidth = Captured(quad, {6}, "texture width"),
			.Size = Captured(build, {10}, "size"),
			.Ascent = Captured(build, {11}, "ascent"),
			.GlyphMap = Captured(getGlyph, {5}, "glyph map"),
			.Secondary = Captured(layOut, {10}, "secondary font"),
			.SecondaryRatio = Captured(layOut, {18}, "secondary ratio"),
			.TextureCount = Captured(build, {5}, "texture count"),
			.Flags = Captured(build, {4, 16}, "flags"),
		};

		// Only set in copies of fonts; without it, italics overhang the next glyph.
		if (const auto setUp = TryUnique(ctx, FontCode::SetUpFontSets))
			res.XShift = TryCaptured(*setUp, {2});
		return res;
	});

	const Signatures::ComplexSignature<UiLayout> Ui("Fonts.Ui", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto setScale = ctx.Unique(FontCode::SetScale, text, "SetScale");
		const auto update = UniqueIn(ctx, FontCode::UpdateCollisionNodeList, CalleesOf(ctx, ctx.Get(NamePlateUpdate), "AddonNamePlate's update"), "UpdateCollisionNodeList");
		const auto updateFunction = FunctionOf(ctx, update, "UpdateCollisionNodeList");
		const auto count = UniqueIn(ctx, FontCode::CountCollisionNodes, CalleesOf(ctx, updateFunction, "UpdateCollisionNodeList"), "CountCollisionNodes");
		return UiLayout{
			.StageUnitManager = Captured(setScale, {2}, "unit manager"),
			.LoadedUnitsCount = Captured(setScale, {5}, "loaded unit count"),
			.LoadedUnitsEntries = Captured(setScale, {6}, "loaded units"),
			.UnitUld = Captured(update, {1}, "unit ULD manager"),
			.UldNodeList = Captured(count, {2}, "node list"),
			.UldNodeListCount = Captured(count, {1}, "node list count"),
			.NodeType = Captured(count, {3, 4}, "node type"),
			.ComponentNodeComponent = Captured(count, {5}, "component"),
			.ComponentUld = Captured(count, {6, 7}, "component ULD manager"),
		};
	});

	const Signatures::ComplexSignature<ResourceHandleLayout> ResourceHandles("Fonts.ResourceHandles", [](ResolveContext& ctx) {
		const auto m = ctx.Unique(FontCode::ResourceHandleDtor, ctx.Text(), "ResourceHandle destructor");
		return ResourceHandleLayout{
			.FileName = Captured(m, {5}, "file name"),
			.FileNameLength = Captured(m, {6, 7}, "file name length"),
			.FileNameCapacity = Captured(m, {4}, "file name capacity"),
		};
	});

	const Signatures::ComplexSignature<TextureFunctions> Textures("Fonts.Textures", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto views = ctx.Unique(FontCode::TextureCreateViews, text, "texture views");
		const auto create = ctx.Unique(FontCode::CreateTexture2D, text, "CreateTexture2D");
		return TextureFunctions{
			.Device = CapturedTarget<void* const*>(views, {1, 10}, "device instance"),
			.CreateTexture2D = FunctionOf(ctx, create, "CreateTexture2D"),
			.D3D11Texture2D = Captured(views, {3, 4, 7, 8}, "D3D11 texture"),
		};
	});

	const Signatures::ComplexSignature<ReplacerFunctions> Replacer("Fonts.Replacer", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto toggle = ctx.Unique(FontCode::ToggleFontCache, text, "ToggleFontCache");
		const auto buildFunction = CalledFunction(ctx, toggle, {2}, "BuildFontCache");
		const auto freeFunction = CalledFunction(ctx, toggle, {3}, "FreeFontCache");
		const auto buildCache = ctx.MatchAt(FontCode::BuildFontCache, FunctionRuns(ctx, buildFunction, "BuildFontCache").front(), "BuildFontCache");
		const auto freeCache = ctx.MatchAt(FontCode::FreeFontCache, FunctionRuns(ctx, freeFunction, "FreeFontCache").front(), "FreeFontCache");
		const auto vtables = UniqueIn(ctx, FontCode::FontAnalyzerVtables, CalleesOf(ctx, ctx.Get(ModuleInit).InitFontManager, "InitFontManager"), "font analyzer vtables");
		ReplacerFunctions res{
			.BuildFontCache = buildFunction,
			.FreeFontCache = freeFunction,
			.RendererVtable = CapturedTarget<void* const*>(vtables, {1}, "renderer vtable"),
			.RenderCountVtable = CapturedTarget<void* const*>(vtables, {3}, "render count vtable"),
			.FontCacheSlot = Captured(buildCache, {1}, "font cache slot"),
			.FontCacheFlags = Captured(toggle, {1}, "font cache flags"),
		};
		ctx.Require(Captured(freeCache, {1}, "font cache slot") == res.FontCacheSlot, ResolveError::Mismatch, "BuildFontCache and FreeFontCache disagree on the font cache slot");

		// Without it, italics are the game's shear.
		std::optional<int32_t> stateFlags;
		auto agree = true;
		for (const auto signature : {&FontCode::ItalicQuad, &FontCode::ItalicMacro}) {
			if (const auto m = TryUnique(ctx, *signature); m) {
				const auto value = TryCaptured(*m, {1});
				agree = agree && value && (!stateFlags || *stateFlags == *value);
				stateFlags = value;
			}
		}
		if (agree)
			res.StateFlags = stateFlags;
		return res;
	});

	const Signatures::ComplexSignature<FontTablesLayout> FontTables("Fonts.FontTables", [](ResolveContext& ctx) {
		const auto m = ctx.Unique(FontCode::FontTables, ctx.Text(), "font tables");
		return FontTablesLayout{
			.Game = CapturedTarget(m, {5}, "game table"),
			.LobbyA = CapturedTarget(m, {3}, "lobby table A"),
			.LobbyB = CapturedTarget(m, {4}, "lobby table B"),
			.Count = Captured(m, {8}, "table length"),
			.EntrySize = Captured(m, {7}, "entry size"),
			.FdtName = Captured(m, {6}, "FDT name"),
		};
	});

	const Signatures::ComplexSignature<NamePlateFunctions> NamePlates("Fonts.NamePlates", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto update = ctx.Get(NamePlateUpdate);
		const auto caller = UniqueIn(ctx, FontCode::NamePlateAllocateBakeCaller, update, "AllocateBake's caller");
		const auto allocateFunction = CalledFunction(ctx, caller, {4, 11}, "AllocateBake");
		const auto allocate = UniqueIn(ctx, FontCode::NamePlateAllocateBake, allocateFunction, "AllocateBake");
		const auto prepare = UniqueIn(ctx, FontCode::NamePlateBakePrepare, FunctionsBefore(ctx, allocateFunction, 8), "BakePrepare");
		const auto prepareFunction = FunctionOf(ctx, prepare, "BakePrepare");
		const auto draw = UniqueIn(ctx, FontCode::NamePlateDrawBaked, FunctionsAfter(ctx, prepareFunction, 16), "DrawBaked");
		const auto dispatch = ctx.Unique(FontCode::AtkResNodeDispatchEvent, text, "AtkResNode::DispatchEvent");
		const auto setScale = ctx.Unique(FontCode::AtkResNodeSetScale, text, "AtkResNode::SetScale");
		NamePlateFunctions res{
			.AllocateBake = allocateFunction,
			.BakePrepare = prepareFunction,
			.DrawBaked = FunctionOf(ctx, draw, "DrawBaked"),
			.AddonBakePlate = Captured(caller, {2, 7}, "bake plate"),
			.AddonObjects = Captured(caller, {1, 10}, "objects"),
			.ObjectSize = Captured(caller, {5, 8, 9}, "object size"),
			.ObjectNameText = Captured(allocate, {2}, "name text"),
			.ObjectTextW = Captured(allocate, {4}, "text width"),
			.ObjectTextH = Captured(allocate, {5}, "text height"),
			.ObjectNeedsToBeBaked = Captured(caller, {3, 12}, "needs baking"),
			.BakeTextYOffset = Captured(draw, {5}, "bake text Y offset"),
			.BakeAlpha = Captured(draw, {6}, "bake alpha"),
			.RendererCurrentBakeData = Captured(prepare, {1}, "current bake"),
			// The transform's first row is captured as its two floats; the matrix starts at the first.
			.NodeTransform = (std::min)(Captured(draw, {1}, "transform"), Captured(draw, {2}, "transform")),
			.NodeWidth = Captured(draw, {3}, "node width"),
			.NodeHeight = Captured(draw, {4}, "node height"),
			.NodeParent = Captured(dispatch, {5}, "parent node"),
			.NodeScaleX = Captured(setScale, {2}, "node scale"),
		};

		// The caller has the array's size before 7.00, the loop over the plates since.
		auto arraySize = TryCaptured(caller, {6});
		if (!arraySize)
			arraySize = Captured(UniqueIn(ctx, FontCode::NamePlateObjectsLoop, update, "the plates' loop"), {4}, "object array size");
		res.ObjectCount = *arraySize / (std::max)(res.ObjectSize, 1);

		// Draw is the function before PrepareNode; without it or its flag, only the baked modes are available.
		if (const auto before = FunctionsBefore(ctx, prepareFunction, 1); !before.empty()) {
			if (const auto live = TryUniqueIn(ctx, FontCode::NamePlateDraw, before.front()); live) {
				res.Draw = Address(before.front());
				res.RendererFixedFontResolution = TryCaptured(*live, {1}).value_or(0);
			}
		}
		return res;
	});

	const Signatures::ComplexSignature<ShaderTablesLayout> ShaderTables("Fonts.ShaderTables", [](ResolveContext& ctx) {
		const auto text = ctx.Text();
		const auto loop = UniqueIn(ctx, FontCode::AtkServerLoadShadersLoop, ctx.Get(ModuleInit).LoadShaders, "LoadShaders' loop");
		const auto bind = ctx.Unique(FontCode::AtkServerBindFontShaders, text, "BindShaders");
		const auto set = ctx.Unique(FontCode::KernelSetPixelShader, text, "SetPixelShader");
		const auto vertexShaders = Captured(loop, {1}, "vertex shaders");
		ShaderTablesLayout res{
			.BindFontShaders = FunctionOf(ctx, bind, "BindShaders"),
			.PixelShaderFiles = vertexShaders + Captured(loop, {5, 6}, "pixel shader files"),
			.PixelShaders = Captured(bind, {4}, "pixel shaders"),
			.D3DShader = Captured(set, {3}, "D3D shader"),
		};
		ctx.Require(vertexShaders + Captured(loop, {7}, "pixel shaders") == res.PixelShaders, ResolveError::Mismatch, "LoadShaders and BindShaders disagree on the pixel shaders");
		return res;
	});

	const Signatures::ComplexSignature<EdgeShaderFunctions> EdgeShader("Fonts.EdgeShader", [](ResolveContext& ctx) {
		const auto& init = ctx.Get(ModuleInit);
		return EdgeShaderFunctions{
			.LoadShaders = init.LoadShaders,
			.ModuleServer = init.Server,
			.ModuleFontManager = init.FontManager,
		};
	});

	const Signatures::ComplexSignature<LineBreakerFunctions> LineBreaker("Fonts.LineBreaker", [](ResolveContext& ctx) {
		const auto m = ctx.Unique(FontCode::SplitWord, ctx.Text(), "SplitWord");
		return LineBreakerFunctions{
			.SplitWord = Address(m.begin(0)),
			.FitUnits = CapturedTarget(m, {3}, "FitUnits"),
			.ConsumedUnits = Captured(m, {4}, "consumed units"),
			.MaxUnits = Captured(m, {1}, "max units"),
			.UsedUnits = Captured(m, {2}, "used units"),
		};
	});
}
