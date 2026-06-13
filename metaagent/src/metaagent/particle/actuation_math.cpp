#include "metaagent/particle/actuation_math.hpp"

#include "metaagent/core/math.hpp"

#include <cmath>

namespace metaagent::particle {

core::Vec3 ActuationMath::compute_anticipation_world_position(
	const core::Vec3& idle_baseline,
	const int32_t global_index,
	const core::Vec3& pattern_center,
	const float anticipation_elapsed_seconds,
	const float anticipation_amplitude_cm,
	const float anticipation_frequency_hz,
	const float anticipation_idle_blend_duration_seconds)
{
	core::Vec3 to_center = pattern_center - idle_baseline;
	if (to_center.nearly_zero())
	{
		to_center = {0.0f, 0.0f, 1.0f};
	}
	const core::Vec3 radial_dir = to_center.normalized();
	core::Vec3 tangent_dir = {
		radial_dir.y * 0.0f - radial_dir.z * 1.0f,
		radial_dir.z * 0.0f - radial_dir.x * 0.0f,
		radial_dir.x * 1.0f - radial_dir.y * 0.0f};
	if (tangent_dir.nearly_zero())
	{
		tangent_dir = {1.0f, 0.0f, 0.0f};
	}
	else
	{
		tangent_dir = tangent_dir.normalized();
	}

	float idle_blend_weight = 1.0f;
	if (anticipation_idle_blend_duration_seconds > 1e-4f)
	{
		const float idle_blend_normalized = core::math::clamp(
			anticipation_elapsed_seconds / anticipation_idle_blend_duration_seconds,
			0.0f,
			1.0f);
		idle_blend_weight = core::math::smooth_step01(idle_blend_normalized);
	}

	const float anticipation_radians = anticipation_elapsed_seconds
		* core::math::k_two_pi * std::max(0.1f, anticipation_frequency_hz);
	const float index_phase = static_cast<float>(global_index) * 0.37f;
	const float amplitude = std::max(0.0f, anticipation_amplitude_cm) * idle_blend_weight;
	const float radial_pulse = std::sin(anticipation_radians + index_phase) * amplitude * 0.55f;
	const float orbit_pulse = std::cos(anticipation_radians * 0.85f + index_phase * 1.7f) * amplitude * 0.45f;
	const float vertical_pulse = std::sin(anticipation_radians * 1.35f + index_phase * 0.61f) * amplitude * 0.18f;

	return idle_baseline
		+ radial_dir * radial_pulse
		+ tangent_dir * orbit_pulse
		+ core::Vec3 {0.0f, 0.0f, vertical_pulse};
}

void ActuationMath::build_anticipation_world_positions(
	const core::Array<core::Vec3>& idle_baseline_world_positions,
	const core::Vec3& pattern_center,
	const float anticipation_elapsed_seconds,
	const float anticipation_amplitude_cm,
	const float anticipation_frequency_hz,
	core::Array<core::Vec3>& out_world_positions,
	const float anticipation_idle_blend_duration_seconds)
{
	const int32_t particle_count = static_cast<int32_t>(idle_baseline_world_positions.size());
	out_world_positions.resize(particle_count);
	for (int32_t particle_index = 0; particle_index < particle_count; ++particle_index)
	{
		out_world_positions[static_cast<size_t>(particle_index)] = compute_anticipation_world_position(
			idle_baseline_world_positions[static_cast<size_t>(particle_index)],
			particle_index,
			pattern_center,
			anticipation_elapsed_seconds,
			anticipation_amplitude_cm,
			anticipation_frequency_hz,
			anticipation_idle_blend_duration_seconds);
	}
}

float ActuationMath::compute_actuation_blend_alpha(const PatternState state, const float phase)
{
	if (state == PatternState::Holding)
	{
		return 1.0f;
	}
	return core::math::clamp(phase, 0.0f, 1.0f);
}

} // namespace metaagent::particle
