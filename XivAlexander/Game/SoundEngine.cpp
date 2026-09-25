#include "pch.h"
#include "Game/SoundEngine.h"

#include <xivres/util.module_relative.h>

using xivres::util::module_relative;

std::string XivAlexander::Game::to_string(const SoundVoiceRenderInfo& value) {
	return std::format("state +0x{:X}, queued buffers +0x{:X}, mix rate {}",
		value.State, value.QueuedBuffers, module_relative(value.MixRate));
}

std::string XivAlexander::Game::to_string(const SoundVoiceFunctions& value) {
	return std::format(
		"init {}, submit {} @{}, flush {} @{}, set marker {} @{}, destructor {} @{}, state +0x{:X}, queued buffers +0x{:X}",
		module_relative(value.Init),
		module_relative(value.Submit), value.Layout.SubmitSlot,
		module_relative(value.Flush), value.Layout.FlushSlot,
		module_relative(value.SetMarker), value.Layout.SetMarkerSlot,
		module_relative(value.Destructor), value.Layout.DestructorSlot,
		value.Layout.State,
		value.Layout.QueuedBuffers);
}

std::string XivAlexander::Game::to_string(const SoundBufferEndInfo& value) {
	return std::format("handler {}, end of data +0x{:X}", module_relative(value.Handler), value.CallbackLayout.EndOfData);
}
