// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "Gameplay/AI/MetaAgentWanderAIController.h"
#include "AIController.h"
#include "Engine/World.h"

namespace
{
	void ResetAutopilotRuntimeState(FMetaAgentAutopilotState& Autopilot)
	{
		Autopilot.bEnabled = false;
		Autopilot.Pawn.Reset();
		Autopilot.Controller.Reset();
	}
}

void AMetaAgentPlayerController::HandleToggleAutopilotPressed()
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (!IsLocalPlayerController())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float CurrentTimeSeconds = World->GetTimeSeconds();
	const float EffectiveDebounceSeconds = FMath::Max(0.0f, Autopilot.ToggleDebounceSeconds);
	if ((CurrentTimeSeconds - Autopilot.LastToggleTimeSeconds) < EffectiveDebounceSeconds)
	{
		return;
	}

	Autopilot.LastToggleTimeSeconds = CurrentTimeSeconds;

	if (Autopilot.bEnabled)
	{
		DisableAutopilotAndRepossess();
	}
	else
	{
		EnableAutopilotForCurrentPawn();
	}
}

void AMetaAgentPlayerController::EnableAutopilotForCurrentPawn()
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (Autopilot.bEnabled)
	{
		UE_LOG(LogMetaAgent, Verbose, TEXT("Autopilot: enable requested while already enabled."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("Autopilot: world is null; cannot enable."));
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!IsValid(ControlledPawn))
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("Autopilot: no currently possessed pawn to hand to AI."));
		return;
	}

	if (ControlledPawn->GetController() != this)
	{
		UE_LOG(LogMetaAgent, Warning,
			TEXT("Autopilot: current pawn '%s' is not controlled by this player controller."),
			*GetNameSafe(ControlledPawn));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UClass* DesiredAIControllerClass = Autopilot.AIControllerClass.Get();
	if (!DesiredAIControllerClass || !DesiredAIControllerClass->IsChildOf(AAIController::StaticClass()))
	{
		if (DesiredAIControllerClass)
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("Autopilot: configured class '%s' is not an AAIController; falling back to AMetaAgentWanderAIController."),
				*GetNameSafe(DesiredAIControllerClass));
		}
		DesiredAIControllerClass = AMetaAgentWanderAIController::StaticClass();
	}

	AAIController* AIController = World->SpawnActor<AAIController>(
		DesiredAIControllerClass,
		ControlledPawn->GetActorLocation(),
		ControlledPawn->GetActorRotation(),
		SpawnParams);

	if (!AIController)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("Autopilot: failed to spawn AI controller '%s'."), *GetNameSafe(DesiredAIControllerClass));
		return;
	}

	UnPossess();
	AIController->Possess(ControlledPawn);
	if (AIController->GetPawn() != ControlledPawn)
	{
		UE_LOG(LogMetaAgent, Error,
			TEXT("Autopilot: AI controller '%s' failed to possess pawn '%s'. Restoring player control."),
			*GetNameSafe(AIController),
			*GetNameSafe(ControlledPawn));

		AIController->Destroy();
		Possess(ControlledPawn);
		SetViewTargetWithBlend(ControlledPawn, 0.0f);
		ResetAutopilotRuntimeState(Autopilot);
		return;
	}

	Autopilot.Pawn = ControlledPawn;
	Autopilot.Controller = AIController;
	Autopilot.bEnabled = true;
	SetViewTargetWithBlend(ControlledPawn, 0.0f);

	UE_LOG(LogMetaAgent, Log, TEXT("Autopilot: ENABLED for pawn '%s'. Press I again to return to player control."), *GetNameSafe(ControlledPawn));

	EnableCinematicCameraMode();
}

void AMetaAgentPlayerController::DisableAutopilotAndRepossess()
{
	if (!IsMetaAgentRuntimeActive())
	{
		ResetAutopilotRuntimeState(Autopilot);
		return;
	}

	AAIController* CachedAIController = Autopilot.Controller.Get();
	APawn* PawnToRepossess = Autopilot.Pawn.Get();
	if (!PawnToRepossess && CachedAIController)
	{
		PawnToRepossess = CachedAIController->GetPawn();
	}

	if (!PawnToRepossess)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("Autopilot: no cached pawn to repossess."));
		ResetAutopilotRuntimeState(Autopilot);
		DisableCinematicCameraMode();
		return;
	}

	if (AController* CurrentController = PawnToRepossess->GetController())
	{
		if (CurrentController != this)
		{
			if (CurrentController->IsA<AAIController>())
			{
				CurrentController->UnPossess();
				CurrentController->Destroy();
			}
			else
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("Autopilot: pawn '%s' is controlled by non-AI controller '%s'; refusing forced repossession."),
					*GetNameSafe(PawnToRepossess),
					*GetNameSafe(CurrentController));
				return;
			}
		}
	}

	if (IsValid(PawnToRepossess))
	{
		Possess(PawnToRepossess);
		SetViewTargetWithBlend(PawnToRepossess, 0.0f);
	}
	else
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("Autopilot: cached pawn became invalid before repossession."));
	}

	ResetAutopilotRuntimeState(Autopilot);

	UE_LOG(LogMetaAgent, Log, TEXT("Autopilot: DISABLED. Player control restored."));

	DisableCinematicCameraMode();
}

