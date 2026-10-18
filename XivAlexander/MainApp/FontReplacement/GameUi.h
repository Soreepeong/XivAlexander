#pragma once

// Game UI structures the font replacement reads at offsets from the game's code; each group is resolved by the part that uses it.
namespace XivAlexander::Apps::MainApp::FontReplacement::GameUi {
	// AtkResNode.Type of text nodes; types from FirstComponentNodeType on are component nodes.
	constexpr int TextNodeType = 3;
	constexpr int FirstComponentNodeType = 1000;

	// 0 before the game makes it.
	[[nodiscard]] uintptr_t Stage();

	void ResolveStage();

	// Covers GetLoadedUnits and the node accessors.
	void ResolveUnits();

	void ResolveResourceHandles();

	void ResolveTextures();

	// Each an AtkUnitBase*.
	[[nodiscard]] std::vector<uintptr_t> GetLoadedUnits();

	[[nodiscard]] uintptr_t GetUnitUld(uintptr_t unit);

	[[nodiscard]] int GetNodeCount(uintptr_t uld);
	[[nodiscard]] uintptr_t GetNode(uintptr_t uld, int index);
	[[nodiscard]] uint16_t GetNodeType(uintptr_t node);

	// 0 if the node has no component.
	[[nodiscard]] uintptr_t GetComponentUld(uintptr_t componentNode);

	// Stored as an MSVC std::string: inline while shorter than 16.
	[[nodiscard]] std::string GetFileName(uintptr_t handle);

	[[nodiscard]] uintptr_t GetD3D11Texture(uintptr_t kernelTexture);
}
