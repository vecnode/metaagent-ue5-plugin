#include "metaagent/app/gui_actions.hpp"

namespace metaagent::app {
namespace {

CommandId map_particle_gui_action(const core::String& action_id)
{
	if (action_id == "ParticleLoadPreview")
	{
		return CommandId::LoadPreviewImage;
	}
	if (action_id == "ParticleStepBackward")
	{
		return CommandId::PatternStepBackward;
	}
	if (action_id == "ParticleStepForward")
	{
		return CommandId::PatternStepForward;
	}
	return CommandId::Unknown;
}

CommandId map_camera_gui_action(const core::String& action_id)
{
	if (action_id == "ToggleCinematicCamera")
	{
		return CommandId::ToggleCinematicCamera;
	}
	if (action_id == "FocusParticleCamera")
	{
		return CommandId::ToggleFocusParticles;
	}
	return CommandId::Unknown;
}

} // namespace

CommandId command_for_gui_action(const core::String& action_id)
{
	if (action_id == "ToggleHelpPanel")
	{
		return CommandId::ToggleGuiHelp;
	}

	const CommandId particle_command = map_particle_gui_action(action_id);
	if (particle_command != CommandId::Unknown)
	{
		return particle_command;
	}

	return map_camera_gui_action(action_id);
}

CommandResult validate_gui_action(const core::String& action_id, const session::RuntimeSession& session)
{
	const CommandId command = command_for_gui_action(action_id);
	if (command != CommandId::Unknown)
	{
		return validate_command(command, session);
	}

	CommandResult result;
	result.handled = true;
	result.success = session.active;
	if (!result.success)
	{
		result.user_message = "MetaAgent runtime is inactive.";
	}
	return result;
}

} // namespace metaagent::app
