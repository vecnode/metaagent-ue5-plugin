// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/GUIRuntime/MetaAgentGUIRuntime.h"

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/NetworkingRuntime/MetaAgentGameInstance.h"

namespace
{
	void InitializeDefaultHelpPanelLines(FMetaAgentGUIState& GUI)
	{
		if (GUI.bHelpPanelInitialized && GUI.BaseHelpPanelLines.Num() > 0)
		{
			return;
		}

		GUI.BaseHelpPanelLines.Reset();
		GUI.BaseHelpPanelLines.Add(TEXT("Escape  : Quit application"));
		GUI.BaseHelpPanelLines.Add(TEXT("H       : Toggle this controls panel"));
		GUI.BaseHelpPanelLines.Add(TEXT("P       : Cycle playable camera modes"));
		GUI.BaseHelpPanelLines.Add(TEXT("O       : Toggle cinematic camera"));
		GUI.BaseHelpPanelLines.Add(TEXT("--------------------------------"));
		GUI.BaseHelpPanelLines.Add(TEXT("J       : Toggle HiRes frame capture"));
		GUI.BaseHelpPanelLines.Add(TEXT("U       : Show capture output status"));
		GUI.BaseHelpPanelLines.Add(TEXT("Y       : Not in flight yet (reserved)"));
		GUI.BaseHelpPanelLines.Add(TEXT("--------------------------------"));
		GUI.BaseHelpPanelLines.Add(TEXT("W/A/S/D : Move (input fallback)"));
		GUI.BaseHelpPanelLines.Add(TEXT("Shift   : Sprint modifier (input fallback)"));
		GUI.BaseHelpPanelLines.Add(TEXT("Mouse   : Look input (input fallback)"));
		GUI.BaseHelpPanelLines.Add(TEXT("Wheel   : Third-person zoom (when in third-person mode)"));
		GUI.bHelpPanelInitialized = true;
	}

	void RebuildDisplayHelpLines(FMetaAgentGUIState& GUI)
	{
		GUI.HelpPanelLines = GUI.BaseHelpPanelLines;
		GUI.HelpPanelLines.Add(TEXT("--------------------------------"));
		GUI.HelpPanelLines.Add(GUI.RecordingStatusLine.IsEmpty() ? TEXT("Recording: OFF") : GUI.RecordingStatusLine);
	}
}

void FMetaAgentGUIRuntime::RunApplyHelpPanelSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentGUIState& GUI)
{
	InitializeDefaultHelpPanelLines(GUI);
	RebuildDisplayHelpLines(GUI);

	if (AMetaAgentHUD* MetaAgentHUD = Controller.GetHUD<AMetaAgentHUD>())
	{
		MetaAgentHUD->SetHelpPanelLines(GUI.HelpPanelLines);
		MetaAgentHUD->SetHelpPanelVisible(GUI.bHelpPanelVisible);

		TArray<FString> NetworkingLines;
		if (const UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(&Controller))
		{
			NetworkingLines = GI->GetNetworkingRuntimePanelLines();
		}
		else
		{
			NetworkingLines.Add(TEXT("Networking Runtime"));
			NetworkingLines.Add(TEXT("GameInstance : MetaAgentGameInstance NOT active"));
			NetworkingLines.Add(TEXT("Check Maps & Modes > Game Instance Class"));
		}

		MetaAgentHUD->SetNetworkingPanelLines(NetworkingLines);
		MetaAgentHUD->SetNetworkingPanelVisible(GUI.bHelpPanelVisible);

		MetaAgentHUD->SetRecordingPanelLines(Controller.BuildRecordingRuntimePanelLines());
		MetaAgentHUD->SetRecordingPanelVisible(GUI.bHelpPanelVisible);
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

	InitializeDefaultHelpPanelLines(GUI);
	GUI.bHelpPanelVisible = !GUI.bHelpPanelVisible;
	RunApplyHelpPanelSequence(Controller, GUI);

	UE_LOG(LogMetaAgent, Log, TEXT("GUIRuntime: Help panel %s."), GUI.bHelpPanelVisible ? TEXT("ENABLED") : TEXT("DISABLED"));
}
