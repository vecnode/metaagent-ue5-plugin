#pragma once

#include "metaagent/export.hpp"
#include "metaagent/particle/pattern_types.hpp"

namespace metaagent::particle {

class ActuationMath {
public:
	METAAGENT_API static core::Vec3 compute_anticipation_world_position(
		const core::Vec3& idle_baseline,
		int32_t global_index,
		const core::Vec3& pattern_center,
		float anticipation_elapsed_seconds,
		float anticipation_amplitude_cm,
		float anticipation_frequency_hz,
		float anticipation_idle_blend_duration_seconds = 0.35f);

	METAAGENT_API static void build_anticipation_world_positions(
		const core::Array<core::Vec3>& idle_baseline_world_positions,
		const core::Vec3& pattern_center,
		float anticipation_elapsed_seconds,
		float anticipation_amplitude_cm,
		float anticipation_frequency_hz,
		core::Array<core::Vec3>& out_world_positions,
		float anticipation_idle_blend_duration_seconds = 0.35f);

	METAAGENT_API static float compute_actuation_blend_alpha(PatternState state, float phase);
};

} // namespace metaagent::particle
