// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"

#include "Systems/GUIRuntime/MetaAgentGUIRuntime.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/GUIRuntime/MetaAgentRuntimePanelTypes.h"

void AMetaAgentPlayerController::HandleToggleHelpPanelPressed()
{
	FMetaAgentGUIRuntime::RunToggleHelpPanelSequence(*this, GUI);
}

void AMetaAgentPlayerController::ApplyGUIHelpPanelState()
{
	FMetaAgentGUIRuntime::RunApplyHelpPanelSequence(*this, GUI);
}

void AMetaAgentPlayerController::ApplyGUIInteractionInputModeFromPanelState()
{
	if (!IsLocalPlayerController() || ShouldUseTouchControls())
	{
		return;
	}

	if (GUI.bHelpPanelVisible)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
		bShowMouseCursor = true;
		bEnableClickEvents = true;
		bEnableMouseOverEvents = true;
		SetIgnoreLookInput(true);
		SetIgnoreMoveInput(true);
	}
	else
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
		bShowMouseCursor = false;
		bEnableClickEvents = false;
		bEnableMouseOverEvents = false;
		ApplyCharacterInputRuntimeState();
	}
}

void AMetaAgentPlayerController::HandleGUIPanelMousePressed()
{
	if (!GUI.bHelpPanelVisible)
	{
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>();
	if (!MetaAgentHUD)
	{
		return;
	}

	FName ActionId = NAME_None;
	if (MetaAgentHUD->HitTestRuntimePanelAction(MouseX, MouseY, ActionId))
	{
		DispatchGUIAction(ActionId);
	}
}

void AMetaAgentPlayerController::DispatchGUIAction(const FName ActionId)
{
	if (ActionId.IsNone())
	{
		return;
	}

	if (ActionId == MetaAgentRuntimeIds::ToggleHelpPanel)
	{
		FMetaAgentGUIRuntime::RunToggleHelpPanelSequence(*this, GUI);
		return;
	}

	if (ActionId == MetaAgentRuntimeIds::QuitApplication)
	{
		HandleEscapePressed();
		return;
	}

	FName SectionExpandId = NAME_None;
	if (ParseSectionExpandAction(ActionId, SectionExpandId))
	{
		ToggleRuntimeSectionExpanded(SectionExpandId);
		return;
	}

	FName RuntimeToggleId = NAME_None;
	if (ParseRuntimeToggleAction(ActionId, RuntimeToggleId))
	{
		EMetaAgentModularRuntime Runtime = EMetaAgentModularRuntime::GUI;
		if (TryMapRuntimeIdToModularRuntime(RuntimeToggleId, Runtime))
		{
			ToggleModularRuntime(Runtime);
		}
		return;
	}

	if (ActionId == MetaAgentRuntimeIds::ToggleCinematicCamera)
	{
		if (IsModularRuntimeEnabled(EMetaAgentModularRuntime::Camera))
		{
			ToggleCinematicCameraMode();
			ApplyGUIHelpPanelState();
		}
		return;
	}

	if (ActionId == MetaAgentRuntimeIds::ToggleAutopilot)
	{
		if (IsModularRuntimeEnabled(EMetaAgentModularRuntime::AI))
		{
			ToggleAutopilotFromGUI();
		}
		return;
	}

	if (ActionId == MetaAgentRuntimeIds::ToggleRecording)
	{
		if (IsModularRuntimeEnabled(EMetaAgentModularRuntime::Recording))
		{
			HandleToggleRecordingPressed();
		}
		return;
	}

	if (ActionId == MetaAgentRuntimeIds::ReportRecording)
	{
		if (IsModularRuntimeEnabled(EMetaAgentModularRuntime::Recording))
		{
			HandleReportRecordingStatusPressed();
		}
		return;
	}

	if (ActionId == MetaAgentRuntimeIds::StartAudio)
	{
		if (IsModularRuntimeEnabled(EMetaAgentModularRuntime::Networking))
		{
			HandleStartAudioPressed();
		}
		return;
	}

	if (ActionId == MetaAgentRuntimeIds::StartImage)
	{
		if (IsModularRuntimeEnabled(EMetaAgentModularRuntime::Networking))
		{
			HandleStartImagePressed();
		}
		return;
	}

	if (IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
	{
		if (ActionId == MetaAgentRuntimeIds::ParticleLoadPreview) { HandleParticleLoadPreviewPressed(); return; }
		if (ActionId == MetaAgentRuntimeIds::ParticlePlayFullCycle) { HandleParticlePlayFullCyclePressed(); return; }
		if (ActionId == MetaAgentRuntimeIds::ParticleStepBackward) { HandleParticleStepPatternBackwardPressed(); return; }
		if (ActionId == MetaAgentRuntimeIds::ParticleStepForward) { HandleParticleStepPatternForwardPressed(); return; }
		if (ActionId == MetaAgentRuntimeIds::ParticleSlowPreset) { HandleParticleSlowPresetPressed(); return; }
		if (ActionId == MetaAgentRuntimeIds::ParticleDramaticPreset) { HandleParticleDramaticPresetPressed(); return; }
		if (ActionId == MetaAgentRuntimeIds::ParticleSnappyPreset) { HandleParticleSnappyPresetPressed(); return; }
		if (ActionId == MetaAgentRuntimeIds::ParticleDreamyPreset) { HandleParticleDreamyPresetPressed(); return; }
		if (ActionId == MetaAgentRuntimeIds::ParticleMorph) { HandleParticleMorphPressed(); return; }
		if (ActionId == MetaAgentRuntimeIds::ParticleCycleSampling) { HandleParticleCycleSamplingPressed(); return; }
		if (ActionId == MetaAgentRuntimeIds::ParticleCycleForming) { HandleParticleCycleFormingPressed(); return; }
		if (ActionId == MetaAgentRuntimeIds::ParticleCycleReturning) { HandleParticleCycleReturningPressed(); return; }
	}

	ApplyGUIHelpPanelState();
}
