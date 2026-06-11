// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/GUIRuntime/MetaAgentGUIRuntime.h"

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/GUIRuntime/MetaAgentRuntimePanelTypes.h"
#include "Systems/NetworkingRuntime/MetaAgentGameInstance.h"
#include "Systems/ParticleRuntime/MetaAgentParticleInputRouter.h"
#include "Systems/ParticleRuntime/MetaAgentParticleOrchestrator.h"

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

	void ApplySectionExpandState(FMetaAgentGUIState& GUI, FMetaAgentGUIRuntimeSection& Section)
	{
		if (const bool* CachedExpanded = GUI.SectionExpandedStates.Find(Section.RuntimeId))
		{
			Section.bSectionExpanded = *CachedExpanded;
			return;
		}

		const bool bHasDetails = Section.StatusLines.Num() > 0 || Section.ActionRows.Num() > 0;
		Section.bSectionExpanded = Section.bRuntimeEnabled || !bHasDetails;
		GUI.SectionExpandedStates.Add(Section.RuntimeId, Section.bSectionExpanded);
	}

	void FinalizeSection(FMetaAgentGUIState& GUI, FMetaAgentGUIRuntimeSection Section)
	{
		ApplySectionExpandState(GUI, Section);
		GUI.RuntimeSections.Add(Section);
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
		FinalizeSection(GUI, MakeSection(
			MetaAgentRuntimeIds::GUI,
			TEXT("GUI Runtime"),
			true,
			true,
			Rows));
	}

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("W/A/S/D"), TEXT("Move"), NAME_None));
		Rows.Add(MakeActionRow(TEXT("Shift"), TEXT("Sprint modifier"), NAME_None));
		Rows.Add(MakeActionRow(TEXT("Mouse"), TEXT("Look"), NAME_None));
		GUI.bCharacterInputRuntimeEnabled = true;
		FinalizeSection(GUI, MakeSection(
			MetaAgentRuntimeIds::CharacterInput,
			TEXT("Character Input Runtime"),
			true,
			true,
			Rows));
	}

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("O"), TEXT("Toggle cinematic camera"), MetaAgentRuntimeIds::ToggleCinematicCamera));
		Rows.Add(MakeActionRow(TEXT("Wheel"), TEXT("Zoom camera distance"), NAME_None));
		FinalizeSection(GUI, MakeSection(
			MetaAgentRuntimeIds::Camera,
			TEXT("Camera Runtime"),
			false,
			GUI.bCameraRuntimeEnabled,
			Rows));
	}

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("I"), TEXT("Toggle AI autopilot"), MetaAgentRuntimeIds::ToggleAutopilot));
		FinalizeSection(GUI, MakeSection(
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
		FinalizeSection(GUI, MakeSection(
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
		FinalizeSection(GUI, MakeSection(
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

		TArray<FString> StatusLines = Controller.BuildParticleRuntimePanelStatusLines();

		FMetaAgentGUIRuntimeSection ParticleSection = MakeSection(
			MetaAgentRuntimeIds::Particle,
			TEXT("Particle Runtime"),
			false,
			GUI.bParticleRuntimeEnabled,
			Rows,
			StatusLines);
		if (const UMetaAgentParticleOrchestrator* Orchestrator = Controller.GetParticleOrchestrator())
		{
			ParticleSection.PreviewThumbnails = Orchestrator->GetPanelPreviewThumbnails();
		}
		FinalizeSection(GUI, ParticleSection);
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
