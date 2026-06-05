// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/CameraRuntime/MetaAgentCameraRuntime.h"

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
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
		CinematicCamera.OrbitSpeedScale = FMath::Max(0.01f, CinematicCamera.OrbitSpeedScale);
		CinematicCamera.TurnsPerDirection = FMath::Max(1, CinematicCamera.TurnsPerDirection);
		CinematicCamera.CloseOrbitRadius = FMath::Max(60.0f, CinematicCamera.CloseOrbitRadius);
		CinematicCamera.SwayHorizontalAmplitude = FMath::Max(0.0f, CinematicCamera.SwayHorizontalAmplitude);
		CinematicCamera.SwayVerticalAmplitude = FMath::Max(0.0f, CinematicCamera.SwayVerticalAmplitude);
		CinematicCamera.SwayFrequency = FMath::Max(0.1f, CinematicCamera.SwayFrequency);
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

void FMetaAgentCameraRuntime::RunEnvironmentZoomSequence(
	AMetaAgentPlayerController& Controller,
	const float DeltaTime,
	FMetaAgentCameraZoomState& CameraZoom)
{
	SanitizeCameraZoomState(CameraZoom);

	if (!Controller.IsLocalPlayerController())
	{
		return;
	}

	// Consume discrete mouse wheel input
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

	// Consume analog wheel-axis input if discrete was not used
	if (!bConsumedDiscreteWheelInput)
	{
		const float WheelAxis = Controller.GetInputAnalogKeyState(EKeys::MouseWheelAxis);
		if (!FMath::IsNearlyZero(WheelAxis, KINDA_SMALL_NUMBER))
		{
			CameraZoom.DesiredDistance -= WheelAxis * CameraZoom.MouseWheelStep;
		}
	}

	// Clamp zoom distance to bounds
	CameraZoom.DesiredDistance = FMath::Clamp(CameraZoom.DesiredDistance, CameraZoom.MinDistance, CameraZoom.MaxDistance);

	UE_LOG(LogMetaAgent, Log, TEXT("EnvironmentZoom: DesiredDistance=%.2f (min=%.2f, max=%.2f)"), 
		CameraZoom.DesiredDistance, CameraZoom.MinDistance, CameraZoom.MaxDistance);
}

void FMetaAgentCameraRuntime::RunToggleCinematicCameraSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentCinematicCameraState& CinematicCamera)
{
	if (!Controller.IsLocalPlayerController())
	{
		return;
	}

	// Calculate a default focus location at the scene origin
	const FVector DefaultFocusLocation = FVector(0.0f, 0.0f, 100.0f);

	if (CinematicCamera.bModeEnabled)
	{
		RunDisableCinematicCameraSequence(Controller, CinematicCamera);
	}
	else
	{
		RunEnableCinematicCameraSequence(Controller, CinematicCamera, DefaultFocusLocation);
	}
}

void FMetaAgentCameraRuntime::RunEnableCinematicCameraSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentCinematicCameraState& CinematicCamera,
	FVector TargetFocusLocation)
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

	// Clean up invalid runtime camera actor
	if (CinematicCamera.RuntimeCameraActor && !IsValid(CinematicCamera.RuntimeCameraActor))
	{
		CinematicCamera.RuntimeCameraActor = nullptr;
	}

	// Destroy if world mismatch
	if (CinematicCamera.RuntimeCameraActor && CinematicCamera.RuntimeCameraActor->GetWorld() != World)
	{
		CinematicCamera.RuntimeCameraActor->Destroy();
		CinematicCamera.RuntimeCameraActor = nullptr;
	}

	// Spawn new runtime camera actor if needed
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

	// Initialize cinematic camera state
	CinematicCamera.PreViewTarget = Controller.GetViewTarget();
	CinematicCamera.PanElapsedSeconds = 0.0f;
	CinematicCamera.SwayPhaseOffset = FMath::FRandRange(0.0f, 2.0f * PI);
	CinematicCamera.OrbitAccumulatedYawDegrees = 0.0f;
	CinematicCamera.OrbitDirectionSign = 1;
	CinematicCamera.DirectionTravelDegrees = 0.0f;
	CinematicCamera.CompletedTurnsThisDirection = 0;

	// Calculate initial orbit radius from current camera position
	const FVector CurrentCameraLocation = Controller.PlayerCameraManager->GetCameraLocation();
	const FVector ToCamera = CurrentCameraLocation - TargetFocusLocation;
	const FVector ToCameraXY(ToCamera.X, ToCamera.Y, 0.0f);

	CinematicCamera.OrbitRadius = FMath::Clamp(
		ToCameraXY.Size(),
		60.0f,
		5000.0f);

	if (ToCameraXY.IsNearlyZero())
	{
		CinematicCamera.OrbitRadius = CinematicCamera.CloseOrbitRadius;
		CinematicCamera.StartOrbitYawDegrees = 0.0f;
	}
	else
	{
		CinematicCamera.StartOrbitYawDegrees = ToCameraXY.Rotation().Yaw;
	}

	CinematicCamera.CameraHeightOffset = FMath::Clamp(ToCamera.Z, 30.0f, 160.0f);

	// Position runtime camera at current location
	CinematicCamera.RuntimeCameraActor->SetActorLocationAndRotation(
		CurrentCameraLocation,
		Controller.PlayerCameraManager->GetCameraRotation());

	Controller.bAutoManageActiveCameraTarget = false;
	Controller.SetViewTargetWithBlend(CinematicCamera.RuntimeCameraActor, CinematicCamera.BlendInSeconds);
	CinematicCamera.bModeEnabled = true;

	// Keep player input fully enabled during cinematic for environment exploration
	Controller.SetIgnoreMoveInput(false);
	Controller.SetIgnoreLookInput(false);

	UE_LOG(LogMetaAgent, Log,
		TEXT("CinematicCamera: ENABLED at focus (%.1f, %.1f, %.1f) style=%s radius=%.1f speedScale=%.2f. Press O to exit."),
		TargetFocusLocation.X, TargetFocusLocation.Y, TargetFocusLocation.Z,
		GetCinematicStyleLabel(CinematicCamera.ActiveStyle),
		CinematicCamera.OrbitRadius,
		CinematicCamera.OrbitSpeedScale);
}

