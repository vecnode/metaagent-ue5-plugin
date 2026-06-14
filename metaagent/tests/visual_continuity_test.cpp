#include "metaagent.h"

#include <cmath>
#include <iostream>

namespace {

bool expect_near(const float actual, const float expected, const float tolerance, const char* label)
{
	if (std::fabs(actual - expected) <= tolerance)
	{
		return true;
	}

	std::cerr << label << " expected " << expected << " got " << actual << '\n';
	return false;
}

bool expect_true(const bool value, const char* label)
{
	if (value)
	{
		return true;
	}

	std::cerr << label << " failed\n";
	return false;
}

float max_position_delta(
	const metaagent::core::Array<metaagent::core::Vec3>& left,
	const metaagent::core::Array<metaagent::core::Vec3>& right)
{
	const size_t count = std::min(left.size(), right.size());
	float max_delta = 0.0f;
	for (size_t index = 0; index < count; ++index)
	{
		const float dx = left[index].x - right[index].x;
		const float dy = left[index].y - right[index].y;
		const float dz = left[index].z - right[index].z;
		max_delta = std::max(max_delta, std::sqrt(dx * dx + dy * dy + dz * dz));
	}
	return max_delta;
}

metaagent::particle::DisplayedPose make_displayed_pose(
	const std::initializer_list<metaagent::core::Vec3>& positions)
{
	metaagent::particle::DisplayedPose pose;
	pose.world_positions.assign(positions.begin(), positions.end());
	metaagent::particle::compute_pattern_center(pose.world_positions, pose.pattern_center);
	return pose;
}

bool expect_zero_compose_delta(
	metaagent::particle::ParticleScheduler& scheduler,
	const metaagent::particle::DisplayedPose& displayed,
	const char* label)
{
	metaagent::particle::ActuationComposeInput compose_input;
	compose_input.pattern_state = scheduler.pattern_runtime.state;
	compose_input.blend_alpha = scheduler.pattern_runtime.phase;
	compose_input.baseline_world_positions = &scheduler.pattern_runtime.baseline_world_positions;
	compose_input.pattern_world_targets = &scheduler.pattern_runtime.pattern_world_targets;

	metaagent::core::Array<metaagent::core::Vec3> composed;
	metaagent::particle::ActuationMath::compose_world_positions(compose_input, composed);

	const float delta = max_position_delta(composed, displayed.world_positions);
	return expect_near(delta, 0.0f, 1e-4f, label);
}

} // namespace

int main()
{
	using namespace metaagent::particle;

	{
		ParticleScheduler scheduler;
		const DisplayedPose displayed = make_displayed_pose({{10.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f}});
		scheduler.freeze_displayed_pose(displayed);

		if (!expect_true(
			scheduler.pattern_runtime.baseline_world_positions.size() == 2,
			"freeze baseline count"))
		{
			return 1;
		}
		if (!expect_near(scheduler.pattern_runtime.pattern_center.x, 15.0f, 1e-4f, "freeze center"))
		{
			return 1;
		}
	}

	{
		ParticleScheduler scheduler;
		scheduler.pattern_runtime.state = PatternState::Holding;
		scheduler.pattern_runtime.phase = 1.0f;
		scheduler.pattern_runtime.baseline_world_positions = {{0.0f, 0.0f, 0.0f}, {100.0f, 0.0f, 0.0f}};
		scheduler.pattern_runtime.pattern_world_targets = {{100.0f, 0.0f, 0.0f}, {200.0f, 0.0f, 0.0f}};

		const DisplayedPose displayed = make_displayed_pose({{5.0f, 1.0f, 0.0f}, {15.0f, 2.0f, 0.0f}});
		TransitionResult result;
		result.new_state = PatternState::Holding;
		result.action = TransitionAction::EnterState;
		apply_visual_continuity_for_transition(scheduler, result, displayed);

		if (!expect_zero_compose_delta(scheduler, displayed, "holding continuity delta"))
		{
			return 1;
		}
	}

	{
		ParticleScheduler scheduler;
		scheduler.pattern_runtime.state = PatternState::Preparing;
		scheduler.pattern_runtime.baseline_world_positions = {{0.0f, 0.0f, 0.0f}};
		scheduler.pattern_runtime.pattern_world_targets = {{50.0f, 0.0f, 0.0f}};

		const DisplayedPose displayed = make_displayed_pose({{7.0f, 3.0f, 1.0f}});
		TransitionResult result;
		result.new_state = PatternState::Preparing;
		result.action = TransitionAction::EnterState;
		apply_visual_continuity_for_transition(scheduler, result, displayed);

		if (!expect_zero_compose_delta(scheduler, displayed, "preparing continuity delta"))
		{
			return 1;
		}
	}

	{
		ParticleScheduler scheduler;
		scheduler.pattern_runtime.state = PatternState::Forming;
		scheduler.pattern_runtime.canonical_pattern_world_targets = {{100.0f, 0.0f, 0.0f}, {200.0f, 0.0f, 0.0f}};
		scheduler.pattern_runtime.baseline_world_positions = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
		scheduler.pattern_runtime.pattern_world_targets = scheduler.pattern_runtime.canonical_pattern_world_targets;

		const DisplayedPose displayed = make_displayed_pose({{12.0f, 4.0f, 0.0f}, {18.0f, 6.0f, 0.0f}});
		TransitionResult result;
		result.new_state = PatternState::Forming;
		result.action = TransitionAction::EnterState;
		apply_visual_continuity_for_transition(scheduler, result, displayed);

		if (!expect_near(
			scheduler.pattern_runtime.baseline_world_positions[0].x,
			12.0f,
			1e-4f,
			"forming baseline x"))
		{
			return 1;
		}
		if (!expect_true(
			scheduler.pattern_runtime.pattern_world_targets.size()
				== scheduler.pattern_runtime.canonical_pattern_world_targets.size(),
			"forming target count"))
		{
			return 1;
		}
		if (!expect_near(
			scheduler.pattern_runtime.pattern_world_targets[0].x,
			100.0f,
			1e-4f,
			"forming canonical target x"))
		{
			return 1;
		}
	}

	{
		ParticleScheduler scheduler;
		scheduler.pattern_runtime.idle_baseline_world_positions = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};

		const DisplayedPose displayed = make_displayed_pose({{3.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f}});
		TransitionResult result;
		result.new_state = PatternState::Returning;
		result.action = TransitionAction::BeginConfiguredReturn;
		apply_visual_continuity_for_transition(scheduler, result, displayed);

		const float delta = max_position_delta(
			scheduler.pattern_runtime.return_hold_positions,
			displayed.world_positions);
		if (!expect_near(delta, 0.0f, 1e-4f, "return hold snapshot delta"))
		{
			return 1;
		}
	}

	return 0;
}
