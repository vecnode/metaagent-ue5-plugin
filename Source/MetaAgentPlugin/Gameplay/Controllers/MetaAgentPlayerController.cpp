// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.


#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "AIController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Core/MetaAgent.h"
#include "Systems/CameraRuntime/MetaAgentCameraRuntime.h"
#include "Systems/GUIRuntime/MetaAgentGUIRuntime.h"
#include "Systems/CharacterRuntime/MetaAgentCharacterRuntime.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/NetworkingRuntime/MetaAgentGameInstance.h"
#include "Gameplay/AI/MetaAgentWanderAIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LevelSequence.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelinePrimaryConfig.h"
#include "MoviePipelineQueueEngineSubsystem.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelineSetting.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SVirtualJoystick.h"

#if WITH_EDITOR
#include "Recorder/TakeRecorderSubsystem.h"
#include "TakeRecorderSettings.h"
#endif

namespace
{
	UAnimationAsset* GetEmergencyIdleAsset()
	{
		static UAnimationAsset* IdleAsset = nullptr;
		if (!IdleAsset)
		{
			IdleAsset = Cast<UAnimationAsset>(StaticLoadObject(UAnimationAsset::StaticClass(), nullptr,
				TEXT("/MetaAgentPlugin/Animations/Loco/MTN_N_Idle.MTN_N_Idle")));
		}

		return IdleAsset;
	}

	UAnimationAsset* GetEmergencyWalkAsset()
	{
		static UAnimationAsset* WalkAsset = nullptr;
		if (!WalkAsset)
		{
			WalkAsset = Cast<UAnimationAsset>(StaticLoadObject(UAnimationAsset::StaticClass(), nullptr,
				TEXT("/MetaAgentPlugin/Animations/Loco/MTN_N_Walk_InPlace.MTN_N_Walk_InPlace")));
		}

		return WalkAsset;
	}

	UAnimationAsset* GetEmergencyRunAsset()
	{
		static UAnimationAsset* RunAsset = nullptr;
		static bool bTriedLoad = false;
		if (!bTriedLoad)
		{
			bTriedLoad = true;

			// Prefer CitySample crowd locomotion so skeleton compatibility matches MTN assets used here.
			RunAsset = Cast<UAnimationAsset>(StaticLoadObject(UAnimationAsset::StaticClass(), nullptr,
				TEXT("/MetaAgentPlugin/External/CitySampleCrowd/Character/Anims/Loco/MTN_N_WalkQuickly_F.MTN_N_WalkQuickly_F")));

			if (!RunAsset)
			{
			RunAsset = Cast<UAnimationAsset>(StaticLoadObject(UAnimationAsset::StaticClass(), nullptr,
				TEXT("/MetaAgentPlugin/Animations/Loco/MTN_N_Run_InPlace.MTN_N_Run_InPlace")));
			}

			if (!RunAsset)
			{
				// Project fallback used by the stock third-person content.
				RunAsset = Cast<UAnimationAsset>(StaticLoadObject(UAnimationAsset::StaticClass(), nullptr,
					TEXT("/MetaAgentPlugin/External/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.MF_Unarmed_Jog_Fwd")));
			}

			if (!RunAsset)
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("AnimFallback: no emergency run asset could be loaded from plugin-local paths."));
			}
		}

		return RunAsset;
	}

	UInputMappingContext* ResolveMappingContextWithFallback(
		const TSoftObjectPtr<UInputMappingContext>& SoftReference,
		const TCHAR* LegacyPath,
		const TCHAR* ContextLabel,
		const UObject* Owner)
	{
		if (!SoftReference.IsNull())
		{
			if (UInputMappingContext* LoadedFromSoftRef = SoftReference.LoadSynchronous())
			{
				return LoadedFromSoftRef;
			}

			UE_LOG(LogMetaAgent, Warning,
				TEXT("'%s' failed to load soft mapping context '%s' for %s."),
				*GetNameSafe(Owner),
				*SoftReference.ToString(),
				ContextLabel);
		}

		if (LegacyPath)
		{
			if (UInputMappingContext* LoadedFromLegacyPath = Cast<UInputMappingContext>(StaticLoadObject(UInputMappingContext::StaticClass(), nullptr, LegacyPath)))
			{
				return LoadedFromLegacyPath;
			}
		}

		UE_LOG(LogMetaAgent, Warning,
			TEXT("'%s' has no valid mapping context for %s. Configure asset references in Blueprint/Class Defaults."),
			*GetNameSafe(Owner),
			ContextLabel);

		return nullptr;
	}
}

