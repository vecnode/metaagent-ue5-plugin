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
	active_session.features.networking = true;

	assert(validate_command(CommandId::CycleCinematicStyle, active_session).success);
	assert(validate_command(CommandId::StartPlatformAudio, active_session).success);
	assert(validate_command(CommandId::StartPlatformImage, active_session).success);
	assert(parse_command_name("start audio") == CommandId::StartPlatformAudio);
	assert(command_for_gui_action("StartAudio") == CommandId::StartPlatformAudio);
	assert(command_for_gui_action("CycleCinematicStyle") == CommandId::CycleCinematicStyle);

	RuntimeSession networking_off = active_session;
	networking_off.features.networking = false;
	assert(!validate_command(CommandId::StartPlatformAudio, networking_off).success);

	return 0;
}
