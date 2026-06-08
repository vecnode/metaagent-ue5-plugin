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
#include "Systems/ParticleRuntime/MetaAgentImagePreviewRuntime.h"
#include "Systems/ParticleRuntime/MetaAgentParticleRuntime.h"
#include "Systems/ParticleRuntime/MetaAgentNiagaraExportHandler.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeBuilder.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/NetworkingRuntime/MetaAgentGameInstance.h"
#include "Gameplay/AI/MetaAgentWanderAIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "EngineUtils.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SVirtualJoystick.h"

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

	bool IsLikelyUnarmedFallbackAnimClass(const UClass* AnimClass)
	{
		if (!AnimClass)
		{
			return false;
		}

		const FString ClassName = AnimClass->GetName();
		const FString ClassPath = AnimClass->GetPathName();
		return ClassName.Contains(TEXT("UNARMED"), ESearchCase::IgnoreCase)
			|| ClassPath.Contains(TEXT("/UNARMED/"), ESearchCase::IgnoreCase)
			|| ClassPath.Contains(TEXT("ABP_UNARMED"), ESearchCase::IgnoreCase)
			|| ClassPath.Contains(TEXT("ABP_Unarmed"), ESearchCase::IgnoreCase);
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

	if (ParticleRuntime)
	{
		ParticleRuntime->TickRuntime(DeltaTime);
	}

	const bool bAggressiveRebindWindow = ParticleCallbackStartupFrameCounter < FMath::Max(0, ParticleCallbackAggressiveRebindFrames);
	if (bAggressiveRebindWindow)
	{
		ParticleCallbackStartupFrameCounter++;
		EnsureParticleExportCallbackBindings(false);
	}
	else
	{
		ParticleCallbackRebindFrameCounter++;
		if (ParticleCallbackRebindFrameCounter >= FMath::Max(1, ParticleCallbackRebindEveryNFrames))
		{
			ParticleCallbackRebindFrameCounter = 0;
			EnsureParticleExportCallbackBindings(false);
		}
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
			const bool bUsingUnarmedFallbackClass = IsLikelyUnarmedFallbackAnimClass(ActiveAnimClass);
			const bool bUsingKnownStaticRiskFallbackClass = bUsingCrowdFallbackClass || bUsingUnarmedFallbackClass;
			const float Speed2D = ControlledCharacter->GetVelocity().Size2D();

			if (!MovementDiagnostics.bAutoFallbackActivated
				&& MovementDiagnostics.bPreferAnimBlueprintLocomotion
				&& MovementDiagnostics.bEnableAutoFallbackOnAnimStall
				&& bUsingKnownStaticRiskFallbackClass)
			{
				if (PrimaryMesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint
					&& PrimaryMesh->GetAnimInstance()
					&& Speed2D >= MovementDiagnostics.AutoFallbackMinSpeed)
				{
					// Some fallback AnimBPs can stay visually static on recovered MetaHuman body meshes.
					// If movement is sustained in AnimBlueprint mode, trigger emergency fallback.
					MovementDiagnostics.MovingWithoutPoseChangeSeconds += DeltaTime;

					if (MovementDiagnostics.MovingWithoutPoseChangeSeconds >= MovementDiagnostics.AutoFallbackStallSeconds)
					{
						MovementDiagnostics.bAutoFallbackActivated = true;
						const TCHAR* FallbackLabel = bUsingCrowdFallbackClass ? TEXT("crowd") : TEXT("unarmed");
						UE_LOG(LogMetaAgent, Warning,
							TEXT("AnimFallback: '%s' detected sustained movement with %s fallback AnimBP (Speed2D=%.2f, Duration=%.2fs). Activating emergency single-node fallback."),
							*GetNameSafe(ControlledCharacter),
							FallbackLabel,
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

			// If a known fallback AnimBP is present but still visually static on this pawn,
			// force a deterministic idle/walk single-node animation from the same content set.
			if (bUseEmergencyFallback && bUsingKnownStaticRiskFallbackClass)
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

	// Environment-only viewer mode: always apply zoom
	if (ControlledPawn && !CinematicCamera.bModeEnabled)
	{
		ApplyMouseWheelZoom(ControlledPawn, DeltaTime);
	}

	UpdateRecordingCaptureStatus();
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

	if (IsLocalPlayerController())
	{
		ParticleRuntime = NewObject<UMetaAgentParticleRuntime>(this, TEXT("MetaAgentParticleRuntime"));
		if (ParticleRuntime)
		{
			ParticleRuntime->InitializeRuntime(this);
			SyncParticlePatternConfigToRuntime();
			BindParticleRuntimeDelegates();
			UE_LOG(LogMetaAgent, Log, TEXT("%s"), *ParticleRuntime->BuildStatusText());
		}

		ParticleExportHandler = NewObject<UMetaAgentNiagaraExportHandler>(this, TEXT("MetaAgentNiagaraExportHandler"));
		if (ParticleExportHandler)
		{
			ParticleExportHandler->Initialize(this);
		}

		ParticleCallbackStartupFrameCounter = 0;
		ParticleCallbackRebindFrameCounter = 0;
		NiagaraExportBoundComponents.Reset();
		EnsureParticleExportCallbackBindings(true);
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

void AMetaAgentPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Recording.bTakeRecordingActive)
	{
		StopAutopilotTakeRecording();
	}

	Super::EndPlay(EndPlayReason);
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
			InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleHelpPanelPressed);
			InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AMetaAgentPlayerController::HandleLoadLatestPngPreviewPressed);
			InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AMetaAgentPlayerController::HandleStartAudioPressed);
			InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AMetaAgentPlayerController::HandleStartImagePressed);
			InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleAutopilotPressed);
			InputComponent->BindKey(EKeys::J, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleRecordingPressed);
			InputComponent->BindKey(EKeys::U, IE_Pressed, this, &AMetaAgentPlayerController::HandleReportRecordingStatusPressed);
			InputComponent->BindKey(EKeys::O, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleCinematicCameraPressed);
			InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AMetaAgentPlayerController::HandleParticlePatternPressed);
			InputComponent->BindKey(EKeys::C, IE_Pressed, this, &AMetaAgentPlayerController::HandleRandomBoxPatternPressed);
			InputComponent->BindKey(EKeys::B, IE_Pressed, this, &AMetaAgentPlayerController::HandleParticlePatternSlowPresetPressed);
			InputComponent->BindKey(EKeys::N, IE_Pressed, this, &AMetaAgentPlayerController::HandleParticlePatternDramaticPresetPressed);
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

