// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/CameraRuntime/MetaAgentCameraRuntime.h"

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"

namespace
{
		void SanitizeCameraZoomState(FMetaAgentCameraZoomState& CameraZoom)
		{
			CameraZoom.MinDistance = FMath::Max(0.0f, CameraZoom.MinDistance);
			CameraZoom.MaxDistance = FMath::Max(CameraZoom.MinDistance + 1.0f, CameraZoom.MaxDistance);
			CameraZoom.MouseWheelStep = FMath::Max(1.0f, CameraZoom.MouseWheelStep);
			CameraZoom.InterpSpeed = FMath::Max(0.01f, CameraZoom.InterpSpeed);

			if (CameraZoom.DesiredDistance >= 0.0f)
			{
				CameraZoom.DesiredDistance = FMath::Clamp(CameraZoom.DesiredDistance, CameraZoom.MinDistance, CameraZoom.MaxDistance);
			}
		}

		void SanitizeCinematicCameraState(FMetaAgentCinematicCameraState& CinematicCamera)
		{
			CinematicCamera.BlendInSeconds = FMath::Max(0.0f, CinematicCamera.BlendInSeconds);
			CinematicCamera.BlendOutSeconds = FMath::Max(0.0f, CinematicCamera.BlendOutSeconds);
			CinematicCamera.PanDegrees = FMath::Clamp(CinematicCamera.PanDegrees, 1.0f, 360.0f);
			CinematicCamera.PanDurationSeconds = FMath::Max(0.1f, CinematicCamera.PanDurationSeconds);
			CinematicCamera.OscillationYawAmplitudeDegrees = FMath::Max(0.0f, CinematicCamera.OscillationYawAmplitudeDegrees);
			CinematicCamera.HeadFocusAlpha = FMath::Clamp(CinematicCamera.HeadFocusAlpha, 0.0f, 1.0f);
			CinematicCamera.OrbitSpeedScale = FMath::Max(0.01f, CinematicCamera.OrbitSpeedScale);
			CinematicCamera.TurnsPerDirection = FMath::Max(1, CinematicCamera.TurnsPerDirection);
			CinematicCamera.CloseOrbitRadius = FMath::Max(60.0f, CinematicCamera.CloseOrbitRadius);
			CinematicCamera.SwayHorizontalAmplitude = FMath::Max(0.0f, CinematicCamera.SwayHorizontalAmplitude);
			CinematicCamera.SwayVerticalAmplitude = FMath::Max(0.0f, CinematicCamera.SwayVerticalAmplitude);
			CinematicCamera.SwayFrequency = FMath::Max(0.1f, CinematicCamera.SwayFrequency);
		}

	bool TryGetFirstValidSocketLocation(
		const USkeletalMeshComponent* Mesh,
		const TArray<FName>& CandidateSockets,
		FVector& OutLocation)
	{
		if (!Mesh)
		{
			return false;
		}

		for (const FName& SocketName : CandidateSockets)
		{
			if (Mesh->DoesSocketExist(SocketName))
			{
				OutLocation = Mesh->GetSocketLocation(SocketName);
				return true;
			}
		}

		return false;
	}

	bool TryGetFirstValidSocketName(
		const USkeletalMeshComponent* Mesh,
		const TArray<FName>& CandidateSockets,
		FName& OutSocketName)
	{
		if (!Mesh)
		{
			return false;
		}

		for (const FName& SocketName : CandidateSockets)
		{
			if (Mesh->DoesSocketExist(SocketName))
			{
				OutSocketName = SocketName;
				return true;
			}
		}

		return false;
	}

	FName ResolveFirstPersonCameraSocket(const USkeletalMeshComponent* Mesh)
	{
		FName SocketName = NAME_None;
		if (Mesh)
		{
			const TArray<FName> CandidateSockets = {TEXT("head"), TEXT("Head"), TEXT("headSocket"), TEXT("neck_01")};
			if (TryGetFirstValidSocketName(Mesh, CandidateSockets, SocketName))
			{
				return SocketName;
			}
		}

		return NAME_None;
	}
}

