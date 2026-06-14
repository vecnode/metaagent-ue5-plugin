#include "metaagent.h"

#include <cassert>

int main()
{
	using namespace metaagent::input;
	using namespace metaagent::app;
	using namespace metaagent::session;

	const InputPolicy gameplay = policy_for_runtime(false, false, true);
	assert(gameplay.block_mouse_buttons);
	assert(gameplay.allow_mouse_wheel_zoom);
	assert(!gameplay.allow_gui_clicks);

	const InputPolicy gui = policy_for_runtime(true, false, true);
	assert(gui.allow_gui_clicks);
	assert(!gui.allow_mouse_wheel_zoom);
	assert(!gui.block_mouse_buttons);

	RuntimeSession session;
	session.active = true;
	session.features.particle = true;
	session.features.camera = true;

	assert(command_for_gui_action("ParticleStepForward") == CommandId::PatternStepForward);
	assert(command_for_gui_action("ParticleCyclePreset") == CommandId::ParticleGuiEffect);
	assert(command_for_gui_action("StartAudio") == CommandId::StartPlatformAudio);
	assert(command_for_gui_action("CycleCinematicStyle") == CommandId::CycleCinematicStyle);
	const CommandResult valid = validate_gui_action("ParticleStepForward", session);
	assert(valid.handled && valid.success);

	session.features.particle = false;
	const CommandResult blocked = validate_gui_action("ParticleStepForward", session);
	assert(blocked.handled && !blocked.success);

	return 0;
}