void AMetaAgentPlayerController::HandleStartAudioPressed()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UE_LOG(LogMetaAgent, Log, TEXT("H pressed: sending COMMS start audio request to platform."));

	if (UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
	{
		const FString SourceLabel = GIsEditor ? TEXT("unreal-editor") : TEXT("unreal-standalone");
		GI->SendEventToPlatform(TEXT("key_pressed"), TEXT("start audio"), SourceLabel);

		if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
		{
			MetaAgentHUD->AddTransientMessage(TEXT("COMMS: sent 'start audio'"), FColor::Cyan, 2.0f);
		}

		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleStartImagePressed()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UE_LOG(LogMetaAgent, Log, TEXT("G pressed: sending COMMS start image request to platform."));

	if (UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
	{
		const FString SourceLabel = GIsEditor ? TEXT("unreal-editor") : TEXT("unreal-standalone");
		GI->SendEventToPlatform(TEXT("key_pressed"), TEXT("start image"), SourceLabel);

		if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
		{
			MetaAgentHUD->AddTransientMessage(TEXT("COMMS: sent 'start image'"), FColor::Cyan, 2.0f);
		}

		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleLoadLatestPngPreviewPressed()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	const FString PngPath = FMetaAgentImagePreviewRuntime::ResolveDefaultSdxlPngPath();
	if (!FPaths::FileExists(PngPath))
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("F pressed: file not found '%s'"), *PngPath);
		if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
		{
			MetaAgentHUD->AddTransientMessage(TEXT("Preview PNG not found: sdxl_latest.png"), FColor::Yellow, 2.5f);
		}
		return;
	}

	UTexture2D* ImportedTexture = FMetaAgentImagePreviewRuntime::ImportPngTexture(PngPath);
	if (!ImportedTexture)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("F pressed: failed to import png '%s'"), *PngPath);
		if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
		{
			MetaAgentHUD->AddTransientMessage(TEXT("Failed to import sdxl_latest.png"), FColor::Red, 2.5f);
		}
		return;
	}

	SetLatestPngPreviewTexture(ImportedTexture);
	SetLastLoadedPreviewImagePath(PngPath);
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
	PrepareParticlePatternShapeContext();

	UStaticMeshComponent* PreviewMesh = GetExistingPreviewPlaneMesh();
	if (!PreviewMesh && GetWorld())
	{
		PreviewMesh = FMetaAgentImagePreviewRuntime::FindPreviewPlaneMesh(
			GetWorld(),
			ExistingPreviewPlaneActorName,
			ExistingPreviewPlaneComponentName,
			nullptr);
		CacheExistingPreviewPlaneMesh(PreviewMesh);
	}

	if (!PreviewMesh)
	{
		UE_LOG(LogMetaAgent, Warning,
			TEXT("F pressed: no reusable preview plane found. Expected actor name '%s' or component name '%s'."),
			*ExistingPreviewPlaneActorName.ToString(),
			*ExistingPreviewPlaneComponentName.ToString());

		if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
		{
			MetaAgentHUD->AddTransientMessage(
				TEXT("No preview plane named 'Plane' found (image still loaded for particle shape)."),
				FColor::Yellow,
				3.0f);
		}

		if (GUI.bHelpPanelVisible)
		{
			ApplyGUIHelpPanelState();
		}
		return;
	}

	UMaterialInterface* BasePreviewMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Engine/EngineMaterials/Widget3DPassThrough.Widget3DPassThrough"));
	if (!BasePreviewMaterial)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("F pressed: failed loading Widget3D pass-through material for png preview."));
		return;
	}

	UMaterialInstanceDynamic* PreviewMID = Cast<UMaterialInstanceDynamic>(PreviewMesh->GetMaterial(0));
	if (!PreviewMID)
	{
		PreviewMID = UMaterialInstanceDynamic::Create(BasePreviewMaterial, this);
		if (PreviewMID)
		{
			const int32 MaterialSlots = PreviewMesh->GetNumMaterials();
			for (int32 SlotIndex = 0; SlotIndex < MaterialSlots; ++SlotIndex)
			{
				PreviewMesh->SetMaterial(SlotIndex, PreviewMID);
			}
		}
	}

	if (!PreviewMID)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("F pressed: failed creating dynamic material for preview plane."));
		return;
	}

	PreviewMID->SetTextureParameterValue(TEXT("SlateUI"), ImportedTexture);
	PreviewMID->SetTextureParameterValue(TEXT("SpriteTexture"), ImportedTexture);
	PreviewMID->SetTextureParameterValue(TEXT("Texture"), ImportedTexture);
	const FLinearColor PreviewTint(PreviewPlaneBrightness, PreviewPlaneBrightness, PreviewPlaneBrightness, 1.0f);
	PreviewMID->SetVectorParameterValue(TEXT("TintColorAndOpacity"), PreviewTint);
	PreviewMID->SetVectorParameterValue(TEXT("ColorAndOpacity"), PreviewTint);
	PreviewMID->SetVectorParameterValue(TEXT("TintColor"), PreviewTint);
	PreviewMID->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.0f);
	PreviewMID->SetScalarParameterValue(TEXT("EmissiveScale"), PreviewPlaneBrightness);
	PreviewMID->SetScalarParameterValue(TEXT("Brightness"), PreviewPlaneBrightness);

	PreviewMesh->SetHiddenInGame(false);
	PreviewMesh->SetCastShadow(false);
	PreviewMesh->MarkRenderStateDirty();

	UE_LOG(LogMetaAgent, Log, TEXT("F pressed: loaded '%s' onto reusable scene preview plane."), *PngPath);
	if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
	{
		MetaAgentHUD->AddTransientMessage(
			FString::Printf(TEXT("Loaded sdxl_latest.png (%s)."), *GetParticlePatternShapeText()),
			FColor::Green,
			3.0f);
	}

	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}

	if (ParticleRuntime)
	{
		ParticleRuntime->DiscoverNiagaraComponents(false);
		const FString ParticleStatus = ParticleRuntime->BuildStatusText();
		UE_LOG(LogMetaAgent, Log, TEXT("%s"), *ParticleStatus);

		if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
		{
			MetaAgentHUD->AddTransientMessage(ParticleStatus, FColor::Cyan, 3.0f);
		}
	}
}

