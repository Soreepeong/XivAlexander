#include "pch.h"
#include "MainApp/FontReplacement/Game/NamePlateText.h"

#include "MainApp/FontReplacement/Fonts/FontReplacer.h"
#include "MainApp/FontReplacement/GameCode.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// OnRequestedUpdate (SetCommonNamePlate's body, inlined in 7.56h) never scales a plate below this factor.
	constexpr float MinDistanceFactor = 0.05f;

	// Bake scales beyond these are not worth it (or are a measuring error).
	constexpr float MinBakeScale = 0.5f;
	constexpr float MaxBakeScale = 4.f;

	// A plate shown this much larger than its bake is baked again; within this of its bake scale it is drawn at exactly 1.
	constexpr float ScaleTolerance = 0.02f;
}

FontReplacement::NamePlateText::NamePlateText(FontReplacer& replacer)
	: m_replacer(replacer) {
	GameUi::ResolveUnits();
	GameFontSet::Resolve();
	const auto functions = ResolveGameCode(Game::Resolved::Fonts::NamePlates);
	m_addonBakePlate = functions.AddonBakePlate;
	m_addonObjects = functions.AddonObjects;
	m_objectSize = functions.ObjectSize;
	m_objectCount = functions.ObjectCount;
	m_objectNameText = functions.ObjectNameText;
	m_objectTextW = functions.ObjectTextW;
	m_objectTextH = functions.ObjectTextH;
	m_objectNeedsToBeBaked = functions.ObjectNeedsToBeBaked;
	m_bakeTextYOffset = functions.BakeTextYOffset;
	m_bakeAlpha = functions.BakeAlpha;
	m_rendererCurrentBakeData = functions.RendererCurrentBakeData;
	m_nodeTransform = functions.NodeTransform;
	m_nodeWidth = functions.NodeWidth;
	m_nodeHeight = functions.NodeHeight;
	m_nodeParent = functions.NodeParent;
	m_nodeScaleX = functions.NodeScaleX;
	m_rendererFixedFontResolution = functions.RendererFixedFontResolution;

	if (functions.Draw && m_rendererFixedFontResolution)
		m_bakePlateDrawHook.emplace("NamePlateDraw", reinterpret_cast<uintptr_t>(functions.Draw), [this](uintptr_t renderer, uintptr_t node) { BakePlateDrawDetour(renderer, node); });
	else
		Host::Warning("Live nameplate text is unavailable in this version of the game; it is drawn as baked at full size.");
	m_allocateBakeHook.emplace("NamePlateAllocateBake", reinterpret_cast<uintptr_t>(functions.AllocateBake), [this](uintptr_t renderer, uintptr_t obj) { return AllocateBakeDetour(renderer, obj); });
	m_prepareHook.emplace("NamePlateBakePrepare", reinterpret_cast<uintptr_t>(functions.BakePrepare), [this](uintptr_t renderer, float* rect, GameFontSet* set, uintptr_t node) { PrepareDetour(renderer, rect, set, node); });
	m_drawBakedHook.emplace("NamePlateDrawBaked", reinterpret_cast<uintptr_t>(functions.DrawBaked), [this](uintptr_t renderer, uintptr_t node, uintptr_t bake) { DrawBakedDetour(renderer, node, bake); });
	ForceRebake();
}

FontReplacement::NamePlateText::~NamePlateText() {
	m_bakePlateDrawHook.reset();
	m_allocateBakeHook.reset();

	// Plates are marked for rebaking (and drawn live until then) before the hooks that know their bake scale go.
	try {
		ForceRebake();
	} catch (const std::exception& e) {
		Host::Error("Marking nameplates for baking failed: {}", e.what());
	}

	m_prepareHook.reset();
	m_drawBakedHook.reset();
}

void FontReplacement::NamePlateText::SetMode(FontReplacementNamePlateMode mode) {
	if (m_mode == mode)
		return;
	m_mode = mode;
	ForceRebake();
}

XivAlexander::FontReplacementNamePlateMode FontReplacement::NamePlateText::GetEffectiveMode() const {
	using Mode = XivAlexander::FontReplacementNamePlateMode;
	if (!m_replacer.Enabled())
		return Mode::Game;
	return m_mode == Mode::Live && !m_bakePlateDrawHook ? Mode::BakedAtFullSize : m_mode;
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

std::optional<float> FontReplacement::NamePlateText::GetFullSizeScale(uintptr_t obj) const {
	const auto text = At<uintptr_t>(obj + m_objectNameText);
	if (!text)
		return std::nullopt;
	auto shown = 1.f;
	for (auto node = text; node; node = At<uintptr_t>(node + m_nodeParent))
		shown *= At<float>(node + m_nodeScaleX);
	if (!std::isfinite(shown) || shown <= 0)
		return std::nullopt;
	const auto a = static_cast<float>(At<uint8_t>(obj + m_bakeAlpha)) / 255.f;
	const auto scale = shown / (std::max)(a * a * a * a, MinDistanceFactor);
	return std::isfinite(scale) ? std::optional(scale) : std::nullopt;
}

// OnRequestedUpdate calls this for plates with NeedsToBeBaked, which it clears on success.
uint8_t FontReplacement::NamePlateText::AllocateBakeDetour(uintptr_t renderer, uintptr_t obj) {
	SeeRenderer(renderer);
	auto previous = 0.f;
	if (const auto it = m_bakeScales.find(obj); it != m_bakeScales.end()) {
		previous = it->second;
		m_bakeScales.erase(it);
	}

	const auto fullSize = GetEffectiveMode() == FontReplacementNamePlateMode::BakedAtFullSize ? GetFullSizeScale(obj) : std::nullopt;
	if (!fullSize)
		return m_allocateBakeHook->Original(renderer, obj);

	// Never below the previous bake scale, since the plate was shown that large.
	const auto scale = std::clamp((std::max)(*fullSize, previous), MinBakeScale, MaxBakeScale);

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

	// The original sets the node scale to 1, so a region allocated at another scale needs its own.
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

	// Plates shown over 100 percent up close or when targeted outgrow their bake, and are drawn live until baked again.
	const auto obj = bake;
	if (shown > scale * (1 + ScaleTolerance) && !NeedsToBeBaked(obj) && GetEffectiveMode() == FontReplacementNamePlateMode::BakedAtFullSize)
		SetNeedsToBeBaked(obj);

	// The original point samples only under an exact identity transform (bilinear blurs moving plates even at 1:1), so the
	// node is given in bake pixels and a near-1 transform snaps to identity, as plates aren't rotated.
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

// Live mode borrows the renderer's own live mode, which the game's text render mode setting normally controls.
void FontReplacement::NamePlateText::BakePlateDrawDetour(uintptr_t renderer, uintptr_t node) {
	SeeRenderer(renderer);
	if (GetEffectiveMode() != FontReplacementNamePlateMode::Live) {
		m_bakePlateDrawHook->Original(renderer, node);
		return;
	}

	auto& liveMode = At<uint8_t>(renderer + m_rendererFixedFontResolution);
	const auto saved = liveMode;
	liveMode = 1;
	const auto restore = xivres::util::on_dtor([&] { liveMode = saved; });
	m_bakePlateDrawHook->Original(renderer, node);
}