const TCHAR* FMetaAgentCameraRuntime::GetCinematicStyleLabel(const EMetaAgentCinematicCameraStyle Style)
{
	switch (Style)
	{
	case EMetaAgentCinematicCameraStyle::OscillatingHold:
	default:
		return TEXT("OscillatingHold");
	}
}

void FMetaAgentCameraRuntime::RunPlayableCameraModeCycleSequence(
	AMetaAgentPlayerController& Controller,
	APawn* Pawn,
	FMetaAgentCameraModeState& CameraMode,
	FMetaAgentCameraZoomState& CameraZoom,
	const FMetaAgentCinematicCameraState& CinematicCamera)
{
	if (!Controller.IsLocalPlayerController())
	{
		return;
	}

	if (CinematicCamera.bModeEnabled)
	{
		return;
	}

	switch (CameraMode.ActiveMode)
	{
	case EMetaAgentCameraMode::ThirdPerson:
		RunEnableSpecificCameraModeSequence(Controller, Pawn, CameraMode, CameraZoom, EMetaAgentCameraMode::CloseOverShoulder);
		break;
	case EMetaAgentCameraMode::CloseOverShoulder:
		RunEnableSpecificCameraModeSequence(Controller, Pawn, CameraMode, CameraZoom, EMetaAgentCameraMode::SideCinematicClose);
		break;
	case EMetaAgentCameraMode::SideCinematicClose:
		RunEnableSpecificCameraModeSequence(Controller, Pawn, CameraMode, CameraZoom, EMetaAgentCameraMode::FirstPerson);
		break;
	case EMetaAgentCameraMode::FirstPerson:
	default:
		RunEnableSpecificCameraModeSequence(Controller, Pawn, CameraMode, CameraZoom, EMetaAgentCameraMode::ThirdPerson);
		break;
	}
}

void FMetaAgentCameraRuntime::RunEnableSpecificCameraModeSequence(
	AMetaAgentPlayerController& Controller,
	APawn* Pawn,
	FMetaAgentCameraModeState& CameraMode,
	FMetaAgentCameraZoomState& CameraZoom,
	const EMetaAgentCameraMode TargetMode)
{
	CameraMode.ActiveMode = TargetMode;
	RunApplyCameraModeSequence(Controller, Pawn, CameraMode, CameraZoom);

	switch (TargetMode)
	{
	case EMetaAgentCameraMode::FirstPerson:
		UE_LOG(LogMetaAgent, Log, TEXT("CameraMode: switched to first-person (P cycles: Third -> Close -> Side -> First)."));
		break;
	case EMetaAgentCameraMode::CloseOverShoulder:
		UE_LOG(LogMetaAgent, Log, TEXT("CameraMode: switched to close over-shoulder (P cycles: Third -> Close -> Side -> First)."));
		break;
	case EMetaAgentCameraMode::SideCinematicClose:
		UE_LOG(LogMetaAgent, Log, TEXT("CameraMode: switched to tight side cinematic (P cycles: Third -> Close -> Side -> First)."));
		break;
	case EMetaAgentCameraMode::ThirdPerson:
	default:
		UE_LOG(LogMetaAgent, Log, TEXT("CameraMode: switched to third-person (P cycles: Third -> Close -> Side -> First)."));
		break;
	}
}

