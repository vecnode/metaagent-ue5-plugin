// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Core/MetaAgent.h"
#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"

namespace MetaAgentInputFallbackInternal
{
	static UInputMappingContext* ResolveMappingContextWithFallback(
		const TSoftObjectPtr<UInputMappingContext>& SoftReference,
		const TCHAR* LegacyPath,
		const TCHAR* ContextLabel,
		const UObject* WorldContext)
	{
		if (!SoftReference.IsNull())
		{
			if (UInputMappingContext* LoadedFromSoftRef = SoftReference.LoadSynchronous())
			{
				return LoadedFromSoftRef;
			}
		}

		if (LegacyPath && LegacyPath[0] != TEXT('\0'))
		{
			if (UInputMappingContext* LoadedFromLegacyPath = Cast<UInputMappingContext>(
				StaticLoadObject(UInputMappingContext::StaticClass(), nullptr, LegacyPath)))
			{
				return LoadedFromLegacyPath;
			}
		}

		return nullptr;
	}
}

void AMetaAgentPlayerController::EnsureEnhancedInputMappingContexts()
{
	if (!IsLocalPlayerController() || ShouldUseTouchControls())
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Subsystem)
	{
		return;
	}

	if (DefaultMappingContexts.Num() == 0)
	{
		if (UInputMappingContext* DefaultContext = MetaAgentInputFallbackInternal::ResolveMappingContextWithFallback(
			DefaultMappingContextAsset,
			TEXT("/Game/Input/IMC_Default.IMC_Default"),
			TEXT("DefaultMappingContext"),
			this))
		{
			DefaultMappingContexts.Add(DefaultContext);
		}
	}

	if (MobileExcludedMappingContexts.Num() == 0)
	{
		if (UInputMappingContext* MouseContext = MetaAgentInputFallbackInternal::ResolveMappingContextWithFallback(
			MouseLookMappingContextAsset,
			TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"),
			TEXT("MouseLookMappingContext"),
			this))
		{
			MobileExcludedMappingContexts.Add(MouseContext);
		}
	}

	InputFallback.bAddedAnyMappingContext = false;

	for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
	{
		if (CurrentContext)
		{
			Subsystem->AddMappingContext(CurrentContext, 0);
			InputFallback.bAddedAnyMappingContext = true;
		}
	}

	for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
	{
		if (CurrentContext)
		{
			Subsystem->AddMappingContext(CurrentContext, 0);
			InputFallback.bAddedAnyMappingContext = true;
		}
	}
}

void AMetaAgentPlayerController::RemoveEnhancedInputMappingContexts()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
		{
			if (CurrentContext)
			{
				Subsystem->RemoveMappingContext(CurrentContext);
			}
		}

		for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
		{
			if (CurrentContext)
			{
				Subsystem->RemoveMappingContext(CurrentContext);
			}
		}
	}

	InputFallback.bAddedAnyMappingContext = false;
}

void AMetaAgentPlayerController::ApplyCharacterInputRuntimeState()
{
	if (!IsLocalPlayerController() || ShouldUseTouchControls())
	{
		return;
	}

	const bool bCharacterInputAllowed =
		IsModularRuntimeEnabled(EMetaAgentModularRuntime::CharacterInput) && !IsGUIInteractionModeActive();

	if (bCharacterInputAllowed)
	{
		EnsureEnhancedInputMappingContexts();
		SetIgnoreMoveInput(false);
		SetIgnoreLookInput(false);
	}
	else
	{
		RemoveEnhancedInputMappingContexts();
		SetIgnoreMoveInput(true);
		SetIgnoreLookInput(true);
	}
}

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

	if (IsLookInputIgnored())
	{
		SetIgnoreLookInput(false);
	}

	FRotator NewControlRotation = GetControlRotation();
	NewControlRotation.Yaw = FRotator::NormalizeAxis(NewControlRotation.Yaw + (MouseDeltaX * EffectiveMouseSensitivity));
	NewControlRotation.Pitch = FMath::ClampAngle(NewControlRotation.Pitch - (MouseDeltaY * EffectiveMouseSensitivity), -85.0f, 85.0f);
	SetControlRotation(NewControlRotation);
}
