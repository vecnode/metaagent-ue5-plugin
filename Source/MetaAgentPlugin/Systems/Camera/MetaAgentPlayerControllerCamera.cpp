// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Systems/Camera/MetaAgentCameraRuntime.h"

void AMetaAgentPlayerController::ApplyCameraModeToPawn(APawn* InPawn)
{
	FMetaAgentCameraRuntime::RunApplyCameraModeSequence(*this, InPawn, CameraMode, CameraZoom);
}

void AMetaAgentPlayerController::ConfigureCameraForPawn(APawn* InPawn)
{
	FMetaAgentCameraRuntime::RunApplyCameraModeSequence(*this, InPawn, CameraMode, CameraZoom);
}

void AMetaAgentPlayerController::ApplyMouseWheelZoom(APawn* ControlledPawn, float DeltaTime)
{
	FMetaAgentCameraRuntime::RunThirdPersonZoomSequence(*this, ControlledPawn, DeltaTime, CameraMode, CameraZoom);
}

void AMetaAgentPlayerController::HandleToggleCinematicCameraPressed()
{
	ToggleCinematicCameraMode();
}

void AMetaAgentPlayerController::ToggleCinematicCameraMode()
{
	FMetaAgentCameraRuntime::RunToggleCinematicCameraSequence(*this, CinematicCamera, Autopilot);
}

AActor* AMetaAgentPlayerController::ResolveCinematicTargetActor() const
{
	return FMetaAgentCameraRuntime::ResolveCinematicTargetActor(*const_cast<AMetaAgentPlayerController*>(this), Autopilot);
}

FVector AMetaAgentPlayerController::ResolveCinematicFocusLocation(AActor* TargetActor) const
{
	return FMetaAgentCameraRuntime::ResolveCinematicFocusLocation(TargetActor, CinematicCamera);
}

void AMetaAgentPlayerController::EnableCinematicCameraMode()
{
	FMetaAgentCameraRuntime::RunEnableCinematicCameraSequence(*this, CinematicCamera, Autopilot);
}

void AMetaAgentPlayerController::DisableCinematicCameraMode()
{
	FMetaAgentCameraRuntime::RunDisableCinematicCameraSequence(*this, CinematicCamera, Autopilot);
}

void AMetaAgentPlayerController::UpdateCinematicCamera(float DeltaTime)
{
	FMetaAgentCameraRuntime::RunUpdateCinematicCameraSequence(*this, DeltaTime, CinematicCamera, Autopilot);
}
