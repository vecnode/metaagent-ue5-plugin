// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Systems/CameraRuntime/MetaAgentCameraRuntime.h"

// Environment-only viewing: no character-specific camera setup needed
void AMetaAgentPlayerController::ApplyCameraModeToPawn(APawn* InPawn)
{
	// Simplified for environment-only mode: no-op
	UE_LOG(LogTemp, Log, TEXT("ApplyCameraModeToPawn: environment viewer mode (no pawn camera setup)."));
}

void AMetaAgentPlayerController::ConfigureCameraForPawn(APawn* InPawn)
{
	// Simplified for environment-only mode: no-op
	UE_LOG(LogTemp, Log, TEXT("ConfigureCameraForPawn: environment viewer mode (no pawn camera setup)."));
}

void AMetaAgentPlayerController::ApplyMouseWheelZoom(APawn* ControlledPawn, float DeltaTime)
{
	// Generic zoom for free camera (not pawn-specific)
	FMetaAgentCameraRuntime::RunEnvironmentZoomSequence(*this, DeltaTime, CameraZoom);
}

void AMetaAgentPlayerController::HandleToggleCinematicCameraPressed()
{
	ToggleCinematicCameraMode();
}

void AMetaAgentPlayerController::ToggleCinematicCameraMode()
{
	// Default focus at scene origin, environment-only mode
	FMetaAgentCameraRuntime::RunToggleCinematicCameraSequence(*this, CinematicCamera);
}

AActor* AMetaAgentPlayerController::ResolveCinematicTargetActor() const
{
	// For environment-only mode, return null (focus uses default origin)
	return nullptr;
}

FVector AMetaAgentPlayerController::ResolveCinematicFocusLocation(AActor* TargetActor) const
{
	// For environment-only mode, return origin focus location
	return FVector(0.0f, 0.0f, 100.0f);
}

void AMetaAgentPlayerController::EnableCinematicCameraMode()
{
	// Default focus at scene origin
	const FVector DefaultFocusLocation = FVector(0.0f, 0.0f, 100.0f);
	FMetaAgentCameraRuntime::RunEnableCinematicCameraSequence(*this, CinematicCamera, DefaultFocusLocation);
}

void AMetaAgentPlayerController::DisableCinematicCameraMode()
{
	FMetaAgentCameraRuntime::RunDisableCinematicCameraSequence(*this, CinematicCamera);
}

void AMetaAgentPlayerController::UpdateCinematicCamera(float DeltaTime)
{
	// Default focus at scene origin
	const FVector DefaultFocusLocation = FVector(0.0f, 0.0f, 100.0f);
	FMetaAgentCameraRuntime::RunUpdateCinematicCameraSequence(*this, DeltaTime, CinematicCamera, DefaultFocusLocation);
}
