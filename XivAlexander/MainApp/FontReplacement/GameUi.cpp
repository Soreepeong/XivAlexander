#include "pch.h"
#include "MainApp/FontReplacement/GameUi.h"

#include "MainApp/FontReplacement/GameCode.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace GameUi = XivAlexander::Apps::MainApp::FontReplacement::GameUi;
namespace Fonts = XivAlexander::Game::Resolved::Fonts;
using XivAlexander::Apps::MainApp::FontReplacement::ResolveGameCode;

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

int GameUi::TextNodeType = 3;
int GameUi::FirstComponentNodeType = 1000;

uintptr_t GameUi::Stage() {
	return s_stageInstance ? At<uintptr_t>(s_stageInstance) : 0;
}

void GameUi::ResolveStage() {
	s_stageInstance = reinterpret_cast<uintptr_t>(ResolveGameCode(Fonts::Stage).Instance);
}

void GameUi::ResolveUnits() {
	ResolveStage();
	const auto ui = ResolveGameCode(Fonts::Ui);
	s_stageUnitManager = ui.StageUnitManager;
	s_loadedUnitsCount = ui.LoadedUnitsCount;
	s_loadedUnitsEntries = ui.LoadedUnitsEntries;
	s_unitUld = ui.UnitUld;
	s_uldNodeList = ui.UldNodeList;
	s_uldNodeListCount = ui.UldNodeListCount;
	s_nodeType = ui.NodeType;
	s_componentNodeComponent = ui.ComponentNodeComponent;
	s_componentUld = ui.ComponentUld;
}

void GameUi::ResolveResourceHandles() {
	const auto handles = ResolveGameCode(Fonts::ResourceHandles);
	s_fileName = handles.FileName;
	s_fileNameLength = handles.FileNameLength;
	s_fileNameCapacity = handles.FileNameCapacity;
}

void GameUi::ResolveTextures() {
	s_textureD3D11Texture2D = ResolveGameCode(Fonts::Textures).D3D11Texture2D;
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
