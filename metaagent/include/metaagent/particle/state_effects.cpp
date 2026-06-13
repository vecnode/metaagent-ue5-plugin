#include "metaagent/particle/state_effects.hpp"

#include "metaagent/core/math.hpp"

#include <cmath>

namespace metaagent::particle {
namespace {

constexpr uint32_t k_all_pattern_states_mask = (1u << 9) - 1u;

// Slow dramatic ambient breathing — always on, including Idle.
constexpr float k_ambient_breath_frequency_hz = 0.45f;
constexpr float k_ambient_breath_amplitude_cm = 12.0f;

StateEffectDefinition make_cohesion_definition()
{
	StateEffectDefinition definition;
	definition.effect_id = state_effect_ids::Cohesion;
	definition.kind = StateEffectKind::RadialCohesion;
	definition.allowed_states_mask = k_all_pattern_states_mask;
	definition.strength = 1.0f;
	definition.frequency_hz = 1.25f;
	definition.damping = 0.86f;
	definition.max_offset_cm = 7.0f;
	definition.attack_seconds = 0.35f;
	return definition;
}

StateEffectDefinition make_turbulence_definition()
{
	StateEffectDefinition definition;
	definition.effect_id = state_effect_ids::Turbulence;
	definition.kind = StateEffectKind::TurbulentWake;
	definition.allowed_states_mask = k_all_pattern_states_mask;
	definition.strength = 1.0f;
	definition.frequency_hz = 1.25f;
	definition.damping = 0.82f;
	definition.max_offset_cm = 8.0f;
	definition.attack_seconds = 0.15f;
	return definition;
}

const core::Array<StateEffectDefinition>& built_in_definitions()
{
	static const core::Array<StateEffectDefinition> Definitions = {
		make_cohesion_definition(),
		make_turbulence_definition(),
	};
	return Definitions;
}

void clamp_offset(core::Vec3& offset, const float max_length)
{
	const float length_sq = offset.length_squared();
	const float max_sq = max_length * max_length;
	if (length_sq > max_sq && length_sq > core::math::k_epsilon)
	{
		offset = offset * (max_length / std::sqrt(length_sq));
	}
}

} // namespace

uint32_t state_effect_mask_for(const PatternState state)
{
	return 1u << static_cast<uint32_t>(state);
}

bool state_effect_allows_state(const uint32_t allowed_mask, const PatternState state)
{
	return (allowed_mask & state_effect_mask_for(state)) != 0;
}

const StateEffectDefinition* find_state_effect_definition(const core::String& effect_id)
{
	for (const StateEffectDefinition& definition : built_in_definitions())
	{
		if (definition.effect_id == effect_id)
		{
			return &definition;
		}
	}
	return nullptr;
}

void StateEffectStack::reset()
{
	active_effects_.clear();
	offsets_.clear();
	ambient_breathing_elapsed_seconds_ = 0.0f;
}

StateEffectTriggerResult StateEffectStack::toggle(
	const core::String& effect_id,
	const PatternState /*current_state*/)
{
	StateEffectTriggerResult result;

	const StateEffectDefinition* definition = find_state_effect_definition(effect_id);
	if (!definition)
	{
		result.user_message = "Unknown state effect.";
		return result;
	}

	for (size_t index = 0; index < active_effects_.size(); ++index)
	{
		if (active_effects_[index].definition.effect_id == effect_id)
		{
			active_effects_.erase(active_effects_.begin() + static_cast<std::ptrdiff_t>(index));
			result.success = true;
			result.now_active = false;
			result.user_message = definition->effect_id + " disabled.";
			return result;
		}
	}

	ActiveEffect active;
	active.definition = *definition;
	active.elapsed_seconds = 0.0f;
	active_effects_.push_back(active);

	result.success = true;
	result.now_active = true;
	result.user_message = definition->effect_id + " enabled.";
	return result;
}

bool StateEffectStack::is_active(const core::String& effect_id) const
{
	for (const ActiveEffect& effect : active_effects_)
	{
		if (effect.definition.effect_id == effect_id)
		{
			return true;
		}
	}
	return false;
}

void StateEffectStack::ensure_buffers(const int32_t particle_count)
{
	if (particle_count <= 0)
	{
		offsets_.clear();
		return;
	}

	const size_t count = static_cast<size_t>(particle_count);
	if (offsets_.size() != count)
	{
		offsets_.assign(count, core::Vec3 {});
	}
}

float StateEffectStack::envelope_for(const ActiveEffect& effect) const
{
	if (effect.definition.attack_seconds <= core::math::k_epsilon)
	{
		return 1.0f;
	}

	return core::math::smooth_step01(
		core::math::clamp(effect.elapsed_seconds / effect.definition.attack_seconds, 0.0f, 1.0f));
}

void StateEffectStack::integrate_ambient_breathing(
	const StateEffectTickContext& context,
	const float /*delta_time*/)
{
	if (!context.composed_positions || context.composed_positions->empty())
	{
		return;
	}

	const float breath = std::sin(
		ambient_breathing_elapsed_seconds_ * core::math::k_two_pi * k_ambient_breath_frequency_hz);
	const float amount = k_ambient_breath_amplitude_cm * breath;

	for (size_t index = 0; index < context.composed_positions->size(); ++index)
	{
		const core::Vec3 anchor = (*context.composed_positions)[index];
		core::Vec3 from_center = anchor - context.pattern_center;
		const float radius = from_center.length();
		if (radius <= core::math::k_epsilon)
		{
			continue;
		}

		const core::Vec3 radial = from_center * (1.0f / radius);
		offsets_[index] = offsets_[index] + radial * (-amount);
	}
}

void StateEffectStack::integrate_cohesion(
	const ActiveEffect& effect,
	const StateEffectTickContext& context)
{
	if (!context.composed_positions || context.composed_positions->empty())
	{
		return;
	}

	const float envelope = envelope_for(effect);
	const float ripple = 0.5f + 0.5f * std::sin(
		effect.elapsed_seconds * core::math::k_two_pi * effect.definition.frequency_hz);
	const float amount = effect.definition.max_offset_cm * envelope * effect.definition.strength * ripple;

	for (size_t index = 0; index < context.composed_positions->size(); ++index)
	{
		const core::Vec3 anchor = (*context.composed_positions)[index];
		core::Vec3 from_center = anchor - context.pattern_center;
		const float radius = from_center.length();
		if (radius <= core::math::k_epsilon)
		{
			continue;
		}

		const core::Vec3 radial = from_center * (1.0f / radius);
		offsets_[index] = offsets_[index] + radial * (-amount);
	}
}

void StateEffectStack::integrate_turbulence(
	const ActiveEffect& effect,
	const StateEffectTickContext& context)
{
	if (!context.composed_positions || context.composed_positions->empty())
	{
		return;
	}

	const float envelope = envelope_for(effect);
	const float amplitude = effect.definition.max_offset_cm * envelope * effect.definition.strength;
	const float time = effect.elapsed_seconds;

	for (size_t index = 0; index < context.composed_positions->size(); ++index)
	{
		const float phase = static_cast<float>(index) * 1.73f
			+ time * core::math::k_two_pi * effect.definition.frequency_hz;
		const core::Vec3 turbulence(
			std::sin(phase) * amplitude,
			std::cos(phase * 1.31f) * amplitude,
			std::sin(phase * 0.71f) * amplitude * 0.65f);
		offsets_[index] = offsets_[index] + turbulence;
	}
}

void StateEffectStack::tick(const StateEffectTickContext& context)
{
	if (!context.composed_positions || context.composed_positions->empty())
	{
		return;
	}

	ensure_buffers(static_cast<int32_t>(context.composed_positions->size()));
	if (offsets_.empty())
	{
		return;
	}

	for (core::Vec3& offset : offsets_)
	{
		offset = core::Vec3 {};
	}

	const float delta_time = std::max(0.0f, context.delta_time_seconds);
	ambient_breathing_elapsed_seconds_ += delta_time;
	integrate_ambient_breathing(context, delta_time);

	for (ActiveEffect& effect : active_effects_)
	{
		if (!state_effect_allows_state(effect.definition.allowed_states_mask, context.pattern_state))
		{
			continue;
		}

		effect.elapsed_seconds += delta_time;

		switch (effect.definition.kind)
		{
		case StateEffectKind::RadialCohesion:
			integrate_cohesion(effect, context);
			break;
		case StateEffectKind::TurbulentWake:
			integrate_turbulence(effect, context);
			break;
		default:
			break;
		}
	}

	const float max_combined_offset = 14.0f;
	for (core::Vec3& offset : offsets_)
	{
		clamp_offset(offset, max_combined_offset);
	}
}

void StateEffectStack::apply_to_positions(
	core::Array<core::Vec3>& positions,
	const core::Array<core::Vec3>& composed_positions) const
{
	if (offsets_.empty() || composed_positions.empty())
	{
		positions = composed_positions;
		return;
	}

	const size_t count = composed_positions.size();
	positions.resize(count);
	for (size_t index = 0; index < count; ++index)
	{
		const core::Vec3 offset = index < offsets_.size() ? offsets_[index] : core::Vec3 {};
		positions[index] = composed_positions[index] + offset;
	}
}

float StateEffectStack::emphasis_multiplier() const
{
	const float breath = std::sin(
		ambient_breathing_elapsed_seconds_ * core::math::k_two_pi * k_ambient_breath_frequency_hz);
	return 1.0f + 0.06f * breath;
}

} // namespace metaagent::particle
