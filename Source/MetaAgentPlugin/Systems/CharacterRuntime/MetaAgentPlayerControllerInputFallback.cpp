// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"

void AMetaAgentPlayerController::ApplyFallbackMovementInput(APawn* ControlledPawn)
{
	if (!ControlledPawn
		|| !InputFallback.bEnableKeyboardMovement
		|| InputFallback.bAddedAnyMappingContext
		|| IsGUIInteractionModeActive()
		|| !IsModularRuntimeEnabled(EMetaAgentModularRuntime::CharacterInput)
		|| (CinematicCamera.bModeEnabled && CinematicCamera.bDisablePlayerInput))
	{
		return;
	}

	const float ForwardRaw =
		(IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up) ? 1.0f : 0.0f) -
		(IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down) ? 1.0f : 0.0f);

	const float RightRaw =
		(IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right) ? 1.0f : 0.0f) -
		(IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left) ? 1.0f : 0.0f);

	if (FMath::IsNearlyZero(ForwardRaw) && FMath::IsNearlyZero(RightRaw))
	{
		return;
	}

	if (ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn))
	{
		if (UCharacterMovementComponent* MovementComp = ControlledCharacter->GetCharacterMovement())
		{
			const bool bWantsSprint = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
			const float DesiredSpeed = bWantsSprint ? InputFallback.SprintSpeed : InputFallback.WalkSpeed;
			MovementComp->MaxWalkSpeed = FMath::Max(1.0f, DesiredSpeed);
		}
	}

	const FRotator CurrentControlRotation = GetControlRotation();
	const FRotator YawRotation(0.0f, CurrentControlRotation.Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	ControlledPawn->AddMovementInput(ForwardDirection, ForwardRaw);
	ControlledPawn->AddMovementInput(RightDirection, RightRaw);
}

void AMetaAgentPlayerController::ApplyFallbackLookInput()
{
	if (!InputFallback.bEnableMouseLook
		|| IsGUIInteractionModeActive()
		|| !IsModularRuntimeEnabled(EMetaAgentModularRuntime::CharacterInput)
		|| (CinematicCamera.bModeEnabled && CinematicCamera.bDisablePlayerInput))
	{
		return;
	}

	float MouseDeltaX = 0.0f;
	float MouseDeltaY = 0.0f;
	GetInputMouseDelta(MouseDeltaX, MouseDeltaY);

	// PIE can report zero mouse delta depending on capture mode. Use axis keys as fallback.
	if (FMath::IsNearlyZero(MouseDeltaX) && FMath::IsNearlyZero(MouseDeltaY))
	{
		MouseDeltaX = GetInputAnalogKeyState(EKeys::MouseX);
		MouseDeltaY = GetInputAnalogKeyState(EKeys::MouseY);
	}

	if (FMath::IsNearlyZero(MouseDeltaX) && FMath::IsNearlyZero(MouseDeltaY))
	{
		return;
	}

	const float EffectiveMouseSensitivity = InputFallback.MouseSensitivity * 2.0f;

	// PIE/editor capture paths can ignore AddYawInput/AddPitchInput.
	// Apply rotation directly so camera look always responds to mouse movement.
	if (IsLookInputIgnored())
	{
		SetIgnoreLookInput(false);
	}

	FRotator NewControlRotation = GetControlRotation();
	NewControlRotation.Yaw = FRotator::NormalizeAxis(NewControlRotation.Yaw + (MouseDeltaX * EffectiveMouseSensitivity));
	NewControlRotation.Pitch = FMath::ClampAngle(NewControlRotation.Pitch - (MouseDeltaY * EffectiveMouseSensitivity), -85.0f, 85.0f);
	SetControlRotation(NewControlRotation);
}

