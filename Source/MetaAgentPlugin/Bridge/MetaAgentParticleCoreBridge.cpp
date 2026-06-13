#include "Bridge/MetaAgentParticleCoreBridge.h"

#include "Bridge/MetaAgentTypeBridge.h"
#include "Core/MetaAgent.h"
#include "Systems/ParticleRuntime/MetaAgentParticleRuntime.h"

#include "Containers/StringConv.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "metaagent/particle/scheduler.hpp"

namespace MetaAgentParticleCoreBridge
{
struct FCoreSchedulerState
{
	metaagent::particle::ParticleScheduler Scheduler;
};
} // namespace MetaAgentParticleCoreBridge

struct FMetaAgentCoreBridgeFriend
{
	static void SyncRuntimeToCore(UMetaAgentParticleRuntime& Runtime, metaagent::particle::ParticleScheduler& Scheduler)
	{
		MetaAgentTypeBridge::copy_pattern_config_to_core(Runtime.PatternConfig, Scheduler.pattern_config);
		MetaAgentTypeBridge::copy_pattern_runtime_to_core(Runtime.PatternRuntime, Scheduler.pattern_runtime);
		Scheduler.settings.manual_pattern_state_advance = Runtime.bManualPatternStateAdvance;
		Scheduler.settings.return_release_authority_threshold = Runtime.ReturnReleaseAuthorityThreshold;
		Scheduler.last_pattern_tick_delta_seconds = Runtime.LastPatternTickDeltaSeconds;
		Scheduler.forming_steering_blend_elapsed_seconds = Runtime.FormingSteeringBlendElapsedSeconds;
		Scheduler.steering_target_enabled = Runtime.LatestSnapshot.bSteeringTargetEnabled;
		Scheduler.forming_steering_offsets.clear();
		Scheduler.forming_steering_offsets.reserve(
			static_cast<size_t>(Runtime.LatestSnapshot.SuggestedSteeringDirections.Num()));
		for (const FVector& Offset : Runtime.LatestSnapshot.SuggestedSteeringDirections)
		{
			Scheduler.forming_steering_offsets.push_back(metaagent::core::Vec3(Offset.X, Offset.Y, Offset.Z));
		}
		Scheduler.forming_steering_blend_duration_seconds = Runtime.FormingSteeringBlendDurationSeconds;
	}

	static void SyncCoreToRuntime(UMetaAgentParticleRuntime& Runtime, const metaagent::particle::ParticleScheduler& Scheduler)
	{
		MetaAgentTypeBridge::copy_pattern_config_from_core(Scheduler.pattern_config, Runtime.PatternConfig);
		MetaAgentTypeBridge::copy_pattern_runtime_from_core(Scheduler.pattern_runtime, Runtime.PatternRuntime);
		Runtime.bManualPatternStateAdvance = Scheduler.settings.manual_pattern_state_advance;
		Runtime.ReturnReleaseAuthorityThreshold = Scheduler.settings.return_release_authority_threshold;
		Runtime.LastPatternTickDeltaSeconds = Scheduler.last_pattern_tick_delta_seconds;
		Runtime.FormingSteeringBlendElapsedSeconds = Scheduler.forming_steering_blend_elapsed_seconds;
		Runtime.LatestSnapshot.bSteeringTargetEnabled = Scheduler.steering_target_enabled;
		Runtime.FormingSteeringBlendDurationSeconds = Scheduler.forming_steering_blend_duration_seconds;

		Runtime.LatestSnapshot.SuggestedSteeringDirections.Reset(
			static_cast<int32>(Scheduler.forming_steering_offsets.size()));
		Runtime.LatestSnapshot.SuggestedSteeringDirections.Reserve(
			static_cast<int32>(Scheduler.forming_steering_offsets.size()));
		for (const metaagent::core::Vec3& Offset : Scheduler.forming_steering_offsets)
		{
			Runtime.LatestSnapshot.SuggestedSteeringDirections.Add(FVector(Offset.x, Offset.y, Offset.z));
		}
	}

