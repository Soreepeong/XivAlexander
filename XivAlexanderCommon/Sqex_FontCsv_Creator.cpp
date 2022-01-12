#include "pch.h"
#include "Sqex_FontCsv_Creator.h"

#include "Sqex_FontCsv_DirectWriteFont.h"
#include "Sqex_FontCsv_FreeTypeFont.h"
#include "Sqex_FontCsv_GdiFont.h"
#include "Sqex_Sqpack_EntryRawStream.h"
#include "Sqex_Sqpack_Reader.h"
#include "Utils_Win32_Process.h"
#include "Utils_Win32_ThreadPool.h"

inline void DebugThrowError(const std::exception& e) {
#ifdef _DEBUG
	if (Utils::Win32::MessageBoxF(nullptr, MB_ICONERROR | MB_YESNO, L"Sqex::FontCsv::FontCsvCreator::Compile::Work",
		L"Error: {}\n\nDebug?", e.what()) == IDYES)
		throw;
#endif
}

struct Sqex::FontCsv::FontCsvCreator::CharacterPlan {
private:
	mutable GlyphMeasurement m_bbox;
	char32_t m_codePoint;
	int m_offsetXModifier;
	int m_offsetYModifier;

public:
	const SeCompatibleDrawableFont<uint8_t>* Font;

	CharacterPlan(char32_t character, const SeCompatibleDrawableFont<uint8_t>* font, int offsetXModifier, int offsetYModifier)
		: m_codePoint(character)
		, m_offsetXModifier(offsetXModifier)
		, m_offsetYModifier(offsetYModifier)
		, Font(font) {
	}

	const GlyphMeasurement& GetBbox() const {
		if (m_bbox.empty) {
			m_bbox = Font->Measure(0, 0, m_codePoint);
			m_bbox.advanceX += m_offsetXModifier;
		}
		return m_bbox;
	}

	bool operator<(const CharacterPlan& r) const {
		return m_codePoint < r.m_codePoint;
	}

	bool operator<(char32_t r) const {
		return m_codePoint < r;
	}

	bool operator==(char32_t r) const {
		return m_codePoint == r;
	}

	bool operator!=(char32_t r) const {
		return m_codePoint != r;
	}

	[[nodiscard]] char32_t Character() const {
		return m_codePoint;
	}

	[[nodiscard]] int OffsetYModifier() const {
		return m_offsetYModifier;
	}
};

struct Sqex::FontCsv::FontCsvCreator::Implementation {
	Win32::TpEnvironment WorkPool = { L"FontCsvCreator::Implementation::WorkPool" };
	bool Cancelled = false;
	Win32::Semaphore CoreLimitSemaphore;

	std::vector<CharacterPlan> Plans;
	std::map<std::tuple<const SeCompatibleDrawableFont<uint8_t>*, char32_t, char32_t>, int> Kernings;

	const std::shared_ptr<ModifiableFontCsvStream> Result = std::make_shared<ModifiableFontCsvStream>();

	// arbitrary numbers
	struct {
		size_t ProgressWeight_Bbox = 929;
		size_t ProgressWeight_Layout = 20;
		size_t ProgressWeight_Draw = 50;
		size_t ProgressWeight_Kerning = 1;

		bool Indeterminate = true;
		size_t Max = 0;
		size_t Progress_Bbox = 0;
		size_t Progress_Layout = 0;
		size_t Progress_Draw = 0;
		size_t Progress_Kerning = 0;
		bool Finished = false;

		void SetMax(size_t planCount, size_t kerningCount, size_t borderThickness) {
			ProgressWeight_Draw = borderThickness
				? 1000 * borderThickness * borderThickness  // account for alpha blending
				: 50;
			Max =
				planCount * (ProgressWeight_Bbox + ProgressWeight_Layout + ProgressWeight_Draw) +
				kerningCount * ProgressWeight_Kerning;
			Indeterminate = false;
		}

		[[nodiscard]] size_t Progress() const {
			return ProgressWeight_Bbox * Progress_Bbox +
				ProgressWeight_Layout * Progress_Layout +
				ProgressWeight_Draw * Progress_Draw +
				ProgressWeight_Kerning * Progress_Kerning;
		}
	} Progress;

	CallOnDestruction WaitSemaphore() {
		if (!CoreLimitSemaphore)
			return {};
		if (CoreLimitSemaphore.Wait(INFINITE) != WAIT_OBJECT_0)
			throw std::runtime_error("wait != WAIT_OBJECT_0");
		return CallOnDestruction([this]() {
			void(CoreLimitSemaphore.Release(1));
		});
	}

	struct ResolvedExtremaInfo {
		SSIZE_T globalOffsetX = 0;
		SSIZE_T globalOffsetY = 0;
		uint8_t boundingHeight = 0;
	} Step0Result;
};

Sqex::FontCsv::FontCsvCreator::FontCsvCreator(const Win32::Semaphore& semaphore)
	: m_pImpl(std::make_unique<Implementation>()) {
	m_pImpl->CoreLimitSemaphore = semaphore;
}

Sqex::FontCsv::FontCsvCreator::~FontCsvCreator() = default;

void Sqex::FontCsv::FontCsvCreator::AddCharacter(char32_t codePoint, const SeCompatibleDrawableFont<uint8_t>* font, bool replace, bool extendRange, int offsetXModifier, int offsetYModifier) {
	if (codePoint == '\n' || codePoint == '\r')
		return;
	if (!font->HasCharacter(codePoint))
		return;

	const auto pos = std::lower_bound(m_pImpl->Plans.begin(), m_pImpl->Plans.end(), codePoint);

	if (!extendRange) {
		if (pos == m_pImpl->Plans.end() || *pos != codePoint || !replace)
			return;
		pos->Font = font;
		return;
	}

	if (pos != m_pImpl->Plans.end() && *pos == codePoint) {
		if (replace)
			pos->Font = font;
		return;
	}

	m_pImpl->Plans.insert(pos, CharacterPlan(codePoint, font, offsetXModifier, offsetYModifier));
}

void Sqex::FontCsv::FontCsvCreator::AddCharacter(const SeCompatibleDrawableFont<uint8_t>* font, bool replace, bool extendRange, int offsetXModifier, int offsetYModifier) {
	for (const auto c : font->GetAllCharacters())
		AddCharacter(c, font, replace, extendRange, offsetXModifier, offsetYModifier);
}