void FMetaAgentCameraRuntime::RunDisableCinematicCameraSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentCinematicCameraState& CinematicCamera)
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

	// Restore pre-cinematic view target
	AActor* RestoreViewTarget = nullptr;
	if (CinematicCamera.PreViewTarget.IsValid())
	{
		RestoreViewTarget = CinematicCamera.PreViewTarget.Get();
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
	FVector TargetFocusLocation)
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
		RunDisableCinematicCameraSequence(Controller, CinematicCamera);
		return;
	}

	if (!CinematicCamera.RuntimeCameraActor || !IsValid(CinematicCamera.RuntimeCameraActor))
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("CinematicCamera: runtime camera actor was lost. Disabling cinematic camera."));
		RunDisableCinematicCameraSequence(Controller, CinematicCamera);
		return;
	}

	// Accumulate pan time
	CinematicCamera.PanElapsedSeconds += DeltaTime;
	const float Duration = FMath::Max(0.1f, CinematicCamera.PanDurationSeconds);
	float StyleTimeScale = 1.0f;
	const float TimeWithPhase = CinematicCamera.PanElapsedSeconds + CinematicCamera.SwayPhaseOffset;
	float CurrentOrbitYaw = CinematicCamera.StartOrbitYawDegrees;

	// Apply oscillating hold cinematic style (smooth sway motion)
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

	// Apply sway motion (horizontal and vertical oscillation)
	const float BaseFrequencyRadians = (FMath::Max(0.1f, CinematicCamera.SwayFrequency) * 2.0f * PI) * StyleTimeScale;
	const float OrbitYawRadians = FMath::DegreesToRadians(CurrentOrbitYaw);
	const float HorizontalSway = FMath::Sin(TimeWithPhase * BaseFrequencyRadians) * CinematicCamera.SwayHorizontalAmplitude;
	const float VerticalSway = FMath::Sin((TimeWithPhase * BaseFrequencyRadians * 0.57f) + 1.2f) * CinematicCamera.SwayVerticalAmplitude;

	// Calculate orbital camera position
	const FVector OrbitDirection(FMath::Cos(OrbitYawRadians), FMath::Sin(OrbitYawRadians), 0.0f);
	const FVector OrbitRight(-OrbitDirection.Y, OrbitDirection.X, 0.0f);
	const FVector NewCameraLocation(
		TargetFocusLocation.X + (OrbitDirection.X * CinematicCamera.OrbitRadius) + (OrbitRight.X * HorizontalSway),
		TargetFocusLocation.Y + (OrbitDirection.Y * CinematicCamera.OrbitRadius) + (OrbitRight.Y * HorizontalSway),
		TargetFocusLocation.Z + CinematicCamera.CameraHeightOffset + VerticalSway);

	// Look at focus point
	const FRotator NewCameraRotation = (TargetFocusLocation - NewCameraLocation).Rotation();
	CinematicCamera.RuntimeCameraActor->SetActorLocationAndRotation(NewCameraLocation, NewCameraRotation);
}
