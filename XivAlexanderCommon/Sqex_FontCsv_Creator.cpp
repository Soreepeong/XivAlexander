#include "pch.h"
#include "Sqex_FontCsv_Creator.h"

#include "Sqex_FontCsv_DirectWriteFont.h"
#include "Sqex_FontCsv_FreeTypeFont.h"
#include "Sqex_FontCsv_GdiFont.h"
#include "Sqex_Sqpack_EntryRawStream.h"
#include "Sqex_Sqpack_Reader.h"
#include "Utils_Win32_ThreadPool.h"

inline void DebugThrowError(const std::exception& e) {
#ifdef _DEBUG
	if (Win32::MessageBoxF(nullptr, MB_ICONERROR | MB_YESNO, L"Sqex::FontCsv::FontCsvCreator::Compile::Work",
		L"Error: {}\n\nDebug?", e.what()) == IDYES)
		throw;
#endif
}

struct Sqex::FontCsv::FontCsvCreator::CharacterPlan {
private:
	mutable GlyphMeasurement m_bbox;
	char32_t m_codePoint;

public:
	const SeCompatibleDrawableFont<uint8_t>* Font;

	CharacterPlan(char32_t character, const SeCompatibleDrawableFont<uint8_t>* font)
		: m_codePoint(character)
		, Font(font) {
	}

	const GlyphMeasurement& GetBbox() const {
		if (m_bbox.empty)
			m_bbox = Font->Measure(0, 0, m_codePoint);
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
};

struct Sqex::FontCsv::FontCsvCreator::Implementation {
	Win32::TpEnvironment WorkPool;
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
		SSIZE_T globalOffsetX;
		SSIZE_T globalOffsetY;
		uint8_t boundingHeight;
	} Step0Result;
};

Sqex::FontCsv::FontCsvCreator::FontCsvCreator(const Win32::Semaphore& semaphore)
	: m_pImpl(std::make_unique<Implementation>()) {
	m_pImpl->CoreLimitSemaphore = semaphore;
}

Sqex::FontCsv::FontCsvCreator::~FontCsvCreator() = default;

void Sqex::FontCsv::FontCsvCreator::AddCharacter(char32_t codePoint, const SeCompatibleDrawableFont<uint8_t>*font, bool replace, bool extendRange) {
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

	m_pImpl->Plans.insert(pos, CharacterPlan(codePoint, font));
}

void Sqex::FontCsv::FontCsvCreator::AddCharacter(const SeCompatibleDrawableFont<uint8_t>*font, bool replace, bool extendRange) {
	for (const auto c : font->GetAllCharacters())
		AddCharacter(c, font, replace, extendRange);
}

void Sqex::FontCsv::FontCsvCreator::AddKerning(const  SeCompatibleDrawableFont<uint8_t>*font, char32_t left, char32_t right, int distance, bool replace) {
	if (replace)
		m_pImpl->Kernings.insert_or_assign(std::make_tuple(font, left, right), distance);
	else
		m_pImpl->Kernings.emplace(std::make_tuple(font, left, right), distance);
}

void Sqex::FontCsv::FontCsvCreator::AddKerning(const  SeCompatibleDrawableFont<uint8_t>*font, bool replace) {
	for (const auto& [pair, distance] : font->GetKerningTable())
		AddKerning(font, pair.first, pair.second, static_cast<int>(distance), replace);
}

