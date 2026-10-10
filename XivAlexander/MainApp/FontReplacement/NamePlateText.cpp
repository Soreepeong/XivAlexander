#include "pch.h"
#include "MainApp/FontReplacement/NamePlateText.h"

#include "MainApp/FontReplacement/FontReplacer.h"
#include "MainApp/FontReplacement/GameLayout.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Utilities.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// The game's lower clamp of a plate's distance factor.
	constexpr float MinDistanceFactor = 0.05f;

	// Bake scales beyond these are not worth it (or are a measuring error).
	constexpr float MinBakeScale = 0.5f;
	constexpr float MaxBakeScale = 4.f;

	// A plate shown this much larger than its bake is baked again; within this of its bake scale it is drawn at exactly 1.
	constexpr float ScaleTolerance = 0.02f;
}

FontReplacement::NamePlateText::NamePlateText(FontReplacer& replacer)
	: m_replacer(replacer) {
	uintptr_t allocateBake = 0, prepare = 0, drawBaked = 0;
	GameLayout::Resolve("Nameplate text", [&] {
		GameUi::ResolveUnits();

		// Allocates a plate's bake region, sized by its TextW and TextH, when it NeedsToBeBaked.
		allocateBake = GameLayout::Address("NamePlateAllocateBake");

		// Per-node text setup; for a bake (CurrentBakeData set), node scale 1.
		prepare = GameLayout::Address("NamePlateBakePrepare");

		// Draws a baked plate centered on its node.
		drawBaked = GameLayout::Address("NamePlateDrawBaked");

		m_addonBakePlate = GameLayout::Get("AddonNamePlate.BakePlate");
		m_addonObjects = GameLayout::Get("AddonNamePlate.NamePlateObjectArray");
		m_objectSize = GameLayout::Get("NamePlateObject");
		m_objectCount = GameLayout::Get("NamePlateObjectArray") / (std::max)(m_objectSize, 1);
		m_objectNameText = GameLayout::Get("NamePlateObject.NameText");
		m_objectTextW = GameLayout::Get("NamePlateObject.TextW");
		m_objectTextH = GameLayout::Get("NamePlateObject.TextH");
		m_objectNeedsToBeBaked = GameLayout::Get("NamePlateObject.NeedsToBeBaked");
		m_bakeTextYOffset = GameLayout::Get("BakeData.TextYOffset");
		m_bakeAlpha = GameLayout::Get("BakeData.Alpha");
		m_rendererCurrentBakeData = GameLayout::Get("BakePlateRenderer.CurrentBakeData");

		// The transform's first row is captured as its two floats; the matrix starts at the first.
		m_nodeTransform = (std::min)(GameLayout::Get("AtkResNode.Transform.Row1A"), GameLayout::Get("AtkResNode.Transform.Row1B"));
		m_nodeWidth = GameLayout::Get("AtkResNode.Width");
		m_nodeHeight = GameLayout::Get("AtkResNode.Height");
		m_nodeParent = GameLayout::Get("AtkResNode.ParentNode");
		m_nodeScaleX = GameLayout::Get("AtkResNode.ScaleX");
		GameFontSet::Resolve();
	});

	m_allocateBakeHook.emplace("NamePlateAllocateBake", allocateBake, [this](uintptr_t renderer, uintptr_t obj) { return AllocateBakeDetour(renderer, obj); });
	m_prepareHook.emplace("NamePlateBakePrepare", prepare, [this](uintptr_t renderer, float* rect, GameFontSet* set, uintptr_t node) { PrepareDetour(renderer, rect, set, node); });
	m_drawBakedHook.emplace("NamePlateDrawBaked", drawBaked, [this](uintptr_t renderer, uintptr_t node, uintptr_t bake) { DrawBakedDetour(renderer, node, bake); });
	ForceRebake();
}

FontReplacement::NamePlateText::~NamePlateText() {
	m_allocateBakeHook.reset();

	// Regions baked at another scale must not be drawn unhooked; a plate needing a bake is drawn live (BakePlateRenderer.Draw) until rebaked.
	try {
		ForceRebake();
	} catch (const std::exception& e) {
		Host::Error("Marking nameplates for baking failed: {}", e.what());
	}

	m_prepareHook.reset();
	m_drawBakedHook.reset();
}

void FontReplacement::NamePlateText::ForceRebake() {
	const auto addon = m_lastRenderer - m_addonBakePlate;
	if (const auto units = m_lastRenderer ? GameUi::GetLoadedUnits() : std::vector<uintptr_t>(); std::ranges::find(units, addon) == units.end()) {
		m_rebakePending = true;
		return;
	}

	m_rebakePending = false;
	const auto objects = At<uintptr_t>(addon + m_addonObjects);
	if (!objects)
		return;
	for (auto i = 0; i < m_objectCount; i++)
		SetNeedsToBeBaked(objects + static_cast<size_t>(i) * m_objectSize);
}

void FontReplacement::NamePlateText::SeeRenderer(uintptr_t renderer) {
	m_lastRenderer = renderer;
	if (m_rebakePending)
		ForceRebake();
}

bool FontReplacement::NamePlateText::NeedsToBeBaked(uintptr_t obj) const {
	return At<uint8_t>(obj + m_objectNeedsToBeBaked) != 0;
}

void FontReplacement::NamePlateText::SetNeedsToBeBaked(uintptr_t obj) const {
	At<uint8_t>(obj + m_objectNeedsToBeBaked) = 1;
}