void AMetaAgentPlayerController::EnsureParticleExportCallbackBindings(const bool bLogBindings)
{
	if (!ParticleExportHandler)
	{
		ParticleExportHandler = NewObject<UMetaAgentNiagaraExportHandler>(this, TEXT("MetaAgentNiagaraExportHandler"));
		if (ParticleExportHandler)
		{
			ParticleExportHandler->Initialize(this);
		}
	}

	if (!ParticleExportHandler || NiagaraExportUserVariableName.IsNone())
	{
		if (bLogBindings)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("ParticleRuntime: export handler or User.Export parameter name is missing."));
		}
		return;
	}

	if (!ParticleRuntime)
	{
		ParticleRuntime = NewObject<UMetaAgentParticleRuntime>(this, TEXT("MetaAgentParticleRuntime"));
		if (ParticleRuntime)
		{
			ParticleRuntime->InitializeRuntime(this);
			SyncParticlePatternConfigToRuntime();
		}
	}

	if (!ParticleRuntime)
	{
		return;
	}

	ParticleRuntime->DiscoverNiagaraComponents(false);

	const TArray<UNiagaraComponent*> TrackedComponents = ParticleRuntime->GetTrackedNiagaraComponents();
	int32 BoundComponentCount = 0;
	int32 ReinitializedComponentCount = 0;

	for (UNiagaraComponent* NiagaraComponent : TrackedComponents)
	{
		if (!NiagaraComponent)
		{
			continue;
		}

		const bool bNeedsReinitialize = !NiagaraExportBoundComponents.Contains(NiagaraComponent);
		NiagaraComponent->SetVariableObject(NiagaraExportUserVariableName, ParticleExportHandler);
		if (bNeedsReinitialize)
		{
			NiagaraComponent->ReinitializeSystem();
			NiagaraExportBoundComponents.Add(NiagaraComponent);
			ReinitializedComponentCount++;
		}

		BoundComponentCount++;

		if (bLogBindings)
		{
			const AActor* OwnerActor = NiagaraComponent->GetOwner();
			const UNiagaraSystem* AssetSystem = NiagaraComponent->GetAsset();
			UE_LOG(LogMetaAgent, Log,
				TEXT("ParticleRuntime: bound C++ handler '%s' on component '%s' actor='%s' system='%s' active=%s"),
				*NiagaraExportUserVariableName.ToString(),
				*NiagaraComponent->GetName(),
				OwnerActor ? *OwnerActor->GetActorNameOrLabel() : TEXT("None"),
				AssetSystem ? *AssetSystem->GetName() : TEXT("None"),
				NiagaraComponent->IsActive() ? TEXT("true") : TEXT("false"));
		}
	}

	if (bLogBindings)
	{
		UE_LOG(LogMetaAgent, Log,
			TEXT("ParticleRuntime: C++ export handler bound on %d tracked Niagara component(s), reinitialized %d."),
			BoundComponentCount,
			ReinitializedComponentCount);
	}
}

