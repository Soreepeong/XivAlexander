#include "pch.h"
#include "App_Feature_AnimationLockLatencyHandler.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

class App::Feature::AnimationLockLatencyHandler::Internals {
public:
	// Server responses have been usually taking between 50ms and 100ms on below-1ms
	// latency to server, so 75ms is a good average.
	// The server will do sanity check on the frequency of action use requests,
	// and it's very easy to identify whether you're trying to go below allowed minimum value.
	// This addon is already in gray area. Do NOT decrease this value. You've been warned.
	// Feel free to increase and see how does it feel like to play on high latency instead, though.
	static inline const double ExtraDelay = 0.075;

	class SingleConnectionHandler {
		const uint64_t CAST_SENTINEL = 0;
	public:
		// The game will allow the user to use an action, if server does not respond in 500ms since last action usage.
		// This will result in cancellation of following actions, so to prevent this, we keep track of outgoing action
		// request timestamps, and stack up required animation lock time responses from server.
		// The game will only process the latest animation lock duration information.
		std::deque<uint64_t> PendingActionRequestTimestamps;
		uint64_t m_lastAnimationLockEndsAt = 0;

		Internals& internals;
		Network::SingleConnection& conn;
		SingleConnectionHandler(Internals& internals, Network::SingleConnection& conn)
			: internals(internals)
			, conn(conn) {
			using namespace App::Network::Structures;

			const auto& config = ConfigRepository::Config();

			conn.AddOutgoingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == 0x14 && (pMessage->Data.IPC.SubType == config.RequestUseAction)) {
					// pMessage->DebugPrint(L"Send", true);
					FILETIME ft;
					SYSTEMTIME st;
					GetSystemTimePreciseAsFileTime(&ft);
					FileTimeToSystemTime(&ft, &st);

					const auto now = Utils::GetHighPerformanceCounter();
					PendingActionRequestTimestamps.push_back(now);

					if (PendingActionRequestTimestamps.size() == 1) {
						m_lastAnimationLockEndsAt = now;
					}

					const auto actionId = *reinterpret_cast<const uint32_t*>(pMessage->Data.IPC.Data.Raw + 4);

					Misc::Logger::GetLogger().Format(
						"Attack Request: length=%x t=%02x skill=%04x",
						pMessage->Length,
						pMessage->Data.IPC.Data.Raw[0],
						actionId);
				}
				return true;
				});
			conn.AddIncomingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>& additionalMessages) {
				const auto now = Utils::GetHighPerformanceCounter();

				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == 0x14) {
					// Only interested in messages intended for the current player
					if (pMessage->CurrentActor == pMessage->SourceActor) {
						if (std::find(config.SkillResultResponses, config.SkillResultResponses + _countof(config.SkillResultResponses), static_cast<int>(pMessage->Data.IPC.SubType)) != config.SkillResultResponses + _countof(config.SkillResultResponses)) {
							double newDuration = pMessage->Data.IPC.Data.ActionEffect.AnimationLockDuration;
							if (!PendingActionRequestTimestamps.empty()
								&& pMessage->Data.IPC.Data.ActionEffect.ActionId != 0x0007 // ignore auto-attack
								) {
								// 100ms animation lock after cast ends stays. Modify animation lock duration for instant actions only.
								if (PendingActionRequestTimestamps.front() != CAST_SENTINEL) {
									auto extraDelay = ExtraDelay;
									if (extraDelay <= 0.07) {
										// I told you to not decrease the value below 70ms.
										if (rand() % 10000 < 50) {
											// This is what you get for decreasing the value.
											extraDelay = 5;
										}
									}
									const auto addedDelay = static_cast<uint64_t>(1000. * std::max(0., ExtraDelay + newDuration));
									m_lastAnimationLockEndsAt += addedDelay;
									newDuration = std::max(0.f, static_cast<int64_t>(m_lastAnimationLockEndsAt - now) / 1000.f);
								}
								PendingActionRequestTimestamps.pop_front();
							}
							Misc::Logger::GetLogger().Format(
								"Attack Response: length=%x skill=%04x wait=%.3f->%.3f",
								pMessage->Length,
								pMessage->Data.IPC.Data.ActionEffect.ActionId,
								pMessage->Data.IPC.Data.ActionEffect.AnimationLockDuration, newDuration);

							pMessage->Data.IPC.Data.ActionEffect.AnimationLockDuration = static_cast<float>(newDuration);

						} else if (pMessage->Data.IPC.SubType == config.ActorControlSelf) {

							// Latest action request has been rejected from server.
							if (pMessage->Data.IPC.Data.ActorControlSelf.Category == 0x2bc) {
								if (!PendingActionRequestTimestamps.empty())
									PendingActionRequestTimestamps.pop_front();
								Misc::Logger::GetLogger().Format(
									"Rollback: length=%x p1=%08x p2=%08x skill=%04x",
									pMessage->Length,
									pMessage->Data.IPC.Data.ActorControlSelf.Param1,
									pMessage->Data.IPC.Data.ActorControlSelf.Param2,
									pMessage->Data.IPC.Data.ActorControlSelf.Param3);
							}

						} else if (pMessage->Data.IPC.SubType == config.ActorControl) {
							
							// The server has cancelled a cast in progress.
							if (pMessage->Data.IPC.Data.ActorControl.Category == 0x0f) {
								const auto latency = conn.GetServerResponseDelay();
								double responseWait = latency;
								if (!PendingActionRequestTimestamps.empty()) {
									if (PendingActionRequestTimestamps.front() == 0)
										responseWait = 0;
									else
										responseWait = (now - PendingActionRequestTimestamps.front()) / 1000.f;
									PendingActionRequestTimestamps.pop_front();
								}
								Misc::Logger::GetLogger().Format(
									"CancelCast: length=%x p1=%08x p2=%08x skill=%04x p4=%08x pad1=%04x pad2=%08x latency=%.3f responseWait=%.3f",
									pMessage->Length,
									pMessage->Data.IPC.Data.ActorControl.Param1,
									pMessage->Data.IPC.Data.ActorControl.Param2,
									pMessage->Data.IPC.Data.ActorControl.Param3,
									pMessage->Data.IPC.Data.ActorControl.Param4,
									pMessage->Data.IPC.Data.ActorControl.Padding1,
									pMessage->Data.IPC.Data.ActorControl.Padding2,
									latency, responseWait);
							}

						} else if (pMessage->Data.IPC.SubType == config.ActorCast) {
							const auto latency = conn.GetServerResponseDelay();

							// Mark that the last request was a cast.
							// If it indeed is a cast, the game UI will block the user from generating additional requests,
							// so first item is guaranteed to be the cast action.
							if (!PendingActionRequestTimestamps.empty())
								PendingActionRequestTimestamps[0] = CAST_SENTINEL;

							Misc::Logger::GetLogger().Format(
								"ActorCast: length=%x "
								"skill=%04x type=%02x u1=%02x skill2=%04x u2=%08x time=%.3f "
								"target=%08x rotation=%.3f u3=%08x "
								"x=%d y=%d z=%d u=%04x",
								pMessage->Length,

								pMessage->Data.IPC.Data.ActorCast.ActionId,
								pMessage->Data.IPC.Data.ActorCast.SkillType,
								pMessage->Data.IPC.Data.ActorCast.Unknown1,
								pMessage->Data.IPC.Data.ActorCast.ActionId2,
								pMessage->Data.IPC.Data.ActorCast.Unknown2,
								pMessage->Data.IPC.Data.ActorCast.CastTime,

								pMessage->Data.IPC.Data.ActorCast.TargetId,
								pMessage->Data.IPC.Data.ActorCast.Rotation,
								pMessage->Data.IPC.Data.ActorCast.Unknown3,
								pMessage->Data.IPC.Data.ActorCast.X,
								pMessage->Data.IPC.Data.ActorCast.Y,
								pMessage->Data.IPC.Data.ActorCast.Z,
								pMessage->Data.IPC.Data.ActorCast.Unknown4);
						}
					}
				}
				return true;
				});
		}
		~SingleConnectionHandler() {
			conn.RemoveMessageHandlers(this);
		}
	};

	std::map<Network::SingleConnection*, std::unique_ptr<SingleConnectionHandler>> m_handlers;

	Internals() {
		Network::SocketHook::Instance()->AddOnSocketFoundListener(this, [&](Network::SingleConnection& conn) {
			m_handlers.emplace(&conn, std::make_unique<SingleConnectionHandler>(*this, conn));
		});
		Network::SocketHook::Instance()->AddOnSocketGoneListener(this, [&](Network::SingleConnection& conn) {
			m_handlers.erase(&conn);
		});
	}

	~Internals() {
		m_handlers.clear();
		Network::SocketHook::Instance()->RemoveListeners(this);
	}
};

App::Feature::AnimationLockLatencyHandler::AnimationLockLatencyHandler()
: impl(std::make_unique<Internals>()){
}

App::Feature::AnimationLockLatencyHandler::~AnimationLockLatencyHandler() {
}

void App::Feature::AnimationLockLatencyHandler::ReloadConfig() {
	ConfigRepository::Config().Reload();
}
