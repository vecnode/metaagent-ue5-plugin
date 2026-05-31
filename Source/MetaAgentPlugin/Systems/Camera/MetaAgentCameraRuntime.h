// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"

class AActor;
class APawn;
class AMetaAgentPlayerController;

enum class EMetaAgentCameraMode : uint8;
enum class EMetaAgentCinematicCameraStyle : uint8;

struct FMetaAgentAutopilotState;
struct FMetaAgentCameraModeState;
struct FMetaAgentCameraZoomState;
struct FMetaAgentCinematicCameraState;

struct FMetaAgentCameraRuntime
{
	static void RunPlayableCameraModeCycleSequence(
		AMetaAgentPlayerController& Controller,
		APawn* Pawn,
		FMetaAgentCameraModeState& CameraMode,
		FMetaAgentCameraZoomState& CameraZoom,
		const FMetaAgentCinematicCameraState& CinematicCamera);

	static void RunApplyCameraModeSequence(
		AMetaAgentPlayerController& Controller,
		APawn* Pawn,
		FMetaAgentCameraModeState& CameraMode,
		FMetaAgentCameraZoomState& CameraZoom);

	static void RunEnableSpecificCameraModeSequence(
		AMetaAgentPlayerController& Controller,
		APawn* Pawn,
		FMetaAgentCameraModeState& CameraMode,
		FMetaAgentCameraZoomState& CameraZoom,
		EMetaAgentCameraMode TargetMode);

	static void RunThirdPersonZoomSequence(
		AMetaAgentPlayerController& Controller,
		APawn* Pawn,
		float DeltaTime,
		FMetaAgentCameraModeState& CameraMode,
		FMetaAgentCameraZoomState& CameraZoom);

	static void RunToggleCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentCinematicCameraState& CinematicCamera,
		const FMetaAgentAutopilotState& Autopilot);

	static void RunEnableCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentCinematicCameraState& CinematicCamera,
		const FMetaAgentAutopilotState& Autopilot);

	static void RunDisableCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentCinematicCameraState& CinematicCamera,
		const FMetaAgentAutopilotState& Autopilot);

	static void RunUpdateCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		float DeltaTime,
		FMetaAgentCinematicCameraState& CinematicCamera,
		const FMetaAgentAutopilotState& Autopilot);

	static AActor* ResolveCinematicTargetActor(
		AMetaAgentPlayerController& Controller,
		const FMetaAgentAutopilotState& Autopilot);

	static FVector ResolveCinematicFocusLocation(
		AActor* TargetActor,
		const FMetaAgentCinematicCameraState& CinematicCamera);

	static const TCHAR* GetCinematicStyleLabel(EMetaAgentCinematicCameraStyle Style);
};