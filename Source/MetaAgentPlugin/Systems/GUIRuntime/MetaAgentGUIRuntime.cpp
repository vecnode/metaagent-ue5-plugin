// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/GUIRuntime/MetaAgentGUIRuntime.h"

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/GUIRuntime/MetaAgentRuntimePanelTypes.h"
#include "Systems/NetworkingRuntime/MetaAgentGameInstance.h"
#include "Systems/ParticleRuntime/MetaAgentParticleInputRouter.h"

namespace
{
	FMetaAgentGUIActionRow MakeActionRow(const FString& KeyLabel, const FString& Description, const FName ActionId)
	{
		FMetaAgentGUIActionRow Row;
		Row.KeyLabel = KeyLabel;
		Row.Description = Description;
		Row.ActionId = ActionId;
		return Row;
	}

	FMetaAgentGUIRuntimeSection MakeSection(
		const FName RuntimeId,
		const FString& Title,
		const bool bRuntimeAlwaysOn,
		const bool bRuntimeEnabled,
		const TArray<FMetaAgentGUIActionRow>& ActionRows,
		const TArray<FString>& StatusLines = TArray<FString>())
	{
		FMetaAgentGUIRuntimeSection Section;
		Section.RuntimeId = RuntimeId;
		Section.Title = Title;
		Section.bRuntimeAlwaysOn = bRuntimeAlwaysOn;
		Section.bRuntimeEnabled = bRuntimeEnabled;
		Section.ActionRows = ActionRows;
		Section.StatusLines = StatusLines;
		return Section;
	}
}