	static metaagent::particle::SchedulerCallbacks BuildCallbacks(
		UMetaAgentParticleRuntime& Runtime,
		MetaAgentParticleCoreBridge::FCoreSchedulerState& State)
	{
		metaagent::particle::SchedulerCallbacks Callbacks;

		auto SyncFromRuntime = [&Runtime, &State]()
		{
			FMetaAgentCoreBridgeFriend::SyncRuntimeToCore(Runtime, State.Scheduler);
		};

		Callbacks.build_pattern_targets = [&Runtime, SyncFromRuntime]() -> bool
		{
			const bool bResult = Runtime.BuildPatternTargets();
			SyncFromRuntime();
			return bResult;
		};

		Callbacks.begin_pattern_start = [&Runtime, SyncFromRuntime]() -> bool
		{
			const bool bResult = Runtime.BeginPatternStart();
			SyncFromRuntime();
			return bResult;
		};

		Callbacks.begin_configured_return = [&Runtime, SyncFromRuntime]() -> bool
		{
			const bool bResult = Runtime.BeginConfiguredReturn();
			SyncFromRuntime();
			return bResult;
		};

		Callbacks.request_dissipate_to_center = [&Runtime, SyncFromRuntime]() -> bool
		{
			const bool bResult = Runtime.RequestDissipateToCenter();
			SyncFromRuntime();
			return bResult;
		};

		Callbacks.complete_pattern_run = [&Runtime, SyncFromRuntime]()
		{
			Runtime.CompletePatternRun();
			SyncFromRuntime();
		};

		Callbacks.enter_pattern_state =
			[&Runtime, SyncFromRuntime](
				const metaagent::particle::PatternState NewState,
				const metaagent::particle::PatternState /*PreviousState*/)
		{
			Runtime.EnterPatternState(MetaAgentTypeBridge::from_core_pattern_state(NewState));
			SyncFromRuntime();
		};

		Callbacks.evaluate_phase_for_state = [&Runtime](
			const metaagent::particle::PatternState State,
			const float NormalizedTimeInState) -> float
		{
			return Runtime.EvaluatePhaseForState(
				MetaAgentTypeBridge::from_core_pattern_state(State),
				NormalizedTimeInState);
		};

		Callbacks.commit_anticipation_baseline_for_forming = [&Runtime, SyncFromRuntime]()
		{
			Runtime.CommitAnticipationBaselineForForming();
			SyncFromRuntime();
		};

		Callbacks.on_transition_side_effects = [&Runtime, SyncFromRuntime](
			const metaagent::particle::PatternState /*NewState*/)
		{
			SyncFromRuntime();
		};

		Callbacks.log_info = [](const metaagent::core::String& Message)
		{
			UE_LOG(LogMetaAgent, Log, TEXT("%s"), *FString(UTF8_TO_TCHAR(Message.c_str())));
		};

		Callbacks.log_warning = [](const metaagent::core::String& Message)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("%s"), *FString(UTF8_TO_TCHAR(Message.c_str())));
		};

		return Callbacks;
	}
};