void Sqex::FontCsv::FontCsvCreator::AddKerning(const SeCompatibleDrawableFont<uint8_t>* font, char32_t left, char32_t right, int distance, bool replace) {
	if (replace)
		m_pImpl->Kernings.insert_or_assign(std::make_tuple(font, left, right), distance);
	else
		m_pImpl->Kernings.emplace(std::make_tuple(font, left, right), distance);
}

void Sqex::FontCsv::FontCsvCreator::AddKerning(const SeCompatibleDrawableFont<uint8_t>* font, bool replace) {
	for (const auto& pair : font->GetKerningTable())
		AddKerning(font, pair.first.first, pair.first.second, static_cast<int>(pair.second), replace);
}

void Sqex::FontCsv::FontCsvCreator::AddFont(const SeCompatibleDrawableFont<uint8_t>* font, bool replace, bool extendRange, int offsetXModifier, int offsetYModifier) {
	for (const auto c : font->GetAllCharacters())
		AddCharacter(c, font, replace, extendRange, offsetXModifier, offsetYModifier);
	for (const auto& pair : font->GetKerningTable())
		AddKerning(font, pair.first.first, pair.first.second, static_cast<int>(pair.second), replace);
}

Sqex::FontCsv::FontGenerateProcess Sqex::FontCsv::FontCsvCreator::GetProgress() const {
	return {
		m_pImpl->Progress.Progress(),
		m_pImpl->Progress.Max,
		m_pImpl->Progress.Finished,
		m_pImpl->Progress.Indeterminate ? 1 : 0,
	};
}

void Sqex::FontCsv::FontCsvCreator::Cancel() {
	m_pImpl->Cancelled = true;
	m_pImpl->WorkPool.Cancel();
}

struct Sqex::FontCsv::FontCsvCreator::RenderTarget::Implementation {
	const uint16_t TextureWidth;
	const uint16_t TextureHeight;
	const uint16_t GlyphGap;
	uint16_t CurrentX;
	uint16_t CurrentY;
	uint16_t CurrentLineHeight;

	std::vector<std::shared_ptr<Texture::MemoryBackedMipmap>> Mipmaps;
	std::map<std::tuple<char32_t, const SeCompatibleDrawableFont<uint8_t>*, uint8_t, uint8_t, uint8_t, uint8_t>, AllocatedSpace> DrawnGlyphs;

	struct WorkItem {
		Texture::MemoryBackedMipmap* mipmap;
		SSIZE_T x;
		SSIZE_T y;
		char32_t c;
		const SeCompatibleDrawableFont<uint8_t>* font;
		uint8_t borderThickness;
		uint8_t borderOpacity;

		void Work() {
			if (borderThickness) {
				for (auto i = 0; i <= 2 * borderThickness; ++i)
					for (auto j = 0; j <= 2 * borderThickness; ++j)
						font->Draw(mipmap, x + i, y + j, c, borderOpacity, 0, 0xFF, 0);
				font->Draw(mipmap, x + borderThickness, y + borderThickness, c, 0xFF, 0, 0xFF, 0);
			} else
				font->Draw(mipmap, x, y, c, 0xFF, 0x00);
		}
	};

	std::deque<WorkItem> WorkItems;
	std::atomic_size_t ProcessedWorkItemIndex;

	std::pair<AllocatedSpace, bool> AllocateSpace(char32_t c, const SeCompatibleDrawableFont<uint8_t>* font, uint8_t boundingWidth, uint8_t boundingHeight, uint8_t borderThickness, uint8_t borderOpacity) {
		const auto actualGlyphGap = static_cast<uint16_t>(GlyphGap + borderThickness);

		const auto [it, isNewEntry] = DrawnGlyphs.emplace(std::make_tuple(c, font, borderThickness, borderOpacity, boundingWidth, boundingHeight), AllocatedSpace{});
		if (isNewEntry) {
			auto newTargetRequired = false;
			if (Mipmaps.empty())
				newTargetRequired = true;
			else {
				if (static_cast<size_t>(0) + CurrentX + boundingWidth + actualGlyphGap >= TextureWidth) {
					CurrentX = actualGlyphGap;
					CurrentY += CurrentLineHeight + actualGlyphGap + 1;  // Account for rounding errors
					CurrentLineHeight = 0;
				}
				if (CurrentY + boundingHeight + actualGlyphGap + 1 >= TextureHeight)
					newTargetRequired = true;
			}

			if (newTargetRequired) {
				Mipmaps.emplace_back(std::make_shared<Texture::MemoryBackedMipmap>(TextureWidth, TextureHeight, 1, Texture::Format::L8));
				CurrentX = CurrentY = GlyphGap;
				CurrentLineHeight = 0;
			}

			if (CurrentX < actualGlyphGap)
				CurrentX = actualGlyphGap;
			if (CurrentY < actualGlyphGap)
				CurrentY = actualGlyphGap;

			it->second = AllocatedSpace{
				.Index = static_cast<uint16_t>(Mipmaps.size() - 1),
				.X = CurrentX,
				.Y = CurrentY,
				.BoundingHeight = boundingHeight,
			};

			CurrentX += boundingWidth + GlyphGap;
			CurrentLineHeight = std::max<uint16_t>(CurrentLineHeight, boundingHeight);
		}

		return std::make_pair(it->second, isNewEntry);
	}

	template<typename TextureTypeSupportingRGBA = Texture::RGBA4444, Texture::Format TextureFormat = Texture::Format::A4R4G4B4>
	void Finalize() {
		auto mipmaps{std::move(Mipmaps)};
		while (mipmaps.size() % 4)
			mipmaps.push_back(std::make_shared<Texture::MemoryBackedMipmap>(mipmaps[0]->Width(), mipmaps[0]->Height(), 1, Texture::Format::L8));

		for (size_t i = 0; i < mipmaps.size() / 4; ++i) {
			Mipmaps.push_back(std::make_shared<Texture::MemoryBackedMipmap>(mipmaps[0]->Width(), mipmaps[0]->Height(), 1, TextureFormat));

			const auto target = Mipmaps.back()->View<TextureTypeSupportingRGBA>();
			const auto b = mipmaps[i * 4 + 0]->View<uint8_t>();
			const auto g = mipmaps[i * 4 + 1]->View<uint8_t>();
			const auto r = mipmaps[i * 4 + 2]->View<uint8_t>();
			const auto a = mipmaps[i * 4 + 3]->View<uint8_t>();
			for (size_t j = 0; j < target.size(); ++j)
				target[j].SetFrom(
					r[j] * TextureTypeSupportingRGBA::MaxR / 255,
					g[j] * TextureTypeSupportingRGBA::MaxG / 255,
					b[j] * TextureTypeSupportingRGBA::MaxB / 255,
					a[j] * TextureTypeSupportingRGBA::MaxA / 255
				);
		}
	}
};

