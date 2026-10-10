#include "pch.h"
#include "MainApp/Features/CrowdFix.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

#include <xivres/util.on_dtor.h>

#include "Game/SignatureDefinitions.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"

namespace XivAlexander::Apps::MainApp::Features {
	namespace {
		namespace Resolved = Game::Resolved::CrowdFix;
		using Game::Signatures::ComplexSignature;
		using Game::Signatures::ResolveError;
		using Misc::Hooks::PointerFunctionOf;

		// From FFXIVClientStructs (VisibilityFlags.Model); unlike the other layouts, the game has no single test to read it from. As of 7.56h.
		constexpr uint64_t VisibilityFlagsModel = 1 << 1;

		template<typename T>
		T& At(void* base, ptrdiff_t offset) {
			return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
		}

		template<typename T>
		const T& At(const void* base, ptrdiff_t offset) {
			return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset);
		}

		template<typename TFn>
		TFn VirtualFunction(void* object, size_t slot) {
			return reinterpret_cast<TFn>((*static_cast<void* const* const*>(object))[slot]);
		}

		void WriteCode(void* address, std::span<const uint8_t> bytes) {
			DWORD oldProtect;
			VirtualProtect(address, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect);
			std::memcpy(address, bytes.data(), bytes.size());
			VirtualProtect(address, bytes.size(), oldProtect, &oldProtect);
			FlushInstructionCache(GetCurrentProcess(), address, bytes.size());
		}

		// One store, so another thread runs either the old or the new instruction.
		template<typename T>
		void WriteCodeAtomically(void* address, T value) {
			DWORD oldProtect;
			VirtualProtect(address, sizeof value, PAGE_EXECUTE_READWRITE, &oldProtect);
			*static_cast<volatile T*>(address) = value;
			VirtualProtect(address, sizeof value, oldProtect, &oldProtect);
			FlushInstructionCache(GetCurrentProcess(), address, sizeof value);
		}

		struct FrameState {
			void* TaskManager{};
			uint64_t Index{};
		};

		class FixBase {
			mutable std::mutex m_statusMtx;
			std::string m_status = "Off";

		protected:
			bool m_available = false;
			bool m_waiting = false;

			void SetStatus(std::string status) {
				const auto lock = std::lock_guard(m_statusMtx);
				m_status = std::move(status);
			}

			template<typename T>
			bool Resolve(const ComplexSignature<T>& signature, T& out) {
				if (const auto status = signature.Resolve(out); status != ResolveError::Ok) {
					SetStatus(std::format("Unavailable: {}", status.Detail));
					return false;
				}
				return true;
			}

		public:
			FixBase() = default;
			FixBase(const FixBase&) = delete;
			FixBase& operator=(const FixBase&) = delete;
			virtual ~FixBase() = default;

			[[nodiscard]] bool Available() const { return m_available; }

			/// Whether the last SetEnabled(true) could not finish yet, because what it needs does not exist yet.
			[[nodiscard]] bool Waiting() const { return m_waiting; }

			[[nodiscard]] std::string Status() const {
				const auto lock = std::lock_guard(m_statusMtx);
				return m_status;
			}

			[[nodiscard]] virtual bool Enabled() const = 0;

			/// Game main thread, before the frame's tasks run.
			virtual void SetEnabled(bool enabled, const FrameState& frame) = 0;

			/// Game main thread, before the frame's tasks run, every frame.
			virtual void Update(const FrameState& frame) {}
		};

		class UnavailableFix final : public FixBase {
		public:
			explicit UnavailableFix(std::string reason) {
				SetStatus(std::format("Unavailable: {}", reason));
			}

			[[nodiscard]] bool Enabled() const override { return false; }
			void SetEnabled(bool, const FrameState&) override {}
		};

		/// DeviceDX11::PostTick's two Kernel::Notifier loops are patched to call only the notifiers that can do work, tracked through Link and Unlink.
		class IdleNotifierFilter final : public FixBase {
			static constexpr auto VerifyInterval = std::chrono::seconds(10);
			static constexpr size_t CallCodeLength = 24;

			const std::shared_ptr<Misc::Logger> m_logger;
			Resolved::SkipIdleNotifiersFunctions m_list;
			std::optional<PointerFunctionOf<Resolved::NotifierLinkFn>> m_link;
			std::optional<PointerFunctionOf<Resolved::NotifierLinkFn>> m_unlink;

			// Changed only under the notifier lock, which PostTick holds while it walks them.
			std::vector<void*> m_active;
			std::unordered_map<void*, size_t> m_activeIndex;
			std::vector<void*> m_snapshot;

			std::vector<uint8_t> m_prePresentOriginal;
			std::vector<uint8_t> m_postKickOriginal;
			std::chrono::steady_clock::time_point m_nextVerify;

			xivres::util::on_dtor::multi m_hooks;

		public:
			explicit IdleNotifierFilter(std::shared_ptr<Misc::Logger> logger)
				: m_logger(std::move(logger)) {
				if (!Resolve(Resolved::SkipIdleNotifiers, m_list))
					return;

				if (m_list.PrePresentLoopLength < CallCodeLength || m_list.PostKickLoopLength < CallCodeLength) {
					SetStatus("Unavailable: the notifier loops are too short to patch");
					return;
				}

				m_link.emplace("Kernel::Notifier::Link", m_list.Link);
				m_unlink.emplace("Kernel::Notifier::Unlink", m_list.Unlink);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return !m_prePresentOriginal.empty(); }

			/// The UI draws inside PostTick's Present, so patching from there would rewrite the function that is running.
			void SetEnabled(bool enabled, const FrameState&) override {
				if (!m_available || enabled == Enabled())
					return;

				if (enabled)
					Enable();
				else
					Disable("Off");
			}

			/// Periodically re-walks the whole list and drops the patch if the set ever drifted.
			void Update(const FrameState&) override {
				if (!Enabled() || std::chrono::steady_clock::now() < m_nextVerify)
					return;

				m_nextVerify = std::chrono::steady_clock::now() + VerifyInterval;
				size_t missing = 0;
				size_t stale;

				// No allocations: the walk runs under the game's notifier lock.
				EnterCriticalSection(m_list.Lock);
				{
					size_t found = 0;
					for (auto node = *m_list.Head; node; node = At<void*>(node, m_list.NextOffset)) {
						if (m_activeIndex.contains(node))
							found++;
						else if (CanDoWork(node))
							missing++;
					}
					stale = m_active.size() - found;
				}
				LeaveCriticalSection(m_list.Lock);

				if (missing || stale) {
					Disable(std::format("Disabled: verification found {} missing and {} stale notifiers", missing, stale));
					m_logger->Format<LogLevel::Warning>(LogCategory::General, "CrowdFix: idle notifier filter disabled: verification found {} missing and {} stale notifiers", missing, stale);
				}
			}

		private:
			void Enable() {
				EnterCriticalSection(m_list.Lock);
				m_active.clear();
				m_activeIndex.clear();
				for (auto node = *m_list.Head; node; node = At<void*>(node, m_list.NextOffset)) {
					if (CanDoWork(node))
						Add(node);
				}

				m_hooks += m_link->SetHook([this](void* node) { LinkDetour(node); });
				m_hooks += m_unlink->SetHook([this](void* node) { UnlinkDetour(node); });
				LeaveCriticalSection(m_list.Lock);

				m_prePresentOriginal = WriteCall(m_list.PrePresentLoop, m_list.PrePresentLoopLength, &CallActivePrePresent);
				m_postKickOriginal = WriteCall(m_list.PostKickLoop, m_list.PostKickLoopLength, &CallActivePostKick);
				m_nextVerify = std::chrono::steady_clock::now() + VerifyInterval;
				SetStatus("On");
			}

			void Disable(std::string status) {
				if (!m_prePresentOriginal.empty())
					WriteCode(m_list.PrePresentLoop, m_prePresentOriginal);
				if (!m_postKickOriginal.empty())
					WriteCode(m_list.PostKickLoop, m_postKickOriginal);

				m_prePresentOriginal.clear();
				m_postKickOriginal.clear();
				m_hooks.clear();
				SetStatus(std::move(status));
			}

