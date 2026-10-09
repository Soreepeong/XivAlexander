#pragma once

// The rest of the game's UI the font replacement reads, at the offsets the game's code says (GameLayout): the loaded units
// and their node trees, resource handles' file names and Kernel textures. Each group is resolved by the part that uses it.
namespace XivAlexander::Apps::MainApp::FontReplacement::GameUi {
	// AtkResNode.Type of a text node, and the first of component nodes (the types from there on are).
	extern int TextNodeType;
	extern int FirstComponentNodeType;

	// Gets AtkStage, or 0 before it is made.
	[[nodiscard]] uintptr_t Stage();

	// Resolves Stage.
	void ResolveStage();

	// Resolves the loaded units and their node trees: GetLoadedUnits and the node accessors.
	void ResolveUnits();

	// Resolves GetFileName.
	void ResolveResourceHandles();

	// Resolves GetD3D11Texture.
	void ResolveTextures();

	// Gets every loaded unit (AtkUnitBase*).
	[[nodiscard]] std::vector<uintptr_t> GetLoadedUnits();

	// Gets the ULD manager of a unit.
	[[nodiscard]] uintptr_t GetUnitUld(uintptr_t unit);

	[[nodiscard]] int GetNodeCount(uintptr_t uld);
	[[nodiscard]] uintptr_t GetNode(uintptr_t uld, int index);
	[[nodiscard]] uint16_t GetNodeType(uintptr_t node);

	// Gets the ULD manager of a component node's component; 0 if it has none.
	[[nodiscard]] uintptr_t GetComponentUld(uintptr_t componentNode);

	// Gets a resource handle's file name (an MSVC std::string: inline while it is shorter than 16).
	[[nodiscard]] std::string GetFileName(uintptr_t handle);

	// Gets a Kernel::Texture's ID3D11Texture2D.
	[[nodiscard]] uintptr_t GetD3D11Texture(uintptr_t kernelTexture);
}