Sqex::FontCsv::FontCsvCreator::RenderTarget::RenderTarget(uint16_t textureWidth, uint16_t textureHeight, uint16_t glyphGap)
	: m_pImpl(std::make_unique<Implementation>(textureWidth, textureHeight, glyphGap, glyphGap, glyphGap, 0)) {
}

Sqex::FontCsv::FontCsvCreator::RenderTarget::~RenderTarget() = default;

void Sqex::FontCsv::FontCsvCreator::RenderTarget::Finalize(Texture::Format textureFormat) {
	switch (textureFormat) {
		case Texture::Format::A4R4G4B4:
			return m_pImpl->Finalize<Texture::RGBA4444, Texture::Format::A4R4G4B4>();

		case Texture::Format::A8R8G8B8:
			return m_pImpl->Finalize<Texture::RGBA8888, Texture::Format::A8R8G8B8>();

		case Texture::Format::X8R8G8B8:
			return m_pImpl->Finalize<Texture::RGBA8888, Texture::Format::X8R8G8B8>();

		default:
			throw std::invalid_argument("Unsupported texture type for generating font");
	}
}

std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> Sqex::FontCsv::FontCsvCreator::RenderTarget::AsMipmapStreamVector() const {
	std::vector<std::shared_ptr<const Texture::MipmapStream>> res;
	for (const auto& i : m_pImpl->Mipmaps)
		res.emplace_back(i);
	return res;
}

std::vector<std::shared_ptr<Sqex::Texture::ModifiableTextureStream>> Sqex::FontCsv::FontCsvCreator::RenderTarget::AsTextureStreamVector() const {
	std::vector<std::shared_ptr<Texture::ModifiableTextureStream>> res;
	for (const auto& i : m_pImpl->Mipmaps) {
		auto texture = std::make_shared<Texture::ModifiableTextureStream>(i->Type(), i->Width(), i->Height());
		texture->AppendMipmap(i);
		res.emplace_back(std::move(texture));
	}
	return res;
}

Sqex::FontCsv::FontCsvCreator::RenderTarget::AllocatedSpace Sqex::FontCsv::FontCsvCreator::RenderTarget::QueueDraw(
	char32_t c,
	const SeCompatibleDrawableFont<uint8_t>* font,
	SSIZE_T drawOffsetX, SSIZE_T drawOffsetY,
	uint8_t boundingWidth, uint8_t boundingHeight,
	uint8_t borderThickness, uint8_t borderOpacity
) {
	const auto [space, drawRequired] = m_pImpl->AllocateSpace(c, font, boundingWidth, boundingHeight, borderThickness, borderOpacity);

	if (drawRequired) {
		m_pImpl->WorkItems.emplace_back(Implementation::WorkItem{
			.mipmap = m_pImpl->Mipmaps[space.Index].get(),
			.x = space.X + drawOffsetX,
			.y = space.Y + drawOffsetY,
			.c = c,
			.font = font,
			.borderThickness = borderThickness,
			.borderOpacity = borderOpacity,
		});
	}

	return space;
}

bool Sqex::FontCsv::FontCsvCreator::RenderTarget::WorkOnNextItem() {
	const auto index = m_pImpl->ProcessedWorkItemIndex++;
	if (index >= m_pImpl->WorkItems.size())
		return false;

	m_pImpl->WorkItems[index].Work();

	return true;
}

void Sqex::FontCsv::FontCsvCreator::Step0_CalcMax() {
	m_pImpl->Progress.SetMax(
		m_pImpl->Plans.size(),
		m_pImpl->Kernings.size(),
		this->BorderOpacity ? this->BorderThickness : 0);
}

void Sqex::FontCsv::FontCsvCreator::Step1_CalcBbox() {
	const auto borderThickness = static_cast<uint8_t>(this->BorderOpacity ? this->BorderThickness : 0);
	GlyphMeasurement maxBbox;
	uint32_t maxAscent = 0, maxLineHeight = 0;
	{
		std::vector<GlyphMeasurement> maxBboxes(m_pImpl->WorkPool.ThreadCount());
		std::vector<uint32_t> maxAscents(m_pImpl->WorkPool.ThreadCount());
		std::vector<uint32_t> maxLineHeights(m_pImpl->WorkPool.ThreadCount());
		for (size_t i = 0; i < m_pImpl->WorkPool.ThreadCount(); ++i) {
			m_pImpl->WorkPool.SubmitWork([this, &maxBboxes, &maxAscents, &maxLineHeights, startI = i]() {
				try {
					const auto waitRelease = m_pImpl->WaitSemaphore();
					auto& box = maxBboxes[startI];
					auto& ascent = maxAscents[startI];
					auto& lineHeight = maxLineHeights[startI];
					for (auto i = startI; i < m_pImpl->Plans.size(); i += maxBboxes.size()) {
						if (m_pImpl->Cancelled)
							return;

						m_pImpl->Progress.Progress_Bbox++;

						box.ExpandToFit(m_pImpl->Plans[i].GetBbox());
						ascent = std::max(ascent, m_pImpl->Plans[i].Font->Ascent());
						lineHeight = std::max(lineHeight, m_pImpl->Plans[i].Font->LineHeight());
					}
				} catch (const std::exception& e) {
					OnError(e);
					DebugThrowError(e);
				}
			});
		}
		m_pImpl->WorkPool.WaitOutstanding();
		if (m_pImpl->Cancelled)
			return;

		for (const auto& bbox : maxBboxes)
			maxBbox.ExpandToFit(bbox);
		for (const auto& n : maxAscents)
			maxAscent = std::max(n, maxAscent);
		for (const auto& n : maxLineHeights)
			maxLineHeight = std::max(n, maxLineHeight);
	}

	m_pImpl->Result->Ascent((AscentPixels == AutoVerticalValues ? maxAscent : AscentPixels) + borderThickness);
	m_pImpl->Result->LineHeight((LineHeightPixels == AutoVerticalValues ? maxLineHeight : LineHeightPixels) + 2 * borderThickness);

	m_pImpl->Step0Result = {
		.globalOffsetX = std::max<SSIZE_T>(MinGlobalOffsetX,
			std::min<SSIZE_T>(MaxGlobalOffsetX,
				std::max<SSIZE_T>(0, -maxBbox.left))),
		.globalOffsetY = std::min<SSIZE_T>(maxBbox.top, 0),
		.boundingHeight = static_cast<uint8_t>(maxBbox.bottom + std::max<SSIZE_T>(-maxBbox.top, 0)),
	};
}

