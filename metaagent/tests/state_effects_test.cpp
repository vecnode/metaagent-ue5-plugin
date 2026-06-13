#include "metaagent.h"

#include <cassert>
#include <cmath>

int main()
{
	using namespace metaagent::particle;
	using namespace metaagent::core;

	StateEffectStack stack;

	Array<Vec3> composed;
	composed.push_back(Vec3(100.0f, 0.0f, 0.0f));
	composed.push_back(Vec3(0.0f, 100.0f, 0.0f));
	composed.push_back(Vec3(-100.0f, 0.0f, 0.0f));

	StateEffectTickContext context;
	context.pattern_state = PatternState::Idle;
	context.pattern_center = Vec3 {};
	context.state_elapsed_seconds = 0.0f;
	context.delta_time_seconds = 1.0f / 30.0f;
	context.composed_positions = &composed;

	float min_radius = composed[0].length();
	float max_radius = composed[0].length();
	for (int step = 0; step < 240; ++step)
	{
		stack.tick(context);
		context.state_elapsed_seconds += context.delta_time_seconds;

		Array<Vec3> with_overlay;
		stack.apply_to_positions(with_overlay, composed);
		const float radius = with_overlay[0].length();
		min_radius = std::min(min_radius, radius);
		max_radius = std::max(max_radius, radius);
	}

	assert(max_radius - min_radius > 1.0f);

	context.pattern_state = PatternState::Returning;
	StateEffectTriggerResult cohesion = stack.toggle(state_effect_ids::Cohesion, PatternState::Returning);
	assert(cohesion.success && cohesion.now_active);

	float cohesion_energy = 0.0f;
	for (int step = 0; step < 30; ++step)
	{
		stack.tick(context);
		context.state_elapsed_seconds += context.delta_time_seconds;
	}

	Array<Vec3> with_cohesion;
	stack.apply_to_positions(with_cohesion, composed);
	for (size_t index = 0; index < with_cohesion.size(); ++index)
	{
		const Vec3 delta = with_cohesion[index] - composed[index];
		cohesion_energy += delta.length_squared();
	}
	assert(cohesion_energy > 1.0f);

	stack.toggle(state_effect_ids::Cohesion, PatternState::Returning);
	StateEffectTriggerResult turbulence = stack.toggle(state_effect_ids::Turbulence, PatternState::Holding);
	assert(turbulence.success && turbulence.now_active);

	float turbulence_energy = 0.0f;
	for (int step = 0; step < 30; ++step)
	{
		stack.tick(context);
		context.state_elapsed_seconds += context.delta_time_seconds;
	}

	Array<Vec3> with_turbulence;
	stack.apply_to_positions(with_turbulence, composed);
	for (size_t index = 0; index < with_turbulence.size(); ++index)
	{
		const Vec3 delta = with_turbulence[index] - composed[index];
		turbulence_energy += delta.length_squared();
	}
	assert(turbulence_energy > 1.0f);

	assert(find_state_effect_definition(state_effect_ids::Cohesion));
	assert(state_effect_allows_state(
		find_state_effect_definition(state_effect_ids::Cohesion)->allowed_states_mask,
		PatternState::Idle));
	assert(state_effect_allows_state(
		find_state_effect_definition(state_effect_ids::Turbulence)->allowed_states_mask,
		PatternState::Returning));

	return 0;
}