void FMetaAgentCameraRuntime::RunApplyCameraModeSequence(
	AMetaAgentPlayerController& Controller,
	APawn* InPawn,
	FMetaAgentCameraModeState& CameraMode,
	FMetaAgentCameraZoomState& CameraZoom)
{
	SanitizeCameraZoomState(CameraZoom);

	if (!InPawn)
	{
		return;
	}

	const bool bUseFirstPersonCamera = CameraMode.ActiveMode == EMetaAgentCameraMode::FirstPerson;
	const bool bUseCloseOverShoulderCamera = CameraMode.ActiveMode == EMetaAgentCameraMode::CloseOverShoulder;
	const bool bUseSideCinematicCloseCamera = CameraMode.ActiveMode == EMetaAgentCameraMode::SideCinematicClose;
	const float ThirdPersonArmTarget = CameraMode.LastThirdPersonArmLength > 0.0f
		? CameraMode.LastThirdPersonArmLength
		: CameraMode.ThirdPersonArmLength;
	USpringArmComponent* SpringArm = nullptr;
	UCameraComponent* FollowCam = nullptr;
	USceneComponent* PawnRoot = InPawn->GetRootComponent();
	USceneComponent* CameraParent = PawnRoot;
	FName CameraParentSocket = NAME_None;
	USkeletalMeshComponent* CharacterMesh = nullptr;
	ACharacter* CharacterPawn = Cast<ACharacter>(InPawn);

	if (CharacterPawn)
	{
		CharacterMesh = CharacterPawn->GetMesh();

		if (bUseFirstPersonCamera && CharacterMesh)
		{
			CameraParent = CharacterMesh;
			if (CameraMode.bPreferHeadSocketForFirstPerson)
			{
				CameraParentSocket = ResolveFirstPersonCameraSocket(CharacterMesh);
			}
		}

		if (UCapsuleComponent* CapsuleComp = CharacterPawn->GetCapsuleComponent())
		{
			if (!bUseFirstPersonCamera)
			{
				CameraParent = CapsuleComp;
			}
		}
	}

	TInlineComponentArray<USpringArmComponent*> SpringArms;
	InPawn->GetComponents(SpringArms);
	for (USpringArmComponent* Candidate : SpringArms)
	{
		if (Candidate && Candidate->GetFName() == TEXT("CameraBoom"))
		{
			SpringArm = Candidate;
			break;
		}
	}
	if (!SpringArm)
	{
		SpringArm = InPawn->FindComponentByClass<USpringArmComponent>();
	}

	if (!SpringArm && CameraParent)
	{
		SpringArm = NewObject<USpringArmComponent>(InPawn, TEXT("CameraBoom"));
		if (SpringArm)
		{
			InPawn->AddInstanceComponent(SpringArm);
			if (CameraParentSocket != NAME_None)
			{
				SpringArm->SetupAttachment(CameraParent, CameraParentSocket);
			}
			else
			{
				SpringArm->SetupAttachment(CameraParent);
			}
			SpringArm->TargetArmLength = bUseFirstPersonCamera
				? 0.0f
				: (bUseCloseOverShoulderCamera
					? CameraMode.CloseOverShoulderArmLength
					: (bUseSideCinematicCloseCamera ? CameraMode.SideCinematicCloseArmLength : CameraMode.ThirdPersonArmLength));
			SpringArm->bUsePawnControlRotation = true;
			SpringArm->bDoCollisionTest = false;
			SpringArm->RegisterComponent();

			UE_LOG(LogMetaAgent, Warning,
				TEXT("CameraSetup: Pawn '%s' had no spring arm. Added runtime CameraBoom fallback."),
				*GetNameSafe(InPawn));
		}
	}
	else if (SpringArm && CameraParent)
	{
		const bool bNeedsReattach = SpringArm->GetAttachParent() != CameraParent;
		const bool bSocketMismatch =
			(CameraParentSocket != NAME_None) &&
			(SpringArm->GetAttachSocketName() != CameraParentSocket);
		if (bNeedsReattach || bSocketMismatch)
		{
			if (CameraParentSocket != NAME_None)
			{
				SpringArm->AttachToComponent(CameraParent, FAttachmentTransformRules::KeepRelativeTransform, CameraParentSocket);
			}
			else
			{
				SpringArm->AttachToComponent(CameraParent, FAttachmentTransformRules::KeepRelativeTransform);
			}
		}
	}

	TInlineComponentArray<UCameraComponent*> Cameras;
	InPawn->GetComponents(Cameras);
	for (UCameraComponent* Candidate : Cameras)
	{
		if (Candidate && Candidate->GetFName() == TEXT("FollowCamera"))
		{
			FollowCam = Candidate;
			break;
		}
	}
	if (!FollowCam)
	{
		FollowCam = InPawn->FindComponentByClass<UCameraComponent>();
	}

	if (!FollowCam)
	{
		FollowCam = NewObject<UCameraComponent>(InPawn, TEXT("FollowCamera"));
		if (FollowCam)
		{
			InPawn->AddInstanceComponent(FollowCam);
			if (SpringArm)
			{
				FollowCam->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
			}
			else if (PawnRoot)
			{
				FollowCam->SetupAttachment(PawnRoot);
			}
			FollowCam->bUsePawnControlRotation = false;
			FollowCam->RegisterComponent();
			FollowCam->Activate();

			UE_LOG(LogMetaAgent, Warning,
				TEXT("CameraSetup: Pawn '%s' had no camera. Added runtime FollowCamera fallback."),
				*GetNameSafe(InPawn));
		}
	}
	else if (FollowCam)
	{
		if (SpringArm)
		{
			if (FollowCam->GetAttachParent() != SpringArm)
			{
				FollowCam->AttachToComponent(SpringArm, FAttachmentTransformRules::KeepRelativeTransform, USpringArmComponent::SocketName);
			}
		}
		else if (CameraParent)
		{
			if (FollowCam->GetAttachParent() != CameraParent)
			{
				FollowCam->AttachToComponent(CameraParent, FAttachmentTransformRules::KeepRelativeTransform);
			}
		}
	}

	if (SpringArm)
	{
		SpringArm->TargetArmLength = bUseFirstPersonCamera
			? 0.0f
			: (bUseCloseOverShoulderCamera
				? CameraMode.CloseOverShoulderArmLength
				: (bUseSideCinematicCloseCamera ? CameraMode.SideCinematicCloseArmLength : ThirdPersonArmTarget));
		SpringArm->bUsePawnControlRotation = true;
		SpringArm->bDoCollisionTest = false;
		SpringArm->TargetOffset = FVector::ZeroVector;
		SpringArm->SocketOffset = FVector::ZeroVector;

		if (bUseFirstPersonCamera)
		{
			if (!CameraParentSocket.IsNone())
			{
				SpringArm->SetRelativeLocation(FVector::ZeroVector);
			}
			else if (CharacterPawn)
			{
				SpringArm->TargetOffset = FVector(0.0f, 0.0f, CharacterPawn->BaseEyeHeight + CameraMode.FirstPersonEyeHeightOffset);
			}
			CameraMode.PreferredFirstPersonSocket = CameraParentSocket;
		}
		else if (bUseCloseOverShoulderCamera)
		{
			SpringArm->TargetOffset = FVector(0.0f, 0.0f, CameraMode.CloseOverShoulderHeightOffset);
			SpringArm->SocketOffset = FVector(0.0f, CameraMode.CloseOverShoulderLateralOffset, 0.0f);
		}
		else if (bUseSideCinematicCloseCamera)
		{
			SpringArm->TargetOffset = FVector(0.0f, 0.0f, CameraMode.SideCinematicCloseHeightOffset);
			SpringArm->SocketOffset = FVector(0.0f, CameraMode.SideCinematicCloseLateralOffset, 0.0f);
		}
		else
		{
			CameraMode.LastThirdPersonArmLength = FMath::Max(0.0f, ThirdPersonArmTarget);
			CameraZoom.DesiredDistance = FMath::Clamp(ThirdPersonArmTarget, CameraZoom.MinDistance, CameraZoom.MaxDistance);
		}
	}

	if (FollowCam)
	{
		FollowCam->bUsePawnControlRotation = false;
		FollowCam->Activate();
		if (bUseFirstPersonCamera)
		{
			FollowCam->SetRelativeLocation(FVector(CameraMode.FirstPersonForwardOffset, 0.0f, CameraMode.FirstPersonVerticalOffset));
			FollowCam->SetRelativeRotation(FRotator::ZeroRotator);
		}
		else
		{
			FollowCam->SetRelativeLocation(FVector::ZeroVector);
			FollowCam->SetRelativeRotation(FRotator::ZeroRotator);
		}
	}

	Controller.bAutoManageActiveCameraTarget = false;
	Controller.SetViewTargetWithBlend(InPawn, 0.0f);

	UE_LOG(LogMetaAgent, Log,
		TEXT("CameraSetup: Pawn='%s' Mode=%s SpringArm=%s Camera=%s TargetDistance=%.2f Socket=%s"),
		*GetNameSafe(InPawn),
		bUseFirstPersonCamera
			? TEXT("FirstPerson")
			: (bUseCloseOverShoulderCamera
				? TEXT("CloseOverShoulder")
				: (bUseSideCinematicCloseCamera ? TEXT("SideCinematicClose") : TEXT("ThirdPerson"))),
		SpringArm ? TEXT("found") : TEXT("MISSING"),
		FollowCam ? TEXT("found") : TEXT("MISSING"),
		CameraZoom.DesiredDistance,
		CameraParentSocket.IsNone() ? TEXT("none") : *CameraParentSocket.ToString());
}