uint16_t Sqex::FontCsv::FontCsvCreator::RenderTarget::TextureWidth() const {
	return m_pImpl->TextureWidth;
}

uint16_t Sqex::FontCsv::FontCsvCreator::RenderTarget::TextureHeight() const {
	return m_pImpl->TextureHeight;
}

void Sqex::FontCsv::FontCsvCreator::Step2_Layout(RenderTarget& renderTarget) {
	try {
		const auto borderThickness = static_cast<uint8_t>(this->BorderOpacity ? this->BorderThickness : 0);
		const auto borderOpacity = static_cast<uint8_t>(this->BorderThickness ? this->BorderOpacity : 0);

		m_pImpl->Result->TextureWidth(renderTarget.TextureWidth());
		m_pImpl->Result->TextureHeight(renderTarget.TextureHeight());
		m_pImpl->Result->Points(SizePoints);
		m_pImpl->Result->ReserveStorage(m_pImpl->Plans.size(), m_pImpl->Kernings.size());
		for (auto& plan : m_pImpl->Plans) {
			if (m_pImpl->Cancelled)
				return;

			m_pImpl->Progress.Progress_Layout++;

			const auto& bbox = plan.GetBbox();
			if (bbox.empty)
				continue;

			const auto leftExtension = std::max<SSIZE_T>(-bbox.left, m_pImpl->Step0Result.globalOffsetX);
			const auto boundingWidth = static_cast<uint8_t>(leftExtension + bbox.right + borderThickness + borderThickness);
			const auto nextOffsetX = static_cast<int8_t>(bbox.advanceX + leftExtension - boundingWidth - m_pImpl->Step0Result.globalOffsetX);
			auto currentOffsetY = static_cast<int8_t>(
				(AlignToBaseline ? m_pImpl->Result->Ascent() - plan.Font->Ascent() : (0LL + m_pImpl->Result->LineHeight() - plan.Font->LineHeight()) / 2)
				+ m_pImpl->Step0Result.globalOffsetY
				+ GlobalOffsetYModifier
				- borderThickness
			);

			auto boundingHeight = m_pImpl->Step0Result.boundingHeight;
			auto drawOffsetY = 0;

			if (CompactLayout) {
				drawOffsetY = -static_cast<int8_t>(bbox.top);
				currentOffsetY += static_cast<int8_t>(bbox.top - m_pImpl->Step0Result.globalOffsetY);
				boundingHeight = static_cast<uint8_t>(bbox.Height());
			}

			currentOffsetY += static_cast<int8_t>(plan.OffsetYModifier());

			boundingHeight += static_cast<SSIZE_T>(2) * borderThickness;
			const auto space = renderTarget.QueueDraw(plan.Character(), plan.Font,
				leftExtension, drawOffsetY,
				boundingWidth, boundingHeight, borderThickness, borderOpacity);

			boundingHeight = std::min(space.BoundingHeight, boundingHeight);
			m_pImpl->Result->AddFontEntry(plan.Character(), space.Index, space.X, space.Y, boundingWidth, boundingHeight, nextOffsetX, currentOffsetY);
		}
	} catch (const std::exception& e) {
		OnError(e);
		DebugThrowError(e);
	}
}

void Sqex::FontCsv::FontCsvCreator::Step3_Draw(RenderTarget& target) {
	try {
		for (size_t i = 0; i < m_pImpl->WorkPool.ThreadCount(); ++i) {
			m_pImpl->WorkPool.SubmitWork([&]() {
				try {
					while (!m_pImpl->Cancelled && target.WorkOnNextItem())
						m_pImpl->Progress.Progress_Draw++;
				} catch (const std::exception& e) {
					OnError(e);
					DebugThrowError(e);
				}
			});
		}

		for (const auto& [pair, distance] : m_pImpl->Kernings) {
			m_pImpl->Progress.Progress_Kerning++;

			const auto [font, c1, c2] = pair;
			const auto f1it = std::lower_bound(m_pImpl->Plans.begin(), m_pImpl->Plans.end(), c1);
			const auto f2it = std::lower_bound(m_pImpl->Plans.begin(), m_pImpl->Plans.end(), c2);

			if (f1it == m_pImpl->Plans.end() || f2it == m_pImpl->Plans.end())
				continue;

			if (f1it->Font != font && f2it->Font != font)
				continue;

			if (f1it->Font != f2it->Font
				&& AlwaysApplyKerningCharacters.find(c1) == AlwaysApplyKerningCharacters.end()
				&& AlwaysApplyKerningCharacters.find(c2) == AlwaysApplyKerningCharacters.end())
				continue;

			if (!distance)
				continue;

			m_pImpl->Result->AddKerning(c1, c2, distance);
		}

		m_pImpl->WorkPool.WaitOutstanding();
		m_pImpl->Progress.Finished = true;
	} catch (const std::exception& e) {
		OnError(e);
		DebugThrowError(e);
	}
}

std::shared_ptr<Sqex::FontCsv::ModifiableFontCsvStream> Sqex::FontCsv::FontCsvCreator::GetResult() const {
	return m_pImpl->Result;
}

struct Sqex::FontCsv::FontSetsCreator::Implementation {
	const CreateConfig::FontCreateConfig Config;
	const std::filesystem::path GamePath;
	const Win32::Event CancelEvent = Win32::Event::Create();
	bool Cancelled = false;
	Win32::Thread WorkerThread;

	std::map<Sqex::GameReleaseRegion, std::filesystem::path> GameRootDirectories;

