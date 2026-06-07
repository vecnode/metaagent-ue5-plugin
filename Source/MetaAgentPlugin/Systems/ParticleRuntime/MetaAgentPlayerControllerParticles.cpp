// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"

#include "Core/MetaAgent.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/ParticleRuntime/MetaAgentParticleRuntime.h"

void AMetaAgentPlayerController::HandleParticlePatternPressed()
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (!ParticleRuntime)
	{
		RefreshParticleRuntimeTracking();
	}

	if (!ParticleRuntime)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("ParticleRuntime: pattern request ignored because runtime is not initialized."));
		return;
	}

	ParticleRuntime->DiscoverNiagaraComponents(false);
	ParticleRuntime->ForceCaptureParticles();

	if (!ParticleRuntime->StartSquarePattern())
	{
		if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
		{
			MetaAgentHUD->AddTransientMessage(
				TEXT("Particle pattern unavailable (busy or no captured particles)."),
				FColor::Orange,
				2.5f);
		}
		return;
	}

	if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
	{
		MetaAgentHUD->AddTransientMessage(
			FString::Printf(TEXT("Particle square pattern started (%s)."), *ParticleRuntime->BuildPatternStatusText()),
			FColor::Cyan,
			3.0f);
	}
}

bool AMetaAgentPlayerController::StartParticleSquarePattern()
{
	if (!ParticleRuntime)
	{
		return false;
	}

	ParticleRuntime->ForceCaptureParticles();
	return ParticleRuntime->StartSquarePattern();
}

FString AMetaAgentPlayerController::GetParticlePatternStatusText() const
{
	if (!ParticleRuntime)
	{
		return TEXT("Pattern State: Idle | Phase: 0.00 | Particles: 0");
	}

	return ParticleRuntime->BuildPatternStatusText();
}
