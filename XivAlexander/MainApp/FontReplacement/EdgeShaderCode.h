#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement::EdgeShaderCode {
	// The bits of FontEdgePS.hlsl's Softness (0.123456789f), compiled as immediate operands.
	constexpr uint32_t Sentinel = 0x3DFCD6EA;

	// The embedded FontEdgePS.cso with the softness in place; as compiled, with failure set, if it has no Sentinel.
	std::vector<uint8_t> Get(float softness, std::string& failure);

	// Replaces each Sentinel in the code chunk and sets the checksum; returns how many (with none, dxbc is left as it was).
	size_t Patch(std::span<uint8_t> dxbc, float softness);

	// The DXBC container's checksum, of everything after it.
	std::array<uint8_t, 16> ComputeChecksum(std::span<const uint8_t> dxbc);
}