	std::mutex SourceFontMapAccessMtx, SourceFontLoadMtx;
	std::map<std::filesystem::path, std::unique_ptr<Sqpack::Reader>> SqpackReaders;
	std::map<std::tuple<std::filesystem::path, std::filesystem::path>, std::vector<std::shared_ptr<const Texture::MipmapStream>>> GameTextures;
	std::map<std::string, std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>> SourceFonts;
	std::map<std::string, std::filesystem::path> ResolvedGameIndexFiles;

	ResultFontSets Result;
	std::mutex ResultMtx;
	std::map<std::string, std::map<std::string, std::unique_ptr<FontCsvCreator>>> ResultWork;

	Win32::TpEnvironment WorkPool = { L"FontSetsCreator::Implementation::WorkPool" };
	std::map<std::string, std::unique_ptr<Win32::TpEnvironment>> TextureGroupWorkPools;
	Win32::Semaphore CoreLimitSemaphore;
	std::string LastErrorMessage;

	CallOnDestruction::Multiple Cleanup;

	Implementation(CreateConfig::FontCreateConfig config, std::filesystem::path gamePath, LONG maxCoreCount)
		: Config(std::move(config))
		, GamePath(std::move(gamePath))
		, CoreLimitSemaphore(maxCoreCount ? Win32::Semaphore::Create(nullptr, maxCoreCount, maxCoreCount) : nullptr) {
	}

	~Implementation() {
		Cleanup.Clear();
	}

	CallOnDestruction WaitSemaphore() {
		if (!CoreLimitSemaphore)
			return {};
		if (CoreLimitSemaphore.Wait(INFINITE) != WAIT_OBJECT_0)
			throw std::runtime_error("wait != WAIT_OBJECT_0");
		return CallOnDestruction([this]() {
			void(CoreLimitSemaphore.Release(1));
		});
	}

	const SeCompatibleDrawableFont<uint8_t>& GetSourceFont(const std::string& name) {
		{
			const auto lock = std::lock_guard(SourceFontMapAccessMtx);
			if (const auto it = SourceFonts.find(name); it != SourceFonts.end())
				return *it->second;
		}
		{
			const auto lockLoad = std::lock_guard(SourceFontLoadMtx);

			// test again
			{
				const auto lock = std::lock_guard(SourceFontMapAccessMtx);
				if (const auto it = SourceFonts.find(name); it != SourceFonts.end())
					return *it->second;
			}

			std::shared_ptr<SeCompatibleDrawableFont<uint8_t>> newFont;
			const auto& inputFontSource = Config.sources.at(name);
			if (const auto& source = inputFontSource.gameSource; inputFontSource.isGameSource) {
				std::filesystem::path indexFilePath;
				if (source.indexFile.empty() && source.gameIndexFileName.empty())
					indexFilePath = GamePath / LR"(sqpack\ffxiv\000000.win32.index)";
				else {
					indexFilePath = canonical(source.indexFile);
					if (!exists(indexFilePath)) {
						indexFilePath = ResolvedGameIndexFiles.at(source.gameIndexFileName);
					}
				}

				auto reader = SqpackReaders.find(indexFilePath);
				if (reader == SqpackReaders.end())
					reader = SqpackReaders.emplace(indexFilePath, std::make_unique<Sqpack::Reader>(indexFilePath)).first;

				auto textureKey = std::make_tuple(indexFilePath, source.texturePath);
				auto textures = GameTextures.find(textureKey);
				if (textures == GameTextures.end())
					textures = GameTextures.emplace(textureKey, std::vector<std::shared_ptr<const Texture::MipmapStream>>()).first;

				for (size_t i = textures->second.size(); ; ++i) {
					try {
						const auto texturePath = std::format(source.texturePath.string(), i + 1);
						auto provider = reader->second->GetEntryProvider(texturePath);
						auto rawTextureStream = std::make_shared<Sqpack::EntryRawStream>(std::move(provider));
						auto mipmap = Texture::MipmapStream::FromTexture(std::move(rawTextureStream), 0);

						// preload sqex entry layout so that we can process stuff multithreaded later
						void(mipmap->ReadStreamIntoVector<char>(0));

						textures->second.emplace_back(std::move(mipmap));
					} catch (const std::out_of_range&) {
						break;
					}
				}

				newFont = std::make_shared<SeDrawableFont<Texture::RGBA4444, uint8_t>>(
					std::make_shared<ModifiableFontCsvStream>(Sqpack::EntryRawStream(reader->second->GetEntryProvider(source.fdtPath))), textures->second);
				newFont->AdvanceWidthDelta(source.advanceWidthDelta);

			} else if (const auto& source = inputFontSource.gdiSource; inputFontSource.isGdiSource) {
				auto s2(source);
				s2.lfHeight *= source.oversampleScale;
				newFont = std::make_shared<GdiDrawingFont<uint8_t>>(s2);
				newFont->AdvanceWidthDelta(source.advanceWidthDelta);

				if (source.oversampleScale > 1)
					newFont = std::make_shared<Sqex::FontCsv::SeOversampledFont<uint8_t>>(std::move(newFont), source.oversampleScale);

			} else if (const auto& source = inputFontSource.directWriteSource; inputFontSource.isDirectWriteSource) {
				std::shared_ptr<DirectWriteDrawingFont<uint8_t>> dfont;

				if (source.fontFile.empty() && source.familyName.empty())
					throw std::invalid_argument("Neither of fontFile nor familyName was specified.");

				std::string accumulatedError;
				if (!source.fontFile.empty()) {
					try {
						dfont = std::make_shared<DirectWriteDrawingFont<uint8_t>>(
							source.fontFile, source.faceIndex, static_cast<float>(source.height * source.oversampleScale), source.renderMode
						);
					} catch (const std::exception& e) {
						if (source.familyName.empty())
							throw;
						accumulatedError += e.what();
					}
				}

				if (!dfont && !source.familyName.empty()) {
					try {
						dfont = std::make_shared<DirectWriteDrawingFont<uint8_t>>(
							FromUtf8(source.familyName).c_str(), static_cast<float>(source.height * source.oversampleScale), static_cast<DWRITE_FONT_WEIGHT>(source.weight), source.stretch, source.style, source.renderMode
						);
					} catch (const std::exception& e) {
						if (!accumulatedError.empty())
							accumulatedError += "; ";
						accumulatedError += e.what();
					}
				}

				if (!dfont)
					throw std::invalid_argument(accumulatedError);

				if (source.measureUsingFreeType)
					dfont->SetMeasureWithFreeType();
				newFont = std::move(dfont);
				newFont->AdvanceWidthDelta(source.advanceWidthDelta * source.oversampleScale);

				if (source.oversampleScale > 1)
					newFont = std::make_shared<Sqex::FontCsv::SeOversampledFont<uint8_t>>(std::move(newFont), source.oversampleScale);

			} else if (const auto& source = inputFontSource.freeTypeSource; inputFontSource.isFreeTypeSource) {
				if (source.fontFile.empty() && source.familyName.empty())
					throw std::invalid_argument("Neither of fontFile nor familyName was specified.");

				std::string accumulatedError;
				if (!source.fontFile.empty()) {
					try {
						newFont = std::make_shared<FreeTypeDrawingFont<uint8_t>>(
							source.fontFile, source.faceIndex, static_cast<float>(source.height * source.oversampleScale), source.loadFlags
						);
					} catch (const std::exception& e) {
						if (source.familyName.empty())
							throw;
						accumulatedError += e.what();
					}
				}

				if (!newFont && !source.familyName.empty()) {
					try {
						newFont = std::make_shared<FreeTypeDrawingFont<uint8_t>>(
							FromUtf8(source.familyName).c_str(), static_cast<float>(source.height * source.oversampleScale), static_cast<DWRITE_FONT_WEIGHT>(source.weight), source.stretch, source.style, source.loadFlags
						);
					} catch (const std::exception& e) {
						if (!accumulatedError.empty())
							accumulatedError += "; ";
						accumulatedError += e.what();
					}
				}

				if (!newFont)
					throw std::invalid_argument(accumulatedError);

				newFont->AdvanceWidthDelta(source.advanceWidthDelta * source.oversampleScale);

				if (source.oversampleScale > 1)
					newFont = std::make_shared<Sqex::FontCsv::SeOversampledFont<uint8_t>>(std::move(newFont), source.oversampleScale);

			} else
				throw std::invalid_argument("Could not identify which font to load.");

			// preload for multithreading
			void(newFont->GetAllCharacters());
			void(newFont->GetKerningTable());

			const auto lockAccess = std::lock_guard(SourceFontMapAccessMtx);
			const auto newFontRawPtr = newFont.get();
			SourceFonts.emplace(name, std::move(newFont));
			return *newFontRawPtr;
		}
	}

