#include "pch.h"

#include <mmreg.h>
#include <XivAlexanderCommon/Sqex_Sound_Reader.h>
#include <XivAlexanderCommon/Sqex_Sound_Writer.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>

int main() {
	const Sqex::Sqpack::Reader readerC(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\0c0000.win32.index)");
	const auto rc_1 = Sqex::Sound::ScdReader(readerC["music/ffxiv/bgm_pvp_battle.scd"]);
	const auto rc_2 = Sqex::Sound::ScdReader(readerC["music/ffxiv/bgm_ban_odin.scd"]);
	const Sqex::Sqpack::Reader reader7(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\070000.win32.index)");
	const auto r7_1 = Sqex::Sound::ScdReader(reader7["sound/foot/dev/6999.scd"]);
	const auto r7_2 = Sqex::Sound::ScdReader(reader7["sound/system/sample_system.scd"]);
	const auto r7_3 = Sqex::Sound::ScdReader(reader7["sound/zingle/zingle_titlegamestart.scd"]);
	const auto r7_4 = Sqex::Sound::ScdReader(reader7["sound/system/se_ui.scd"]);

	const auto entry = r7_2.GetSoundEntry(0);
	Sqex::Sound::ScdWriter writer;
	writer.SetTable1(r7_2.ReadTable1Entries());
	writer.SetTable4(r7_2.ReadTable4Entries());
	writer.SetTable2(r7_2.ReadTable2Entries());
	// writer.SetTable5(r7_2.ReadTable5Entries());
	// writer.SetSoundEntry(0, ScdWriter::SoundEntry::FromWave(Sqex::MemoryRandomAccessStream(entry.GetMsAdpcmWavFile())));
	
	const auto chord = Sqex::Sound::ScdWriter::SoundEntry::FromWave(Sqex::FileRandomAccessStream(LR"(C:\Windows\Media\Chord.wav)"));
	for (size_t i = 0; i < r7_4.GetSoundEntryCount(); i++) {
		writer.SetSoundEntry(i, chord);
	}

	const auto v2 = writer.Export();
	Utils::Win32::File::Create(LR"(C:\Users\SP\AppData\Roaming\XivAlexander\ReplacementFileEntries\ffxiv\070000\sound\system\se_ui.scd)",
		GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0
	).Write(0, std::span(v2));

	// const auto v = rc_1.GetSoundEntry(0).GetOggFile();
	// Utils::Win32::File::Create(L"Z:\\test.ogg", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0).Write(0, std::span(v));

	return 0;
}
