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
	// Environment-only camera control: simplified for viewer mode without character dependencies

	static void RunEnvironmentZoomSequence(
		AMetaAgentPlayerController& Controller,
		float DeltaTime,
		FMetaAgentCameraZoomState& CameraZoom);

	static void RunToggleCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentCinematicCameraState& CinematicCamera);

	static void RunEnableCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentCinematicCameraState& CinematicCamera,
		FVector TargetFocusLocation);

	static void RunDisableCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentCinematicCameraState& CinematicCamera);

	static void RunUpdateCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		float DeltaTime,
		FMetaAgentCinematicCameraState& CinematicCamera,
		FVector TargetFocusLocation);

	static const TCHAR* GetCinematicStyleLabel(EMetaAgentCinematicCameraStyle Style);
};