void AMetaAgentPlayerController::SubmitNiagaraParticlePositions(
	const TArray<FVector>& ParticlePositions,
	const FName SourceActorName,
	const FName SourceComponentName)
{
	if (!ParticleRuntime)
	{
		ParticleRuntime = NewObject<UMetaAgentParticleRuntime>(this, TEXT("MetaAgentParticleRuntime"));
		if (ParticleRuntime)
		{
			ParticleRuntime->InitializeRuntime(this);
			SyncParticlePatternConfigToRuntime();
		}
	}

	if (!ParticleRuntime)
	{
		return;
	}

	ParticleRuntime->SubmitExportedParticlePositions(ParticlePositions, SourceActorName, SourceComponentName);

	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::RefreshParticleRuntimeTracking()
{
	if (!ParticleRuntime)
	{
		ParticleRuntime = NewObject<UMetaAgentParticleRuntime>(this, TEXT("MetaAgentParticleRuntime"));
		if (ParticleRuntime)
		{
			ParticleRuntime->InitializeRuntime(this);
			SyncParticlePatternConfigToRuntime();
		}
	}

	if (!ParticleRuntime)
	{
		return;
	}

	ParticleRuntime->DiscoverNiagaraComponents(true);
	const FString ParticleStatus = ParticleRuntime->BuildStatusText();
	UE_LOG(LogMetaAgent, Log, TEXT("%s"), *ParticleStatus);

	if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
	{
		MetaAgentHUD->AddTransientMessage(ParticleStatus, FColor::Cyan, 3.0f);
	}
}

void AMetaAgentPlayerController::SetParticleSteeringTarget(const FVector TargetLocation, const float Strength)
{
	if (!ParticleRuntime)
	{
		ParticleRuntime = NewObject<UMetaAgentParticleRuntime>(this, TEXT("MetaAgentParticleRuntime"));
		if (ParticleRuntime)
		{
			ParticleRuntime->InitializeRuntime(this);
			SyncParticlePatternConfigToRuntime();
		}
	}

	if (!ParticleRuntime)
	{
		return;
	}

	ParticleRuntime->SetSteeringTarget(TargetLocation, Strength);
}

void AMetaAgentPlayerController::ClearParticleSteeringTarget()
{
	if (!ParticleRuntime)
	{
		return;
	}

	ParticleRuntime->ClearSteeringTarget();
}

TArray<FVector> AMetaAgentPlayerController::GetParticleSteeringDirections() const
{
	if (!ParticleRuntime)
	{
		return TArray<FVector>();
	}

	return ParticleRuntime->GetSuggestedSteeringDirections();
}

bool AMetaAgentPlayerController::IsParticleCaptureActive() const
{
	return ParticleRuntime && ParticleRuntime->HasKnownParticleData();
}

int32 AMetaAgentPlayerController::GetCapturedParticleCount() const
{
	if (!ParticleRuntime)
	{
		return 0;
	}

	return ParticleRuntime->GetKnownParticleCount();
}

bool AMetaAgentPlayerController::HasReceivedParticleCallback() const
{
	return ParticleRuntime && ParticleRuntime->HasReceivedAnyCallback();
}

bool AMetaAgentPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

