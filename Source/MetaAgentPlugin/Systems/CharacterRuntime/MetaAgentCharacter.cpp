// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Characters/MetaAgentCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "Engine/Engine.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "Systems/NetworkingRuntime/MetaAgentGameInstance.h"
#include "Core/MetaAgent.h"

namespace
{
	UInputAction* ResolveInputActionWithFallback(
		UInputAction* ExistingAction,
		const TSoftObjectPtr<UInputAction>& SoftReference,
		const TCHAR* LegacyPath,
		const TCHAR* ActionLabel,
		const UObject* Owner)
	{
		if (ExistingAction)
		{
			return ExistingAction;
		}

		if (!SoftReference.IsNull())
		{
			if (UInputAction* LoadedFromSoftRef = SoftReference.LoadSynchronous())
			{
				return LoadedFromSoftRef;
			}

			UE_LOG(LogMetaAgent, Warning,
				TEXT("'%s' failed to load soft input action '%s' for %s."),
				*GetNameSafe(Owner),
				*SoftReference.ToString(),
				ActionLabel);
		}

		if (LegacyPath)
		{
			if (UInputAction* LoadedFromLegacyPath = Cast<UInputAction>(StaticLoadObject(UInputAction::StaticClass(), nullptr, LegacyPath)))
			{
				return LoadedFromLegacyPath;
			}
		}

		UE_LOG(LogMetaAgent, Warning,
			TEXT("'%s' has no valid input action for %s. Configure asset references in Blueprint/Class Defaults."),
			*GetNameSafe(Owner),
			ActionLabel);

		return nullptr;
	}
}

AMetaAgentCharacter::AMetaAgentCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 300.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 350.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AMetaAgentCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!JumpAction)
	{
		JumpAction = ResolveInputActionWithFallback(
			JumpAction,
			JumpActionAsset,
			TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"),
			TEXT("JumpAction"),
			this);
	}

	if (!MoveAction)
	{
		MoveAction = ResolveInputActionWithFallback(
			MoveAction,
			MoveActionAsset,
			TEXT("/Game/Input/Actions/IA_Move.IA_Move"),
			TEXT("MoveAction"),
			this);
	}

	if (!LookAction)
	{
		LookAction = ResolveInputActionWithFallback(
			LookAction,
			LookActionAsset,
			TEXT("/Game/Input/Actions/IA_Look.IA_Look"),
			TEXT("LookAction"),
			this);
	}

	if (!MouseLookAction)
	{
		MouseLookAction = ResolveInputActionWithFallback(
			MouseLookAction,
			MouseLookActionAsset,
			TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"),
			TEXT("MouseLookAction"),
			this);
	}

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		if (JumpAction)
		{
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		}

		// Moving
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMetaAgentCharacter::Move);
		}

		if (MouseLookAction)
		{
			EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMetaAgentCharacter::Look);
		}

		// Looking
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMetaAgentCharacter::Look);
		}

		if (!MoveAction || (!LookAction && !MouseLookAction))
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("'%s' has missing input actions (Move/Look). Check BP defaults or /Game/Input/Actions assets."),
				*GetNameSafe(this));
		}

	}
	else
	{
		UE_LOG(LogMetaAgent, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AMetaAgentCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AMetaAgentCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AMetaAgentCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AMetaAgentCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AMetaAgentCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AMetaAgentCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

void AMetaAgentCharacter::PrintHelloWorld()
{
	const FVector CharacterLocation = GetActorLocation();
	const FString PositionText = FString::Printf(
		TEXT("Character Position: X=%.2f Y=%.2f Z=%.2f"),
		CharacterLocation.X,
		CharacterLocation.Y,
		CharacterLocation.Z);

	UE_LOG(LogMetaAgent, Log, TEXT("Hello World!"));
	UE_LOG(LogMetaAgent, Log, TEXT("%s"), *PositionText);

	if (const UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
	{
		const FString ServerStatus = GI->GetLocalHttpServerStatusText();
		UE_LOG(LogMetaAgent, Log, TEXT("%s"), *ServerStatus);
	}

}