void FMetaAgentCameraRuntime::RunThirdPersonZoomSequence(
	AMetaAgentPlayerController& Controller,
	APawn* ControlledPawn,
	const float DeltaTime,
	FMetaAgentCameraModeState& CameraMode,
	FMetaAgentCameraZoomState& CameraZoom)
{
	SanitizeCameraZoomState(CameraZoom);

	if (!ControlledPawn)
	{
		return;
	}

	if (CameraMode.ActiveMode != EMetaAgentCameraMode::ThirdPerson)
	{
		return;
	}

	USpringArmComponent* SpringArm = ControlledPawn->FindComponentByClass<USpringArmComponent>();
	if (!SpringArm)
	{
		return;
	}

	if (CameraZoom.DesiredDistance < 0.0f)
	{
		CameraZoom.DesiredDistance = FMath::Clamp(
			CameraMode.LastThirdPersonArmLength > 0.0f ? CameraMode.LastThirdPersonArmLength : SpringArm->TargetArmLength,
			CameraZoom.MinDistance,
			CameraZoom.MaxDistance);
	}

	bool bConsumedDiscreteWheelInput = false;
	if (Controller.WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		CameraZoom.DesiredDistance -= CameraZoom.MouseWheelStep;
		bConsumedDiscreteWheelInput = true;
	}
	if (Controller.WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		CameraZoom.DesiredDistance += CameraZoom.MouseWheelStep;
		bConsumedDiscreteWheelInput = true;
	}

	if (!bConsumedDiscreteWheelInput)
	{
		const float WheelAxis = Controller.GetInputAnalogKeyState(EKeys::MouseWheelAxis);
		if (!FMath::IsNearlyZero(WheelAxis, KINDA_SMALL_NUMBER))
		{
			CameraZoom.DesiredDistance -= WheelAxis * CameraZoom.MouseWheelStep;
		}
	}

	CameraZoom.DesiredDistance = FMath::Clamp(CameraZoom.DesiredDistance, CameraZoom.MinDistance, CameraZoom.MaxDistance);
	CameraMode.LastThirdPersonArmLength = CameraZoom.DesiredDistance;
	SpringArm->TargetArmLength = FMath::FInterpTo(
		SpringArm->TargetArmLength,
		CameraZoom.DesiredDistance,
		DeltaTime,
		CameraZoom.InterpSpeed);
}

