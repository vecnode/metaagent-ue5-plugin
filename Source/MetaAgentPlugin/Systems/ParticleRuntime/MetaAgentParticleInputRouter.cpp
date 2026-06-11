// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleInputRouter.h"

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Systems/GUIRuntime/MetaAgentRuntimePanelTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticleOrchestrator.h"

namespace
{
	FMetaAgentGUIActionRow MakeParticleRow(const FString& KeyLabel, const FString& Description, const FName ActionId)
	{
		FMetaAgentGUIActionRow Row;
		Row.KeyLabel = KeyLabel;
		Row.Description = Description;
		Row.ActionId = ActionId;
		return Row;
	}
}

void FMetaAgentParticleInputRouter::BindKeyboardInput(
	AMetaAgentPlayerController* Controller,
	UInputComponent* InputComponent,
	UMetaAgentParticleOrchestrator* Orchestrator)
{
	if (!Controller || !InputComponent || !Orchestrator)
	{
		return;
	}

	InputComponent->BindKey(EKeys::F, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleLoadPreviewPressed);
	InputComponent->BindKey(EKeys::Comma, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleStepPatternBackwardPressed);
	InputComponent->BindKey(EKeys::Period, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleStepPatternForwardPressed);
	InputComponent->BindKey(EKeys::B, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleSlowPresetPressed);
	InputComponent->BindKey(EKeys::N, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleDramaticPresetPressed);
	InputComponent->BindKey(EKeys::T, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleCycleSamplingPressed);
	InputComponent->BindKey(EKeys::Y, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleCycleFormingPressed);
}

TArray<FString> FMetaAgentParticleInputRouter::GetParticleKeyHelpLines()
{
	TArray<FString> Lines;
	for (const FMetaAgentGUIActionRow& Row : GetParticleGUIActionRows())
	{
		Lines.Add(FString::Printf(TEXT("%-7s : %s"), *Row.KeyLabel, *Row.Description));
	}
	Lines.Add(TEXT("Console: MetaAgent.Pattern.ScatterGrid / .ScatterJitter / .Forming"));
	Lines.Add(TEXT("Console: MetaAgent.Pattern.Cancel / .SkipHold / .Ready"));
	return Lines;
}

TArray<FMetaAgentGUIActionRow> FMetaAgentParticleInputRouter::GetParticleGUIActionRows()
{
	TArray<FMetaAgentGUIActionRow> Rows;
	Rows.Add(MakeParticleRow(TEXT("F"), TEXT("Load sdxl_latest.png preview + image shape source"), MetaAgentRuntimeIds::ParticleLoadPreview));
	Rows.Add(MakeParticleRow(TEXT("Play"), TEXT("Full image reveal cycle (auto state machine)"), MetaAgentRuntimeIds::ParticlePlayFullCycle));
	Rows.Add(MakeParticleRow(TEXT("<<"), TEXT("Step pattern state backward"), MetaAgentRuntimeIds::ParticleStepBackward));
	Rows.Add(MakeParticleRow(TEXT(">>"), TEXT("Step pattern state forward (Idle starts Forming)"), MetaAgentRuntimeIds::ParticleStepForward));
	Rows.Add(MakeParticleRow(TEXT("B / N"), TEXT("Apply Slow / Dramatic preset"), MetaAgentRuntimeIds::ParticleSlowPreset));
	Rows.Add(MakeParticleRow(TEXT("T"), TEXT("Cycle image sampling (Gray / Sobel)"), MetaAgentRuntimeIds::ParticleCycleSampling));
	Rows.Add(MakeParticleRow(TEXT("Y"), TEXT("Cycle forming mode (Lerp / Arc / Spiral)"), MetaAgentRuntimeIds::ParticleCycleForming));
	return Rows;
}