void AMetaAgentPlayerController::HandleToggleCameraModePressed()
{
	FMetaAgentCameraRuntime::RunPlayableCameraModeCycleSequence(*this, GetPawn(), CameraMode, CameraZoom, CinematicCamera);
}

void AMetaAgentPlayerController::EnableFirstPersonCameraMode()
{
	FMetaAgentCameraRuntime::RunEnableSpecificCameraModeSequence(*this, GetPawn(), CameraMode, CameraZoom, EMetaAgentCameraMode::FirstPerson);
}

void AMetaAgentPlayerController::EnableCloseOverShoulderCameraMode()
{
	FMetaAgentCameraRuntime::RunEnableSpecificCameraModeSequence(*this, GetPawn(), CameraMode, CameraZoom, EMetaAgentCameraMode::CloseOverShoulder);
}

void AMetaAgentPlayerController::EnableSideCinematicCloseCameraMode()
{
	FMetaAgentCameraRuntime::RunEnableSpecificCameraModeSequence(*this, GetPawn(), CameraMode, CameraZoom, EMetaAgentCameraMode::SideCinematicClose);
}

void AMetaAgentPlayerController::EnableThirdPersonCameraMode()
{
	FMetaAgentCameraRuntime::RunEnableSpecificCameraModeSequence(*this, GetPawn(), CameraMode, CameraZoom, EMetaAgentCameraMode::ThirdPerson);
}

void AMetaAgentPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (!InPawn)
	{
		return;
	}

	ResetMovementDiagnosticsState();
	ApplyCameraModeToPawn(InPawn);

	if (ACharacter* PossessedCharacter = Cast<ACharacter>(InPawn))
	{
		FMetaAgentCharacterRuntime::RunPossessedCharacterBootstrapSequence(
			PossessedCharacter,
			MovementDiagnostics,
			InputFallback);
	}

	ApplyCameraModeToPawn(InPawn);

	UE_LOG(LogMetaAgent, Log,
		TEXT("AMetaAgentPlayerController: Possessed '%s' (%s)."),
		*GetNameSafe(InPawn),
		*InPawn->GetClass()->GetName());
}

void AMetaAgentPlayerController::ResetMovementDiagnosticsState()
{
	MovementDiagnostics.bLoggedMovementAnimDiagnostics = false;
	MovementDiagnostics.bMovementProbeActive = false;
	MovementDiagnostics.ProbeElapsedSeconds = 0.0f;
	MovementDiagnostics.ProbePeakSpeed2D = 0.0f;
	MovementDiagnostics.ProbePeakAcceleration2D = 0.0f;
	MovementDiagnostics.ProbeSampleCount = 0;
	MovementDiagnostics.bAutoFallbackActivated = false;
	MovementDiagnostics.MovingWithoutPoseChangeSeconds = 0.0f;
	MovementDiagnostics.LastProbeBoneLocation = FVector::ZeroVector;
	MovementDiagnostics.bHasLastProbeBoneLocation = false;
	MovementDiagnostics.bEmergencyWalkActive = false;
	MovementDiagnostics.EmergencyWalkBlendAlpha = 0.0f;
}

void AMetaAgentPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (!IsLocalPlayerController())
	{
		return;
	}

	if (CinematicCamera.bModeEnabled)
	{
		UpdateCinematicCamera(DeltaTime);
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn && !CinematicCamera.bModeEnabled)
	{
		return;
	}

	ApplyFallbackMovementInput(ControlledPawn);
	ApplyFallbackLookInput();
	LogMovementAnimationDiagnostics(ControlledPawn);
	UpdateMovementProbe(ControlledPawn, DeltaTime);

	if (ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn))
	{
		if (USkeletalMeshComponent* PrimaryMesh = ControlledCharacter->GetMesh())
		{
			UClass* ActiveAnimClass = PrimaryMesh->GetAnimClass();
			const bool bUsingCrowdFallbackClass =
				ActiveAnimClass && ActiveAnimClass->GetName().Contains(TEXT("tal_nrw_animbp"), ESearchCase::IgnoreCase);
			const float Speed2D = ControlledCharacter->GetVelocity().Size2D();

			if (!MovementDiagnostics.bAutoFallbackActivated
				&& MovementDiagnostics.bPreferAnimBlueprintLocomotion
				&& MovementDiagnostics.bEnableAutoFallbackOnAnimStall
				&& bUsingCrowdFallbackClass)
			{
				if (PrimaryMesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint
					&& PrimaryMesh->GetAnimInstance()
					&& Speed2D >= MovementDiagnostics.AutoFallbackMinSpeed)
				{
					// Crowd fallback AnimBP can stay visually static for this recovered MetaHuman setup.
					// If movement is sustained in AnimBlueprint mode, trigger emergency fallback.
					MovementDiagnostics.MovingWithoutPoseChangeSeconds += DeltaTime;

					if (MovementDiagnostics.MovingWithoutPoseChangeSeconds >= MovementDiagnostics.AutoFallbackStallSeconds)
					{
						MovementDiagnostics.bAutoFallbackActivated = true;
						UE_LOG(LogMetaAgent, Warning,
							TEXT("AnimFallback: '%s' detected sustained movement with crowd AnimBP (Speed2D=%.2f, Duration=%.2fs). Activating emergency single-node fallback."),
							*GetNameSafe(ControlledCharacter),
							Speed2D,
							MovementDiagnostics.MovingWithoutPoseChangeSeconds);
					}
				}
				else
				{
					MovementDiagnostics.MovingWithoutPoseChangeSeconds = 0.0f;
					MovementDiagnostics.bHasLastProbeBoneLocation = false;
				}
			}

			const bool bUseEmergencyFallback =
				MovementDiagnostics.bEnableEmergencySingleNodeLocomotion
				|| MovementDiagnostics.bAutoFallbackActivated;

			// If the crowd fallback AnimBP is present but still visually static on this pawn,
			// force a deterministic idle/walk single-node animation from the same content set.
			if (bUseEmergencyFallback && bUsingCrowdFallbackClass)
			{
				UAnimationAsset* IdleAsset = GetEmergencyIdleAsset();
				UAnimationAsset* WalkAsset = GetEmergencyWalkAsset();

				if (IdleAsset && WalkAsset)
				{
					UAnimationAsset* RunAsset = GetEmergencyRunAsset();

					const float WalkEnterSpeed = FMath::Max(0.1f, MovementDiagnostics.EmergencyWalkEnterSpeed);
					const float WalkExitSpeed = FMath::Clamp(
						MovementDiagnostics.EmergencyWalkExitSpeed,
						0.1f,
						WalkEnterSpeed - 0.1f);
					const float RunEnterSpeed = FMath::Max(WalkEnterSpeed + 5.0f, MovementDiagnostics.EmergencyRunEnterSpeed);
					const float RunExitSpeed = FMath::Clamp(
						MovementDiagnostics.EmergencyRunExitSpeed,
						WalkExitSpeed + 5.0f,
						RunEnterSpeed - 5.0f);

					if (RunAsset)
					{
						if (MovementDiagnostics.bEmergencyRunActive)
						{
							if (Speed2D <= RunExitSpeed)
							{
								MovementDiagnostics.bEmergencyRunActive = false;
							}
						}
						else if (Speed2D >= RunEnterSpeed)
						{
							MovementDiagnostics.bEmergencyRunActive = true;
						}
					}
					else
					{
						MovementDiagnostics.bEmergencyRunActive = false;
						MovementDiagnostics.EmergencyRunBlendAlpha = 0.0f;
					}

					if (MovementDiagnostics.bEmergencyRunActive)
					{
						MovementDiagnostics.bEmergencyWalkActive = true;
					}

					if (MovementDiagnostics.bEmergencyWalkActive)
					{
						if (!MovementDiagnostics.bEmergencyRunActive && Speed2D <= WalkExitSpeed)
						{
							MovementDiagnostics.bEmergencyWalkActive = false;
						}
					}
					else if (Speed2D >= WalkEnterSpeed)
					{
						MovementDiagnostics.bEmergencyWalkActive = true;
					}

					const float TargetWalkBlendAlpha = MovementDiagnostics.bEmergencyWalkActive ? 1.0f : 0.0f;
					const float WalkBlendSeconds = MovementDiagnostics.bEmergencyWalkActive
						? FMath::Max(0.01f, MovementDiagnostics.EmergencyWalkBlendInSeconds)
						: FMath::Max(0.01f, MovementDiagnostics.EmergencyWalkBlendOutSeconds);
					MovementDiagnostics.EmergencyWalkBlendAlpha = FMath::FInterpTo(
						MovementDiagnostics.EmergencyWalkBlendAlpha,
						TargetWalkBlendAlpha,
						DeltaTime,
						1.0f / WalkBlendSeconds);
					const float WalkBlendAlpha = FMath::Clamp(MovementDiagnostics.EmergencyWalkBlendAlpha, 0.0f, 1.0f);

					const float TargetRunBlendAlpha = (RunAsset && MovementDiagnostics.bEmergencyRunActive) ? 1.0f : 0.0f;
					const float RunBlendSeconds = (RunAsset && MovementDiagnostics.bEmergencyRunActive)
						? FMath::Max(0.01f, MovementDiagnostics.EmergencyWalkBlendInSeconds * 0.7f)
						: FMath::Max(0.01f, MovementDiagnostics.EmergencyWalkBlendOutSeconds * 0.7f);
					MovementDiagnostics.EmergencyRunBlendAlpha = FMath::FInterpTo(
						MovementDiagnostics.EmergencyRunBlendAlpha,
						TargetRunBlendAlpha,
						DeltaTime,
						1.0f / RunBlendSeconds);
					const float RunBlendAlpha = FMath::Clamp(MovementDiagnostics.EmergencyRunBlendAlpha, 0.0f, 1.0f);

					if (PrimaryMesh->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
					{
						PrimaryMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					}

					UAnimSingleNodeInstance* SingleNodeInstance = PrimaryMesh->GetSingleNodeInstance();
					UAnimationAsset* CurrentAsset = SingleNodeInstance ? SingleNodeInstance->GetAnimationAsset() : nullptr;
					UAnimationAsset* DesiredAsset = CurrentAsset;

					const float IdleSnapSpeed = FMath::Max(0.0f, MovementDiagnostics.EmergencyIdleSnapSpeed);
					const float IdleSnapAlpha = FMath::Min(0.08f, FMath::Max(0.01f, MovementDiagnostics.EmergencyWalkExitAssetAlpha * 0.5f));
					const bool bShouldSnapToIdle =
						!MovementDiagnostics.bEmergencyWalkActive
						&& Speed2D <= IdleSnapSpeed
						&& WalkBlendAlpha <= IdleSnapAlpha;

					if (!DesiredAsset)
					{
						DesiredAsset = bShouldSnapToIdle
							? IdleAsset
							: ((RunAsset && RunBlendAlpha >= 0.5f) ? RunAsset : WalkAsset);
					}
					else if (CurrentAsset == IdleAsset)
					{
						if (!bShouldSnapToIdle && WalkBlendAlpha >= MovementDiagnostics.EmergencyWalkEnterAssetAlpha)
						{
							DesiredAsset = (RunAsset && RunBlendAlpha >= 0.5f) ? RunAsset : WalkAsset;
						}
					}
					else if (CurrentAsset == WalkAsset)
					{
						if (RunAsset && RunBlendAlpha >= 0.7f)
						{
							DesiredAsset = RunAsset;
						}
						else if (bShouldSnapToIdle || WalkBlendAlpha <= FMath::Min(0.02f, MovementDiagnostics.EmergencyWalkExitAssetAlpha))
						{
							DesiredAsset = IdleAsset;
						}
					}
					else if (RunAsset && CurrentAsset == RunAsset)
					{
						if (RunBlendAlpha <= 0.3f)
						{
							DesiredAsset = bShouldSnapToIdle ? IdleAsset : WalkAsset;
						}
					}
					else
					{
						DesiredAsset = bShouldSnapToIdle
							? IdleAsset
							: ((RunAsset && RunBlendAlpha >= 0.5f) ? RunAsset : WalkAsset);
					}

					if (CurrentAsset != DesiredAsset)
					{
						PrimaryMesh->PlayAnimation(DesiredAsset, true);
						SingleNodeInstance = PrimaryMesh->GetSingleNodeInstance();

						if (RunAsset && DesiredAsset == RunAsset)
						{
							static bool bLoggedRunFallbackActivation = false;
							if (!bLoggedRunFallbackActivation)
							{
								bLoggedRunFallbackActivation = true;
								UE_LOG(LogMetaAgent, Warning,
									TEXT("AnimFallback: switched to emergency run asset '%s' for '%s'."),
									*GetNameSafe(RunAsset),
									*GetNameSafe(ControlledCharacter));
							}
						}
					}

					if (SingleNodeInstance)
					{
						const float AuthoredWalkSpeed = FMath::Max(1.0f, MovementDiagnostics.EmergencySingleNodeAuthoredWalkSpeed);
						const float AuthoredRunSpeed = FMath::Max(1.0f, MovementDiagnostics.EmergencySingleNodeAuthoredRunSpeed);

						float DesiredPlayRate = 1.0f;
						if (DesiredAsset == WalkAsset)
						{
							const float RawWalkPlayRate = FMath::Clamp(Speed2D / AuthoredWalkSpeed, 0.60f, 2.20f);
							DesiredPlayRate = FMath::Lerp(
								FMath::Clamp(MovementDiagnostics.EmergencyWalkStartPlayRate, 0.1f, 2.0f),
								RawWalkPlayRate,
								WalkBlendAlpha);
						}
						else if (RunAsset && DesiredAsset == RunAsset)
						{
							const float RawWalkPlayRate = FMath::Clamp(Speed2D / AuthoredWalkSpeed, 0.60f, 2.20f);
							const float RawRunPlayRate = FMath::Clamp(Speed2D / AuthoredRunSpeed, 0.70f, 2.80f);
							DesiredPlayRate = FMath::Lerp(RawWalkPlayRate, RawRunPlayRate, RunBlendAlpha);
						}

						SingleNodeInstance->SetPlayRate(FMath::Clamp(DesiredPlayRate, 0.1f, 3.0f));
					}
				}
			}
		}
	}

	if (ControlledPawn && !CinematicCamera.bModeEnabled && CameraMode.ActiveMode == EMetaAgentCameraMode::ThirdPerson)
	{
		ApplyMouseWheelZoom(ControlledPawn, DeltaTime);
	}

	if (Recording.bRenderInProgress && GEngine)
	{
		if (UMoviePipelineQueueEngineSubsystem* RenderSubsystem = GEngine->GetEngineSubsystem<UMoviePipelineQueueEngineSubsystem>())
		{
			if (UMoviePipelineExecutorBase* ActiveExecutor = RenderSubsystem->GetActiveExecutor())
			{
				const float RenderProgress = FMath::Clamp(ActiveExecutor->GetStatusProgress(), 0.0f, 1.0f);
				Recording.RenderStatusText = FString::Printf(TEXT("Rendering: %.0f%%"), RenderProgress * 100.0f);
				Recording.RenderStatusColor = FColor::Yellow;
				UpdateRecordingStatusHud();
			}
		}
	}
}

void AMetaAgentPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (IsLocalPlayerController() && !ShouldUseTouchControls())
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
		bShowMouseCursor = false;
		SetIgnoreLookInput(false);
		SetIgnoreMoveInput(false);
	}

	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogMetaAgent, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}

	UpdateRecordingStatusHud();
	ApplyGUIHelpPanelState();
}

void AMetaAgentPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (InputComponent)
	{
		if (!InputFallback.bUtilityKeysBound)
		{
			InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AMetaAgentPlayerController::HandleEscapePressed);
			InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleHelpPanelPressed);
			InputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AMetaAgentPlayerController::HandleYPressed);
			InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleCameraModePressed);
			InputComponent->BindKey(EKeys::J, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleAutopilotPressed);
			InputComponent->BindKey(EKeys::U, IE_Pressed, this, &AMetaAgentPlayerController::HandleRenderRecordedTakePressed);
			InputComponent->BindKey(EKeys::O, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleCinematicCameraPressed);
			InputFallback.bUtilityKeysBound = true;
		}
	}

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		InputFallback.bAddedAnyMappingContext = false;

		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			if (DefaultMappingContexts.Num() == 0)
			{
				if (UInputMappingContext* DefaultContext = ResolveMappingContextWithFallback(
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
				if (UInputMappingContext* MouseContext = ResolveMappingContextWithFallback(
					MouseLookMappingContextAsset,
					TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"),
					TEXT("MouseLookMappingContext"),
					this))
				{
					MobileExcludedMappingContexts.Add(MouseContext);
				}
			}

			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				if (CurrentContext)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
					InputFallback.bAddedAnyMappingContext = true;
				}
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					if (CurrentContext)
					{
						Subsystem->AddMappingContext(CurrentContext, 0);
						InputFallback.bAddedAnyMappingContext = true;
					}
				}
			}

			if (!InputFallback.bAddedAnyMappingContext)
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("AMetaAgentPlayerController: No input mapping contexts were added. Raw keyboard/mouse fallback remains active."));
			}
		}
		else
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("AMetaAgentPlayerController: EnhancedInputLocalPlayerSubsystem unavailable. Raw keyboard/mouse fallback remains active."));
		}
	}
}

void AMetaAgentPlayerController::HandleEscapePressed()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void AMetaAgentPlayerController::HandleYPressed()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UE_LOG(LogMetaAgent, Log, TEXT("Y pressed: requesting platform agent toggle."));

	if (UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
	{
		const FString SourceLabel = GIsEditor ? TEXT("unreal-editor") : TEXT("unreal-standalone");
		GI->SendEventToPlatform(TEXT("key_pressed"), TEXT("toggle_agent"), SourceLabel);
	}
}

bool AMetaAgentPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