void FMetaAgentCameraRuntime::RunToggleCinematicCameraSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentCinematicCameraState& CinematicCamera,
	const FMetaAgentAutopilotState& Autopilot)
{
	if (!Controller.IsLocalPlayerController())
	{
		return;
	}

	if (CinematicCamera.bModeEnabled)
	{
		RunDisableCinematicCameraSequence(Controller, CinematicCamera, Autopilot);
	}
	else
	{
		RunEnableCinematicCameraSequence(Controller, CinematicCamera, Autopilot);
	}
}

AActor* FMetaAgentCameraRuntime::ResolveCinematicTargetActor(
	AMetaAgentPlayerController& Controller,
	const FMetaAgentAutopilotState& Autopilot)
{
	if (APawn* ControlledPawn = Controller.GetPawn())
	{
		return ControlledPawn;
	}

	if (Autopilot.Pawn.IsValid())
	{
		return Autopilot.Pawn.Get();
	}

	if (AActor* ViewTarget = Controller.GetViewTarget())
	{
		return ViewTarget;
	}

	return nullptr;
}

FVector FMetaAgentCameraRuntime::ResolveCinematicFocusLocation(
	AActor* TargetActor,
	const FMetaAgentCinematicCameraState& CinematicCamera)
{
	if (!TargetActor)
	{
		return FVector::ZeroVector;
	}

	const FVector FallbackLocation = TargetActor->GetActorLocation() + FVector(0.0f, 0.0f, CinematicCamera.LookAtZOffset);

	const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
	if (!TargetCharacter)
	{
		return FallbackLocation;
	}

	const USkeletalMeshComponent* Mesh = TargetCharacter->GetMesh();
	if (!Mesh)
	{
		return FallbackLocation;
	}

	FVector HeadLocation = FVector::ZeroVector;
	FVector ChestLocation = FVector::ZeroVector;

	const bool bHasHead = TryGetFirstValidSocketLocation(
		Mesh,
		{TEXT("head"), TEXT("Head"), TEXT("headSocket"), TEXT("neck_01")},
		HeadLocation);

	const bool bHasChest = TryGetFirstValidSocketLocation(
		Mesh,
		{TEXT("spine_03"), TEXT("spine_02"), TEXT("Spine_03"), TEXT("chest")},
		ChestLocation);

	if (bHasHead && bHasChest)
	{
		return FMath::Lerp(ChestLocation, HeadLocation, FMath::Clamp(CinematicCamera.HeadFocusAlpha, 0.0f, 1.0f));
	}

	if (bHasHead)
	{
		return HeadLocation;
	}

	if (bHasChest)
	{
		return ChestLocation;
	}

	return FallbackLocation;
}