			[[nodiscard]] bool CanDoWork(void* node) const {
				const auto vtable = *static_cast<void* const* const*>(node);
				return CallbackMayWork(node, vtable[m_list.PostKickSlot]) || CallbackMayWork(node, vtable[m_list.PrePresentSlot]);
			}

			[[nodiscard]] bool CallbackMayWork(const void* node, const void* callback) const {
				const auto code = static_cast<const uint8_t*>(callback);
				if (code[0] == 0xC3 || (code[0] == 0xC2 && code[1] == 0 && code[2] == 0))
					return false;

				for (const auto& t : m_list.CallbackTests) {
					if (t.Function != callback)
						continue;

					const auto flags = At<uint32_t>(node, t.FlagsOffset);
					switch (t.Test) {
						case Resolved::NotifierWorkTest::BufferFlags:
						case Resolved::NotifierWorkTest::ConstantBufferFlags:
							return flags & t.Mask;

						case Resolved::NotifierWorkTest::IndexBufferFlags:
							return (flags & t.Mask) && !(flags & t.SecondMask);

						case Resolved::NotifierWorkTest::TextureMappedFlags:
							return (flags & t.Mask) == t.Mask;

						case Resolved::NotifierWorkTest::TextureMappedOrUploadFlags:
							return (flags & t.Mask) == t.Mask || (flags & t.SecondMask);
					}
				}

				// a callback we know nothing about
				return true;
			}

			void Add(void* node) {
				if (m_activeIndex.try_emplace(node, m_active.size()).second)
					m_active.push_back(node);
			}

			void Remove(void* node) {
				const auto it = m_activeIndex.find(node);
				if (it == m_activeIndex.end())
					return;

				const auto index = it->second;
				m_activeIndex.erase(it);
				const auto last = m_active.back();
				m_active.pop_back();
				if (index != m_active.size()) {
					m_active[index] = last;
					m_activeIndex[last] = index;
				}
			}

			void LinkDetour(void* node) {
				EnterCriticalSection(m_list.Lock);
				m_link->bridge(node);
				if (CanDoWork(node))
					Add(node);
				LeaveCriticalSection(m_list.Lock);
			}

			void UnlinkDetour(void* node) {
				EnterCriticalSection(m_list.Lock);
				// Before the original, so PostTick can never reach a node that is being destroyed.
				Remove(node);
				m_unlink->bridge(node);
				LeaveCriticalSection(m_list.Lock);
			}

			void CallActive(size_t slot) {
				// a callback may link or unlink notifiers
				m_snapshot.assign(m_active.begin(), m_active.end());
				for (const auto node : m_snapshot)
					VirtualFunction<void(*)(void*)>(node, slot)(node);
			}

			static void CallActivePrePresent(IdleNotifierFilter* self) {
				self->CallActive(self->m_list.PrePresentSlot);
			}

			static void CallActivePostKick(IdleNotifierFilter* self) {
				self->CallActive(self->m_list.PostKickSlot);
			}

			// mov rcx, this; mov rax, target; call rax; jmp rel8 to the end of the original loop
			std::vector<uint8_t> WriteCall(uint8_t* site, size_t length, void(*target)(IdleNotifierFilter*)) {
				std::vector<uint8_t> original(site, site + length);

				std::vector<uint8_t> code;
				const auto append = [&code](uint64_t value) {
					for (size_t i = 0; i < sizeof value; i++)
						code.push_back(static_cast<uint8_t>(value >> (i * 8)));
				};
				code.insert(code.end(), {0x48, 0xB9});
				append(reinterpret_cast<uint64_t>(this));
				code.insert(code.end(), {0x48, 0xB8});
				append(reinterpret_cast<uint64_t>(target));
				code.insert(code.end(), {0xFF, 0xD0, 0xEB, static_cast<uint8_t>(length - CallCodeLength)});
				WriteCode(site, code);
				return original;
			}
		};

		/// Instead of the stock wake-all, a job submit wakes one sleeping worker, and each woken worker wakes the next while work remains, using the game's own counters.
		class JobWakeChain final : public FixBase {
			static constexpr int32_t MaxWorkers = 256;  // a larger count is from a pool that is not set up yet

			Resolved::ChainWorkerWakeupsFunctions m_functions;
			std::optional<PointerFunctionOf<Resolved::JobPoolWakeAllFn>> m_wakeAll;
			Misc::Hooks::ImportedFunction<DWORD, HANDLE, DWORD> m_wait{"kernel32!WaitForSingleObject", "kernel32.dll", "WaitForSingleObject"};

			// Filled once: sleeping workers may still read it after the hooks are gone.
			std::vector<HANDLE> m_workerEvents;
			void* m_pool{};

			xivres::util::on_dtor::multi m_hooks;

		public:
			JobWakeChain() {
				if (!Resolve(Resolved::ChainWorkerWakeups, m_functions))
					return;

				if (!m_wait) {
					SetStatus("Unavailable: WaitForSingleObject is not imported");
					return;
				}

				m_wakeAll.emplace("TaskManager::JobPool::WakeAll", m_functions.WakeAll);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return !!m_pool; }

			void SetEnabled(bool enabled, const FrameState& frame) override {
				m_waiting = false;
				if (!m_available || enabled == Enabled())
					return;

				if (!enabled) {
					m_hooks.clear();

					// Sleeping workers are still inside the wait detour; a stock wake-all gets them out.
					(*m_wakeAll)(m_pool);
					m_pool = nullptr;
					SetStatus("Off");
					return;
				}

				const auto pool = frame.TaskManager ? static_cast<uint8_t*>(frame.TaskManager) + m_functions.Layout.TaskManagerJobPool : nullptr;
				if (!pool || !PoolReady(pool)) {
					m_waiting = true;
					SetStatus("Waiting for the job pool");
					return;
				}

				if (m_workerEvents.empty()) {
					const auto& layout = m_functions.Layout;
					const auto threads = At<uint8_t**>(pool, layout.Threads);
					for (int32_t i = 0, count = At<int32_t>(pool, layout.ThreadCount); i < count; i++)
						m_workerEvents.push_back(At<HANDLE>(threads[i], layout.ThreadEvent));
				}

				m_pool = pool;
				m_hooks += m_wait.SetHook([this](HANDLE handle, DWORD milliseconds) { return WaitDetour(handle, milliseconds); });
				m_hooks += m_wakeAll->SetHook([this](void* jobPool) { WakeOne(jobPool, true); });
				SetStatus(std::format("On, {} workers", m_workerEvents.size()));
			}

		private:
			/// Whether the pool's workers all exist: everything the stock wake-all reads, which itself trusts the count.
			[[nodiscard]] bool PoolReady(const uint8_t* pool) const {
				const auto& layout = m_functions.Layout;
				const auto threads = At<uint8_t* const*>(pool, layout.Threads);
				const auto count = At<int32_t>(pool, layout.ThreadCount);
				if (!threads || count <= 0 || count > MaxWorkers)
					return false;

				return std::all_of(threads, threads + count, [&layout](const uint8_t* thread) { return thread && At<HANDLE>(thread, layout.ThreadEvent); });
			}

			/// Wakes at most one sleeping worker. With bumpAwake, running workers also get another pass like the stock code.
			int WakeOne(void* jobPool, bool bumpAwake) const {
				const auto& layout = m_functions.Layout;
				const auto threads = At<uint8_t**>(jobPool, layout.Threads);
				int woken = 0;

				for (int32_t i = 0, count = At<int32_t>(jobPool, layout.ThreadCount); i < count; i++) {
					const auto worker = threads[i];
					if (worker[layout.ThreadSkip])
						continue;

					const auto wakeCount = std::atomic_ref(At<int32_t>(worker, layout.ThreadWakeCount));
					const auto current = wakeCount.load();
					if (current >= layout.ThreadWakeLimit)
						continue;

					// Further sleepers are left to the chain; awake ones only get bumped by the submitter.
					if (current == 0 ? woken > 0 : !bumpAwake)
						continue;

					if (wakeCount.fetch_add(1) == 0) {
						SetEvent(At<HANDLE>(worker, layout.ThreadEvent));
						woken++;
					}
				}

				return woken;
			}

