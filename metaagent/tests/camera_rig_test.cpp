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

} // namespace

int main()
{
	using namespace metaagent::camera;

	metaagent::core::Array<metaagent::core::Vec3> points;
	points.push_back({0.0f, 0.0f, 0.0f});
	points.push_back({100.0f, 0.0f, 50.0f});
	points.push_back({0.0f, 100.0f, 25.0f});

	const Bounds3 bounds = compute_bounds(points);
	const FocusTarget focus = make_focus_from_bounds(bounds);

	if (!expect_near(focus.focus_point.x, 50.0f, 0.01f, "focus.x"))
	{
		return 1;
	}

	if (!expect_near(focus.focus_point.y, 50.0f, 0.01f, "focus.y"))
	{
		return 1;
	}

	CinematicSettings settings;
	CinematicRuntimeState state;
	const metaagent::core::Vec3 camera_start = {500.0f, 0.0f, 120.0f};
	initialize_cinematic_state_from_pose(state, settings, camera_start, focus);

	if (!expect_true(state.mode_enabled, "mode enabled after init"))
	{
		return 1;
	}

	const CameraPose pose = compute_cinematic_pose(settings, state, focus, 0.5f);
	if (!expect_true(!pose.location.nearly_zero(), "pose location"))
	{
		return 1;
	}

	ZoomSettings zoom;
	zoom.min_distance = 10.0f;
	zoom.max_distance = 100.0f;
	zoom.desired_distance = 50.0f;
	ZoomInput input;
	input.discrete_wheel_delta = 1.0f;
	apply_zoom_input(zoom, input);

	if (!expect_near(zoom.desired_distance, 40.0f, 0.01f, "zoom after wheel up"))
	{
		return 1;
	}

	settings.active_style = CinematicStyle::SlowOrbit;
	state.pan_elapsed_seconds = 0.0f;
	const CameraPose slow_pose_a = compute_cinematic_pose(settings, state, focus, 1.0f);
	const CameraPose slow_pose_b = compute_cinematic_pose(settings, state, focus, 2.0f);
	if (!expect_true(!slow_pose_a.location.nearly_zero(), "slow orbit pose a"))
	{
		return 1;
	}
	if (!expect_true(!slow_pose_b.location.nearly_zero(), "slow orbit pose b"))
	{
		return 1;
	}

	if (!expect_true(cycle_cinematic_style(CinematicStyle::OscillatingHold) == CinematicStyle::SlowOrbit, "cycle style"))
	{
		return 1;
	}

	return 0;
}
