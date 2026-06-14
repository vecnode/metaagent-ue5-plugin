#include "Host/MetaAgentHostServicesBridge.h"

#include "MetaAgentPlayerController.h"

metaagent::runtime::HostServiceCallbacks FMetaAgentHostServicesBridge::BuildFromPlayerController(
	AMetaAgentPlayerController& Controller)
{
	metaagent::runtime::HostServiceCallbacks Callbacks;

	Callbacks.toggle_recording = [&Controller]() -> bool
	{
		if (!Controller.IsModularRuntimeEnabled(EMetaAgentModularRuntime::Recording))
		{
			return false;
		}
		Controller.ToggleRecordingFromGUI();
		return true;
	};

	Callbacks.toggle_autopilot = [&Controller]() -> bool
	{
		if (!Controller.IsModularRuntimeEnabled(EMetaAgentModularRuntime::AI))
		{
			return false;
		}
		Controller.ToggleAutopilotFromGUI();
		return true;
	};

	Callbacks.query_recording = [&Controller]() -> metaagent::runtime::RecordingSnapshot
	{
		return Controller.BuildRecordingHostSnapshot();
	};

	Callbacks.query_ai = [&Controller]() -> metaagent::runtime::AiSnapshot
	{
		return Controller.BuildAiHostSnapshot();
	};

	return Callbacks;
}