			DWORD WaitDetour(HANDLE handle, DWORD milliseconds) {
				const auto result = m_wait.bridge(handle, milliseconds);
				const auto queue = static_cast<const volatile uint32_t*>(m_functions.QueueIndices);
				if (result == WAIT_OBJECT_0 && milliseconds == INFINITE && queue[1] != queue[0]
					&& std::ranges::find(m_workerEvents, handle) != m_workerEvents.end())
					WakeOne(m_pool, false);

				return result;
			}
		};

		/// Only the first pose sync walk of a frame is let through; the later ones, from each RenderView, find nothing dirtied in between.
		class SkeletonSyncDedupe final : public FixBase {
			std::optional<PointerFunctionOf<Resolved::SkeletonPoseSyncWalkFn>> m_syncWalk;

			// the walks run on the main thread too
			uint64_t m_frame = 0;
			uint64_t m_lastSyncedFrame = UINT64_MAX;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			SkeletonSyncDedupe() {
				Resolved::DedupeSkeletonSyncsFunctions functions;
				if (!Resolve(Resolved::DedupeSkeletonSyncs, functions))
					return;

				m_syncWalk.emplace("Render::SkeletonPoseSyncWalk", functions.SyncWalk);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			void SetEnabled(bool enabled, const FrameState&) override {
				if (!m_available || enabled == m_enabled)
					return;

				if (enabled)
					m_hooks += m_syncWalk->SetHook([this](void* skeletonList) { SyncWalkDetour(skeletonList); });
				else
					m_hooks.clear();

				m_enabled = enabled;
				SetStatus(enabled ? "On" : "Off");
			}

			void Update(const FrameState& frame) override {
				m_frame = frame.Index;
			}

		private:
			void SyncWalkDetour(void* skeletonList) {
				if (m_lastSyncedFrame == m_frame)
					return;

				m_lastSyncedFrame = m_frame;
				m_syncWalk->bridge(skeletonList);
			}
		};

		/// The culling setup's clear of the view visibility table is cut to the highest object slot ever used; slots above it were never written.
		class CullingClearTrim final : public FixBase {
			static constexpr int ObjectsPerWord = 32;

			// The bitmask is read from the culling manager's slot allocator, which also checks that it covers every slot.
			Resolved::TrimCullingClearFunctions m_clear;
			int m_highestWord = -1;
			bool m_enabled = false;

		public:
			CullingClearTrim() {
				if (!Resolve(Resolved::TrimCullingClear, m_clear))
					return;

				if (*m_clear.ClearCount != m_clear.FullCount || m_clear.ObjectMaskWords * ObjectsPerWord != m_clear.FullCount) {
					SetStatus("Unavailable: unexpected clear count");
					return;
				}

				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			/// The culling setup only runs on the main thread too.
			void SetEnabled(bool enabled, const FrameState&) override {
				if (!m_available || enabled == m_enabled)
					return;

				m_enabled = enabled;
				if (!enabled)
					WriteCodeAtomically(m_clear.ClearCount, m_clear.FullCount);

				SetStatus(enabled ? "On" : "Off");
			}

			void Update(const FrameState&) override {
				if (!m_enabled)
					return;

				const auto cullingManager = *m_clear.CullingManager;
				if (!cullingManager)
					return;

				const auto mask = At<const uint32_t*>(cullingManager, m_clear.ObjectMask);
				if (!mask)
					return;

				// High water mark: never shrinks, so a slot that ever held an object keeps getting cleared.
				for (auto word = static_cast<int>(m_clear.ObjectMaskWords) - 1; word > m_highestWord; word--) {
					if (mask[word]) {
						m_highestWord = word;
						break;
					}
				}

				// One spare word of margin for objects added later in the frame.
				const auto count = static_cast<uint32_t>(std::min((m_highestWord + 2) * ObjectsPerWord, static_cast<int>(m_clear.FullCount)));
				if (count != *m_clear.ClearCount) {
					WriteCodeAtomically(m_clear.ClearCount, count);
					SetStatus(std::format("On, clearing {} of {} slots", count, m_clear.FullCount));
				}
			}
		};

		/// The graphics small-object allocator's backing frees of non-slab blocks happen after the slab check releases its lock, instead of under it.
		class AllocatorFreeLock final : public FixBase {
			// Read and cross-checked from the original Free: a wrong layout would hand slab blocks to the backing allocator and corrupt the heap.
			Resolved::GraphicsAllocatorLayout m_layout;
			std::optional<PointerFunctionOf<Resolved::GraphicsAllocatorFreeFn>> m_free;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			AllocatorFreeLock() {
				Resolved::ShortenAllocatorLockFunctions functions;
				if (!Resolve(Resolved::ShortenAllocatorLock, functions))
					return;

				m_layout = functions.Layout;
				m_free.emplace("Graphics::SmallObjectAllocator::Free", functions.Free);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			void SetEnabled(bool enabled, const FrameState&) override {
				if (!m_available || enabled == m_enabled)
					return;

				if (enabled)
					m_hooks += m_free->SetHook([this](void* allocator, void* block) { FreeDetour(allocator, block); });
				else
					m_hooks.clear();

				m_enabled = enabled;
				SetStatus(enabled ? "On" : "Off");
			}

		private:
			void FreeDetour(void* allocator, void* block) {
				if (!block)
					return;

				const auto criticalSection = &At<CRITICAL_SECTION>(allocator, m_layout.Lock);
				EnterCriticalSection(criticalSection);
				if (IsSlabBlock(allocator, block)) {
					// takes the (recursive) lock again
					m_free->bridge(allocator, block);
					LeaveCriticalSection(criticalSection);
					return;
				}

				LeaveCriticalSection(criticalSection);
				const auto backing = At<void*>(allocator, m_layout.Backing);
				VirtualFunction<void(*)(void*, void*)>(backing, m_layout.BackingFreeSlot)(backing, block);
			}

			/// The original's test; needs the allocator lock, as the chunk table is reallocated when it grows.
			[[nodiscard]] bool IsSlabBlock(void* allocator, void* block) const {
				const auto page = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(block) & ~static_cast<uintptr_t>(m_layout.PageMask));
				const auto index = At<uint32_t>(page, m_layout.PageIndex);
				if (index >= At<uint32_t>(allocator, m_layout.ChunkCount))
					return false;

				const auto chunk = At<uint8_t*>(At<uint8_t*>(allocator, m_layout.ChunkTable), m_layout.ChunkBase + index * m_layout.ChunkStride);
				return chunk && static_cast<uint64_t>(static_cast<uint8_t*>(block) - chunk) < m_layout.ChunkSpan;
			}
		};

		/// Staging blocks freed to the graphics allocator are pooled in power-of-two buckets via its vtable, and stay genuine allocations valid for the original Free after disabling.
		class StagingPool final : public FixBase {
			// Header marker of blocks the backing allocator hands out directly (no size); unvalidated, as that allocator is only known at run time. As of 7.56h.
			static constexpr ptrdiff_t DirectMarkerOffset = -0x10;
			static constexpr uint16_t DirectMarker = 0xFFFF;

			static constexpr int MinClassShift = 6;  // 64 B
			static constexpr int MaxClassShift = 16;  // 64 KB
			static constexpr size_t ClassBudgetBytes = 2 * 1024 * 1024;
			static constexpr uint64_t PoolAlignment = 0x10;

			using TerminateFn = void(*)(void* allocator);
			using AllocFn = void*(*)(void* allocator, uint64_t size, uint64_t alignment);
			using FreeFn = void(*)(void* allocator, void* block);
			using SizeFn = uint64_t(*)(void* allocator, void* block);

			class Bucket {
				std::mutex m_mtx;
				std::vector<void*> m_blocks;
				size_t m_count = 0;

			public:
				explicit Bucket(size_t capacity)
					: m_blocks(capacity) {}

