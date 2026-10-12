#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement::EdgeShaderCode {
	// The bits of FontEdgePS.hlsl's Softness (0.123456789f), compiled as immediate operands.
	constexpr uint32_t Sentinel = 0x3DFCD6EA;

	// The embedded FontEdgePS.cso with the softness in place; as compiled, with failure set, if it has no Sentinel.
	std::vector<uint8_t> Get(float softness, std::string& failure);

	// Replaces each Sentinel in the code chunk and sets the checksum; returns how many (with none, dxbc is left as it was).
	size_t Patch(std::span<uint8_t> dxbc, float softness);

	// Replaces each word of the code chunk equal to a sentinel (a float's bits) with its value and sets the checksum; returns how many.
	size_t PatchConstants(std::span<uint8_t> dxbc, std::initializer_list<std::pair<uint32_t, float>> values);

	// The DXBC container's checksum, of everything after it.
	std::array<uint8_t, 16> ComputeChecksum(std::span<const uint8_t> dxbc);
}
