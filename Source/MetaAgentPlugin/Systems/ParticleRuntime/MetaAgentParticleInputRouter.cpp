// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleInputRouter.h"

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Systems/ParticleRuntime/MetaAgentParticleOrchestrator.h"

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
	InputComponent->BindKey(EKeys::V, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleImageRevealPressed);
	InputComponent->BindKey(EKeys::C, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleBoxSculptPressed);
	InputComponent->BindKey(EKeys::B, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleSlowPresetPressed);
	InputComponent->BindKey(EKeys::N, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleDramaticPresetPressed);
	InputComponent->BindKey(EKeys::One, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticlePlayNormalPressed);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticlePlaySlowPressed);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticlePlayDramaticPressed);
	InputComponent->BindKey(EKeys::R, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleReplayLastPressed);
	InputComponent->BindKey(EKeys::T, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleCycleSamplingPressed);
	InputComponent->BindKey(EKeys::Y, IE_Pressed, Controller, &AMetaAgentPlayerController::HandleParticleCycleFormingPressed);
}

TArray<FString> FMetaAgentParticleInputRouter::GetParticleKeyHelpLines()
{
	TArray<FString> Lines;
	Lines.Add(TEXT("PARTICLES (orchestrator)"));
	Lines.Add(TEXT("F       : Load sdxl_latest.png preview + image shape source"));
	Lines.Add(TEXT("V       : Image reveal pattern"));
	Lines.Add(TEXT("C       : Random 3D box sculpt"));
	Lines.Add(TEXT("1/2/3   : Play Normal / Slow / Dramatic image patterns"));
	Lines.Add(TEXT("B / N   : Apply Slow / Dramatic preset (then V)"));
	Lines.Add(TEXT("R       : Replay last particle effect"));
	Lines.Add(TEXT("T       : Cycle image sampling (Gray / Fill / Sobel)"));
	Lines.Add(TEXT("Y       : Cycle forming mode (Lerp / Arc / Spiral / Wave / Spring / Niagara)"));
	Lines.Add(TEXT("Console: MetaAgent.Pattern.ScatterGrid / .ScatterJitter / .Forming"));
	Lines.Add(TEXT("Console: MetaAgent.Pattern.Cancel / .SkipHold / .Ready"));
	return Lines;
}