				bool TryPush(void* block) {
					const auto lock = std::lock_guard(m_mtx);
					if (m_count == m_blocks.size())
						return false;

					m_blocks[m_count++] = block;
					return true;
				}

				bool TryPop(void*& block) {
					const auto lock = std::lock_guard(m_mtx);
					if (!m_count)
						return false;

					block = m_blocks[--m_count];
					return true;
				}

				void Clear() {
					const auto lock = std::lock_guard(m_mtx);
					m_count = 0;
				}
			};

			// The vtable slots are plain function pointers; there is one instance at a time.
			static inline std::atomic<StagingPool*> s_instance;

			Resolved::PoolStagingBlocksFunctions m_allocatorManager;
			std::vector<std::unique_ptr<Bucket>> m_buckets;

			void** m_vtable{};
			TerminateFn m_originalTerminate{};
			AllocFn m_originalAlloc{};
			FreeFn m_originalFree{};
			SizeFn m_blockSize{};
			std::atomic<void*> m_target{};

		public:
			StagingPool() {
				for (auto i = MinClassShift; i <= MaxClassShift; i++)
					m_buckets.emplace_back(std::make_unique<Bucket>(std::max<size_t>(16, ClassBudgetBytes >> i)));

				if (!Resolve(Resolved::PoolStagingBlocks, m_allocatorManager))
					return;

				s_instance = this;
				m_available = true;
			}

			~StagingPool() override {
				if (s_instance == this)
					s_instance = nullptr;
			}

			[[nodiscard]] bool Enabled() const override { return !!m_vtable; }

			void SetEnabled(bool enabled, const FrameState&) override {
				m_waiting = false;
				if (!m_available || enabled == Enabled())
					return;

				if (!enabled) {
					Restore();
					Drain();
					SetStatus("Off");
					return;
				}

				const auto manager = *m_allocatorManager.AllocatorManager;
				const auto allocator = manager ? At<void*>(manager, m_allocatorManager.AllocatorOffset) : nullptr;
				if (!allocator) {
					m_waiting = true;
					SetStatus("Waiting for the graphics allocator");
					return;
				}

				// The slots are only known for the class whose vtable they were read from.
				const auto table = *static_cast<void***>(allocator);
				if (table != m_allocatorManager.Vtable) {
					m_available = false;
					SetStatus("Unavailable: unexpected allocator class");
					return;
				}

				const auto& slots = m_allocatorManager;
				m_target = allocator;
				m_originalTerminate = reinterpret_cast<TerminateFn>(table[slots.TerminateSlot]);
				m_originalAlloc = reinterpret_cast<AllocFn>(table[slots.AllocSlot]);
				m_originalFree = reinterpret_cast<FreeFn>(table[slots.FreeSlot]);
				m_blockSize = reinterpret_cast<SizeFn>(table[slots.SizeSlot]);
				m_vtable = table;
				WriteSlot(table, slots.TerminateSlot, &TerminateDetour);
				WriteSlot(table, slots.FreeSlot, &FreeDetour);
				WriteSlot(table, slots.AllocSlot, &AllocDetour);
				SetStatus("On");
			}

		private:
			template<typename TFn>
			static void WriteSlot(void** table, size_t slot, TFn function) {
				DWORD oldProtect;
				VirtualProtect(&table[slot], sizeof function, PAGE_READWRITE, &oldProtect);
				std::atomic_ref(table[slot]).store(reinterpret_cast<void*>(function));
				VirtualProtect(&table[slot], sizeof function, oldProtect, &oldProtect);
			}

			static void* AllocDetour(void* allocator, uint64_t size, uint64_t alignment) {
				return s_instance.load(std::memory_order_relaxed)->Alloc(allocator, size, alignment);
			}

			static void FreeDetour(void* allocator, void* block) {
				s_instance.load(std::memory_order_relaxed)->Free(allocator, block);
			}

			static void TerminateDetour(void* allocator) {
				s_instance.load(std::memory_order_relaxed)->Terminate(allocator);
			}

			void* Alloc(void* allocator, uint64_t size, uint64_t alignment) {
				if (allocator != m_target.load(std::memory_order_relaxed) || alignment > PoolAlignment || size > (1ULL << MaxClassShift))
					return m_originalAlloc(allocator, size, alignment);

				const auto sizeClass = std::max(MinClassShift, static_cast<int>(std::bit_width(std::max<uint64_t>(size, 1) - 1))) - MinClassShift;
				if (void* block; m_buckets[sizeClass]->TryPop(block)) {
					// as the original wrapper does
					std::atomic_ref(At<int32_t>(allocator, m_allocatorManager.AllocCounter)).fetch_add(1);
					return block;
				}

				// Round misses up to the class size so the block comes back to the same class when freed.
				return m_originalAlloc(allocator, 1ULL << (sizeClass + MinClassShift), PoolAlignment);
			}

			void Free(void* allocator, void* block) {
				if (block && allocator == m_target.load(std::memory_order_relaxed)
					&& !(reinterpret_cast<uintptr_t>(block) & (PoolAlignment - 1))
					&& At<uint16_t>(block, DirectMarkerOffset) != DirectMarker) {
					// Slab element size or requested size; a block goes to the largest class it can hold, or is left alone past twice the top class.
					const auto size = m_blockSize(allocator, block);
					if (size >= (1ULL << MinClassShift) && size < (2ULL << MaxClassShift)) {
						const auto sizeClass = std::min(static_cast<int>(std::bit_width(size)) - 1, MaxClassShift) - MinClassShift;
						if (m_buckets[sizeClass]->TryPush(block))
							return;
					}
				}

				m_originalFree(allocator, block);
			}

			void Terminate(void* allocator) {
				if (allocator == m_target.load()) {
					// Everything is about to be released wholesale; forget pooled blocks instead of freeing them.
					m_target = nullptr;
					for (const auto& bucket : m_buckets)
						bucket->Clear();
					SetStatus("Off (allocator terminated)");
				}

				m_originalTerminate(allocator);
			}

			void Restore() {
				if (!m_vtable)
					return;

				WriteSlot(m_vtable, m_allocatorManager.AllocSlot, m_originalAlloc);
				WriteSlot(m_vtable, m_allocatorManager.FreeSlot, m_originalFree);
				WriteSlot(m_vtable, m_allocatorManager.TerminateSlot, m_originalTerminate);
				m_vtable = nullptr;
			}

			void Drain() {
				const auto allocator = m_target.exchange(nullptr);
				for (const auto& bucket : m_buckets) {
					for (void* block; bucket->TryPop(block);) {
						if (allocator)
							m_originalFree(allocator, block);
					}
				}

				// Blocks still held by the game are genuine allocations and are freed normally later.
			}
		};

		/// Minion follow AI is skipped for hidden minions, which Companion::Update still warps back to their owner when too far behind.
		class HiddenMinionFreeze final : public FixBase {
			// where Companion::Update reads the render flags
			size_t m_renderFlagsOffset{};
			std::optional<PointerFunctionOf<Resolved::CompanionFollowFn>> m_follow;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			HiddenMinionFreeze() {
				Resolved::FreezeHiddenMinionsFunctions follow;
				if (!Resolve(Resolved::FreezeHiddenMinions, follow))
					return;

				m_renderFlagsOffset = follow.RenderFlagsOffset;
				m_follow.emplace("Companion::Follow", follow.Follow);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			void SetEnabled(bool enabled, const FrameState&) override {
				if (!m_available || enabled == m_enabled)
					return;

				if (enabled)
					m_hooks += m_follow->SetHook([this](void* companion) { FollowDetour(companion); });
				else
					m_hooks.clear();

				m_enabled = enabled;
				SetStatus(enabled ? "On" : "Off");
			}

		private:
			void FollowDetour(void* companion) {
				if (At<uint64_t>(companion, m_renderFlagsOffset) & VisibilityFlagsModel)
					return;

				m_follow->bridge(companion);
			}
		};

