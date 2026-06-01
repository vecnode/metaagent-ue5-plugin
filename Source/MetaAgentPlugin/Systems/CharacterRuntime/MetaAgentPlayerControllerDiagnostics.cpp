// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void AMetaAgentPlayerController::LogMovementAnimationDiagnostics(APawn* ControlledPawn)
{
	if (!ControlledPawn || MovementDiagnostics.bLoggedMovementAnimDiagnostics)
	{
		return;
	}

	const float Speed2D = ControlledPawn->GetVelocity().Size2D();
	if (Speed2D <= 10.0f)
	{
		return;
	}

	if (ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn))
	{
		if (UCharacterMovementComponent* MovementComp = ControlledCharacter->GetCharacterMovement())
		{
			UE_LOG(LogMetaAgent, Log,
				TEXT("MovementDiag: Pawn='%s' MaxWalkSpeed=%.2f MaxAcceleration=%.2f InputPending=%.3f InputLast=%.3f MovementMode=%d"),
				*GetNameSafe(ControlledPawn),
				MovementComp->MaxWalkSpeed,
				MovementComp->MaxAcceleration,
				ControlledCharacter->GetPendingMovementInputVector().Size(),
				ControlledCharacter->GetLastMovementInputVector().Size(),
				static_cast<int32>(MovementComp->MovementMode));

			const UCapsuleComponent* CapsuleComp = ControlledCharacter->GetCapsuleComponent();
			if (CapsuleComp)
			{
				const float CapsuleHalfHeight = CapsuleComp->GetScaledCapsuleHalfHeight();
				const float CapsuleBottomZ = CapsuleComp->GetComponentLocation().Z - CapsuleHalfHeight;

				USkeletalMeshComponent* BodyMesh = nullptr;
				TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes;
				ControlledCharacter->GetComponents(SkeletalMeshes);
				for (USkeletalMeshComponent* MeshComp : SkeletalMeshes)
				{
					if (!MeshComp)
					{
						continue;
					}

					if (MeshComp->GetFName() == TEXT("Body"))
					{
						BodyMesh = MeshComp;
						break;
					}

					if (!BodyMesh && MeshComp->GetAnimInstance())
					{
						BodyMesh = MeshComp;
					}
				}

				if (BodyMesh)
				{
					const FBoxSphereBounds MeshBounds = BodyMesh->Bounds;
					const float MeshFeetWorldZ = MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z;
					const float MeshFeetVsCapsuleBottom = MeshFeetWorldZ - CapsuleBottomZ;

					UE_LOG(LogMetaAgent, Warning,
						TEXT("GroundDiag: Pawn='%s' CapsuleHalfHeight=%.2f CapsuleBottomZ=%.2f BodyRelZ=%.2f BodyWorldZ=%.2f MeshFeetWorldZ=%.2f MeshFeetVsCapsuleBottom=%.2f FloorDist=%.2f LineDist=%.2f bWalkable=%s"),
						*GetNameSafe(ControlledCharacter),
						CapsuleHalfHeight,
						CapsuleBottomZ,
						BodyMesh->GetRelativeLocation().Z,
						BodyMesh->GetComponentLocation().Z,
						MeshFeetWorldZ,
						MeshFeetVsCapsuleBottom,
						MovementComp->CurrentFloor.FloorDist,
						MovementComp->CurrentFloor.LineDist,
						MovementComp->CurrentFloor.IsWalkableFloor() ? TEXT("true") : TEXT("false"));
				}
			}
		}
	}

	UE_LOG(LogMetaAgent, Log,
		TEXT("AnimDiag: Pawn='%s' Speed2D=%.2f"),
		*GetNameSafe(ControlledPawn),
		Speed2D);

	TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes;
	ControlledPawn->GetComponents(SkeletalMeshes);

	for (int32 MeshIndex = 0; MeshIndex < SkeletalMeshes.Num(); ++MeshIndex)
	{
		USkeletalMeshComponent* MeshComp = SkeletalMeshes[MeshIndex];
		if (!MeshComp)
		{
			continue;
		}

		const TCHAR* AnimationModeLabel = TEXT("Unknown");
		switch (MeshComp->GetAnimationMode())
		{
		case EAnimationMode::AnimationBlueprint:
			AnimationModeLabel = TEXT("AnimationBlueprint");
			break;
		case EAnimationMode::AnimationSingleNode:
			AnimationModeLabel = TEXT("AnimationSingleNode");
			break;
		case EAnimationMode::AnimationCustomMode:
			AnimationModeLabel = TEXT("AnimationCustomMode");
			break;
		default:
			break;
		}

		UE_LOG(LogMetaAgent, Log,
			TEXT("AnimDiagMesh[%d]: Comp='%s' Visible=%s Mesh='%s' Skeleton='%s' AnimMode=%s AnimClass='%s' AnimInstance='%s'"),
			MeshIndex,
			*GetNameSafe(MeshComp),
			MeshComp->IsVisible() ? TEXT("true") : TEXT("false"),
			*GetNameSafe(MeshComp->GetSkeletalMeshAsset()),
			(MeshComp->GetSkeletalMeshAsset() && MeshComp->GetSkeletalMeshAsset()->GetSkeleton()) ? *GetNameSafe(MeshComp->GetSkeletalMeshAsset()->GetSkeleton()) : TEXT("none"),
			AnimationModeLabel,
			*GetNameSafe(MeshComp->GetAnimClass()),
			MeshComp->GetAnimInstance() ? *MeshComp->GetAnimInstance()->GetClass()->GetName() : TEXT("none"));
	}

	MovementDiagnostics.bLoggedMovementAnimDiagnostics = true;
}