	void Compile() {
		std::map<std::string, std::unique_ptr<FontCsvCreator::RenderTarget>> renderTargets;
		for (const auto& target : Config.targets) {
			const auto& textureGroupFilenamePattern = target.first;
			const auto& fonts = target.second;
			renderTargets.emplace(textureGroupFilenamePattern, std::make_unique<FontCsvCreator::RenderTarget>(Config.textureWidth, Config.textureHeight, Config.glyphGap));
			TextureGroupWorkPools.emplace(textureGroupFilenamePattern, std::make_unique<Win32::TpEnvironment>(L"FontCsvCreator::Implementation::TextureGroupWorkPools"));
			Result.Result.emplace(textureGroupFilenamePattern, ResultFontSet{});
			auto& remainingFonts = ResultWork.emplace(textureGroupFilenamePattern, std::map<std::string, std::unique_ptr<FontCsvCreator>>()).first->second;
			for (const auto& fontName : fonts.fontTargets | std::views::keys) {
				auto creator = std::make_unique<FontCsvCreator>();
				Cleanup += creator->OnError([this](const std::exception& e) {
					if (LastErrorMessage.empty()) {
						if (e.what() && *e.what())
							LastErrorMessage = e.what();
						else
							LastErrorMessage = "Unknown error";
						void(Win32::Thread(L"Canceller", [this]() { Cancel(false); }));
					}
				});
				remainingFonts.emplace(fontName, std::move(creator));
			}
		}

		for (const auto& target : Config.targets) {
			const auto& textureGroupFilenamePattern = target.first;
			const auto& fonts = target.second;
			WorkPool.SubmitWork([&]() {
				if (Cancelled)
					return;

				auto& resultSet = Result.Result.at(textureGroupFilenamePattern);
				auto& target = *renderTargets.at(textureGroupFilenamePattern);
				auto& remainingFonts = ResultWork.at(textureGroupFilenamePattern);
				auto& textureGroupWorkPool = *TextureGroupWorkPools.at(textureGroupFilenamePattern);
				std::vector<std::string> sortedRemainingFontList;
				for (const auto& i : fonts.fontTargets | std::views::keys)
					sortedRemainingFontList.emplace_back(i);

				// Need to calculate/render small items first to avoid small components in big fonts reserving heights
				std::ranges::sort(sortedRemainingFontList, [&](const auto& l, const auto& r) {
					return fonts.fontTargets.at(l).height < fonts.fontTargets.at(r).height;
				});

				try {
					// Step 0. Calculate max progress value.
					for (const auto& fontName : sortedRemainingFontList) {
						if (Cancelled)
							return;

						textureGroupWorkPool.SubmitWork([this, &plan = fonts.fontTargets.at(fontName) , &creator = *remainingFonts.at(fontName)]() {
							try {
								const auto semaphoreHolder = WaitSemaphore();
								if (Cancelled)
									return;

								creator.SizePoints = static_cast<float>(plan.height);
								if (plan.autoAscent)
									creator.AscentPixels = FontCsvCreator::AutoVerticalValues;
								else if (!plan.ascentFrom.empty())
									creator.AscentPixels = GetSourceFont(plan.ascentFrom).Ascent();
								else
									creator.AscentPixels = 0;
								if (plan.autoLineHeight)
									creator.LineHeightPixels = FontCsvCreator::AutoVerticalValues;
								else if (!plan.ascentFrom.empty())
									creator.LineHeightPixels = GetSourceFont(plan.lineHeightFrom).LineHeight();
								else
									creator.LineHeightPixels = 0;
								creator.MinGlobalOffsetX = plan.minGlobalOffsetX;
								creator.MaxGlobalOffsetX = plan.maxGlobalOffsetX;
								creator.GlobalOffsetYModifier = plan.globalOffsetY;
								creator.AlwaysApplyKerningCharacters.insert(plan.charactersToKernAcrossFonts.begin(), plan.charactersToKernAcrossFonts.end());
								creator.AlignToBaseline = plan.alignToBaseline;
								creator.BorderThickness = plan.borderThickness;
								creator.BorderOpacity = plan.borderOpacity;
								creator.CompactLayout = plan.compactLayout == CreateConfig::SingleFontTarget::CompactLayout_NoOverride ? Config.compactLayout : (plan.compactLayout == CreateConfig::SingleFontTarget::CompactLayout_Override_Enable);

								for (const auto& source : plan.sources) {
									const auto& sourceFont = GetSourceFont(source.name);
									if (source.ranges.empty()) {
										creator.AddFont(&sourceFont, source.replace, source.extendRange, source.offsetXModifier, source.offsetYModifier);
									} else {
										for (const auto& rangeName : source.ranges) {
											for (const auto& range : Config.ranges.at(rangeName).ranges | std::views::values) {
												for (auto i = range.from; i < range.to; ++i)
													creator.AddCharacter(i, &sourceFont, source.replace, source.extendRange, source.offsetXModifier, source.offsetYModifier);
												// separate line to prevent overflow (i < range.to might never be false)
												creator.AddCharacter(range.to, &sourceFont, source.replace, source.extendRange, source.offsetXModifier, source.offsetYModifier);

												if (Cancelled)
													return;
											}
										}
									}
									creator.AddKerning(&sourceFont, source.replace);

									if (Cancelled)
										return;
								}

								creator.Step0_CalcMax();
							} catch (const std::exception& e) {
								if (LastErrorMessage.empty()) {
									LastErrorMessage = e.what() && *e.what() ? e.what() : "Unknown error";
									void(Win32::Thread(L"Canceller", [this]() { Cancel(false); }));

									DebugThrowError(e);
								}
							}
						});
					}
					textureGroupWorkPool.WaitOutstanding();

					// Step 1. Calculate bounding boxes of every glyph used.
					for (const auto& fontName : sortedRemainingFontList) {
						textureGroupWorkPool.SubmitWork([this, &creator = *remainingFonts.at(fontName)]() {
							try {
								const auto semaphoreHolder = WaitSemaphore();
								if (Cancelled)
									return;

								creator.Step1_CalcBbox();
							} catch (const std::exception& e) {
								if (LastErrorMessage.empty()) {
									LastErrorMessage = e.what() && *e.what() ? e.what() : "Unknown error";
									void(Win32::Thread(L"Canceller", [this]() { Cancel(false); }));

									DebugThrowError(e);
								}
							}
						});
					}
					textureGroupWorkPool.WaitOutstanding();

					// Step 2. Calculate where to put each glyph. Cannot be serialized.
					for (const auto& fontName : sortedRemainingFontList) {
						if (Cancelled)
							return;

						remainingFonts.at(fontName)->Step2_Layout(target);
					}

					// Step 3. Draw glyphs onto mipmaps.
					for (const auto& fontName : sortedRemainingFontList) {
						if (Cancelled)
							return;

						textureGroupWorkPool.SubmitWork([&, fontName = fontName, &creator = *remainingFonts.at(fontName)]() {
							try {
								const auto semaphoreHolder = WaitSemaphore();
								if (Cancelled)
									return;

								creator.Step3_Draw(target);
								if (Cancelled)
									return;

								auto compileResult = creator.GetResult();
								{
									const auto lock = std::lock_guard(ResultMtx);
									resultSet.Fonts.emplace(fontName, std::move(compileResult));
									if (resultSet.Fonts.size() != sortedRemainingFontList.size())
										return;
								}
								if (Cancelled)
									return;

								target.Finalize(Config.textureFormat);

								resultSet.Textures = target.AsTextureStreamVector();
							} catch (const std::exception& e) {
								if (LastErrorMessage.empty()) {
									LastErrorMessage = e.what() && *e.what() ? e.what() : "Unknown error";
									void(Win32::Thread(L"Canceller", [this]() { Cancel(false); }));

									DebugThrowError(e);
								}
							}
						});
					}
				} catch (const std::exception& e) {
					if (LastErrorMessage.empty()) {
						LastErrorMessage = e.what() && *e.what() ? e.what() : "Unknown error";
						void(Win32::Thread(L"Canceller", [this]() { Cancel(false); }));

						DebugThrowError(e);
					}
				}
				textureGroupWorkPool.WaitOutstanding();
			});
		}

		WorkPool.WaitOutstanding();
	}

