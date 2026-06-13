#include "metaagent.h"

#include <cassert>

int main()
{
	using namespace metaagent::app;
	using namespace metaagent::session;

	RuntimeSession active_session;
	active_session.active = true;
	active_session.features.camera = true;
	active_session.features.particle = true;
	active_session.features.ui = true;

	const CommandResult cinematic = validate_command(CommandId::ToggleCinematicCamera, active_session);
	assert(cinematic.handled && cinematic.success);

	const CommandResult step = validate_command(CommandId::PatternStepForward, active_session);
	assert(step.handled && step.success);

	RuntimeSession inactive_session = active_session;
	inactive_session.active = false;
	const CommandResult inactive_step = validate_command(CommandId::PatternStepForward, inactive_session);
	assert(inactive_step.handled && !inactive_step.success);

	RuntimeSession camera_off = active_session;
	camera_off.features.camera = false;
	const CommandResult blocked_camera = validate_command(CommandId::ToggleFocusParticles, camera_off);
	assert(blocked_camera.handled && !blocked_camera.success);

	RuntimeSession particle_off = active_session;
	particle_off.features.particle = false;
	const CommandResult blocked_particle = validate_command(CommandId::LoadPreviewImage, particle_off);
	assert(blocked_particle.handled && !blocked_particle.success);

	assert(parse_command_name("pattern>>") == CommandId::PatternStepForward);
	assert(parse_command_name("focus_particles") == CommandId::ToggleFocusParticles);

	return 0;
}