		/// Jumps over job list Prepare's second wait on its manual-reset event (both variants): only Prepare resets the event, so that wait always returns at once.
		class PrepareWaitSkip final : public FixBase {
			static constexpr uint16_t CallIndirect = 0x15FF;  // FF 15
			static constexpr uint16_t JumpOver = 0x04EB;  // EB 04: skips the rest of the 6 byte call

			Resolved::SkipPrepareWaitFunctions m_sites;
			bool m_enabled = false;

		public:
			PrepareWaitSkip() {
				m_available = Resolve(Resolved::SkipPrepareWait, m_sites);
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			/// Any thread would do: a thread in Prepare runs either the call or the jump.
			void SetEnabled(bool enabled, const FrameState&) override {
				if (!m_available || enabled == m_enabled)
					return;

				m_enabled = enabled;
				for (const auto site : {m_sites.ArrayList, m_sites.SingleItemList})
					WriteCodeAtomically(site, enabled ? JumpOver : CallIndirect);

				SetStatus(enabled ? "On" : "Off");
			}
		};

		/// RenderView's single-item BG instancing prep job runs inline at kick time via the list's own claim and task functions, leaving the list as a worker would.
		class BgPrepInline final : public FixBase {
			/// Queue entry as filled by the job list's describe and read by the workers; the state's size is not read from the game. As of 7.56h.
			struct JobDescriptor {
				void* Claim;
				void* Owner;
				uint8_t State[0x10];
			};

			using ClaimFn = void*(*)(void* owner, void* state, void*** argument, int32_t* remaining);

			Resolved::InlineBgPrepFunctions m_prep;
			std::optional<PointerFunctionOf<Resolved::JobListKickFn>> m_kick;

			void* m_prepList{};
			void* m_taskManager{};
			uint8_t* m_context{};
			DWORD m_mainThreadId{};

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			BgPrepInline() {
				if (!Resolve(Resolved::InlineBgPrep, m_prep))
					return;

				m_kick.emplace("TaskManager::KickJobList", m_prep.Kick);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			/// The inline path runs only on the main thread inside RenderView, so it is never in flight here; the stock kick then finds a signaled list.
			void SetEnabled(bool enabled, const FrameState& frame) override {
				m_waiting = false;
				if (!m_available || enabled == m_enabled)
					return;

				if (!enabled) {
					m_hooks.clear();
					m_enabled = false;
					SetStatus("Off");
					return;
				}

				const auto manager = *m_prep.RenderManager;
				if (!manager) {
					m_waiting = true;
					SetStatus("Waiting for the render manager");
					return;
				}

				// What the workers pass to tasks, found the way they find it: through their pool.
				const auto& pool = m_prep.Pool;
				const auto jobPool = frame.TaskManager ? static_cast<uint8_t*>(frame.TaskManager) + pool.TaskManagerJobPool : nullptr;
				const auto threads = jobPool ? At<uint8_t* const*>(jobPool, pool.Threads) : nullptr;
				if (!threads || At<int32_t>(jobPool, pool.ThreadCount) <= 0 || !threads[0]) {
					m_waiting = true;
					SetStatus("Waiting for the job pool");
					return;
				}

				if (At<uint8_t*>(threads[0], m_prep.Run.ThreadPool) != jobPool) {
					m_available = false;
					SetStatus("Unavailable: the job pool workers belong to another pool");
					return;
				}

				m_taskManager = frame.TaskManager;
				m_context = jobPool + m_prep.Run.PoolContext;
				m_prepList = static_cast<uint8_t*>(manager) + m_prep.PrepListOffset;
				m_mainThreadId = GetCurrentThreadId();
				m_hooks += m_kick->SetHook([this](void* taskManager, void* jobList) { return KickDetour(taskManager, jobList); });
				m_enabled = true;
				SetStatus("On");
			}

		private:
			/// Main thread, job workers, the action timeline thread and bone physics all kick: keep the filter cheap.
			uint32_t KickDetour(void* taskManager, void* jobList) {
				if (jobList != m_prepList || taskManager != m_taskManager || GetCurrentThreadId() != m_mainThreadId)
					return m_kick->bridge(taskManager, jobList);

				// Same job list calls as the kick: item count, prepare (waits for the previous run, resets counters), describe.
				if (!VirtualFunction<uint32_t(*)(void*)>(jobList, m_prep.ListCountSlot)(jobList))
					return 0;

				VirtualFunction<void(*)(void*)>(jobList, m_prep.ListPrepareSlot)(jobList);
				JobDescriptor descriptor{};
				VirtualFunction<JobDescriptor*(*)(void*, JobDescriptor*)>(jobList, m_prep.ListDescribeSlot)(jobList, &descriptor);

				// Same steps as InnerThread::Run: claim a task, run it with the pool context.
				const auto& run = m_prep.Run;
				while (true) {
					void** argument = nullptr;
					int32_t remaining = 0;
					const auto task = reinterpret_cast<ClaimFn>(descriptor.Claim)(descriptor.Owner, descriptor.State, &argument, &remaining);
					if (!task)
						break;

					if (argument)
						VirtualFunction<void(*)(void*, void*, void*)>(task, run.TaskRunWithArgumentSlot)(task, m_context, *argument);
					else
						VirtualFunction<void(*)(void*, void*)>(task, run.TaskRunSlot)(task, m_context);

					if (!remaining)
						break;
				}

				return 1;
			}
		};

		/// Both hidden-bar sites of the hotbar update jump over PrepareSlotForRender, whose output is unread for them; a bar shown later is prepared that frame.
		class HiddenHotbarSkip final : public FixBase {
			Resolved::SkipHiddenHotbarsFunctions m_sites;
			uint16_t m_barOriginal{};
			uint16_t m_crossBarOriginal{};
			bool m_enabled = false;

		public:
			HiddenHotbarSkip() {
				if (!Resolve(Resolved::SkipHiddenHotbars, m_sites))
					return;

				if (m_sites.Length < 2 || m_sites.Length - 2 > 0x7F) {
					SetStatus("Unavailable: unexpected code");
					return;
				}

				m_barOriginal = *reinterpret_cast<const uint16_t*>(m_sites.Bar);
				m_crossBarOriginal = *reinterpret_cast<const uint16_t*>(m_sites.CrossBar);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			/// The hotbar update runs on the main thread too, so the code is never mid-execution while it changes.
			void SetEnabled(bool enabled, const FrameState&) override {
				if (!m_available || enabled == m_enabled)
					return;

				// jmp rel8 to the slot index increment after the prepare call
				const auto jump = static_cast<uint16_t>(0xEB | (m_sites.Length - 2) << 8);
				m_enabled = enabled;
				WriteCodeAtomically(m_sites.Bar, enabled ? jump : m_barOriginal);
				WriteCodeAtomically(m_sites.CrossBar, enabled ? jump : m_crossBarOriginal);
				SetStatus(enabled ? "On" : "Off");
			}
		};

		/// Each attach depth level of the animation tail runs on the animation submit's parallel-for group, joined between levels; same-depth skeletons are independent.
		class AnimTailParallel final : public FixBase {
			static constexpr int32_t MinParallel = 16;  // smaller depth levels run serially

			// The animation update's tail entries; nothing reads their stride in a way that can be captured. As of 7.56h.
			struct Entry {
				void* Skeleton;
				int32_t Depth;
			};

			// TailJob is a plain function pointer the workers call; there is one instance at a time.
			static inline std::atomic<AnimTailParallel*> s_instance;
			static inline float s_jobDeltaTime;
			static inline thread_local bool s_inUpdate;
			static inline thread_local int s_tailCalls;

			// All layouts (the parallel-for group above all) are read and cross-checked from the animation submit, its append and the tail.
			Resolved::ParallelAnimTailFunctions m_functions;
			std::optional<PointerFunctionOf<Resolved::AnimationUpdateFn>> m_update;
			std::optional<PointerFunctionOf<Resolved::AnimationTailFn>> m_tail;
			// What the group holds when only this thread appends: the append stores through null past the last chunk.
			int32_t m_maxSkeletons{};
			std::vector<void*> m_mainOnly;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			AnimTailParallel() {
				if (!Resolve(Resolved::ParallelAnimTail, m_functions))
					return;

				const auto& group = m_functions.Group;
				m_maxSkeletons = static_cast<int32_t>(group.ChunkCount * group.ChunkBlocks * group.BlockItems);
				m_mainOnly.resize(m_maxSkeletons);
				m_update.emplace("Animation::Update", m_functions.Update);
				m_tail.emplace("Animation::FinishSkeleton", m_functions.Tail);
				s_instance = this;
				m_available = true;
			}