void Sqex::FontCsv::FontCsvCreator::AddFont(const  SeCompatibleDrawableFont<uint8_t>*font, bool replace, bool extendRange) {
	for (const auto c : font->GetAllCharacters())
		AddCharacter(c, font, replace, extendRange);
	for (const auto& [pair, distance] : font->GetKerningTable())
		AddKerning(font, pair.first, pair.second, static_cast<int>(distance), replace);
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
	const uint16_t m_textureWidth;
	const uint16_t m_textureHeight;
	const uint16_t m_glyphGap;
	uint16_t m_currentX;
	uint16_t m_currentY;
	uint16_t m_currentLineHeight;

	std::vector<std::shared_ptr<Texture::MemoryBackedMipmap>> m_mipmaps;
	std::map<std::tuple<char32_t, const SeCompatibleDrawableFont<uint8_t>*, uint8_t, uint8_t>, AllocatedSpace> m_drawnGlyphs;

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

	std::deque<WorkItem> m_workItems;
	std::atomic_size_t m_processedWorkItemIndex;

	std::pair<AllocatedSpace, bool> AllocateSpace(char32_t c, const SeCompatibleDrawableFont<uint8_t>* font, SSIZE_T drawOffsetX, SSIZE_T drawOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, uint8_t borderThickness, uint8_t borderOpacity) {
		const auto actualGlyphGap = static_cast<uint16_t>(m_glyphGap + borderThickness);

		const auto [it, isNewEntry] = m_drawnGlyphs.emplace(std::make_tuple(c, font, borderThickness, borderOpacity), AllocatedSpace{});
		if (isNewEntry) {
			auto newTargetRequired = false;
			if (m_mipmaps.empty())
				newTargetRequired = true;
			else {
				if (static_cast<size_t>(0) + m_currentX + boundingWidth + actualGlyphGap >= m_textureWidth) {
					m_currentX = actualGlyphGap;
					m_currentY += m_currentLineHeight + actualGlyphGap + 1;  // Account for rounding errors
					m_currentLineHeight = 0;
				}
				if (m_currentY + boundingHeight + actualGlyphGap + 1 >= m_textureHeight)
					newTargetRequired = true;
			}

			if (newTargetRequired) {
				m_mipmaps.emplace_back(std::make_shared<Texture::MemoryBackedMipmap>(
					m_textureWidth, m_textureHeight,
					Texture::CompressionType::L8_1,
					std::vector<uint8_t>(static_cast<size_t>(m_textureWidth) * m_textureHeight)));
				m_currentX = m_currentY = m_glyphGap;
				m_currentLineHeight = 0;
			}

			if (m_currentX < actualGlyphGap)
				m_currentX = actualGlyphGap;
			if (m_currentY < actualGlyphGap)
				m_currentY = actualGlyphGap;

			it->second = AllocatedSpace{
				.DrawOffsetX = drawOffsetX,
				.DrawOffsetY = drawOffsetY,
				.Index = static_cast<uint16_t>(m_mipmaps.size() - 1),
				.X = m_currentX,
				.Y = m_currentY,
				.BoundingHeight = boundingHeight,
			};

			m_currentX += boundingWidth + m_glyphGap;
			m_currentLineHeight = std::max<uint16_t>(m_currentLineHeight, boundingHeight);
		}

		return std::make_pair(it->second, isNewEntry);
	}

	template<typename TextureTypeSupportingRGBA = Texture::RGBA4444, Texture::CompressionType CompressionType = Texture::CompressionType::RGBA4444>
	void Finalize() {
		auto mipmaps = std::move(m_mipmaps);
		while (mipmaps.size() % 4)
			mipmaps.push_back(std::make_shared<Texture::MemoryBackedMipmap>(
				mipmaps[0]->Width(), mipmaps[0]->Height(), Texture::CompressionType::L8_1,
				std::vector<uint8_t>(static_cast<size_t>(mipmaps[0]->Width()) * mipmaps[0]->Height())));

		for (size_t i = 0; i < mipmaps.size() / 4; ++i) {
			m_mipmaps.push_back(std::make_shared<Texture::MemoryBackedMipmap>(
				mipmaps[0]->Width(), mipmaps[0]->Height(), CompressionType,
				std::vector<uint8_t>(sizeof TextureTypeSupportingRGBA * mipmaps[0]->Width() * mipmaps[0]->Height())));

			const auto target = m_mipmaps.back()->View<TextureTypeSupportingRGBA>();
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

void Sqex::FontCsv::FontCsvCreator::RenderTarget::Finalize(Texture::CompressionType compressionType) {
	switch (compressionType) {
		case Texture::CompressionType::RGBA4444:
			return m_pImpl->Finalize<Texture::RGBA4444, Texture::CompressionType::RGBA4444>();

		case Texture::CompressionType::RGBA_1:
			return m_pImpl->Finalize<Texture::RGBA8888, Texture::CompressionType::RGBA_1>();

		case Texture::CompressionType::RGBA_2:
			return m_pImpl->Finalize<Texture::RGBA8888, Texture::CompressionType::RGBA_2>();

		default:
			throw std::invalid_argument("Unsupported texture type for generating font");
	}
}

std::vector<std::shared_ptr<const Sqex::Texture::MipmapStream>> Sqex::FontCsv::FontCsvCreator::RenderTarget::AsMipmapStreamVector() const {
	std::vector<std::shared_ptr<const Texture::MipmapStream>> res;
	for (const auto& i : m_pImpl->m_mipmaps)
		res.emplace_back(i);
	return res;
}

std::vector<std::shared_ptr<Sqex::Texture::ModifiableTextureStream>> Sqex::FontCsv::FontCsvCreator::RenderTarget::AsTextureStreamVector() const {
	std::vector<std::shared_ptr<Texture::ModifiableTextureStream>> res;
	for (const auto& i : m_pImpl->m_mipmaps) {
		auto texture = std::make_shared<Texture::ModifiableTextureStream>(i->Type(), i->Width(), i->Height());
		texture->AppendMipmap(i);
		res.emplace_back(std::move(texture));
	}
	return res;
}

Sqex::FontCsv::FontCsvCreator::RenderTarget::AllocatedSpace Sqex::FontCsv::FontCsvCreator::RenderTarget::QueueDraw(
	char32_t c,
	const SeCompatibleDrawableFont<uint8_t>*font,
	SSIZE_T drawOffsetX, SSIZE_T drawOffsetY,
	uint8_t boundingWidth, uint8_t boundingHeight,
	uint8_t borderThickness, uint8_t borderOpacity
) {
	const auto [space, drawRequired] = m_pImpl->AllocateSpace(c, font, drawOffsetX, drawOffsetY, boundingWidth, boundingHeight, borderThickness, borderOpacity);

	if (drawRequired) {
		m_pImpl->m_workItems.emplace_back(Implementation::WorkItem{
			.mipmap = m_pImpl->m_mipmaps[space.Index].get(),
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
	const auto index = m_pImpl->m_processedWorkItemIndex++;
	if (index >= m_pImpl->m_workItems.size())
		return false;

	m_pImpl->m_workItems[index].Work();

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
		.globalOffsetY = GlobalOffsetYModifier - borderThickness + std::min<SSIZE_T>(maxBbox.top, 0),
		.boundingHeight = static_cast<uint8_t>(maxBbox.bottom + std::max<SSIZE_T>(-maxBbox.top, 0) + static_cast<SSIZE_T>(2) * borderThickness),
	};
}

uint16_t Sqex::FontCsv::FontCsvCreator::RenderTarget::TextureWidth() const {
	return m_pImpl->m_textureWidth;
}

uint16_t Sqex::FontCsv::FontCsvCreator::RenderTarget::TextureHeight() const {
	return m_pImpl->m_textureHeight;
}

void Sqex::FontCsv::FontCsvCreator::Step2_Layout(RenderTarget & renderTarget) {
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
			const auto currentOffsetY = static_cast<int8_t>(
				(AlignToBaseline ? m_pImpl->Result->Ascent() - plan.Font->Ascent() : (0LL + m_pImpl->Result->LineHeight() - plan.Font->LineHeight()) / 2)
				+ m_pImpl->Step0Result.globalOffsetY
				);

			const auto space = renderTarget.QueueDraw(plan.Character(), plan.Font,
				leftExtension, 0,
				boundingWidth, m_pImpl->Step0Result.boundingHeight, borderThickness, borderOpacity);

			const auto resultingX = static_cast<uint16_t>(space.X + space.DrawOffsetX - leftExtension);
			const auto resultingY = static_cast<uint16_t>(space.Y + space.DrawOffsetY);
			m_pImpl->Result->AddFontEntry(plan.Character(), space.Index, resultingX, resultingY, boundingWidth, std::min(space.BoundingHeight, m_pImpl->Step0Result.boundingHeight), nextOffsetX, currentOffsetY);
		}
	} catch (const std::exception& e) {
		OnError(e);
		DebugThrowError(e);
	}
}

void Sqex::FontCsv::FontCsvCreator::Step3_Draw(RenderTarget & target) {
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

	std::mutex SourceFontMapAccessMtx, SourceFontLoadMtx;
	std::map<std::filesystem::path, std::unique_ptr<Sqpack::Reader>> SqpackReaders;
	std::map<std::tuple<std::filesystem::path, std::filesystem::path>, std::vector<std::shared_ptr<const Texture::MipmapStream>>> GameTextures;
	std::map<std::string, std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>>> SourceFonts;

	ResultFontSets Result;
	std::mutex ResultMtx;
	std::map<std::string, std::map<std::string, std::unique_ptr<FontCsvCreator>>> ResultWork;

	Win32::TpEnvironment WorkPool;
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

			std::shared_ptr<const SeCompatibleDrawableFont<uint8_t>> newFont;
			const auto& inputFontSource = Config.sources.at(name);
			if (const auto& source = inputFontSource.gameSource; inputFontSource.isGameSource) {
				auto indexFilePath = source.indexFile.empty() ? GamePath / LR"(sqpack\ffxiv\000000.win32.index)" : std::filesystem::path(source.indexFile);
				indexFilePath = canonical(indexFilePath);

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
					} catch (const Sqpack::Reader::EntryNotFoundError&) {
						break;
					}
				}

				newFont = std::make_shared<SeDrawableFont<Texture::RGBA4444, uint8_t>>(
					std::make_shared<ModifiableFontCsvStream>(Sqpack::EntryRawStream(reader->second->GetEntryProvider(source.fdtPath))), textures->second);
			} else if (const auto& source = inputFontSource.gdiSource; inputFontSource.isGdiSource) {
				newFont = std::make_shared<GdiDrawingFont<uint8_t>>(source);
			} else if (const auto& source = inputFontSource.directWriteSource; inputFontSource.isDirectWriteSource) {
				if (!source.fontFile.empty())
					newFont = std::make_shared<DirectWriteDrawingFont<uint8_t>>(
						source.fontFile, source.faceIndex, static_cast<float>(source.height), source.renderMode
						);
				else if (!source.familyName.empty())
					newFont = std::make_shared<DirectWriteDrawingFont<uint8_t>>(
						FromUtf8(source.familyName).c_str(), static_cast<float>(source.height), static_cast<DWRITE_FONT_WEIGHT>(source.weight), source.stretch, source.style, source.renderMode
						);
				else
					throw std::invalid_argument("Neither of fontFile nor familyName was specified.");
			} else if (const auto& source = inputFontSource.freeTypeSource; inputFontSource.isFreeTypeSource) {
				if (!source.fontFile.empty())
					newFont = std::make_shared<FreeTypeDrawingFont<uint8_t>>(
						source.fontFile, source.faceIndex, static_cast<float>(source.height), source.loadFlags
						);
				else if (!source.familyName.empty())
					newFont = std::make_shared<FreeTypeDrawingFont<uint8_t>>(
						FromUtf8(source.familyName).c_str(), static_cast<float>(source.height), static_cast<DWRITE_FONT_WEIGHT>(source.weight), source.stretch, source.style, source.loadFlags
						);
				else
					throw std::invalid_argument("Neither of fontFile nor familyName was specified.");
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
		for (const auto& [textureGroupFilenamePattern, fonts] : Config.targets) {
			renderTargets.emplace(textureGroupFilenamePattern, std::make_unique<FontCsvCreator::RenderTarget>(Config.textureWidth, Config.textureHeight, Config.glyphGap));
			TextureGroupWorkPools.emplace(textureGroupFilenamePattern, std::make_unique<Win32::TpEnvironment>());
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

		for (const auto& [textureGroupFilenamePattern, fonts] : Config.targets) {
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

				std::ranges::sort(sortedRemainingFontList, [&](const auto& l, const auto& r) {
					return fonts.fontTargets.at(l).height > fonts.fontTargets.at(r).height;
				});

				// Step 0. Calculate max progress value.
				for (const auto& fontName : sortedRemainingFontList) {
					if (Cancelled)
						return;

					textureGroupWorkPool.SubmitWork([this, &plan = fonts.fontTargets.at(fontName) , &creator = *remainingFonts.at(fontName)]() {
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

						for (const auto& source : plan.sources) {
							const auto& sourceFont = GetSourceFont(source.name);
							if (source.ranges.empty()) {
								creator.AddFont(&sourceFont, source.replace, source.extendRange);
							} else {
								for (const auto& rangeName : source.ranges) {
									for (const auto& range : Config.ranges.at(rangeName).ranges | std::views::values) {
										for (auto i = range.from; i < range.to; ++i)
											creator.AddCharacter(i, &sourceFont, source.replace, source.extendRange);
										if (range.from != range.to)  // separate line to prevent overflow (i < range.to might never be false)
											creator.AddCharacter(range.to, &sourceFont, source.replace, source.extendRange);

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
					});
				}
				textureGroupWorkPool.WaitOutstanding();

				// Step 1. Calculate bounding boxes of every glyph used.
				for (const auto& fontName : sortedRemainingFontList) {
					textureGroupWorkPool.SubmitWork([this, &creator = *remainingFonts.at(fontName)]() {
						const auto semaphoreHolder = WaitSemaphore();
						if (Cancelled)
							return;

						creator.Step1_CalcBbox();
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
						const auto semaphoreHolder = WaitSemaphore();
						if (Cancelled)
							return;

						try {
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

							target.Finalize(Config.textureType);

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
		if (wait)
			WorkerThread.Wait();
	}
};

Sqex::FontCsv::FontSetsCreator::FontSetsCreator(CreateConfig::FontCreateConfig config, std::filesystem::path gamePath, LONG maxCoreCount)
	: m_pImpl(std::make_unique<Implementation>(std::move(config), std::move(gamePath), maxCoreCount ? maxCoreCount : Win32::GetCoreCount())) {
	m_pImpl->WorkerThread = Win32::Thread(L"FontSetsCreator", [this]() { m_pImpl->Compile(); });
}

Sqex::FontCsv::FontSetsCreator::~FontSetsCreator() {
	m_pImpl->Cancel();
}

std::map<Sqex::Sqpack::EntryPathSpec, std::shared_ptr<const Sqex::RandomAccessStream>> Sqex::FontCsv::FontSetsCreator::ResultFontSets::GetAllStreams() const {
	std::map<Sqpack::EntryPathSpec, std::shared_ptr<const RandomAccessStream>> result;

	for (const auto& [textureFilenameFormat, fontSet] : Result) {
		for (size_t i = 0; i < fontSet.Textures.size(); ++i)
			result.emplace(std::format("common/font/{}", std::format(textureFilenameFormat, i + 1)), fontSet.Textures[i]);

		for (const auto& [fontName, newFontCsv] : fontSet.Fonts)
			result.emplace(std::format("common/font/{}", fontName), newFontCsv);
	}

	return result;
}

const Sqex::FontCsv::FontSetsCreator::ResultFontSets& Sqex::FontCsv::FontSetsCreator::GetResult() const {
	if (m_pImpl->Cancelled)
		throw std::runtime_error(m_pImpl->LastErrorMessage.empty() ? "Cancelled" : m_pImpl->LastErrorMessage.c_str());
	if (m_pImpl->WorkerThread.Wait(0) == WAIT_TIMEOUT)
		throw std::runtime_error("not finished");

	return m_pImpl->Result;
}

bool Sqex::FontCsv::FontSetsCreator::Wait(DWORD timeout) const {
	const auto res = m_pImpl->WorkerThread.Wait(false, { m_pImpl->CancelEvent }, timeout);
	if (res == WAIT_TIMEOUT)
		return false;
	if (res == WAIT_OBJECT_0)
		return true;
	throw std::runtime_error(m_pImpl->LastErrorMessage.empty() ? "Cancelled" : m_pImpl->LastErrorMessage.c_str());
}

const std::string& Sqex::FontCsv::FontSetsCreator::GetError() const {
	return m_pImpl->LastErrorMessage;
}

Sqex::FontCsv::FontGenerateProcess Sqex::FontCsv::FontSetsCreator::GetProgress() const {
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