	void Cancel(bool wait = true) {
		Cancelled = true;
		CancelEvent.Set();
		WorkPool.Cancel();
		for (const auto& c : TextureGroupWorkPools)
			c.second->Cancel();
		const auto lock = std::lock_guard(ResultMtx);
		for (const auto& v1 : ResultWork | std::views::values) {
			for (const auto& v2 : v1 | std::views::values) {
				v2->Cancel();
			}
		}
		if (wait && WorkerThread)
			WorkerThread.Wait();
	}
};

Sqex::FontCsv::FontSetsCreator::FontSetsCreator(CreateConfig::FontCreateConfig config, std::filesystem::path gamePath, LONG maxCoreCount)
	: m_pImpl(std::make_unique<Implementation>(std::move(config), std::move(gamePath), maxCoreCount ? maxCoreCount : Win32::GetCoreCount())) {
}

Sqex::FontCsv::FontSetsCreator::~FontSetsCreator() {
	m_pImpl->Cancel();
}

std::map<Sqex::Sqpack::EntryPathSpec, std::shared_ptr<const Sqex::RandomAccessStream>, Sqex::Sqpack::EntryPathSpec::FullPathComparator> Sqex::FontCsv::FontSetsCreator::ResultFontSets::GetAllStreams() const {
	std::map<Sqpack::EntryPathSpec, std::shared_ptr<const RandomAccessStream>, Sqpack::EntryPathSpec::FullPathComparator> result;

	for (const auto& fontSet : Result) {
		for (size_t i = 0; i < fontSet.second.Textures.size(); ++i)
			result.emplace(std::format("common/font/{}", std::format(fontSet.first, i + 1)), fontSet.second.Textures[i]);

		for (const auto& entry : fontSet.second.Fonts)
			result.emplace(std::format("common/font/{}", entry.first), entry.second);
	}

	return result;
}

_COM_SMARTPTR_TYPEDEF(IDWriteFactory, __uuidof(IDWriteFactory));
_COM_SMARTPTR_TYPEDEF(IDWriteFontCollection, __uuidof(IDWriteFontCollection));

void Sqex::FontCsv::FontSetsCreator::ProvideGameDirectory(Sqex::GameReleaseRegion region, std::filesystem::path path) {
	m_pImpl->GameRootDirectories.emplace(region, std::move(path));
}

