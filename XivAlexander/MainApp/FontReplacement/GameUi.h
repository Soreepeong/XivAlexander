#pragma once

// Game UI structures the font replacement reads at offsets from the game's code (GameLayout); each group is resolved by the part that uses it.
namespace XivAlexander::Apps::MainApp::FontReplacement::GameUi {
	// AtkResNode.Type of text nodes; types from FirstComponentNodeType on are component nodes.
	extern int TextNodeType;
	extern int FirstComponentNodeType;

	// AtkStage, or 0 before it is made.
	[[nodiscard]] uintptr_t Stage();

	void ResolveStage();

	// GetLoadedUnits and the node accessors.
	void ResolveUnits();

	void ResolveResourceHandles();

	void ResolveTextures();

	// Each an AtkUnitBase*.
	[[nodiscard]] std::vector<uintptr_t> GetLoadedUnits();

	[[nodiscard]] uintptr_t GetUnitUld(uintptr_t unit);

	[[nodiscard]] int GetNodeCount(uintptr_t uld);
	[[nodiscard]] uintptr_t GetNode(uintptr_t uld, int index);
	[[nodiscard]] uint16_t GetNodeType(uintptr_t node);

	// The ULD manager of the node's component; 0 if it has none.
	[[nodiscard]] uintptr_t GetComponentUld(uintptr_t componentNode);

	// Stored as an MSVC std::string: inline while shorter than 16.
	[[nodiscard]] std::string GetFileName(uintptr_t handle);

	// Gets a Kernel::Texture's ID3D11Texture2D.
	[[nodiscard]] uintptr_t GetD3D11Texture(uintptr_t kernelTexture);
}
