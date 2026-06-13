#pragma once

#include "Systems/ParticleRuntime/MetaAgentParticleRepresentationTypes.h"

class UMetaAgentParticleRuntime;

namespace MetaAgentParticleCoreBridge {

struct FCoreSchedulerState;

FCoreSchedulerState* get_or_create_state(UMetaAgentParticleRuntime& runtime);
void sync_runtime_to_core(UMetaAgentParticleRuntime& runtime);
void sync_core_to_runtime(UMetaAgentParticleRuntime& runtime);

void tick_pattern_runtime(UMetaAgentParticleRuntime& runtime, float delta_time_seconds);
void build_representation_frame(const UMetaAgentParticleRuntime& runtime, FMetaAgentParticleRepresentationFrame& out_frame);
bool dispatch_pattern_transition(UMetaAgentParticleRuntime& runtime, EMetaAgentPatternTransitionTrigger trigger, bool b_skip_return = false);

float get_active_state_duration_seconds(const UMetaAgentParticleRuntime& runtime);
float get_active_state_time_remaining_seconds(const UMetaAgentParticleRuntime& runtime);
FString build_pattern_status_text(const UMetaAgentParticleRuntime& runtime);
FString build_pattern_timings_text(const UMetaAgentParticleRuntime& runtime);

} // namespace MetaAgentParticleCoreBridge
