// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"

#include "Core/MetaAgent.h"
#include "Systems/GUIRuntime/MetaAgentRuntimePanelTypes.h"
#include "Systems/NetworkingRuntime/MetaAgentGameInstance.h"

bool AMetaAgentPlayerController::IsModularRuntimeEnabled(const EMetaAgentModularRuntime Runtime) const
{
	switch (Runtime)
	{
	case EMetaAgentModularRuntime::GUI:
		return true;
	case EMetaAgentModularRuntime::Camera:
		return GUI.bCameraRuntimeEnabled;
	case EMetaAgentModularRuntime::AI:
		return GUI.bAIRuntimeEnabled;
	case EMetaAgentModularRuntime::Recording:
		return GUI.bRecordingRuntimeEnabled;
	case EMetaAgentModularRuntime::Networking:
		return GUI.bNetworkingRuntimeEnabled;
	case EMetaAgentModularRuntime::Particle:
		return GUI.bParticleRuntimeEnabled;
	case EMetaAgentModularRuntime::CharacterInput:
		return GUI.bCharacterInputRuntimeEnabled;
	default:
		return true;
	}
}

bool AMetaAgentPlayerController::IsGUIInteractionModeActive() const
{
	return GUI.bHelpPanelVisible && IsLocalPlayerController() && !ShouldUseTouchControls();
}

void AMetaAgentPlayerController::SetModularRuntimeEnabled(const EMetaAgentModularRuntime Runtime, const bool bEnabled)
{
	if (Runtime == EMetaAgentModularRuntime::GUI)
	{
		return;
	}

	switch (Runtime)
	{
	case EMetaAgentModularRuntime::Camera:
		GUI.bCameraRuntimeEnabled = bEnabled;
		if (!bEnabled)
		{
			if (CinematicCamera.bModeEnabled)
			{
				DisableCinematicCameraMode();
			}
		}
		break;
	case EMetaAgentModularRuntime::AI:
		GUI.bAIRuntimeEnabled = bEnabled;
		if (!bEnabled && (Autopilot.bEnabled || Autopilot.Controller.IsValid() || !GetPawn()))
		{
			DisableAutopilotAndRepossess();
		}
		break;
	case EMetaAgentModularRuntime::Recording:
		GUI.bRecordingRuntimeEnabled = bEnabled;
		if (!bEnabled && Recording.bTakeRecordingActive)
		{
			StopAutopilotTakeRecording();
		}
		break;
	case EMetaAgentModularRuntime::Networking:
		GUI.bNetworkingRuntimeEnabled = bEnabled;
		if (UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
		{
			if (bEnabled)
			{
				GI->StartNetworkingRuntime();
			}
			else
			{
				GI->StopNetworkingRuntime();
			}
		}
		break;
	case EMetaAgentModularRuntime::Particle:
		GUI.bParticleRuntimeEnabled = bEnabled;
		break;
	case EMetaAgentModularRuntime::CharacterInput:
		GUI.bCharacterInputRuntimeEnabled = bEnabled;
		break;
	default:
		break;
	}

	ApplyGUIHelpPanelState();
}

void AMetaAgentPlayerController::ToggleModularRuntime(const EMetaAgentModularRuntime Runtime)
{
	if (Runtime == EMetaAgentModularRuntime::GUI)
	{
		return;
	}

	SetModularRuntimeEnabled(Runtime, !IsModularRuntimeEnabled(Runtime));
}