namespace MetaAgentParticleCoreBridge
{
namespace
{
FString FromCoreString(const metaagent::core::String& Value)
{
	return FString(UTF8_TO_TCHAR(Value.c_str()));
}

TMap<TWeakObjectPtr<UMetaAgentParticleRuntime>, TUniquePtr<FCoreSchedulerState>>& GetStateMap()
{
	static TMap<TWeakObjectPtr<UMetaAgentParticleRuntime>, TUniquePtr<FCoreSchedulerState>> StateMap;
	return StateMap;
}

void PruneStaleStates(TMap<TWeakObjectPtr<UMetaAgentParticleRuntime>, TUniquePtr<FCoreSchedulerState>>& StateMap)
{
	for (auto It = StateMap.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
} // namespace

FCoreSchedulerState* get_or_create_state(UMetaAgentParticleRuntime& Runtime)
{
	TMap<TWeakObjectPtr<UMetaAgentParticleRuntime>, TUniquePtr<FCoreSchedulerState>>& StateMap = GetStateMap();
	PruneStaleStates(StateMap);

	TWeakObjectPtr<UMetaAgentParticleRuntime> RuntimeKey(&Runtime);
	if (TUniquePtr<FCoreSchedulerState>* ExistingState = StateMap.Find(RuntimeKey))
	{
		return ExistingState->Get();
	}

	TUniquePtr<FCoreSchedulerState> NewState = MakeUnique<FCoreSchedulerState>();
	FCoreSchedulerState* NewStatePtr = NewState.Get();
	StateMap.Add(RuntimeKey, MoveTemp(NewState));
	return NewStatePtr;
}

void sync_runtime_to_core(UMetaAgentParticleRuntime& Runtime)
{
	FCoreSchedulerState* State = get_or_create_state(Runtime);
	if (!State)
	{
		return;
	}

	FMetaAgentCoreBridgeFriend::SyncRuntimeToCore(Runtime, State->Scheduler);
}

void sync_core_to_runtime(UMetaAgentParticleRuntime& Runtime)
{
	FCoreSchedulerState* State = get_or_create_state(Runtime);
	if (!State)
	{
		return;
	}

	FMetaAgentCoreBridgeFriend::SyncCoreToRuntime(Runtime, State->Scheduler);
}

void tick_pattern_runtime(UMetaAgentParticleRuntime& Runtime, const float DeltaTimeSeconds)
{
	FCoreSchedulerState* State = get_or_create_state(Runtime);
	if (!State)
	{
		return;
	}

	sync_runtime_to_core(Runtime);
	metaagent::particle::SchedulerCallbacks Callbacks =
		FMetaAgentCoreBridgeFriend::BuildCallbacks(Runtime, *State);
	State->Scheduler.tick_pattern_runtime(DeltaTimeSeconds, Callbacks);
	sync_core_to_runtime(Runtime);
}

void build_representation_frame(const UMetaAgentParticleRuntime& Runtime, FMetaAgentParticleRepresentationFrame& OutFrame)
{
	UMetaAgentParticleRuntime& MutableRuntime = *const_cast<UMetaAgentParticleRuntime*>(&Runtime);
	sync_runtime_to_core(MutableRuntime);
	FCoreSchedulerState* State = get_or_create_state(MutableRuntime);
	if (!State)
	{
		OutFrame = FMetaAgentParticleRepresentationFrame();
		return;
	}

	metaagent::particle::RepresentationFrame CoreFrame = State->Scheduler.build_representation_frame();
	MetaAgentTypeBridge::copy_representation_frame_from_core(CoreFrame, OutFrame);
}

bool dispatch_pattern_transition(
	UMetaAgentParticleRuntime& Runtime,
	const EMetaAgentPatternTransitionTrigger Trigger,
	const bool bSkipReturn)
{
	FCoreSchedulerState* State = get_or_create_state(Runtime);
	if (!State)
	{
		return false;
	}

	sync_runtime_to_core(Runtime);
	metaagent::particle::SchedulerCallbacks Callbacks =
		FMetaAgentCoreBridgeFriend::BuildCallbacks(Runtime, *State);
	const bool bHandled = State->Scheduler.dispatch_pattern_transition(
		MetaAgentTypeBridge::to_core_transition_trigger(Trigger),
		Callbacks,
		bSkipReturn);
	sync_core_to_runtime(Runtime);
	return bHandled;
}

float get_active_state_duration_seconds(const UMetaAgentParticleRuntime& Runtime)
{
	UMetaAgentParticleRuntime& MutableRuntime = *const_cast<UMetaAgentParticleRuntime*>(&Runtime);
	sync_runtime_to_core(MutableRuntime);
	FCoreSchedulerState* State = get_or_create_state(MutableRuntime);
	return State ? State->Scheduler.get_active_state_duration_seconds() : 0.0f;
}

float get_active_state_time_remaining_seconds(const UMetaAgentParticleRuntime& Runtime)
{
	UMetaAgentParticleRuntime& MutableRuntime = *const_cast<UMetaAgentParticleRuntime*>(&Runtime);
	sync_runtime_to_core(MutableRuntime);
	FCoreSchedulerState* State = get_or_create_state(MutableRuntime);
	return State ? State->Scheduler.get_active_state_time_remaining_seconds() : 0.0f;
}

FString build_pattern_status_text(const UMetaAgentParticleRuntime& Runtime)
{
	UMetaAgentParticleRuntime& MutableRuntime = *const_cast<UMetaAgentParticleRuntime*>(&Runtime);
	sync_runtime_to_core(MutableRuntime);
	FCoreSchedulerState* State = get_or_create_state(MutableRuntime);
	if (!State)
	{
		return TEXT("Pattern State: unavailable");
	}

	const metaagent::core::String Status = State->Scheduler.build_pattern_status_text(
		Runtime.GetLatestSnapshot().ExportedParticleCount,
		Runtime.GetPatternQueueDepth());
	return FromCoreString(Status);
}

FString build_pattern_timings_text(const UMetaAgentParticleRuntime& Runtime)
{
	UMetaAgentParticleRuntime& MutableRuntime = *const_cast<UMetaAgentParticleRuntime*>(&Runtime);
	sync_runtime_to_core(MutableRuntime);
	FCoreSchedulerState* State = get_or_create_state(MutableRuntime);
	if (!State)
	{
		return TEXT("Pattern Timings: unavailable");
	}

	return FromCoreString(State->Scheduler.build_pattern_timings_text());
}
} // namespace MetaAgentParticleCoreBridge
