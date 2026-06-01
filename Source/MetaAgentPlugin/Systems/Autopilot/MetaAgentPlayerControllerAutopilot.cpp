// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "Gameplay/AI/MetaAgentWanderAIController.h"
#include "AIController.h"
#include "Engine/World.h"

void AMetaAgentPlayerController::HandleToggleAutopilotPressed()
{
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
	if ((CurrentTimeSeconds - Autopilot.LastToggleTimeSeconds) < Autopilot.ToggleDebounceSeconds)
	{
		return;
	}

	Autopilot.LastToggleTimeSeconds = CurrentTimeSeconds;

	if (Autopilot.bEnabled)
	{
		StopAutopilotTakeRecording();
		DisableAutopilotAndRepossess();
	}
	else
	{
		EnableAutopilotForCurrentPawn();
		StartAutopilotTakeRecording();
	}
}

void AMetaAgentPlayerController::EnableAutopilotForCurrentPawn()
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("Autopilot: no currently possessed pawn to hand to AI."));
		return;
	}

	Autopilot.Pawn = ControlledPawn;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UClass* DesiredAIControllerClass = Autopilot.AIControllerClass.Get();
	if (!DesiredAIControllerClass)
	{
		DesiredAIControllerClass = AMetaAgentWanderAIController::StaticClass();
	}

	AAIController* AIController = GetWorld()->SpawnActor<AAIController>(
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
	SetViewTargetWithBlend(ControlledPawn, 0.0f);

	Autopilot.bEnabled = true;

	UE_LOG(LogMetaAgent, Log, TEXT("Autopilot: ENABLED for pawn '%s'. Press J again to return to player control."), *GetNameSafe(ControlledPawn));

	EnableCinematicCameraMode();
}

void AMetaAgentPlayerController::DisableAutopilotAndRepossess()
{
	APawn* PawnToRepossess = Autopilot.Pawn.Get();
	if (!PawnToRepossess)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("Autopilot: no cached pawn to repossess."));
		Autopilot.bEnabled = false;
		return;
	}

	if (AController* CurrentController = PawnToRepossess->GetController())
	{
		if (CurrentController != this)
		{
			CurrentController->UnPossess();

			if (CurrentController->IsA<AAIController>())
			{
				CurrentController->Destroy();
			}
		}
	}

	Possess(PawnToRepossess);
	SetViewTargetWithBlend(PawnToRepossess, 0.0f);

	Autopilot.bEnabled = false;
	Autopilot.Pawn.Reset();

	UE_LOG(LogMetaAgent, Log, TEXT("Autopilot: DISABLED. Player control restored."));

	DisableCinematicCameraMode();
}