void FMetaAgentCameraRuntime::RunEnableCinematicCameraSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentCinematicCameraState& CinematicCamera,
	const FMetaAgentAutopilotState& Autopilot)
{
	if (CinematicCamera.bModeEnabled)
	{
		return;
	}

	SanitizeCinematicCameraState(CinematicCamera);

	UWorld* World = Controller.GetWorld();
	if (!World || !Controller.PlayerCameraManager)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("CinematicCamera: missing world or player camera manager. Cannot enable cinematic camera."));
		return;
	}

	if (CinematicCamera.RuntimeCameraActor && !IsValid(CinematicCamera.RuntimeCameraActor))
	{
		CinematicCamera.RuntimeCameraActor = nullptr;
	}

	if (CinematicCamera.RuntimeCameraActor && CinematicCamera.RuntimeCameraActor->GetWorld() != World)
	{
		CinematicCamera.RuntimeCameraActor->Destroy();
		CinematicCamera.RuntimeCameraActor = nullptr;
	}

	AActor* TargetActor = ResolveCinematicTargetActor(Controller, Autopilot);
	if (!TargetActor)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("CinematicCamera: no valid target actor found."));
		return;
	}

	if (!CinematicCamera.RuntimeCameraActor || !IsValid(CinematicCamera.RuntimeCameraActor))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CinematicCamera.RuntimeCameraActor = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(),
			Controller.PlayerCameraManager->GetCameraLocation(),
			Controller.PlayerCameraManager->GetCameraRotation(),
			SpawnParams);
	}

	if (!CinematicCamera.RuntimeCameraActor)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("CinematicCamera: failed to spawn runtime camera actor."));
		return;
	}

	CinematicCamera.PreViewTarget = Controller.GetViewTarget();
	CinematicCamera.TargetActor = TargetActor;
	CinematicCamera.PanElapsedSeconds = 0.0f;
	CinematicCamera.SwayPhaseOffset = FMath::FRandRange(0.0f, 2.0f * PI);
	CinematicCamera.OrbitAccumulatedYawDegrees = 0.0f;
	CinematicCamera.OrbitDirectionSign = 1;
	CinematicCamera.DirectionTravelDegrees = 0.0f;
	CinematicCamera.CompletedTurnsThisDirection = 0;

	const FVector CameraLocation = Controller.PlayerCameraManager->GetCameraLocation();
	const FVector TargetLocation = ResolveCinematicFocusLocation(TargetActor, CinematicCamera);
	const FVector ToCamera = CameraLocation - TargetLocation;
	const FVector ToCameraXY(ToCamera.X, ToCamera.Y, 0.0f);

	CinematicCamera.OrbitRadius = FMath::Clamp(
		ToCameraXY.Size(),
		60.0f,
		5000.0f);
	if (ToCameraXY.IsNearlyZero())
	{
		CinematicCamera.OrbitRadius = CinematicCamera.CloseOrbitRadius;
	}
	CinematicCamera.StartOrbitYawDegrees = ToCameraXY.IsNearlyZero()
		? TargetActor->GetActorRotation().Yaw
		: ToCameraXY.Rotation().Yaw;
	CinematicCamera.CameraHeightOffset = FMath::Clamp(ToCamera.Z, 30.0f, 160.0f);

	CinematicCamera.RuntimeCameraActor->SetActorLocationAndRotation(
		CameraLocation,
		Controller.PlayerCameraManager->GetCameraRotation());

	Controller.bAutoManageActiveCameraTarget = false;
	Controller.SetViewTargetWithBlend(CinematicCamera.RuntimeCameraActor, CinematicCamera.BlendInSeconds);
	CinematicCamera.bModeEnabled = true;

	if (CinematicCamera.bDisablePlayerInput)
	{
		Controller.SetIgnoreMoveInput(true);
		Controller.SetIgnoreLookInput(true);
	}

	UE_LOG(LogMetaAgent, Log,
		TEXT("CinematicCamera: ENABLED around '%s' (style=%s degrees=%.1f duration=%.2fs continuous=%s radius=%.1f speedScale=%.2f turnsPerDirection=%d). Press O to exit."),
		*GetNameSafe(TargetActor),
		GetCinematicStyleLabel(CinematicCamera.ActiveStyle),
		CinematicCamera.PanDegrees,
		CinematicCamera.PanDurationSeconds,
		CinematicCamera.bContinuousOrbit ? TEXT("true") : TEXT("false"),
		CinematicCamera.OrbitRadius,
		CinematicCamera.OrbitSpeedScale,
		CinematicCamera.TurnsPerDirection);

}

