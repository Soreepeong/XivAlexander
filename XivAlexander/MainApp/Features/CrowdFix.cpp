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
		namespace Resolved = Game::Resolved;
		using Game::Signatures::ComplexSignature;
		using Game::Signatures::ResolveError;
		using Misc::Hooks::PointerFunctionOf;

		// Layouts from FFXIVClientStructs.
		constexpr size_t TaskManagerJobPoolOffset = 0x08;
		constexpr size_t TaskManagerPoolContextOffset = 0x40;  // what workers pass to tasks: JobPool + 0x38
		constexpr size_t JobPoolInitializedOffset = 0x00;
		constexpr size_t JobPoolThreadsOffset = 0x08;
		constexpr size_t JobPoolThreadCountOffset = 0x10;
		constexpr size_t InnerThreadSkipOffset = 0x35;
		constexpr size_t InnerThreadWakeCountOffset = 0x38;
		constexpr size_t InnerThreadEventOffset = 0x40;
		constexpr size_t GameObjectRenderFlagsOffset = 0x118;
		constexpr uint64_t VisibilityFlagsModel = 1 << 1;
		constexpr size_t SkeletonPartialSkeletonCountOffset = 0x50;
		constexpr size_t SkeletonPartialSkeletonsOffset = 0x68;
		constexpr size_t PartialSkeletonSize = 0x230;

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

		/// DeviceDX11::PostTick walks every Kernel::Notifier twice per frame (vtbl+0x10 before Present, vtbl+0x08 after
		/// kicking the render thread). Every GPU resource is linked into that list for device events, but only CPU mapped
		/// buffers do per frame work, so a crowd turns into ~50k cache missing calls that return immediately.
		/// This keeps a set of the notifiers that can do work and patches both loops to walk only that set.
		class IdleNotifierFilter final : public FixBase {
			static constexpr auto VerifyInterval = std::chrono::seconds(10);
			static constexpr size_t CallCodeLength = 24;

			const std::shared_ptr<Misc::Logger> m_logger;
			Resolved::GraphicsNotifiers m_list;
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
				if (!Resolve(Resolved::GraphicsNotifierList, m_list))
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
					for (auto node = *m_list.Head; node; node = At<void*>(node, 0x10)) {
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
				for (auto node = *m_list.Head; node; node = At<void*>(node, 0x10)) {
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
				return CallbackMayWork(node, vtable[1]) || CallbackMayWork(node, vtable[2]);
			}

			[[nodiscard]] bool CallbackMayWork(const void* node, const void* callback) const {
				const auto code = static_cast<const uint8_t*>(callback);
				if (code[0] == 0xC3 || (code[0] == 0xC2 && code[1] == 0 && code[2] == 0))
					return false;

				for (const auto& [function, test] : m_list.CallbackTests) {
					if (function != callback)
						continue;

					switch (test) {
						case Resolved::NotifierWorkTest::BufferFlags:
							return At<uint32_t>(node, 0x1C) & 0x11;

						case Resolved::NotifierWorkTest::IndexBufferFlags: {
							const auto flags = At<uint32_t>(node, 0x20);
							return (flags & 0x11) && !(flags & 0x40);
						}

						case Resolved::NotifierWorkTest::TextureMappedFlags:
							return (At<uint32_t>(node, 0x3C) & 0x100010) == 0x100010;

						case Resolved::NotifierWorkTest::TextureMappedOrUploadFlags: {
							const auto flags = At<uint32_t>(node, 0x3C);
							return (flags & 0x100010) == 0x100010 || (flags & 0x2000);
						}

						case Resolved::NotifierWorkTest::ConstantBufferFlags:
							return At<uint32_t>(node, -0x14) & 0x4000;
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
				self->CallActive(2);
			}

			static void CallActivePostKick(IdleNotifierFilter* self) {
				self->CallActive(1);
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

		/// Every job submit calls the pool's wake-all, which SetEvents every sleeping worker from the submitting thread:
		/// up to 15 syscalls per submit, ~100 submits per frame on the main thread.
		/// Here the submitter wakes one sleeping worker, and each worker that wakes while the queue still has work wakes
		/// the next one. Same counters as the game (InnerThread +0x35 skip, +0x38 wake count, +0x40 event).
		class JobWakeChain final : public FixBase {
			Resolved::JobPoolWake m_functions;
			std::optional<PointerFunctionOf<Resolved::JobPoolWakeAllFn>> m_wakeAll;
			Misc::Hooks::ImportedFunction<DWORD, HANDLE, DWORD> m_wait{"kernel32!WaitForSingleObject", "kernel32.dll", "WaitForSingleObject"};

			// Filled once: sleeping workers may still read it after the hooks are gone.
			std::vector<HANDLE> m_workerEvents;
			void* m_pool{};

			xivres::util::on_dtor::multi m_hooks;

		public:
			JobWakeChain() {
				if (!Resolve(Resolved::JobPoolWakeFunctions, m_functions))
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
					// the stock wake-all takes over immediately
					m_hooks.clear();

					// Sleeping workers are still inside the wait detour; a stock wake-all gets them out.
					(*m_wakeAll)(m_pool);
					m_pool = nullptr;
					SetStatus("Off");
					return;
				}

				const auto pool = frame.TaskManager ? static_cast<uint8_t*>(frame.TaskManager) + TaskManagerJobPoolOffset : nullptr;
				if (!pool || !At<bool>(pool, JobPoolInitializedOffset)) {
					m_waiting = true;
					SetStatus("Waiting for the job pool");
					return;
				}

				if (m_workerEvents.empty()) {
					const auto threads = At<uint8_t**>(pool, JobPoolThreadsOffset);
					for (int32_t i = 0, count = At<int32_t>(pool, JobPoolThreadCountOffset); i < count; i++)
						m_workerEvents.push_back(At<HANDLE>(threads[i], InnerThreadEventOffset));
				}

				m_pool = pool;
				m_hooks += m_wait.SetHook([this](HANDLE handle, DWORD milliseconds) { return WaitDetour(handle, milliseconds); });
				m_hooks += m_wakeAll->SetHook([](void* jobPool) { WakeOne(jobPool, true); });
				SetStatus(std::format("On, {} workers", m_workerEvents.size()));
			}

		private:
			/// Wakes at most one sleeping worker. With bumpAwake, running workers also get another pass like the stock code.
			static int WakeOne(void* jobPool, bool bumpAwake) {
				const auto threads = At<uint8_t**>(jobPool, JobPoolThreadsOffset);
				int woken = 0;

				for (int32_t i = 0, count = At<int32_t>(jobPool, JobPoolThreadCountOffset); i < count; i++) {
					const auto worker = threads[i];
					if (worker[InnerThreadSkipOffset])
						continue;

					const auto wakeCount = std::atomic_ref(At<int32_t>(worker, InnerThreadWakeCountOffset));
					const auto current = wakeCount.load();
					if (current >= 2)
						continue;

					// Further sleepers are left to the chain; awake ones only get bumped by the submitter.
					if (current == 0 ? woken > 0 : !bumpAwake)
						continue;

					if (wakeCount.fetch_add(1) == 0) {
						SetEvent(At<HANDLE>(worker, InnerThreadEventOffset));
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

		/// The pose sync walk calls hkaPose::syncModelSpace on each partial skeleton of every render skeleton. It runs
		/// once from Render::Manager::Render and then again from every Manager::RenderView call (~21 per frame), although
		/// nothing on the render path dirties poses in between. This lets the first walk of a frame through and skips the rest.
		class SkeletonSyncDedupe final : public FixBase {
			std::optional<PointerFunctionOf<Resolved::SkeletonPoseSyncWalkFn>> m_syncWalk;

			// the walks run on the main thread too
			uint64_t m_frame = 0;
			uint64_t m_lastSyncedFrame = UINT64_MAX;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			SkeletonSyncDedupe() {
				Resolved::SkeletonPoseSyncWalkFn syncWalk;
				if (!Resolve(Resolved::SkeletonPoseSyncWalkFunction, syncWalk))
					return;

				m_syncWalk.emplace("Render::SkeletonPoseSyncWalk", syncWalk);
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

		/// The culling setup (~6 calls per frame) zeroes the whole per-object view visibility table (CullingManager+0x20):
		/// 40,960 object slots x 16 bytes = 640 KB per call, however few objects exist.
		/// This rewrites the loop count so only slots up to the highest object slot ever used (from the object bitmask at
		/// CullingManager+0x18) are cleared. Slots above that mark have never been written, so they are still zero.
		class CullingClearTrim final : public FixBase {
			static constexpr uint32_t FullCount = 0xA000;
			static constexpr int MaskWords = 0x500;
			static constexpr int ObjectsPerWord = 32;

			Resolved::CullingVisibilityClear m_clear;
			int m_highestWord = -1;
			bool m_enabled = false;

		public:
			CullingClearTrim() {
				if (!Resolve(Resolved::CullingVisibilityClearLoop, m_clear))
					return;

				if (*m_clear.ClearCount != FullCount) {
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
					WriteCodeAtomically(m_clear.ClearCount, FullCount);

				SetStatus(enabled ? "On" : "Off");
			}

			void Update(const FrameState&) override {
				if (!m_enabled)
					return;

				const auto cullingManager = *m_clear.CullingManager;
				if (!cullingManager)
					return;

				const auto mask = At<const uint32_t*>(cullingManager, 0x18);
				if (!mask)
					return;

				// High water mark: never shrinks, so a slot that ever held an object keeps getting cleared.
				for (auto word = MaskWords - 1; word > m_highestWord; word--) {
					if (mask[word]) {
						m_highestWord = word;
						break;
					}
				}

				// One spare word of margin for objects added later in the frame.
				const auto count = static_cast<uint32_t>(std::min((m_highestWord + 2) * ObjectsPerWord, static_cast<int>(FullCount)));
				if (count != *m_clear.ClearCount) {
					WriteCodeAtomically(m_clear.ClearCount, count);
					SetStatus(std::format("On, clearing {} of {} slots", count, FullCount));
				}
			}
		};

		/// The graphics small-object allocator (AllocatorManager+0x10) takes its lock before checking whether a block came
		/// from its slabs, and for every other block calls the backing allocator's free while still holding that lock.
		/// Draw building frees ~1,200 such staging blocks per frame from all job threads, so small allocations queue behind
		/// backing frees. Here the slab check stays under the lock, but backing frees happen after releasing it; slab blocks
		/// still go through the original Free.
		/// Mostly relieves the job workers, so it matters on CPUs where the main thread ends up waiting for them.
		class AllocatorFreeLock final : public FixBase {
			static constexpr size_t BackingOffset = 0x108;
			static constexpr size_t ChunkTableOffset = 0x110;
			static constexpr size_t ChunkCountOffset = 0x130;
			static constexpr size_t LockOffset = 0x158;
			static constexpr size_t BackingFreeSlot = 4;

			std::optional<PointerFunctionOf<Resolved::GraphicsAllocatorFreeFn>> m_free;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			AllocatorFreeLock() {
				Resolved::GraphicsAllocatorFreeFn free;
				if (!Resolve(Resolved::GraphicsAllocatorFreeFunction, free))
					return;

				m_free.emplace("Graphics::SmallObjectAllocator::Free", free);
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

				const auto criticalSection = &At<CRITICAL_SECTION>(allocator, LockOffset);
				EnterCriticalSection(criticalSection);
				if (IsSlabBlock(allocator, block)) {
					// takes the (recursive) lock again
					m_free->bridge(allocator, block);
					LeaveCriticalSection(criticalSection);
					return;
				}

				LeaveCriticalSection(criticalSection);
				const auto backing = At<void*>(allocator, BackingOffset);
				VirtualFunction<void(*)(void*, void*)>(backing, BackingFreeSlot)(backing, block);
			}

			/// The original's membership test (page header at block & ~0x3FF). Only valid under the allocator lock, since
			/// the chunk table is reallocated when it grows.
			static bool IsSlabBlock(void* allocator, void* block) {
				const auto page = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(block) & ~static_cast<uintptr_t>(0x3FF));
				const auto index = At<uint32_t>(page, 0x1C);
				if (index >= At<uint32_t>(allocator, ChunkCountOffset))
					return false;

				const auto chunk = At<uint8_t*>(At<uint8_t*>(allocator, ChunkTableOffset), 0x10 + index * 0x30);
				return chunk && static_cast<uint64_t>(static_cast<uint8_t*>(block) - chunk) < 0x4000;
			}
		};

		/// Every dynamic buffer write during draw building frees its previous staging block and allocates a new one
		/// through the graphics allocator at AllocatorManager+0x10. Its Alloc/Free go to a small-object allocator whose
		/// Free takes a global lock and, for larger blocks, calls the backing allocator (second global lock, coalescing and
		/// a region re-sort) while still holding it, so the draw-building job threads spend about a third of their time
		/// waiting on each other.
		/// This keeps freed blocks of that allocator in power-of-two size buckets and hands them back out, so most
		/// allocations never reach those locks. The allocator's vtable slots are swapped (they are tiny forwarding
		/// wrappers). Every pooled block is a genuine allocation of that allocator: its size class comes from the
		/// allocator's own size query, so blocks still held by the game stay valid for the original Free after the pool
		/// is turned off.
		class StagingPool final : public FixBase {
			// Alloc = mov eax, 1; lock xadd [rcx+0x210], eax; ...  Free = add rcx, 0x90; mov rax, [rcx]; jmp [rax+0x20]
			static constexpr uint8_t AllocWrapperCode[]{0xB8, 0x01, 0x00, 0x00, 0x00, 0xF0, 0x0F, 0xC1, 0x81, 0x10, 0x02, 0x00, 0x00};
			static constexpr uint8_t FreeWrapperCode[]{0x48, 0x81, 0xC1, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x20};
			static constexpr uint8_t SizeWrapperCode[]{0x48, 0x81, 0xC1, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x48};

			static constexpr size_t TerminateSlot = 1;
			static constexpr size_t AllocSlot = 2;
			static constexpr size_t FreeSlot = 4;
			static constexpr size_t SizeSlot = 9;
			static constexpr size_t AllocCounterOffset = 0x210;
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

			void* const* m_allocatorManager{};
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

				if (!Resolve(Resolved::GraphicsAllocatorManagerInstance, m_allocatorManager))
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

				const auto manager = *m_allocatorManager;
				const auto allocator = manager ? At<void*>(manager, 0x10) : nullptr;
				if (!allocator) {
					m_waiting = true;
					SetStatus("Waiting for the graphics allocator");
					return;
				}

				const auto table = *static_cast<void***>(allocator);
				if (!Matches(table[AllocSlot], AllocWrapperCode) || !Matches(table[FreeSlot], FreeWrapperCode) || !Matches(table[SizeSlot], SizeWrapperCode)) {
					m_available = false;
					SetStatus("Unavailable: unexpected allocator layout");
					return;
				}

				m_target = allocator;
				m_originalTerminate = reinterpret_cast<TerminateFn>(table[TerminateSlot]);
				m_originalAlloc = reinterpret_cast<AllocFn>(table[AllocSlot]);
				m_originalFree = reinterpret_cast<FreeFn>(table[FreeSlot]);
				m_blockSize = reinterpret_cast<SizeFn>(table[SizeSlot]);
				m_vtable = table;
				WriteSlot(table, TerminateSlot, &TerminateDetour);
				WriteSlot(table, FreeSlot, &FreeDetour);
				WriteSlot(table, AllocSlot, &AllocDetour);
				SetStatus("On");
			}

		private:
			static bool Matches(const void* function, std::span<const uint8_t> code) {
				return std::memcmp(function, code.data(), code.size()) == 0;
			}

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
					std::atomic_ref(At<int32_t>(allocator, AllocCounterOffset)).fetch_add(1);
					return block;
				}

				// Round misses up to the class size so the block comes back to the same class when freed.
				return m_originalAlloc(allocator, 1ULL << (sizeClass + MinClassShift), PoolAlignment);
			}

			void Free(void* allocator, void* block) {
				// Blocks the backing allocator handed out directly (marker 0xFFFF at -0x10) have no size in their header.
				if (block && allocator == m_target.load(std::memory_order_relaxed)
					&& !(reinterpret_cast<uintptr_t>(block) & (PoolAlignment - 1))
					&& At<uint16_t>(block, DirectMarkerOffset) != DirectMarker) {
					// The allocator's own size query: slab element size from the page header, or the requested size from
					// the block header. A block goes to the largest class it can hold; past twice the top class it is left alone.
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

				WriteSlot(m_vtable, AllocSlot, m_originalAlloc);
				WriteSlot(m_vtable, FreeSlot, m_originalFree);
				WriteSlot(m_vtable, TerminateSlot, m_originalTerminate);
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

		/// Minions are client-side objects whose follow AI (called from Companion::Update) sweeps a sphere against the
		/// level collision every frame. Hiding a minion only sets the model render flag, so a crowd of hidden minions still
		/// pays for all of it. This skips the follow AI for minions whose model is hidden; Companion::Update still warps
		/// them back to their owner when they fall too far behind.
		class HiddenMinionFreeze final : public FixBase {
			std::optional<PointerFunctionOf<Resolved::CompanionFollowFn>> m_follow;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			HiddenMinionFreeze() {
				Resolved::CompanionFollowFn follow;
				if (!Resolve(Resolved::CompanionFollowFunction, follow))
					return;

				m_follow.emplace("Companion::Follow", follow);
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
				if (At<uint64_t>(companion, GameObjectRenderFlagsOffset) & VisibilityFlagsModel)
					return;

				m_follow->bridge(companion);
			}
		};

		/// Every job kick prepares its job list, which waits for the previous run and then calls WaitForSingleObject on
		/// the same manual-reset event a second time before resetting it. The event can only be reset by this function,
		/// so the second wait always returns at once: ~75 wasted syscalls per frame on the main thread.
		/// This turns that call into a jump over it, in both Prepare variants (array lists and single-item lists).
		class PrepareWaitSkip final : public FixBase {
			static constexpr uint16_t CallIndirect = 0x15FF;  // FF 15
			static constexpr uint16_t JumpOver = 0x04EB;  // EB 04: skips the rest of the 6 byte call

			Resolved::JobListPrepareWaits m_sites;
			bool m_enabled = false;

		public:
			PrepareWaitSkip() {
				m_available = Resolve(Resolved::JobListPrepareWaitCalls, m_sites);
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

		/// Manager::RenderView kicks a single-item job (BG instancing prep) at the start of every view and
		/// BGInstancingRenderer::Render waits for it a little later: ~20 kicks per frame, each with an enqueue, a worker
		/// wake and an event wait, for ~1 us of work. This runs that one item on the main thread at kick time instead,
		/// through the list's own claim and task functions, so the list ends up exactly as a worker would leave it.
		class BgPrepInline final : public FixBase {
			/// As filled by JobList vf1: claim function, its object, then 16 bytes the claim function reads.
			struct JobDescriptor {
				void* Claim;
				void* Owner;
				uint8_t State[0x10];
			};

			using ClaimFn = void*(*)(void* owner, void* state, void*** argument, int32_t* remaining);

			Resolved::BgInstancingPrep m_prep;
			std::optional<PointerFunctionOf<Resolved::JobListKickFn>> m_kick;

			void* m_prepList{};
			DWORD m_mainThreadId{};

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			BgPrepInline() {
				if (!Resolve(Resolved::BgInstancingPrepJob, m_prep))
					return;

				m_kick.emplace("TaskManager::KickJobList", m_prep.Kick);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			/// The inline path only runs on the main thread inside RenderView, so it is never in flight here; after
			/// disabling, the stock kick finds a finished, signaled list.
			void SetEnabled(bool enabled, const FrameState&) override {
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

				m_prepList = static_cast<uint8_t*>(manager) + m_prep.PrepListOffset;
				m_mainThreadId = GetCurrentThreadId();
				m_hooks += m_kick->SetHook([this](void* taskManager, void* jobList) { return KickDetour(taskManager, jobList); });
				m_enabled = true;
				SetStatus("On");
			}

		private:
			/// Main thread, job workers, the action timeline thread and bone physics all kick: keep the filter cheap.
			uint32_t KickDetour(void* taskManager, void* jobList) {
				if (jobList != m_prepList || GetCurrentThreadId() != m_mainThreadId)
					return m_kick->bridge(taskManager, jobList);

				// item count
				if (!VirtualFunction<uint32_t(*)(void*)>(jobList, 4)(jobList))
					return 0;

				// Prepare: waits for the previous run, resets counters
				VirtualFunction<void(*)(void*)>(jobList, 2)(jobList);
				JobDescriptor descriptor{};
				VirtualFunction<JobDescriptor*(*)(void*, JobDescriptor*)>(jobList, 1)(jobList, &descriptor);

				// Same steps as InnerThread::Run: claim a task, run it with the pool context.
				const auto context = static_cast<uint8_t*>(taskManager) + TaskManagerPoolContextOffset;
				while (true) {
					void** argument = nullptr;
					int32_t remaining = 0;
					const auto task = reinterpret_cast<ClaimFn>(descriptor.Claim)(descriptor.Owner, descriptor.State, &argument, &remaining);
					if (!task)
						break;

					if (argument)
						VirtualFunction<void(*)(void*, void*, void*)>(task, 2)(task, context, *argument);
					else
						VirtualFunction<void(*)(void*, void*)>(task, 1)(task, context);

					if (!remaining)
						break;
				}

				return 1;
			}
		};

		/// The hotbar update walks all 18 bars and both cross hotbar sets every frame. For hidden ones it still runs
		/// RaptureHotbarModule::PrepareSlotForRender on every slot into a throwaway intermediate. Nothing reads the result:
		/// a bar that becomes visible gets a full prepare that same frame. This jumps over the intermediate setup and the
		/// prepare call at both hidden-bar sites; the number array clears and the item reload call before them still run.
		class HiddenHotbarSkip final : public FixBase {
			Resolved::HiddenHotbarPrepares m_sites;
			uint16_t m_barOriginal{};
			uint16_t m_crossBarOriginal{};
			bool m_enabled = false;

		public:
			HiddenHotbarSkip() {
				if (!Resolve(Resolved::HiddenHotbarPrepareCalls, m_sites))
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

				// jmp rel8 to the inc esi after the prepare call
				const auto jump = static_cast<uint16_t>(0xEB | (m_sites.Length - 2) << 8);
				m_enabled = enabled;
				WriteCodeAtomically(m_sites.Bar, enabled ? jump : m_barOriginal);
				WriteCodeAtomically(m_sites.CrossBar, enabled ? jump : m_crossBarOriginal);
				SetStatus(enabled ? "On" : "Off");
			}
		};

		/// After sampling, the animation update finishes every skeleton serially on the main thread (blend timers, pose
		/// copies), sorted by attach depth so parents go before children. Skeletons of the same depth do not touch each
		/// other, so this runs each depth level on the job pool, through the same parallel-for group the animation submit
		/// uses earlier in the frame, with a full join between levels.
		/// Skeletons that cast a ground ray or have pending animation control removals stay on the main thread.
		class AnimTailParallel final : public FixBase {
			static constexpr size_t GroupOffset = 0x30;  // parallel-for group inside the submit base
			static constexpr int32_t MinParallel = 16;  // smaller depth levels run serially
			static constexpr int32_t MaxSkeletons = 4096;  // group capacity: 16 chunks x 32 blocks x 8 items
			static constexpr size_t WriterSize = 40;

			struct Entry {
				void* Skeleton;
				int32_t Depth;
			};

			// TailJob is a plain function pointer the workers call; there is one instance at a time.
			static inline std::atomic<AnimTailParallel*> s_instance;
			static inline float s_jobDeltaTime;
			static inline thread_local bool s_inUpdate;
			static inline thread_local int s_tailCalls;

			Resolved::AnimationTail m_functions;
			std::optional<PointerFunctionOf<Resolved::AnimationUpdateFn>> m_update;
			std::optional<PointerFunctionOf<Resolved::AnimationTailFn>> m_tail;
			std::vector<void*> m_mainOnly = std::vector<void*>(MaxSkeletons);

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			AnimTailParallel() {
				if (!Resolve(Resolved::AnimationTailFunctions, m_functions))
					return;

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
				if (count <= 0 || count > MaxSkeletons || entries[0].Skeleton != skeleton || !base) {
					// unexpected state: let the stock loop do everything
					s_inUpdate = false;
					return m_tail->bridge(skeleton, deltaTime);
				}

				const auto group = static_cast<uint8_t*>(base) + GroupOffset;
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
					FlushWriters(group);
					s_jobDeltaTime = deltaTime;
					At<void*>(group, 0x20) = nullptr;
					At<void*>(group, 0x28) = reinterpret_cast<void*>(&TailJob);
					std::atomic_ref(At<int32_t>(group, 0xB4)).exchange(0);
					std::atomic_ref(At<int32_t>(group, 0xB8)).exchange(0);
					if (std::atomic_ref(At<int32_t>(group, 0xB0)).load()) {
						const auto jobList = At<void*>(group, 0x18);
						m_functions.Kick(*m_functions.TaskManager, jobList);
						for (size_t i = 0; i < mainCount; i++)
							m_tail->bridge(m_mainOnly[i], deltaTime);

						mainCount = 0;
						if (At<uint8_t>(group, 0xBC))
							m_functions.HelpPerItem(group);
						else
							m_functions.HelpBlocks(group);

						// wait
						VirtualFunction<void(*)(void*)>(jobList, 3)(jobList);
					} else {
						for (auto i = start; i < end; i++) {
							if (const auto skeleton = entries[i].Skeleton; !NeedsMainThread(skeleton))
								m_tail->bridge(skeleton, deltaTime);
						}
					}

					At<void*>(group, 0x20) = nullptr;
					At<void*>(group, 0x28) = nullptr;
					ResetWriters(group);
					for (size_t i = 0; i < 16; i++) {
						if (const auto chunk = At<uint32_t*>(group, 0x30 + i * 8))
							*chunk = 0;
					}

					At<uint32_t>(group, 0xB0) = 0;
				}

				for (size_t i = 0; i < mainCount; i++)
					m_tail->bridge(m_mainOnly[i], deltaTime);
			}

			static void FlushWriters(uint8_t* group) {
				auto writer = At<uint8_t*>(group, 0x08);
				for (uint32_t i = 0, writers = At<uint32_t>(group, 0x10); i < writers; i++, writer += WriterSize) {
					if (const auto block = At<uint32_t*>(writer, 0x20))
						*block = At<uint32_t>(writer, 0x10);
				}
			}

			static void ResetWriters(uint8_t* group) {
				FlushWriters(group);
				auto writer = At<uint8_t*>(group, 0x08);
				for (uint32_t i = 0, writers = At<uint32_t>(group, 0x10); i < writers; i++, writer += WriterSize) {
					At<uint32_t>(writer, 0x10) = 8;
					At<void*>(writer, 0x20) = nullptr;
				}
			}

			/// Ground ray casts (BG collision) and animation control removals are only done on the main thread.
			static bool NeedsMainThread(void* skeleton) {
				if (const auto ground = At<void*>(skeleton, 0x80);
					ground && (At<uint8_t>(ground, 0x10) & 1) && At<void*>(ground, 0x18) && At<void*>(ground, 0x20))
					return true;

				const auto partials = At<uint8_t*>(skeleton, SkeletonPartialSkeletonsOffset);
				for (size_t i = 0, count = At<uint16_t>(skeleton, SkeletonPartialSkeletonCountOffset); i < count; i++) {
					// pose, pending removals
					if (const auto partial = partials + i * PartialSkeletonSize; At<void*>(partial, 0x148) && At<uint64_t>(partial, 0x1C0))
						return true;
				}

				return false;
			}

			/// Job worker or main help loop: one skeleton per item.
			static void TailJob(void* context, void** item) {
				s_instance.load(std::memory_order_relaxed)->m_tail->bridge(*item, s_jobDeltaTime);
			}
		};

		/// Camera culling splits BG objects into jobs of 200, but puts every character into a single job, so one thread
		/// culls and registers all characters while the others wait at the join. The job function only reads its item
		/// (type, index list, start, count), so the thread that gets the character item works through it in small chunks
		/// and every thread that finishes its own item of the same group helps.
		class CharacterCullSplit final : public FixBase {
			static constexpr uint8_t CharacterItem = 2;
			static constexpr size_t ItemSize = 0x40;
			static constexpr size_t StartOffset = 0x30;
			static constexpr size_t CountOffset = 0x34;
			static constexpr int32_t Chunk = 16;

			/// Shared by the owner and its helpers; one character item is in flight at a time (the culling runs one view at
			/// a time and joins before returning).
			struct Shared {
				void* CullingManager{};
				uint8_t Item[ItemSize]{};
				int32_t Total{};
				std::atomic<int32_t> Next;
				std::atomic<int32_t> Active;
			};

			std::optional<PointerFunctionOf<Resolved::CameraCullJobFn>> m_cullJob;
			Shared m_shared;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			CharacterCullSplit() {
				Resolved::CameraCullJobFn cullJob;
				if (!Resolve(Resolved::CameraCullJobFunction, cullJob))
					return;

				m_cullJob.emplace("CameraCulling::Job", cullJob);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			/// An owner already inside the detour keeps claiming until every chunk is done, so disabling mid-frame only
			/// stops new help.
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
				if (item[0] == CharacterItem && At<uint32_t>(item, CountOffset) > Chunk && !m_shared.Active.load()) {
					std::memcpy(m_shared.Item, item, ItemSize);
					m_shared.CullingManager = cullingManager;
					m_shared.Total = static_cast<int32_t>(At<uint32_t>(item, CountOffset));
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
				uint8_t local[ItemSize];
				std::memcpy(local, m_shared.Item, ItemSize);
				const auto start = At<uint32_t>(local, StartOffset);
				const auto total = m_shared.Total;
				while (true) {
					const auto i = m_shared.Next.fetch_add(Chunk);
					if (i >= total)
						break;

					At<uint32_t>(local, StartOffset) = start + static_cast<uint32_t>(i);
					At<uint32_t>(local, CountOffset) = static_cast<uint32_t>(std::min(Chunk, total - i));
					m_cullJob->bridge(m_shared.CullingManager, local);
				}
			}
		};

		/// The per-view cell culling group and the culling setup tail group hand out work in blocks of up to 8 items. With
		/// ~20 cells per view only ~3 threads get any, and the main thread then waits for the slowest block. Both groups
		/// already have the engine's per-item claim variant (one item at a time from a flat counter); this routes their
		/// block help function to it, on workers and the main thread alike.
		class CullPerItemClaim final : public FixBase {
			Resolved::CullingParallelFors m_groups;
			std::optional<PointerFunctionOf<Resolved::ParallelForHelpFn>> m_cellHelp;
			std::optional<PointerFunctionOf<Resolved::ParallelForHelpFn>> m_setupHelp;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			CullPerItemClaim() {
				if (!Resolve(Resolved::CullingParallelForGroups, m_groups))
					return;

				m_cellHelp.emplace("CullingManager::CellGroup::HelpBlocks", m_groups.CellHelpBlocks);
				m_setupHelp.emplace("CullingManager::SetupGroup::HelpBlocks", m_groups.SetupHelpBlocks);
				m_available = true;
			}

			[[nodiscard]] bool Enabled() const override { return m_enabled; }

			/// Every thread in one fork-join must use the same claim mode, and these groups only run (fully joined) inside
			/// rendering, which is not running now.
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

		/// DeviceDX11::PostTick gathers every context's command list for each of the 87 lists: it copies every 16 KB block
		/// twice (once to a scratch area, once back) and merge sorts the entries, ~1300 calls and a few MB of copying per
		/// frame. This copies only the used entries, skips the sort when they are already in key order (the sort is stable,
		/// so it would leave them unchanged), and otherwise calls the game's merge sort over the whole range, which splits
		/// and merges exactly like the gather's inlined top level. The result is byte for byte what the game produces.
		class GatherUsedBytes final : public FixBase {
			static constexpr size_t ContextArrayOffset = 0x08;  // Device::ContextArray
			static constexpr size_t ContextCountOffset = 0x6C;
			static constexpr size_t ContextSize = 0x2F78;  // sizeof(Kernel::Context)
			static constexpr size_t ListsOffset = 0x18;  // per list: first block, write pointer, u32 free slots, u32 blocks
			static constexpr size_t ListSize = 24;
			static constexpr size_t BlockSize = 0x4000;
			static constexpr uint32_t EntriesPerBlock = 1024;
			static constexpr size_t NextBlockOffset = 0x3FF0;  // link entry in the last slot of a full block
			static constexpr size_t EntrySize = 16;  // u32 sort key, 4 bytes, command pointer

			Resolved::CommandListGather m_functions;
			std::optional<PointerFunctionOf<Resolved::CommandListGatherFn>> m_gather;

			xivres::util::on_dtor::multi m_hooks;
			bool m_enabled = false;

		public:
			GatherUsedBytes() {
				if (!Resolve(Resolved::CommandListGatherFunctions, m_functions))
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

				const auto contexts = At<uint32_t>(device, ContextCountOffset);
				const auto contextArray = At<uint8_t*>(device, ContextArrayOffset);
				*total = 0;
				for (uint32_t i = 0; i < contexts; i++) {
					const auto descriptor = contextArray + i * ContextSize + ListsOffset + list * ListSize;
					const auto blocks = At<uint32_t>(descriptor, 0x14);
					counts[i] = 0;
					if (!blocks) {
						results[i] = nullptr;
						continue;
					}

					const auto count = blocks * EntriesPerBlock - At<uint32_t>(descriptor, 0x10);
					const auto destination = *cursor;
					auto block = At<uint8_t*>(descriptor, 0);
					auto output = destination;
					for (uint32_t b = 1; b < blocks; b++) {
						std::memcpy(output, block, BlockSize);
						block = At<uint8_t*>(block, NextBlockOffset);
						output += BlockSize;
					}

					std::memcpy(output, block, count * EntrySize - static_cast<size_t>(output - destination));

					if (!IsSorted(destination, count)) {
						// the same scratch area the stock gather uses, right after this context's reserved blocks
						const auto scratch = destination + static_cast<size_t>(blocks) * BlockSize;
						std::memcpy(scratch, destination, count * EntrySize);
						m_functions.Sort(destination, scratch, 0, static_cast<int32_t>(count) - 1);
					}

					counts[i] = count;
					results[i] = destination;
					*total += count;
					*cursor = destination + count * EntrySize;
					*remaining -= static_cast<uint32_t>(count * EntrySize);
				}

				return contexts;
			}

			static bool IsSorted(const uint8_t* entries, uint32_t count) {
				auto previous = At<uint32_t>(entries, 0);
				for (uint32_t i = 1; i < count; i++) {
					const auto key = At<uint32_t>(entries, i * EntrySize);
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
	std::optional<Misc::Hooks::PointerFunctionOf<Game::Resolved::TaskManagerExecuteAllTasksFn>> ExecuteAllTasks;
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

		// Fixes are toggled and updated from here, like CrowdFix does from Framework.Update: on the main thread, before
		// the frame's tasks, and so outside DeviceDX11::PostTick, which some of them patch.
		Game::Resolved::TaskManagerExecuteAllTasksFn executeAllTasks;
		if (const auto status = Game::Resolved::TaskManagerExecuteAllTasksFunction.Resolve(executeAllTasks); status != Game::Signatures::ResolveError::Ok) {
			Unusable = std::format("Unavailable: {}", status.Detail);
			Logger->Format<LogLevel::Warning>(LogCategory::General, "CrowdFix fixes are left off: {}", status.Detail);
			return;
		}

		ExecuteAllTasks.emplace("TaskManager::ExecuteAllTasks", executeAllTasks);
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