void AMetaAgentPlayerController::UpdateMovementProbe(APawn* ControlledPawn, float DeltaTime)
{
	ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn);
	if (!ControlledCharacter)
	{
		return;
	}

	UCharacterMovementComponent* MovementComp = ControlledCharacter->GetCharacterMovement();
	if (!MovementComp)
	{
		return;
	}

	const float PendingInput = ControlledCharacter->GetPendingMovementInputVector().Size();
	const float LastInput = ControlledCharacter->GetLastMovementInputVector().Size();
	const float InputMagnitude = FMath::Max(PendingInput, LastInput);

	if (!MovementDiagnostics.bMovementProbeActive && InputMagnitude > 0.9f)
	{
		MovementDiagnostics.bMovementProbeActive = true;
		MovementDiagnostics.ProbeElapsedSeconds = 0.0f;
		MovementDiagnostics.ProbePeakSpeed2D = 0.0f;
		MovementDiagnostics.ProbePeakAcceleration2D = 0.0f;
		MovementDiagnostics.ProbeSampleCount = 0;
	}

	if (!MovementDiagnostics.bMovementProbeActive)
	{
		return;
	}

	const float Speed2D = ControlledPawn->GetVelocity().Size2D();
	const float Accel2D = MovementComp->GetCurrentAcceleration().Size2D();
	MovementDiagnostics.ProbePeakSpeed2D = FMath::Max(MovementDiagnostics.ProbePeakSpeed2D, Speed2D);
	MovementDiagnostics.ProbePeakAcceleration2D = FMath::Max(MovementDiagnostics.ProbePeakAcceleration2D, Accel2D);
	MovementDiagnostics.ProbeElapsedSeconds += DeltaTime;
	++MovementDiagnostics.ProbeSampleCount;

	const bool bStopWindow = (MovementDiagnostics.ProbeElapsedSeconds >= 2.0f) || (InputMagnitude < 0.1f);
	if (!bStopWindow)
	{
		return;
	}

	UE_LOG(LogMetaAgent, Warning,
		TEXT("MovementProbe: Pawn='%s' Duration=%.2fs Samples=%d Input=%.3f SpeedNow=%.2f SpeedPeak=%.2f MaxWalkSpeed=%.2f MaxSpeed=%.2f AccelPeak=%.2f MaxAcceleration=%.2f BrakingDecelWalk=%.2f GroundFriction=%.2f BrakingFriction=%.2f BrakingFrictionFactor=%.2f MovementMode=%d"),
		*GetNameSafe(ControlledPawn),
		MovementDiagnostics.ProbeElapsedSeconds,
		MovementDiagnostics.ProbeSampleCount,
		InputMagnitude,
		Speed2D,
		MovementDiagnostics.ProbePeakSpeed2D,
		MovementComp->MaxWalkSpeed,
		MovementComp->GetMaxSpeed(),
		MovementDiagnostics.ProbePeakAcceleration2D,
		MovementComp->GetMaxAcceleration(),
		MovementComp->BrakingDecelerationWalking,
		MovementComp->GroundFriction,
		MovementComp->BrakingFriction,
		MovementComp->BrakingFrictionFactor,
		static_cast<int32>(MovementComp->MovementMode));

	MovementDiagnostics.bMovementProbeActive = false;
}