			~AnimTailParallel() override {
				if (s_instance == this)
					s_instance = nullptr;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			/// The animation update runs on the main thread too, so a toggle never lands inside the tail loop.
			void SetEnabled(bool enabled, const FrameState&) override {
				if (!m_available || enabled == m_enabled)
					return;

				if (enabled) {
					m_hooks += m_tail->SetHook([this](void* skeleton, float deltaTime) { TailDetour(skeleton, deltaTime); });
					m_hooks += m_update->SetHook([this](void* skeletons, float deltaTime) { UpdateDetour(skeletons, deltaTime); });
				} else {
					m_hooks.clear();
				}

				m_enabled = enabled;
				SetStatus(enabled ? "On" : "Off");
			}

		private:
			void UpdateDetour(void* skeletons, float deltaTime) {
				s_inUpdate = true;
				s_tailCalls = 0;
				m_update->bridge(skeletons, deltaTime);
				s_inUpdate = false;
			}

			/// The first tail call of the loop runs the whole sorted batch; the loop's later calls return at once.
			void TailDetour(void* skeleton, float deltaTime) {
				if (!s_inUpdate)
					return m_tail->bridge(skeleton, deltaTime);

				if (s_tailCalls++)
					return;

				const auto count = *m_functions.EntryCount;
				const auto entries = static_cast<const Entry*>(m_functions.Entries);
				const auto base = *m_functions.SubmitBase;
				if (count <= 0 || count > m_maxSkeletons || entries[0].Skeleton != skeleton || !base) {
					// unexpected state: let the stock loop do everything
					s_inUpdate = false;
					return m_tail->bridge(skeleton, deltaTime);
				}

				const auto group = static_cast<uint8_t*>(base) + m_functions.GroupOffset;
				for (int32_t start = 0, end; start < count; start = end) {
					const auto depth = entries[start].Depth;
					for (end = start; end < count && entries[end].Depth == depth; end++) {}

					if (end - start < MinParallel) {
						for (auto i = start; i < end; i++)
							m_tail->bridge(entries[i].Skeleton, deltaTime);
					} else {
						RunLevel(group, start, end, deltaTime);
					}
				}
			}

			void RunLevel(uint8_t* group, int32_t start, int32_t end, float deltaTime) {
				const auto entries = static_cast<const Entry*>(m_functions.Entries);
				size_t mainCount = 0;
				size_t jobCount = 0;
				for (auto i = start; i < end; i++) {
					const auto skeleton = entries[i].Skeleton;
					if (NeedsMainThread(skeleton)) {
						m_mainOnly[mainCount++] = skeleton;
					} else {
						m_functions.Append(*m_functions.SubmitBase, skeleton);
						jobCount++;
					}
				}

				if (jobCount) {
					// Same sequence as the animation submit: publish the writers' block counts, arm, kick, help, wait, reset.
					const auto& layout = m_functions.Group;
					FlushWriters(group);
					s_jobDeltaTime = deltaTime;
					At<void*>(group, layout.JobContext) = nullptr;
					At<void*>(group, layout.Job) = reinterpret_cast<void*>(&TailJob);
					std::atomic_ref(At<int32_t>(group, layout.ClaimCounters[0])).exchange(0);
					std::atomic_ref(At<int32_t>(group, layout.ClaimCounters[1])).exchange(0);
					if (std::atomic_ref(At<int32_t>(group, layout.BlocksClaimed)).load()) {
						const auto jobList = At<void*>(group, layout.JobList);
						m_functions.Kick(*m_functions.TaskManager, jobList);
						for (size_t i = 0; i < mainCount; i++)
							m_tail->bridge(m_mainOnly[i], deltaTime);

						mainCount = 0;
						if (At<uint8_t>(group, layout.PerItemClaims))
							m_functions.HelpPerItem(group);
						else
							m_functions.HelpBlocks(group);

						VirtualFunction<void(*)(void*)>(jobList, layout.JobListWaitSlot)(jobList);
					} else {
						for (auto i = start; i < end; i++) {
							if (const auto skeleton = entries[i].Skeleton; !NeedsMainThread(skeleton))
								m_tail->bridge(skeleton, deltaTime);
						}
					}

					At<void*>(group, layout.JobContext) = nullptr;
					At<void*>(group, layout.Job) = nullptr;
					ResetWriters(group);
					for (size_t i = 0; i < layout.ChunkCount; i++) {
						if (const auto chunk = At<uint32_t*>(group, layout.Chunks + i * sizeof(void*)))
							*chunk = 0;
					}

					At<uint32_t>(group, layout.BlocksClaimed) = 0;
				}

				for (size_t i = 0; i < mainCount; i++)
					m_tail->bridge(m_mainOnly[i], deltaTime);
			}

			void FlushWriters(uint8_t* group) const {
				const auto& layout = m_functions.Group;
				auto writer = At<uint8_t*>(group, layout.Writers);
				for (uint32_t i = 0, writers = At<uint32_t>(group, layout.WriterCount); i < writers; i++, writer += layout.WriterSize) {
					if (const auto block = At<uint32_t*>(writer, layout.WriterBlock))
						*block = At<uint32_t>(writer, layout.WriterItems);
				}
			}

			/// Leaves every writer without a block, and looking full, so that its next append claims a new one.
			void ResetWriters(uint8_t* group) const {
				FlushWriters(group);
				const auto& layout = m_functions.Group;
				auto writer = At<uint8_t*>(group, layout.Writers);
				for (uint32_t i = 0, writers = At<uint32_t>(group, layout.WriterCount); i < writers; i++, writer += layout.WriterSize) {
					At<uint32_t>(writer, layout.WriterItems) = static_cast<uint32_t>(layout.BlockItems);
					At<void*>(writer, layout.WriterBlock) = nullptr;
				}
			}

			/// Ground ray casts (BG collision) and animation control removals are only done on the main thread.
			[[nodiscard]] bool NeedsMainThread(void* skeleton) const {
				// The tail's own test, minus its ScheduleManagement check: that only keeps a few more skeletons on the main thread.
				if (const auto ground = At<const void*>(skeleton, m_functions.SkeletonGround); ground && m_functions.GroundRayActive(ground))
					return true;

				const auto& layout = m_functions.Partials;
				const auto partials = At<uint8_t*>(skeleton, layout.Array);
				for (size_t i = 0, count = At<uint16_t>(skeleton, layout.Count); i < count; i++) {
					// the tail only updates partial skeletons with a pose, which applies the removals
					if (const auto partial = partials + i * layout.Stride; At<void*>(partial, layout.Pose) && At<uint64_t>(partial, m_functions.PartialPendingRemovals))
						return true;
				}

				return false;
			}

			/// Job worker or main help loop: one skeleton per item.
			static void TailJob(void* context, void** item) {
				s_instance.load(std::memory_order_relaxed)->m_tail->bridge(*item, s_jobDeltaTime);
			}
		};

		/// Camera culling's single character item is split into chunks that its owner and every thread finishing its own item work through; the job only reads its item.
		class CharacterCullSplit final : public FixBase {
			static constexpr size_t MaxItemSize = 0x80;
			static constexpr int32_t Chunk = 16;

			/// One character item is in flight at a time: the culling runs one view at a time and joins before returning.
			struct Shared {
				void* CullingManager{};
				uint8_t Item[MaxItemSize]{};
				int32_t Total{};
				std::atomic<int32_t> Next;
				std::atomic<int32_t> Active;
			};