float& FontReplacement::NamePlateText::Transform(uintptr_t node, int index) const {
	return reinterpret_cast<float*>(node + m_nodeTransform)[index];
}

std::optional<float> FontReplacement::NamePlateText::GetShownScale(uintptr_t obj) const {
	const auto text = At<uintptr_t>(obj + m_objectNameText);
	if (!text)
		return std::nullopt;
	auto scale = 1.f;
	for (auto node = text; node; node = At<uintptr_t>(node + m_nodeParent))
		scale *= At<float>(node + m_nodeScaleX);
	return std::isfinite(scale) && scale > 0 ? std::optional(scale) : std::nullopt;
}

std::optional<float> FontReplacement::NamePlateText::GetFullSizeScale(uintptr_t obj) const {
	const auto shown = GetShownScale(obj);
	if (!shown)
		return std::nullopt;
	const auto a = static_cast<float>(At<uint8_t>(obj + m_bakeAlpha)) / 255.f;
	const auto factor = (std::max)(a * a * a * a, MinDistanceFactor);
	const auto scale = *shown / factor;
	return std::isfinite(scale) ? std::optional(std::clamp(scale, MinBakeScale, MaxBakeScale)) : std::nullopt;
}

uint8_t FontReplacement::NamePlateText::AllocateBakeDetour(uintptr_t renderer, uintptr_t obj) {
	SeeRenderer(renderer);
	auto previous = 0.f;
	if (const auto it = m_bakeScales.find(obj); it != m_bakeScales.end()) {
		previous = it->second;
		m_bakeScales.erase(it);
	}

	// As the game bakes while the replacement is off.
	const auto fullSize = m_replacer.Enabled() ? GetFullSizeScale(obj) : std::nullopt;
	if (!fullSize)
		return m_allocateBakeHook->Original(renderer, obj);

	// At least the size it is shown at now, and what it was baked at before (it was shown that large).
	const auto scale = std::clamp((std::max)(*fullSize, (std::max)(GetShownScale(obj).value_or(0.f), previous)), MinBakeScale, MaxBakeScale);

	auto& textW = At<int16_t>(obj + m_objectTextW);
	auto& textH = At<int16_t>(obj + m_objectTextH);
	const auto w = textW, h = textH;
	textW = static_cast<int16_t>(std::ceil(static_cast<float>(w) * scale));
	textH = static_cast<int16_t>(std::ceil(static_cast<float>(h) * scale));
	const auto allocated = m_allocateBakeHook->Original(renderer, obj);
	textW = w;
	textH = h;
	if (allocated)
		m_bakeScales[obj] = scale;
	return allocated;
}

void FontReplacement::NamePlateText::PrepareDetour(uintptr_t renderer, float* rect, GameFontSet* set, uintptr_t node) {
	m_prepareHook->Original(renderer, rect, set, node);

	// A bake of a region allocated at a scale: lay the text out at that scale (the original sets 1).
	const auto bake = At<uintptr_t>(renderer + m_rendererCurrentBakeData);
	if (const auto it = bake ? m_bakeScales.find(bake) : m_bakeScales.end(); it != m_bakeScales.end()) {
		set->NodeScaleX() = it->second;
		set->NodeScaleY() = it->second;
	}
}

void FontReplacement::NamePlateText::DrawBakedDetour(uintptr_t renderer, uintptr_t node, uintptr_t bake) {
	SeeRenderer(renderer);
	const auto it = m_bakeScales.find(bake);
	if (it == m_bakeScales.end()) {
		m_drawBakedHook->Original(renderer, node, bake);
		return;
	}
	const auto scale = it->second;

	float transform[4];
	for (auto i = 0; i < 4; i++)
		transform[i] = Transform(node, i);
	const auto shown = std::sqrt(transform[0] * transform[0] + transform[1] * transform[1]);

	// Shown larger than baked (over 100% up close, targeted): rebake at that size (BakeData is the object's first member); drawn live until then.
	const auto obj = bake;
	if (shown > scale * (1 + ScaleTolerance) && !NeedsToBeBaked(obj) && m_replacer.Enabled())
		SetNeedsToBeBaked(obj);

	// The region is in bake pixels, so the node's size and text offset are scaled to match; near the bake scale the transform is snapped to exactly 1,
	// the only case the original point samples (bilinear at fractional positions blurs even at 1:1). Plates aren't rotated.
	auto& widthField = At<uint16_t>(node + m_nodeWidth);
	auto& heightField = At<uint16_t>(node + m_nodeHeight);
	auto& textYOffsetField = At<int16_t>(bake + m_bakeTextYOffset);
	const auto width = widthField, height = heightField;
	const auto textYOffset = textYOffsetField;
	const auto exact = std::abs(shown / scale - 1) < ScaleTolerance;
	for (auto i = 0; i < 4; i++)
		Transform(node, i) = exact ? (i == 0 || i == 3 ? 1.f : 0.f) : transform[i] / scale;

	widthField = static_cast<uint16_t>((std::min)(65535.f, std::round(static_cast<float>(width) * scale)));
	heightField = static_cast<uint16_t>((std::min)(65535.f, std::round(static_cast<float>(height) * scale)));
	textYOffsetField = static_cast<int16_t>(std::round(static_cast<float>(textYOffset) * scale));
	const auto restore = xivres::util::on_dtor([&] {
		for (auto i = 0; i < 4; i++)
			Transform(node, i) = transform[i];
		widthField = width;
		heightField = height;
		textYOffsetField = textYOffset;
	});
	m_drawBakedHook->Original(renderer, node, bake);
}