void Sqex::FontCsv::FontSetsCreator::VerifyRequirements(
	const std::function<std::filesystem::path(const CreateConfig::GameIndexFile&)>& promptGameIndexFile,
	const std::function<bool(const CreateConfig::FontRequirement&)>& promptFontRequirement
) {
	const auto relativeTo = Win32::Process::Current().PathOf();
	const auto Succ = [](HRESULT hr) {
		if (!SUCCEEDED(hr))
			throw Win32::Error(_com_error(hr));
	};

	IDWriteFactoryPtr factory;
	IDWriteFontCollectionPtr coll;

	for (const auto& singleTextureTarget : m_pImpl->Config.targets | std::views::values) {
		for (const auto& target : singleTextureTarget.fontTargets | std::views::values) {
			for (const auto& sourceInfo : target.sources) {
				auto& sourceOuter = m_pImpl->Config.sources.at(sourceInfo.name);
				if (sourceOuter.isGameSource) {
					auto& source = sourceOuter.gameSource;
					if (!source.indexFile.empty() && exists(Win32::TranslatePath(source.indexFile, relativeTo)))
						continue;
					for (const auto& [name, gameIndexFile] : m_pImpl->Config.gameIndexFiles) {
						if ([&] {
							if (m_pImpl->ResolvedGameIndexFiles.contains(name))
								return 0;

							for (auto path : gameIndexFile.pathList) {
								path = Win32::TranslatePath(path, Win32::Process::Current().PathOf());
								if (exists(path)) {
									m_pImpl->ResolvedGameIndexFiles.emplace(name, std::move(path));
									return 0;
								}
							}

							for (const auto& [region, rootDirectory] : m_pImpl->GameRootDirectories) {
								if (region == gameIndexFile.autoDetectRegion) {
									auto path = rootDirectory / "game" / "sqpack" / gameIndexFile.autoDetectIndexExpac / std::format("{}.win32.index", gameIndexFile.autoDetectIndexFile);
									if (!exists(path))
										continue;
									m_pImpl->ResolvedGameIndexFiles.emplace(name, std::move(path));
									return 0;
								}
							}

							for (auto path : gameIndexFile.fallbackPathList) {
								path = Win32::TranslatePath(path, Win32::Process::Current().PathOf());
								if (exists(path)) {
									m_pImpl->ResolvedGameIndexFiles.emplace(name, std::move(path));
									return 0;
								}
							}

							return 1;
						}()) {
							auto path = promptGameIndexFile(gameIndexFile);
							if (path.empty())
								return;
							path = path / "game" / "sqpack" / gameIndexFile.autoDetectIndexExpac / std::format("{}.win32.index", gameIndexFile.autoDetectIndexFile);
							m_pImpl->ResolvedGameIndexFiles.emplace(name, std::move(path));
						}
					}
				} else if (sourceOuter.isDirectWriteSource || (
					sourceOuter.isFreeTypeSource
					&& (sourceOuter.freeTypeSource.fontFile.empty()
						|| !exists(Win32::TranslatePath(sourceOuter.freeTypeSource.fontFile, relativeTo))
					)
					&& !sourceOuter.freeTypeSource.familyName.empty())) {

					const auto familyName = FromUtf8(sourceOuter.isDirectWriteSource ? sourceOuter.directWriteSource.familyName : sourceOuter.freeTypeSource.familyName);
					for (const auto& rule : m_pImpl->Config.fontRequirements) {
						const auto wname = FromUtf8(rule.name);
						if (lstrcmpiW(wname.c_str(), familyName.c_str()) == 0) {
							while (true) {
								if (!coll) {
									if (!factory)
										Succ(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)));
									Succ(factory->GetSystemFontCollection(&coll));
								}

								BOOL exists = false;
								UINT32 index = UINT32_MAX;
								Succ(coll->FindFamilyName(familyName.c_str(), &index, &exists));
								if (exists)
									break;

								if (!promptFontRequirement(rule))
									return;
								coll = nullptr;
							}
						}
					}
				}
			}
		}
	}
}

void Sqex::FontCsv::FontSetsCreator::Start() {
	m_pImpl->WorkerThread = Win32::Thread(L"FontSetsCreator", [this] { m_pImpl->Compile(); });
}

const Sqex::FontCsv::FontSetsCreator::ResultFontSets& Sqex::FontCsv::FontSetsCreator::GetResult() const {
	if (m_pImpl->Cancelled)
		throw std::runtime_error(m_pImpl->LastErrorMessage.empty() ? "Cancelled" : m_pImpl->LastErrorMessage.c_str());
	if (!m_pImpl->WorkerThread || m_pImpl->WorkerThread.Wait(0) == WAIT_TIMEOUT)
		throw std::runtime_error("not finished");

	return m_pImpl->Result;
}

bool Sqex::FontCsv::FontSetsCreator::Wait(DWORD timeout) const {
	if (!m_pImpl->WorkerThread)
		throw std::runtime_error("Not started yet");
	const auto res = m_pImpl->WorkerThread.Wait(false, {m_pImpl->CancelEvent}, timeout);
	if (res == WAIT_TIMEOUT)
		return false;
	if (res == WAIT_OBJECT_0)
		return true;
	throw std::runtime_error(m_pImpl->LastErrorMessage.empty() ? "Cancelled" : m_pImpl->LastErrorMessage.c_str());
}

HANDLE Sqex::FontCsv::FontSetsCreator::GetWaitableObject() const {
	return m_pImpl->WorkerThread;
}

const std::string& Sqex::FontCsv::FontSetsCreator::GetError() const {
	return m_pImpl->LastErrorMessage;
}

Sqex::FontCsv::FontGenerateProcess Sqex::FontCsv::FontSetsCreator::GetProgress() const {
	if (!m_pImpl->WorkerThread)
		throw std::runtime_error("Not started yet");
	auto result = FontGenerateProcess{
		.Finished = m_pImpl->WorkerThread.Wait(0) != WAIT_TIMEOUT,
		.Indeterminate = 0,
	};
	const auto lock = std::lock_guard(m_pImpl->ResultMtx);
	for (const auto& v1 : m_pImpl->ResultWork | std::views::values) {
		for (const auto& v2 : v1 | std::views::values) {
			result += v2->GetProgress();
		}
	}
	return result;
}