			// The item type and size are read from the culling's item building, the range fields from the job.
			Resolved::SplitCharacterCullingFunctions m_functions;
			std::optional<PointerFunctionOf<Resolved::CameraCullJobFn>> m_cullJob;
			Shared m_shared;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			CharacterCullSplit() {
				if (!Resolve(Resolved::SplitCharacterCulling, m_functions))
					return;

				if (m_functions.ItemSize > MaxItemSize) {
					SetStatus(std::format("Unavailable: unexpected item size {}", m_functions.ItemSize));
					return;
				}

				m_cullJob.emplace("CameraCulling::Job", m_functions.CullJob);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			/// An owner already inside the detour finishes every chunk, so disabling mid-frame only stops new help.
			void SetEnabled(bool enabled, const FrameState&) override {
				if (!m_available || enabled == m_enabled)
					return;

				if (enabled)
					m_hooks += m_cullJob->SetHook([this](void* cullingManager, uint8_t* item) { return CullJobDetour(cullingManager, item); });
				else
					m_hooks.clear();

				m_enabled = enabled;
				SetStatus(enabled ? "On" : "Off");
			}

		private:
			/// Job workers and the main thread's help loop.
			int64_t CullJobDetour(void* cullingManager, uint8_t* item) {
				if (item[0] == m_functions.CharacterItemType && At<uint32_t>(item, m_functions.CountOffset) > Chunk && !m_shared.Active.load()) {
					std::memcpy(m_shared.Item, item, m_functions.ItemSize);
					m_shared.CullingManager = cullingManager;
					m_shared.Total = static_cast<int32_t>(At<uint32_t>(item, m_functions.CountOffset));
					m_shared.Next = 0;
					m_shared.Active = 1;
					Help();
					m_shared.Active = 0;
					return 0;
				}

				const auto result = m_cullJob->bridge(cullingManager, item);
				if (m_shared.Active.load())
					Help();

				return result;
			}

			void Help() {
				uint8_t local[MaxItemSize];
				std::memcpy(local, m_shared.Item, m_functions.ItemSize);
				const auto start = At<uint32_t>(local, m_functions.StartOffset);
				const auto total = m_shared.Total;
				while (true) {
					const auto i = m_shared.Next.fetch_add(Chunk);
					if (i >= total)
						break;

					At<uint32_t>(local, m_functions.StartOffset) = start + static_cast<uint32_t>(i);
					At<uint32_t>(local, m_functions.CountOffset) = static_cast<uint32_t>(std::min(Chunk, total - i));
					m_cullJob->bridge(m_shared.CullingManager, local);
				}
			}
		};

		/// The cell culling and culling setup tail groups' block help is routed to the engine's existing per-item claim variant, spreading their few items wider.
		class CullPerItemClaim final : public FixBase {
			Resolved::PerItemCullingClaimsFunctions m_groups;
			std::optional<PointerFunctionOf<Resolved::ParallelForHelpFn>> m_cellHelp;
			std::optional<PointerFunctionOf<Resolved::ParallelForHelpFn>> m_setupHelp;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			CullPerItemClaim() {
				if (!Resolve(Resolved::PerItemCullingClaims, m_groups))
					return;

				m_cellHelp.emplace("CullingManager::CellGroup::HelpBlocks", m_groups.CellHelpBlocks);
				m_setupHelp.emplace("CullingManager::SetupGroup::HelpBlocks", m_groups.SetupHelpBlocks);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			/// All threads in a fork-join must use the same claim mode; these groups only run, fully joined, inside rendering, which is not running now.
			void SetEnabled(bool enabled, const FrameState&) override {
				if (!m_available || enabled == m_enabled)
					return;

				if (enabled) {
					m_hooks += m_cellHelp->SetHook([this](void* group) { HelpDetour(group, m_groups.CellGroupOffset, m_groups.CellHelpPerItem, *m_cellHelp); });
					m_hooks += m_setupHelp->SetHook([this](void* group) { HelpDetour(group, m_groups.SetupGroupOffset, m_groups.SetupHelpPerItem, *m_setupHelp); });
				} else {
					m_hooks.clear();
				}

				m_enabled = enabled;
				SetStatus(enabled ? "On" : "Off");
			}

		private:
			void HelpDetour(void* group, size_t groupOffset, Resolved::ParallelForHelpFn perItem, PointerFunctionOf<Resolved::ParallelForHelpFn>& blocks) {
				if (const auto cullingManager = *m_groups.CullingManager; cullingManager && group == static_cast<uint8_t*>(cullingManager) + groupOffset)
					perItem(group);
				else
					blocks.bridge(group);
			}
		};

		/// Replaces PostTick's command list gather: copies only used entries and skips the (stable) sort when already in key order, matching the game's output byte for byte.
		class GatherUsedBytes final : public FixBase {
			// Read from the gather itself, the context size above all.
			Resolved::GatherUsedCommandsFunctions m_functions;
			std::optional<PointerFunctionOf<Resolved::CommandListGatherFn>> m_gather;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			GatherUsedBytes() {
				if (!Resolve(Resolved::GatherUsedCommands, m_functions))
					return;

				m_gather.emplace("DeviceDX11::GatherCommandList", m_functions.Gather);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			/// Output is identical either way, so a toggle in the middle of PostTick would be harmless.
			void SetEnabled(bool enabled, const FrameState&) override {
				if (!m_available || enabled == m_enabled)
					return;

				if (enabled) {
					m_hooks += m_gather->SetHook([this](void* device, uint32_t list, uint8_t** cursor, uint32_t* remaining, uint8_t** results, uint32_t* counts, uint32_t* total) {
						return GatherDetour(device, list, cursor, remaining, results, counts, total);
					});
				} else {
					m_hooks.clear();
				}

				m_enabled = enabled;
				SetStatus(enabled ? "On" : "Off");
			}

		private:
			/// Main thread, PostTick, after every producer has been joined.
			uint64_t GatherDetour(void* device, uint32_t list, uint8_t** cursor, uint32_t* remaining, uint8_t** results, uint32_t* counts, uint32_t* total) {
				if (!*cursor)
					return m_gather->bridge(device, list, cursor, remaining, results, counts, total);

				const auto& layout = m_functions.Layout;
				const auto entriesPerBlock = static_cast<uint32_t>(layout.BlockSize / layout.EntrySize);
				const auto contexts = At<uint32_t>(device, layout.ContextCount);
				const auto contextArray = At<uint8_t*>(device, layout.ContextArray);
				*total = 0;
				for (uint32_t i = 0; i < contexts; i++) {
					// first block, write pointer, free slots, blocks
					const auto descriptor = contextArray + i * layout.ContextSize + layout.Lists + list * layout.ListSize;
					const auto blocks = At<uint32_t>(descriptor, layout.ListBlocks);
					counts[i] = 0;
					if (!blocks) {
						results[i] = nullptr;
						continue;
					}

					const auto count = blocks * entriesPerBlock - At<uint32_t>(descriptor, layout.ListFreeSlots);
					const auto destination = *cursor;
					auto block = At<uint8_t*>(descriptor, layout.ListFirstBlock);
					auto output = destination;
					for (uint32_t b = 1; b < blocks; b++) {
						std::memcpy(output, block, layout.BlockSize);
						block = At<uint8_t*>(block, layout.NextBlock);
						output += layout.BlockSize;
					}

					std::memcpy(output, block, count * layout.EntrySize - static_cast<size_t>(output - destination));

					if (!IsSorted(destination, count)) {
						// the same scratch area the stock gather uses, right after this context's reserved blocks
						const auto scratch = destination + static_cast<size_t>(blocks) * layout.BlockSize;
						std::memcpy(scratch, destination, count * layout.EntrySize);
						m_functions.Sort(destination, scratch, 0, static_cast<int32_t>(count) - 1);
					}

					counts[i] = count;
					results[i] = destination;
					*total += count;
					*cursor = destination + count * layout.EntrySize;
					*remaining -= static_cast<uint32_t>(count * layout.EntrySize);
				}

				return contexts;
			}

			[[nodiscard]] bool IsSorted(const uint8_t* entries, uint32_t count) const {
				const auto entrySize = m_functions.Layout.EntrySize;
				auto previous = At<uint32_t>(entries, 0);
				for (uint32_t i = 1; i < count; i++) {
					const auto key = At<uint32_t>(entries, i * entrySize);
					if (key < previous)
						return false;

					previous = key;
				}

				return true;
			}
		};
	}
}

struct XivAlexander::Apps::MainApp::Features::CrowdFix::Implementation {
	static constexpr auto FixCount = static_cast<size_t>(Fix::Count);