void FMetaAgentCameraRuntime::RunDisableCinematicCameraSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentCinematicCameraState& CinematicCamera,
	const FMetaAgentAutopilotState& Autopilot)
{
	if (!CinematicCamera.bModeEnabled)
	{
		return;
	}

	CinematicCamera.bModeEnabled = false;
	CinematicCamera.PanElapsedSeconds = 0.0f;
	CinematicCamera.OrbitAccumulatedYawDegrees = 0.0f;
	CinematicCamera.DirectionTravelDegrees = 0.0f;
	CinematicCamera.CompletedTurnsThisDirection = 0;
	CinematicCamera.TargetActor.Reset();

	if (CinematicCamera.bDisablePlayerInput)
	{
		Controller.SetIgnoreMoveInput(false);
		Controller.SetIgnoreLookInput(false);
	}

	AActor* RestoreViewTarget = nullptr;
	if (CinematicCamera.PreViewTarget.IsValid())
	{
		RestoreViewTarget = CinematicCamera.PreViewTarget.Get();
	}
	else if (APawn* ControlledPawn = Controller.GetPawn())
	{
		RestoreViewTarget = ControlledPawn;
	}
	else if (Autopilot.Pawn.IsValid())
	{
		RestoreViewTarget = Autopilot.Pawn.Get();
	}

	if (RestoreViewTarget)
	{
		Controller.SetViewTargetWithBlend(RestoreViewTarget, CinematicCamera.BlendOutSeconds);
	}

	CinematicCamera.PreViewTarget.Reset();

	UE_LOG(LogMetaAgent, Log, TEXT("CinematicCamera: DISABLED. Restored normal camera."));

}