void FMetaAgentGUIRuntime::BuildRuntimeSections(
	AMetaAgentPlayerController& Controller,
	FMetaAgentGUIState& GUI)
{
	GUI.RuntimeSections.Reset();

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("Q"), TEXT("Toggle controls panel"), MetaAgentRuntimeIds::ToggleHelpPanel));
		Rows.Add(MakeActionRow(TEXT("Esc"), TEXT("Quit application"), MetaAgentRuntimeIds::QuitApplication));
		GUI.RuntimeSections.Add(MakeSection(
			MetaAgentRuntimeIds::GUI,
			TEXT("GUI Runtime"),
			true,
			true,
			Rows));
	}

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("O"), TEXT("Toggle cinematic camera"), MetaAgentRuntimeIds::ToggleCinematicCamera));
		Rows.Add(MakeActionRow(TEXT("Wheel"), TEXT("Zoom camera distance"), NAME_None));
		GUI.RuntimeSections.Add(MakeSection(
			MetaAgentRuntimeIds::Camera,
			TEXT("Camera Runtime"),
			false,
			GUI.bCameraRuntimeEnabled,
			Rows));
	}

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("I"), TEXT("Toggle AI autopilot"), MetaAgentRuntimeIds::ToggleAutopilot));
		GUI.RuntimeSections.Add(MakeSection(
			MetaAgentRuntimeIds::AI,
			TEXT("AI Runtime"),
			false,
			GUI.bAIRuntimeEnabled,
			Rows));
	}

	{
		TArray<FString> StatusLines = Controller.BuildRecordingRuntimePanelLines();
		if (StatusLines.Num() > 0 && StatusLines[0] == TEXT("Recording Runtime"))
		{
			StatusLines.RemoveAt(0);
		}

		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("J"), TEXT("Toggle viewport capture"), MetaAgentRuntimeIds::ToggleRecording));
		Rows.Add(MakeActionRow(TEXT("U"), TEXT("Finalize / show capture output"), MetaAgentRuntimeIds::ReportRecording));
		GUI.RuntimeSections.Add(MakeSection(
			MetaAgentRuntimeIds::Recording,
			TEXT("Recording Runtime"),
			false,
			GUI.bRecordingRuntimeEnabled,
			Rows,
			StatusLines));
	}

	{
		TArray<FString> StatusLines;
		if (const UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(&Controller))
		{
			StatusLines = GI->GetNetworkingRuntimePanelLines();
			if (StatusLines.Num() > 0 && StatusLines[0] == TEXT("Networking Runtime"))
			{
				StatusLines.RemoveAt(0);
			}
			if (StatusLines.Num() > 0 && StatusLines[0] == TEXT("--------------------------------"))
			{
				StatusLines.RemoveAt(0);
			}
			if (StatusLines.Num() > 0 && StatusLines[0].StartsWith(TEXT("COMMS (H/G)")))
			{
				StatusLines.RemoveAt(0);
			}
		}
		else
		{
			StatusLines.Add(TEXT("GameInstance : MetaAgentGameInstance NOT active"));
		}

		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("H"), TEXT("Send HTTP 'start audio'"), MetaAgentRuntimeIds::StartAudio));
		Rows.Add(MakeActionRow(TEXT("G"), TEXT("Send HTTP 'start image'"), MetaAgentRuntimeIds::StartImage));
		GUI.RuntimeSections.Add(MakeSection(
			MetaAgentRuntimeIds::Networking,
			TEXT("Networking Runtime"),
			false,
			GUI.bNetworkingRuntimeEnabled,
			Rows,
			StatusLines));
	}

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		for (const FMetaAgentGUIActionRow& ParticleRow : FMetaAgentParticleInputRouter::GetParticleGUIActionRows())
		{
			Rows.Add(ParticleRow);
		}

		TArray<FString> StatusLines;
		StatusLines.Add(FString::Printf(
			TEXT("Particle Callback Seen: %s"),
			Controller.HasReceivedParticleCallback() ? TEXT("TRUE") : TEXT("FALSE")));
		StatusLines.Add(FString::Printf(
			TEXT("Particle Capture: %s (Count=%d)"),
			Controller.IsParticleCaptureActive() ? TEXT("TRUE") : TEXT("FALSE"),
			Controller.GetCapturedParticleCount()));
		StatusLines.Add(Controller.GetParticlePatternTimingsText());
		StatusLines.Add(Controller.GetParticlePatternShapeText());
		StatusLines.Add(Controller.GetParticlePatternStatusText());
		StatusLines.Add(FString::Printf(TEXT("Pattern Queue Depth: %d"), Controller.GetParticlePatternQueueDepth()));

		GUI.RuntimeSections.Add(MakeSection(
			MetaAgentRuntimeIds::Particle,
			TEXT("Particle Runtime"),
			false,
			GUI.bParticleRuntimeEnabled,
			Rows,
			StatusLines));
	}

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("W/A/S/D"), TEXT("Move (input fallback)"), NAME_None));
		Rows.Add(MakeActionRow(TEXT("Shift"), TEXT("Sprint modifier (input fallback)"), NAME_None));
		Rows.Add(MakeActionRow(TEXT("Mouse"), TEXT("Look input (input fallback)"), NAME_None));
		GUI.RuntimeSections.Add(MakeSection(
			MetaAgentRuntimeIds::CharacterInput,
			TEXT("Character Input Runtime"),
			false,
			GUI.bCharacterInputRuntimeEnabled,
			Rows));
	}
}

void FMetaAgentGUIRuntime::RunApplyHelpPanelSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentGUIState& GUI)
{
	BuildRuntimeSections(Controller, GUI);

	if (AMetaAgentHUD* MetaAgentHUD = Controller.GetHUD<AMetaAgentHUD>())
	{
		MetaAgentHUD->SetRuntimePanelVisible(GUI.bHelpPanelVisible);
		MetaAgentHUD->SetRuntimePanelSections(GUI.RuntimeSections);
	}
}

void FMetaAgentGUIRuntime::RunToggleHelpPanelSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentGUIState& GUI)
{
	if (!Controller.IsLocalPlayerController())
	{
		return;
	}

	GUI.bHelpPanelVisible = !GUI.bHelpPanelVisible;
	Controller.ApplyGUIInteractionInputModeFromPanelState();
	RunApplyHelpPanelSequence(Controller, GUI);

	UE_LOG(LogMetaAgent, Log, TEXT("GUIRuntime: Help panel %s."), GUI.bHelpPanelVisible ? TEXT("ENABLED") : TEXT("DISABLED"));
}