	static constexpr std::array<bool, FixCount> DefaultEnabled{
		true,  // SkipIdleNotifiers
		true,  // ChainWorkerWakeups
		true,  // DedupeSkeletonSyncs
		true,  // TrimCullingClear
		false,  // ShortenAllocatorLock
		false,  // PoolStagingBlocks
		true,  // FreezeHiddenMinions
		false,  // SkipPrepareWait
		false,  // InlineBgPrep
		true,  // SkipHiddenHotbars
		true,  // ParallelAnimTail
		false,  // SplitCharacterCulling
		false,  // PerItemCullingClaims
		true,  // GatherUsedCommands
	};

	const std::shared_ptr<Misc::Logger> Logger;

	std::array<std::unique_ptr<FixBase>, FixCount> Fixes;
	std::optional<Misc::Hooks::PointerFunctionOf<Resolved::TaskManagerExecuteAllTasksFn>> ExecuteAllTasks;
	std::string Unusable;

	mutable std::mutex DesiredMtx;
	std::array<bool, FixCount> Desired = DefaultEnabled;
	std::array<bool, FixCount> Pending{};
	std::atomic_bool AnyPending = true;

	// Held while fixes are toggled or updated; normally only ever taken by the main thread.
	std::mutex ApplyMtx;
	FrameState Frame;
	std::atomic<DWORD> MainThreadId{};
	bool EverEnabled = false;

	xivres::util::on_dtor::multi Cleanup;

	Implementation()
		: Logger(Misc::Logger::Acquire()) {
		Pending.fill(true);

		Create<IdleNotifierFilter>(Fix::SkipIdleNotifiers, Logger);
		Create<JobWakeChain>(Fix::ChainWorkerWakeups);
		Create<SkeletonSyncDedupe>(Fix::DedupeSkeletonSyncs);
		Create<CullingClearTrim>(Fix::TrimCullingClear);
		Create<AllocatorFreeLock>(Fix::ShortenAllocatorLock);
		Create<StagingPool>(Fix::PoolStagingBlocks);
		Create<HiddenMinionFreeze>(Fix::FreezeHiddenMinions);
		Create<PrepareWaitSkip>(Fix::SkipPrepareWait);
		Create<BgPrepInline>(Fix::InlineBgPrep);
		Create<HiddenHotbarSkip>(Fix::SkipHiddenHotbars);
		Create<AnimTailParallel>(Fix::ParallelAnimTail);
		Create<CharacterCullSplit>(Fix::SplitCharacterCulling);
		Create<CullPerItemClaim>(Fix::PerItemCullingClaims);
		Create<GatherUsedBytes>(Fix::GatherUsedCommands);

		// Like CrowdFix's Framework.Update: main thread, before the frame's tasks, so outside DeviceDX11::PostTick, which some fixes patch.
		Resolved::FixDriverFunctions driver;
		if (const auto status = Resolved::FixDriver.Resolve(driver); status != ResolveError::Ok) {
			Unusable = std::format("Unavailable: {}", status.Detail);
			Logger->Format<LogLevel::Warning>(LogCategory::General, "CrowdFix fixes are left off: {}", status.Detail);
			return;
		}

		ExecuteAllTasks.emplace("TaskManager::ExecuteAllTasks", driver.ExecuteAllTasks);
		Cleanup += ExecuteAllTasks->SetHook([this](void* taskManager, float* deltaTime) { OnFrame(taskManager, deltaTime); });
	}

	~Implementation() {
		{
			const auto lock = std::lock_guard(DesiredMtx);
			Desired.fill(false);
			Pending.fill(true);
			AnyPending = true;
		}

		// Let the main thread undo everything at the start of its next frame, as it does every toggle.
		if (const auto mainThreadId = MainThreadId.load(); ExecuteAllTasks && mainThreadId && mainThreadId != GetCurrentThreadId()) {
			for (auto i = 0; i < 100 && AnyPending; i++)
				Sleep(10);
		}

		Cleanup.clear();

		// The main thread is on this thread, or is not running frames anymore.
		{
			const auto lock = std::lock_guard(ApplyMtx);
			if (AnyPending.exchange(false))
				Apply();
		}

		// Job and render threads can still be inside a detour, a patched loop or a pool vtable slot; let them leave.
		if (EverEnabled)
			Sleep(200);

		ExecuteAllTasks.reset();
		for (auto& fix : Fixes)
			fix.reset();
	}

	template<typename T, typename... TArgs>
	void Create(Fix fix, TArgs&&... args) {
		auto& slot = Fixes[static_cast<size_t>(fix)];
		try {
			slot = std::make_unique<T>(std::forward<TArgs>(args)...);
		} catch (const std::exception& e) {
			Logger->Format<LogLevel::Warning>(LogCategory::General, "CrowdFix: fix {} is unavailable: {}", static_cast<size_t>(fix), e.what());
			slot = std::make_unique<UnavailableFix>(e.what());
		}
	}

	void OnFrame(void* taskManager, float* deltaTime) {
		MainThreadId = GetCurrentThreadId();
		{
			const auto lock = std::lock_guard(ApplyMtx);
			Frame.TaskManager = taskManager;
			Frame.Index++;
			if (AnyPending.exchange(false))
				Apply();

			for (const auto& fix : Fixes)
				fix->Update(Frame);
		}

		ExecuteAllTasks->bridge(taskManager, deltaTime);
	}

	void Apply() {
		std::array<bool, FixCount> desired;
		std::array<bool, FixCount> pending;
		{
			const auto lock = std::lock_guard(DesiredMtx);
			desired = Desired;
			pending = Pending;
			Pending.fill(false);
		}

		for (size_t i = 0; i < FixCount; i++) {
			if (!pending[i])
				continue;

			auto& fix = *Fixes[i];
			fix.SetEnabled(desired[i], Frame);
			EverEnabled |= fix.Enabled();

			// The job pool and the graphics allocator may not exist yet right after login; keep retrying until they do.
			if (desired[i] && !fix.Enabled() && fix.Waiting()) {
				const auto lock = std::lock_guard(DesiredMtx);
				Pending[i] = true;
				AnyPending = true;
			}
		}
	}
};

XivAlexander::Apps::MainApp::Features::CrowdFix::CrowdFix()
	: m_pImpl(std::make_unique<Implementation>()) {
}

XivAlexander::Apps::MainApp::Features::CrowdFix::~CrowdFix() = default;

void XivAlexander::Apps::MainApp::Features::CrowdFix::SetEnabled(Fix fix, bool enabled) {
	const auto lock = std::lock_guard(m_pImpl->DesiredMtx);
	m_pImpl->Desired[static_cast<size_t>(fix)] = enabled;
	m_pImpl->Pending[static_cast<size_t>(fix)] = true;
	m_pImpl->AnyPending = true;
}

bool XivAlexander::Apps::MainApp::Features::CrowdFix::IsEnabled(Fix fix) const {
	const auto lock = std::lock_guard(m_pImpl->DesiredMtx);
	return m_pImpl->Desired[static_cast<size_t>(fix)];
}

bool XivAlexander::Apps::MainApp::Features::CrowdFix::IsAvailable(Fix fix) const {
	return m_pImpl->Unusable.empty() && m_pImpl->Fixes[static_cast<size_t>(fix)]->Available();
}

std::string XivAlexander::Apps::MainApp::Features::CrowdFix::GetStatus(Fix fix) const {
	if (!m_pImpl->Unusable.empty())
		return m_pImpl->Unusable;
	return m_pImpl->Fixes[static_cast<size_t>(fix)]->Status();
}