void FMetaAgentCameraRuntime::RunUpdateCinematicCameraSequence(
	AMetaAgentPlayerController& Controller,
	const float DeltaTime,
	FMetaAgentCinematicCameraState& CinematicCamera,
	const FMetaAgentAutopilotState& Autopilot)
{
	if (!CinematicCamera.bModeEnabled)
	{
		return;
	}

	SanitizeCinematicCameraState(CinematicCamera);

	if (DeltaTime <= 0.0f)
	{
		return;
	}

	if (!Controller.GetWorld() || !Controller.PlayerCameraManager)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("CinematicCamera: world/camera manager became invalid during update. Disabling cinematic camera."));
		RunDisableCinematicCameraSequence(Controller, CinematicCamera, Autopilot);
		return;
	}

	if (!CinematicCamera.RuntimeCameraActor || !IsValid(CinematicCamera.RuntimeCameraActor))
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("CinematicCamera: runtime camera actor was lost. Disabling cinematic camera."));
		RunDisableCinematicCameraSequence(Controller, CinematicCamera, Autopilot);
		return;
	}

	AActor* TargetActor = CinematicCamera.TargetActor.Get();
	if (!TargetActor)
	{
		TargetActor = ResolveCinematicTargetActor(Controller, Autopilot);
		if (!TargetActor)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("CinematicCamera: no valid target actor during update. Disabling cinematic camera."));
			RunDisableCinematicCameraSequence(Controller, CinematicCamera, Autopilot);
			return;
		}

		CinematicCamera.TargetActor = TargetActor;
	}

	CinematicCamera.PanElapsedSeconds += DeltaTime;
	const float Duration = FMath::Max(0.1f, CinematicCamera.PanDurationSeconds);
	float StyleTimeScale = 1.0f;
	const float TimeWithPhase = CinematicCamera.PanElapsedSeconds + CinematicCamera.SwayPhaseOffset;
	float CurrentOrbitYaw = CinematicCamera.StartOrbitYawDegrees;

	switch (CinematicCamera.ActiveStyle)
	{
	case EMetaAgentCinematicCameraStyle::OscillatingHold:
	default:
		{
			StyleTimeScale = 0.1f;
			const float OscillationFrequencyRadians = ((2.0f * PI) / Duration) * StyleTimeScale;
			const float YawOffset = FMath::Sin(CinematicCamera.PanElapsedSeconds * OscillationFrequencyRadians)
				* CinematicCamera.OscillationYawAmplitudeDegrees;
			CurrentOrbitYaw += YawOffset;
			break;
		}
	}

	const float BaseFrequencyRadians = (FMath::Max(0.1f, CinematicCamera.SwayFrequency) * 2.0f * PI) * StyleTimeScale;
	const float OrbitYawRadians = FMath::DegreesToRadians(CurrentOrbitYaw);
	const float HorizontalSway = FMath::Sin(TimeWithPhase * BaseFrequencyRadians) * CinematicCamera.SwayHorizontalAmplitude;
	const float VerticalSway = FMath::Sin((TimeWithPhase * BaseFrequencyRadians * 0.57f) + 1.2f) * CinematicCamera.SwayVerticalAmplitude;

	FVector TargetLocation = ResolveCinematicFocusLocation(TargetActor, CinematicCamera);
	if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
	{
		TargetLocation += TargetCharacter->GetVelocity() * 0.05f;
	}

	const FVector OrbitDirection(FMath::Cos(OrbitYawRadians), FMath::Sin(OrbitYawRadians), 0.0f);
	const FVector OrbitRight(-OrbitDirection.Y, OrbitDirection.X, 0.0f);
	const FVector NewCameraLocation(
		TargetLocation.X + (OrbitDirection.X * CinematicCamera.OrbitRadius) + (OrbitRight.X * HorizontalSway),
		TargetLocation.Y + (OrbitDirection.Y * CinematicCamera.OrbitRadius) + (OrbitRight.Y * HorizontalSway),
		TargetLocation.Z + CinematicCamera.CameraHeightOffset + VerticalSway);

	const FRotator NewCameraRotation = (TargetLocation - NewCameraLocation).Rotation();
	CinematicCamera.RuntimeCameraActor->SetActorLocationAndRotation(NewCameraLocation, NewCameraRotation);
}
