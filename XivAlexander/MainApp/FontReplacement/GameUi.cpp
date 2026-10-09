#include "pch.h"
#include "MainApp/FontReplacement/GameUi.h"

#include "MainApp/FontReplacement/GameLayout.h"
#include "MainApp/FontReplacement/Utilities.h"

namespace GameUi = XivAlexander::Apps::MainApp::FontReplacement::GameUi;
using XivAlexander::Apps::MainApp::FontReplacement::GameLayout;

namespace {
	uintptr_t s_stageInstance;
	int s_stageUnitManager;
	int s_loadedUnitsCount;
	int s_loadedUnitsEntries;
	int s_unitUld;
	int s_uldNodeList;
	int s_uldNodeListCount;
	int s_nodeType;
	int s_componentNodeComponent;
	int s_componentUld;
	int s_fileName;
	int s_fileNameLength;
	int s_fileNameCapacity;
	int s_textureD3D11Texture2D;
}

int GameUi::TextNodeType;
int GameUi::FirstComponentNodeType;

uintptr_t GameUi::Stage() {
	return s_stageInstance ? At<uintptr_t>(s_stageInstance) : 0;
}

void GameUi::ResolveStage() {
	s_stageInstance = GameLayout::Target("AtkStage.Instance");
}

void GameUi::ResolveUnits() {
	ResolveStage();
	s_stageUnitManager = GameLayout::Get("AtkStage.RaptureAtkUnitManager");
	s_loadedUnitsCount = GameLayout::Get("AtkUnitManager.AllLoadedUnitsList.Count");
	s_loadedUnitsEntries = GameLayout::Get("AtkUnitManager.AllLoadedUnitsList.Entries");
	s_unitUld = GameLayout::Get("AtkUnitBase.UldManager");
	s_uldNodeList = GameLayout::Get("AtkUldManager.NodeList");
	s_uldNodeListCount = GameLayout::Get("AtkUldManager.NodeListCount");
	s_nodeType = GameLayout::Get("AtkResNode.Type");
	s_componentNodeComponent = GameLayout::Get("AtkComponentNode.Component");
	s_componentUld = GameLayout::Get("AtkComponentBase.UldManager");
	TextNodeType = GameLayout::Get("NodeType.Text");
	FirstComponentNodeType = GameLayout::Get("NodeType.FirstComponent");
}

void GameUi::ResolveResourceHandles() {
	s_fileName = GameLayout::Get("ResourceHandle.FileName");
	s_fileNameLength = GameLayout::Get("ResourceHandle.FileName.Length");
	s_fileNameCapacity = GameLayout::Get("ResourceHandle.FileName.Capacity");
}

void GameUi::ResolveTextures() {
	s_textureD3D11Texture2D = GameLayout::Get("Texture.D3D11Texture2D");
}

std::vector<uintptr_t> GameUi::GetLoadedUnits() {
	std::vector<uintptr_t> units;
	const auto stage = Stage();
	const auto manager = stage ? At<uintptr_t>(stage + s_stageUnitManager) : 0;
	if (!manager)
		return units;
	const auto count = At<uint16_t>(manager + s_loadedUnitsCount);
	const auto entries = reinterpret_cast<const uintptr_t*>(manager + s_loadedUnitsEntries);
	for (auto i = 0; i < count; i++) {
		if (entries[i])
			units.push_back(entries[i]);
	}
	return units;
}

uintptr_t GameUi::GetUnitUld(uintptr_t unit) {
	return unit + s_unitUld;
}

int GameUi::GetNodeCount(uintptr_t uld) {
	return At<uintptr_t>(uld + s_uldNodeList) ? At<uint16_t>(uld + s_uldNodeListCount) : 0;
}

uintptr_t GameUi::GetNode(uintptr_t uld, int index) {
	return reinterpret_cast<const uintptr_t*>(At<uintptr_t>(uld + s_uldNodeList))[index];
}

uint16_t GameUi::GetNodeType(uintptr_t node) {
	return At<uint16_t>(node + s_nodeType);
}

uintptr_t GameUi::GetComponentUld(uintptr_t componentNode) {
	const auto component = At<uintptr_t>(componentNode + s_componentNodeComponent);
	return component ? component + s_componentUld : 0;
}

std::string GameUi::GetFileName(uintptr_t handle) {
	const auto length = At<uint64_t>(handle + s_fileNameLength);
	const auto capacity = At<uint64_t>(handle + s_fileNameCapacity);
	const auto text = capacity < 16 ? reinterpret_cast<const char*>(handle + s_fileName) : At<const char*>(handle + s_fileName);
	return !text || length > 4096 ? std::string() : std::string(text, static_cast<size_t>(length));
}

uintptr_t GameUi::GetD3D11Texture(uintptr_t kernelTexture) {
	return At<uintptr_t>(kernelTexture + s_textureD3D11Texture2D);
}
