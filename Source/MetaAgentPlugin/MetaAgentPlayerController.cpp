// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "AIController.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MetaAgentPlugin.h"
#include "MetaAgentPlayerController.h"
#include "Host/MetaAgentHostSession.h"
#include "Host/MetaAgentInputBridge.h"
#include "metaagent/app/commands.hpp"
#include "MetaAgentGameplay.h"
#include "MetaAgentTypeBridge.h"
#include "metaagent/camera/controller.hpp"
#include "metaagent/camera/types.hpp"
#include "metaagent/input/policy.hpp"
#include "metaagent/particle/effect_catalog.hpp"
#include "metaagent/particle/state_effects.hpp"
#include "Engine/GameViewportClient.h"
#include "MetaAgentHUD.h"
#include "MetaAgentParticleShapes.h"
#include "MetaAgentParticleControl.h"
#include "MetaAgentParticleTypes.h"
#include "MetaAgentParticleControl.h"
#include "MetaAgentParticleRuntime.h"
#include "MetaAgentPlugin.h"
#include "Misc/Paths.h"
#include "MovieSceneCapture.h"
#include "MovieSceneCaptureModule.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Protocols/AudioCaptureProtocol.h"
#include "Protocols/VideoCaptureProtocol.h"
#include "Slate/SceneViewport.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SVirtualJoystick.h"

// ===== MetaAgentPlayerController.cpp =====
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

	TickStartupParticleFocusLock();
	EnforceObservationInputLock();

	if (GUI.bHelpPanelVisible && !ShouldUseTouchControls() && WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		HandleGUIPanelMousePressed();
	}

	if (ParticleOrchestrator && IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
	{
		ParticleOrchestrator->TickOrchestrator(DeltaTime);
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

	if (IsLocalPlayerController() && !IsGUIInteractionModeActive())
	{
		if (CinematicCamera.bModeEnabled)
		{
			ApplyCinematicMouseWheelZoom();
		}
		else if (ControlledPawn)
		{
			ApplyMouseWheelZoom(ControlledPawn, DeltaTime);
		}
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

	if (IsLocalPlayerController())
	{
		EnsureParticleOrchestrator();
		if (ParticleOrchestrator)
		{
			UE_LOG(LogMetaAgent, Log, TEXT("%s"), *ParticleOrchestrator->BuildRuntimeStatusText());
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
	ApplyInitialModularRuntimeStates();
	ApplyStartupParticleFocusView();
	ApplyGUIHelpPanelState();
	ApplyGUIInteractionInputModeFromPanelState();
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
			InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AMetaAgentPlayerController::HandleStartAudioPressed);
			InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AMetaAgentPlayerController::HandleStartImagePressed);
			InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleAutopilotPressed);
			InputComponent->BindKey(EKeys::J, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleRecordingPressed);
			InputComponent->BindKey(EKeys::U, IE_Pressed, this, &AMetaAgentPlayerController::HandleReportRecordingStatusPressed);
			InputComponent->BindKey(EKeys::O, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleCinematicCameraPressed);
			InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AMetaAgentPlayerController::HandleFocusParticlesCameraPressed);
			InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AMetaAgentPlayerController::HandleCycleCinematicStylePressed);
			EnsureParticleOrchestrator();
			FMetaAgentParticleInputRouter::BindKeyboardInput(this, InputComponent, ParticleOrchestrator);
			InputFallback.bUtilityKeysBound = true;
		}
	}

	if (IsLocalPlayerController())
	{
		ApplyCharacterInputRuntimeState();
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

	if (!CanExecuteAppCommand(metaagent::app::CommandId::StartPlatformAudio))
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

	if (!CanExecuteAppCommand(metaagent::app::CommandId::StartPlatformImage))
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

	EnsureParticleOrchestrator();
	UMetaAgentParticleRuntime* ParticleRuntime = GetParticleRuntime();
	if (!ParticleRuntime)
	{
		return;
	}

	ParticleOrchestrator->DiscoverNiagaraComponents(false);

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
	EnsureParticleOrchestrator();
	if (!ParticleOrchestrator)
	{
		return;
	}

	ParticleOrchestrator->SubmitExportedParticlePositions(ParticlePositions, SourceActorName, SourceComponentName);

	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::RefreshParticleRuntimeTracking()
{
	EnsureParticleOrchestrator();
	if (!ParticleOrchestrator)
	{
		return;
	}

	ParticleOrchestrator->DiscoverNiagaraComponents(true);
	const FString ParticleStatus = ParticleOrchestrator->BuildRuntimeStatusText();
	UE_LOG(LogMetaAgent, Log, TEXT("%s"), *ParticleStatus);

	if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
	{
		MetaAgentHUD->AddTransientMessage(ParticleStatus, FColor::Cyan, 3.0f);
	}
}

void AMetaAgentPlayerController::SetParticleSteeringTarget(const FVector TargetLocation, const float Strength)
{
	EnsureParticleOrchestrator();
	if (ParticleOrchestrator)
	{
		ParticleOrchestrator->SetSteeringTarget(TargetLocation, Strength);
	}
}

void AMetaAgentPlayerController::ClearParticleSteeringTarget()
{
	if (ParticleOrchestrator)
	{
		ParticleOrchestrator->ClearSteeringTarget();
	}
}

TArray<FVector> AMetaAgentPlayerController::GetParticleSteeringDirections() const
{
	const UMetaAgentParticleRuntime* Runtime = GetParticleRuntime();
	return Runtime ? Runtime->GetSuggestedSteeringDirections() : TArray<FVector>();
}

bool AMetaAgentPlayerController::IsParticleCaptureActive() const
{
	const UMetaAgentParticleRuntime* Runtime = GetParticleRuntime();
	return Runtime && Runtime->HasKnownParticleData();
}

int32 AMetaAgentPlayerController::GetCapturedParticleCount() const
{
	const UMetaAgentParticleRuntime* Runtime = GetParticleRuntime();
	return Runtime ? Runtime->GetKnownParticleCount() : 0;
}

bool AMetaAgentPlayerController::HasReceivedParticleCallback() const
{
	const UMetaAgentParticleRuntime* Runtime = GetParticleRuntime();
	return Runtime && Runtime->HasReceivedAnyCallback();
}

bool AMetaAgentPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}


// ===== MetaAgentPlayerControllerCamera.cpp =====
void AMetaAgentPlayerController::ApplyCameraModeToPawn(APawn* InPawn)
{
	// Simplified for environment-only mode: no-op
	UE_LOG(LogTemp, Log, TEXT("ApplyCameraModeToPawn: environment viewer mode (no pawn camera setup)."));
}

void AMetaAgentPlayerController::ApplyMouseWheelZoom(APawn* ControlledPawn, float DeltaTime)
{
	if (IsGUIInteractionModeActive() || !IsModularRuntimeEnabled(EMetaAgentModularRuntime::Camera))
	{
		return;
	}

	FMetaAgentCameraRuntime::RunEnvironmentZoomSequence(*this, DeltaTime, CameraZoom);
}

void AMetaAgentPlayerController::HandleToggleCinematicCameraPressed()
{
	if (IsGUIInteractionModeActive())
	{
		return;
	}

	if (!CanExecuteAppCommand(metaagent::app::CommandId::ToggleCinematicCamera))
	{
		return;
	}

	ToggleCinematicCameraMode();
}

void AMetaAgentPlayerController::HandleFocusParticlesCameraPressed()
{
	if (IsGUIInteractionModeActive())
	{
		return;
	}

	if (!IsModularRuntimeEnabled(EMetaAgentModularRuntime::Camera))
	{
		SetModularRuntimeEnabled(EMetaAgentModularRuntime::Camera, true);
		ApplyGUIHelpPanelState();
	}

	if (!CanExecuteAppCommand(metaagent::app::CommandId::ToggleFocusParticles))
	{
		return;
	}

	EnsureParticleOrchestrator();

	const bool bWasEnabled = bCinematicFocusParticles;
	bCinematicFocusParticles = !bCinematicFocusParticles;

	if (bCinematicFocusParticles && !bWasEnabled)
	{
		TryLockParticleFocusTarget(true);
	}

	if (!CinematicCamera.bModeEnabled)
	{
		EnableCinematicCameraMode();
	}
	else if (bCinematicFocusParticles && !bWasEnabled)
	{
		ApplyLockedParticleFocusToCinematicCamera();
		FMetaAgentCameraRuntime::RunRefreshCinematicFocus(*this, CinematicCamera);
	}

	int32 FocusCount = 0;
	if (bCinematicFocusParticles)
	{
		if (const UMetaAgentParticleRuntime* Runtime = GetParticleRuntime())
		{
			TArray<FVector> FocusPoints;
			FocusCount = Runtime->GetFocusableWorldPositions(FocusPoints);
		}
	}

	UE_LOG(LogMetaAgent, Log,
		TEXT("Camera: particle focus %s (%d focusable particles). Press O if cinematic mode is off."),
		bCinematicFocusParticles ? TEXT("ENABLED") : TEXT("DISABLED"),
		FocusCount);
}

void AMetaAgentPlayerController::HandleCycleCinematicStylePressed()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (!CanExecuteAppCommand(metaagent::app::CommandId::CycleCinematicStyle))
	{
		return;
	}

	metaagent::camera::CameraController& CoreCamera = MetaAgentTypeBridge::get_camera_controller(*this);
	MetaAgentTypeBridge::sync_cinematic_settings_to_core(CinematicCamera, CoreCamera.cinematic_settings());
	CoreCamera.cinematic_settings().active_style = metaagent::camera::cycle_cinematic_style(
		CoreCamera.cinematic_settings().active_style);

	switch (CoreCamera.cinematic_settings().active_style)
	{
	case metaagent::camera::CinematicStyle::SlowOrbit:
		CinematicCamera.ActiveStyle = EMetaAgentCinematicCameraStyle::SlowOrbit;
		break;
	case metaagent::camera::CinematicStyle::OscillatingHold:
	default:
		CinematicCamera.ActiveStyle = EMetaAgentCinematicCameraStyle::OscillatingHold;
		break;
	}

	if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
	{
		const TCHAR* StyleLabel = FMetaAgentCameraRuntime::GetCinematicStyleLabel(CinematicCamera.ActiveStyle);
		HUD->AddTransientMessage(FString::Printf(TEXT("Cinematic style: %s"), StyleLabel), FColor::Cyan, 2.0f);
	}

	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
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
	FMetaAgentCameraRuntime::RunEnableCinematicCameraSequence(*this, CinematicCamera);
	ApplyCharacterInputRuntimeState();
}

void AMetaAgentPlayerController::ApplyStartupParticleFocusView()
{
	if (!IsLocalPlayerController() || ShouldUseTouchControls())
	{
		return;
	}

	GUI.bCameraRuntimeEnabled = true;
	bCinematicFocusParticles = true;
	EnsureParticleOrchestrator();
	EnableCinematicCameraMode();

	if (TryLockParticleFocusTarget())
	{
		ApplyLockedParticleFocusToCinematicCamera();
		FMetaAgentCameraRuntime::RunRefreshCinematicFocus(*this, CinematicCamera);
	}
	else
	{
		StartupParticleFocusFramesRemaining = 90;
	}

	UE_LOG(LogMetaAgent, Log, TEXT("Startup: cinematic particle observation enabled. Press Q for controls panel."));
}

bool AMetaAgentPlayerController::TryLockParticleFocusTarget(const bool bForceRelock)
{
	if (bHasLockedParticleFocus && !bForceRelock)
	{
		return true;
	}

	if (!bCinematicFocusParticles)
	{
		return false;
	}

	const UMetaAgentParticleRuntime* Runtime = GetParticleRuntime();
	if (!Runtime)
	{
		return false;
	}

	TArray<FVector> FocusPoints;
	if (Runtime->GetFocusableWorldPositions(FocusPoints) <= 0)
	{
		return false;
	}

	const metaagent::camera::FocusTarget Focus = MetaAgentTypeBridge::make_focus_target_from_world_points(
		FocusPoints,
		ParticleObservationPaddingScale);
	LockedParticleFocusPoint = MetaAgentTypeBridge::from_core_vec3(Focus.focus_point);
	LockedParticleFocusOrbitRadius = FMath::Max(
		ParticleObservationMinOrbitRadius,
		Focus.orbit_radius_hint);
	LockedParticleFocusHeightOffset = Focus.height_offset;
	bHasLockedParticleFocus = true;

	UE_LOG(LogMetaAgent, Log,
		TEXT("Camera: locked particle observation focus (orbit=%.1f, height=%.1f, particles=%d)."),
		LockedParticleFocusOrbitRadius,
		LockedParticleFocusHeightOffset,
		FocusPoints.Num());
	return true;
}

void AMetaAgentPlayerController::BuildLockedParticleFocusTarget(
	metaagent::camera::FocusTarget& OutFocus) const
{
	OutFocus.focus_point = MetaAgentTypeBridge::to_core_vec3(LockedParticleFocusPoint);
	OutFocus.orbit_radius_hint = LockedParticleFocusOrbitRadius;
	OutFocus.height_offset = LockedParticleFocusHeightOffset;
}

void AMetaAgentPlayerController::ApplyLockedParticleFocusToCinematicCamera()
{
	if (!bHasLockedParticleFocus || !CinematicCamera.bModeEnabled)
	{
		return;
	}

	CinematicCamera.OrbitRadius = LockedParticleFocusOrbitRadius;
	CinematicCamera.CameraHeightOffset = LockedParticleFocusHeightOffset;

	metaagent::camera::CameraController& CoreCamera = MetaAgentTypeBridge::get_camera_controller(*this);
	MetaAgentTypeBridge::sync_cinematic_runtime_to_core(CinematicCamera, CoreCamera.cinematic_state());
}

void AMetaAgentPlayerController::TickStartupParticleFocusLock()
{
	if (StartupParticleFocusFramesRemaining <= 0 || bHasLockedParticleFocus)
	{
		return;
	}

	StartupParticleFocusFramesRemaining--;
	if (!TryLockParticleFocusTarget())
	{
		return;
	}

	ApplyLockedParticleFocusToCinematicCamera();
	FMetaAgentCameraRuntime::RunRefreshCinematicFocus(*this, CinematicCamera);
	StartupParticleFocusFramesRemaining = 0;
}

void AMetaAgentPlayerController::EnforceObservationInputLock()
{
	if (!IsLocalPlayerController() || ShouldUseTouchControls())
	{
		return;
	}

	const metaagent::input::InputPolicy Policy = metaagent::input::policy_for_runtime(
		IsGUIInteractionModeActive(),
		IsModularRuntimeEnabled(EMetaAgentModularRuntime::CharacterInput),
		CinematicCamera.bModeEnabled);

	if (Policy.allow_gui_clicks)
	{
		return;
	}

	if (Policy.block_move)
	{
		SetIgnoreMoveInput(true);
	}
	if (Policy.block_look)
	{
		SetIgnoreLookInput(true);
	}
	bShowMouseCursor = false;
}

bool AMetaAgentPlayerController::GetViewportCanvasMousePos(float& OutX, float& OutY) const
{
	if (const ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (const UGameViewportClient* ViewportClient = LocalPlayer->ViewportClient)
		{
			FVector2D MousePos = FVector2D::ZeroVector;
			if (ViewportClient->GetMousePosition(MousePos))
			{
				OutX = MousePos.X;
				OutY = MousePos.Y;
				return true;
			}
		}
	}

	return GetMousePosition(OutX, OutY);
}

void AMetaAgentPlayerController::ApplyCinematicMouseWheelZoom()
{
	if (!CinematicCamera.bModeEnabled || IsGUIInteractionModeActive())
	{
		return;
	}

	const metaagent::input::InputPolicy Policy = metaagent::input::policy_for_runtime(
		false,
		IsModularRuntimeEnabled(EMetaAgentModularRuntime::CharacterInput),
		true);
	if (!Policy.allow_mouse_wheel_zoom)
	{
		return;
	}

	metaagent::camera::ZoomInput Input;
	if (WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		Input.discrete_wheel_delta = 1.0f;
	}
	if (WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		Input.discrete_wheel_delta = -1.0f;
	}
	if (FMath::IsNearlyZero(Input.discrete_wheel_delta))
	{
		Input.analog_wheel_axis = GetInputAnalogKeyState(EKeys::MouseWheelAxis);
	}

	metaagent::camera::CameraController& CoreCamera = MetaAgentTypeBridge::get_camera_controller(*this);
	MetaAgentTypeBridge::sync_cinematic_runtime_to_core(CinematicCamera, CoreCamera.cinematic_state());
	metaagent::camera::apply_orbit_radius_zoom(
		CoreCamera.cinematic_state(),
		Input,
		FMath::Max(120.0f, ParticleObservationMinOrbitRadius * 0.35f),
		5000.0f,
		35.0f);
	MetaAgentTypeBridge::sync_cinematic_runtime_from_core(CoreCamera.cinematic_state(), CinematicCamera);

	if (bHasLockedParticleFocus)
	{
		LockedParticleFocusOrbitRadius = CinematicCamera.OrbitRadius;
	}
}

void AMetaAgentPlayerController::DisableCinematicCameraMode()
{
	FMetaAgentCameraRuntime::RunDisableCinematicCameraSequence(*this, CinematicCamera);
}

void AMetaAgentPlayerController::UpdateCinematicCamera(float DeltaTime)
{
	FMetaAgentCameraRuntime::RunUpdateCinematicCameraSequence(*this, DeltaTime, CinematicCamera);
}

// ===== MetaAgentPlayerControllerGUI.cpp =====
void AMetaAgentPlayerController::HandleToggleHelpPanelPressed()
{
	if (!CanExecuteAppCommand(metaagent::app::CommandId::ToggleGuiHelp))
	{
		return;
	}

	FMetaAgentGUIRuntime::RunToggleHelpPanelSequence(*this, GUI);
}

void AMetaAgentPlayerController::ApplyGUIHelpPanelState()
{
	FMetaAgentGUIRuntime::RunApplyHelpPanelSequence(*this, GUI);
}

void AMetaAgentPlayerController::ApplyGUIInteractionInputModeFromPanelState()
{
	if (!IsLocalPlayerController() || ShouldUseTouchControls())
	{
		return;
	}

	if (GUI.bHelpPanelVisible)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
		bShowMouseCursor = true;
		bEnableClickEvents = true;
		bEnableMouseOverEvents = true;
		SetIgnoreLookInput(true);
		SetIgnoreMoveInput(true);
	}
	else
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
		bShowMouseCursor = false;
		bEnableClickEvents = false;
		bEnableMouseOverEvents = false;
		ApplyCharacterInputRuntimeState();
		if (CinematicCamera.bModeEnabled && CinematicCamera.bDisablePlayerInput)
		{
			SetIgnoreMoveInput(true);
			SetIgnoreLookInput(true);
		}
	}
}

void AMetaAgentPlayerController::HandleGUIPanelMousePressed()
{
	if (!GUI.bHelpPanelVisible)
	{
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!GetViewportCanvasMousePos(MouseX, MouseY))
	{
		return;
	}

	AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>();
	if (!MetaAgentHUD)
	{
		return;
	}

	FName ActionId = NAME_None;
	if (MetaAgentHUD->HitTestRuntimePanelAction(MouseX, MouseY, ActionId))
	{
		DispatchGUIAction(ActionId);
	}
}

void AMetaAgentPlayerController::DispatchGUIAction(const FName ActionId)
{
	FMetaAgentGUIRuntime::DispatchPanelAction(*this, GUI, ActionId);
}

// ===== MetaAgentPlayerControllerRuntime.cpp =====
bool AMetaAgentPlayerController::IsModularRuntimeEnabled(const EMetaAgentModularRuntime Runtime) const
{
	switch (Runtime)
	{
	case EMetaAgentModularRuntime::GUI:
		return true;
	case EMetaAgentModularRuntime::CharacterInput:
		return GUI.bCharacterInputRuntimeEnabled;
	case EMetaAgentModularRuntime::Camera:
		return GUI.bCameraRuntimeEnabled;
	case EMetaAgentModularRuntime::AI:
		return GUI.bAIRuntimeEnabled;
	case EMetaAgentModularRuntime::Recording:
		return GUI.bRecordingRuntimeEnabled;
	case EMetaAgentModularRuntime::Networking:
		return GUI.bNetworkingRuntimeEnabled;
	case EMetaAgentModularRuntime::Particle:
		return GUI.bParticleRuntimeEnabled;
	default:
		return true;
	}
}

FMetaAgentHostSessionSnapshot AMetaAgentPlayerController::BuildHostSessionSnapshot() const
{
	return MetaAgentHostSession::MakeFromPlayerController(*this);
}

bool AMetaAgentPlayerController::CanExecuteAppCommand(
	const metaagent::app::CommandId Command,
	FString* OutUserMessage) const
{
	const FMetaAgentInputBridgeResult Result =
		FMetaAgentInputBridge::ValidateCommand(Command, BuildHostSessionSnapshot());
	if (OutUserMessage && !Result.UserMessage.IsEmpty())
	{
		*OutUserMessage = Result.UserMessage;
	}
	return Result.bHandled && Result.bSuccess;
}

bool AMetaAgentPlayerController::IsGUIInteractionModeActive() const
{
	return GUI.bHelpPanelVisible && IsLocalPlayerController() && !ShouldUseTouchControls();
}

void AMetaAgentPlayerController::SetModularRuntimeEnabled(const EMetaAgentModularRuntime Runtime, const bool bEnabled)
{
	if (Runtime == EMetaAgentModularRuntime::GUI)
	{
		return;
	}

	switch (Runtime)
	{
	case EMetaAgentModularRuntime::CharacterInput:
		GUI.bCharacterInputRuntimeEnabled = bEnabled;
		ApplyCharacterInputRuntimeState();
		break;
	case EMetaAgentModularRuntime::Camera:
		GUI.bCameraRuntimeEnabled = bEnabled;
		if (!bEnabled)
		{
			if (CinematicCamera.bModeEnabled)
			{
				DisableCinematicCameraMode();
			}
		}
		break;
	case EMetaAgentModularRuntime::AI:
		GUI.bAIRuntimeEnabled = bEnabled;
		if (!bEnabled && (Autopilot.bEnabled || Autopilot.Controller.IsValid() || !GetPawn()))
		{
			DisableAutopilotAndRepossess();
		}
		break;
	case EMetaAgentModularRuntime::Recording:
		GUI.bRecordingRuntimeEnabled = bEnabled;
		if (!bEnabled && Recording.bTakeRecordingActive)
		{
			StopAutopilotTakeRecording();
		}
		break;
	case EMetaAgentModularRuntime::Networking:
		GUI.bNetworkingRuntimeEnabled = bEnabled;
		if (UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
		{
			if (bEnabled)
			{
				GI->StartNetworkingRuntime();
			}
			else
			{
				GI->StopNetworkingRuntime();
			}
		}
		break;
	case EMetaAgentModularRuntime::Particle:
		GUI.bParticleRuntimeEnabled = bEnabled;
		break;
	default:
		break;
	}

	ApplyGUIHelpPanelState();
}

void AMetaAgentPlayerController::ToggleRuntimeSectionExpanded(const FName RuntimeId)
{
	if (RuntimeId.IsNone())
	{
		return;
	}

	bool& bExpanded = GUI.SectionExpandedStates.FindOrAdd(RuntimeId);
	bExpanded = !bExpanded;
	ApplyGUIHelpPanelState();
}

void AMetaAgentPlayerController::ApplyInitialModularRuntimeStates()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	ApplyCharacterInputRuntimeState();

	if (!GUI.bNetworkingRuntimeEnabled)
	{
		if (UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
		{
			GI->StopNetworkingRuntime();
		}
	}
}

void AMetaAgentPlayerController::ToggleModularRuntime(const EMetaAgentModularRuntime Runtime)
{
	if (Runtime == EMetaAgentModularRuntime::GUI)
	{
		return;
	}

	SetModularRuntimeEnabled(Runtime, !IsModularRuntimeEnabled(Runtime));
}

// ===== MetaAgentPlayerControllerDiagnostics.cpp =====
void AMetaAgentPlayerController::LogMovementAnimationDiagnostics(APawn* ControlledPawn)
{
	if (!ControlledPawn || MovementDiagnostics.bLoggedMovementAnimDiagnostics)
	{
		return;
	}

	const float Speed2D = ControlledPawn->GetVelocity().Size2D();
	if (Speed2D <= 10.0f)
	{
		return;
	}

	if (ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn))
	{
		if (UCharacterMovementComponent* MovementComp = ControlledCharacter->GetCharacterMovement())
		{
			UE_LOG(LogMetaAgent, Log,
				TEXT("MovementDiag: Pawn='%s' MaxWalkSpeed=%.2f MaxAcceleration=%.2f InputPending=%.3f InputLast=%.3f MovementMode=%d"),
				*GetNameSafe(ControlledPawn),
				MovementComp->MaxWalkSpeed,
				MovementComp->MaxAcceleration,
				ControlledCharacter->GetPendingMovementInputVector().Size(),
				ControlledCharacter->GetLastMovementInputVector().Size(),
				static_cast<int32>(MovementComp->MovementMode));

			const UCapsuleComponent* CapsuleComp = ControlledCharacter->GetCapsuleComponent();
			if (CapsuleComp)
			{
				const float CapsuleHalfHeight = CapsuleComp->GetScaledCapsuleHalfHeight();
				const float CapsuleBottomZ = CapsuleComp->GetComponentLocation().Z - CapsuleHalfHeight;

				USkeletalMeshComponent* BodyMesh = nullptr;
				TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes;
				ControlledCharacter->GetComponents(SkeletalMeshes);
				for (USkeletalMeshComponent* MeshComp : SkeletalMeshes)
				{
					if (!MeshComp)
					{
						continue;
					}

					if (MeshComp->GetFName() == TEXT("Body"))
					{
						BodyMesh = MeshComp;
						break;
					}

					if (!BodyMesh && MeshComp->GetAnimInstance())
					{
						BodyMesh = MeshComp;
					}
				}

				if (BodyMesh)
				{
					const FBoxSphereBounds MeshBounds = BodyMesh->Bounds;
					const float MeshFeetWorldZ = MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z;
					const float MeshFeetVsCapsuleBottom = MeshFeetWorldZ - CapsuleBottomZ;

					UE_LOG(LogMetaAgent, Warning,
						TEXT("GroundDiag: Pawn='%s' CapsuleHalfHeight=%.2f CapsuleBottomZ=%.2f BodyRelZ=%.2f BodyWorldZ=%.2f MeshFeetWorldZ=%.2f MeshFeetVsCapsuleBottom=%.2f FloorDist=%.2f LineDist=%.2f bWalkable=%s"),
						*GetNameSafe(ControlledCharacter),
						CapsuleHalfHeight,
						CapsuleBottomZ,
						BodyMesh->GetRelativeLocation().Z,
						BodyMesh->GetComponentLocation().Z,
						MeshFeetWorldZ,
						MeshFeetVsCapsuleBottom,
						MovementComp->CurrentFloor.FloorDist,
						MovementComp->CurrentFloor.LineDist,
						MovementComp->CurrentFloor.IsWalkableFloor() ? TEXT("true") : TEXT("false"));
				}
			}
		}
	}

	UE_LOG(LogMetaAgent, Log,
		TEXT("AnimDiag: Pawn='%s' Speed2D=%.2f"),
		*GetNameSafe(ControlledPawn),
		Speed2D);

	TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes;
	ControlledPawn->GetComponents(SkeletalMeshes);

	for (int32 MeshIndex = 0; MeshIndex < SkeletalMeshes.Num(); ++MeshIndex)
	{
		USkeletalMeshComponent* MeshComp = SkeletalMeshes[MeshIndex];
		if (!MeshComp)
		{
			continue;
		}

		const TCHAR* AnimationModeLabel = TEXT("Unknown");
		switch (MeshComp->GetAnimationMode())
		{
		case EAnimationMode::AnimationBlueprint:
			AnimationModeLabel = TEXT("AnimationBlueprint");
			break;
		case EAnimationMode::AnimationSingleNode:
			AnimationModeLabel = TEXT("AnimationSingleNode");
			break;
		case EAnimationMode::AnimationCustomMode:
			AnimationModeLabel = TEXT("AnimationCustomMode");
			break;
		default:
			break;
		}

		UE_LOG(LogMetaAgent, Log,
			TEXT("AnimDiagMesh[%d]: Comp='%s' Visible=%s Mesh='%s' Skeleton='%s' AnimMode=%s AnimClass='%s' AnimInstance='%s'"),
			MeshIndex,
			*GetNameSafe(MeshComp),
			MeshComp->IsVisible() ? TEXT("true") : TEXT("false"),
			*GetNameSafe(MeshComp->GetSkeletalMeshAsset()),
			(MeshComp->GetSkeletalMeshAsset() && MeshComp->GetSkeletalMeshAsset()->GetSkeleton()) ? *GetNameSafe(MeshComp->GetSkeletalMeshAsset()->GetSkeleton()) : TEXT("none"),
			AnimationModeLabel,
			*GetNameSafe(MeshComp->GetAnimClass()),
			MeshComp->GetAnimInstance() ? *MeshComp->GetAnimInstance()->GetClass()->GetName() : TEXT("none"));
	}

	MovementDiagnostics.bLoggedMovementAnimDiagnostics = true;
}

void AMetaAgentPlayerController::UpdateMovementProbe(APawn* ControlledPawn, float DeltaTime)
{
	ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn);
	if (!ControlledCharacter)
	{
		return;
	}

	UCharacterMovementComponent* MovementComp = ControlledCharacter->GetCharacterMovement();
	if (!MovementComp)
	{
		return;
	}

	const float PendingInput = ControlledCharacter->GetPendingMovementInputVector().Size();
	const float LastInput = ControlledCharacter->GetLastMovementInputVector().Size();
	const float InputMagnitude = FMath::Max(PendingInput, LastInput);

	if (!MovementDiagnostics.bMovementProbeActive && InputMagnitude > 0.9f)
	{
		MovementDiagnostics.bMovementProbeActive = true;
		MovementDiagnostics.ProbeElapsedSeconds = 0.0f;
		MovementDiagnostics.ProbePeakSpeed2D = 0.0f;
		MovementDiagnostics.ProbePeakAcceleration2D = 0.0f;
		MovementDiagnostics.ProbeSampleCount = 0;
	}

	if (!MovementDiagnostics.bMovementProbeActive)
	{
		return;
	}

	const float Speed2D = ControlledPawn->GetVelocity().Size2D();
	const float Accel2D = MovementComp->GetCurrentAcceleration().Size2D();
	MovementDiagnostics.ProbePeakSpeed2D = FMath::Max(MovementDiagnostics.ProbePeakSpeed2D, Speed2D);
	MovementDiagnostics.ProbePeakAcceleration2D = FMath::Max(MovementDiagnostics.ProbePeakAcceleration2D, Accel2D);
	MovementDiagnostics.ProbeElapsedSeconds += DeltaTime;
	++MovementDiagnostics.ProbeSampleCount;

	const bool bStopWindow = (MovementDiagnostics.ProbeElapsedSeconds >= 2.0f) || (InputMagnitude < 0.1f);
	if (!bStopWindow)
	{
		return;
	}

	UE_LOG(LogMetaAgent, Warning,
		TEXT("MovementProbe: Pawn='%s' Duration=%.2fs Samples=%d Input=%.3f SpeedNow=%.2f SpeedPeak=%.2f MaxWalkSpeed=%.2f MaxSpeed=%.2f AccelPeak=%.2f MaxAcceleration=%.2f BrakingDecelWalk=%.2f GroundFriction=%.2f BrakingFriction=%.2f BrakingFrictionFactor=%.2f MovementMode=%d"),
		*GetNameSafe(ControlledPawn),
		MovementDiagnostics.ProbeElapsedSeconds,
		MovementDiagnostics.ProbeSampleCount,
		InputMagnitude,
		Speed2D,
		MovementDiagnostics.ProbePeakSpeed2D,
		MovementComp->MaxWalkSpeed,
		MovementComp->GetMaxSpeed(),
		MovementDiagnostics.ProbePeakAcceleration2D,
		MovementComp->GetMaxAcceleration(),
		MovementComp->BrakingDecelerationWalking,
		MovementComp->GroundFriction,
		MovementComp->BrakingFriction,
		MovementComp->BrakingFrictionFactor,
		static_cast<int32>(MovementComp->MovementMode));

	MovementDiagnostics.bMovementProbeActive = false;
}


// ===== MetaAgentPlayerControllerInputFallback.cpp =====
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

	if (!IsGUIInteractionModeActive())
	{
		RemoveEnhancedInputMappingContexts();
		SetIgnoreMoveInput(true);
		SetIgnoreLookInput(true);
		return;
	}

	const bool bCharacterInputAllowed = IsModularRuntimeEnabled(EMetaAgentModularRuntime::CharacterInput);
	if (bCharacterInputAllowed)
	{
		EnsureEnhancedInputMappingContexts();
		SetIgnoreMoveInput(true);
		SetIgnoreLookInput(true);
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
		|| !IsGUIInteractionModeActive()
		|| !InputFallback.bEnableKeyboardMovement
		|| InputFallback.bAddedAnyMappingContext
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
	// Mouse is reserved for GUI panel hit-testing only.
}

// ===== MetaAgentPlayerControllerAutopilot.cpp =====
namespace
{
	void ResetAutopilotRuntimeState(FMetaAgentAutopilotState& Autopilot)
	{
		Autopilot.bEnabled = false;
		Autopilot.Pawn.Reset();
		Autopilot.Controller.Reset();
	}
}

void AMetaAgentPlayerController::ToggleAutopilotFromGUI()
{
	if (!IsMetaAgentRuntimeActive() || !IsModularRuntimeEnabled(EMetaAgentModularRuntime::AI) || !IsLocalPlayerController())
	{
		return;
	}

	if (Autopilot.bEnabled || Autopilot.Controller.IsValid() || !GetPawn())
	{
		DisableAutopilotAndRepossess();
	}
	else
	{
		EnableAutopilotForCurrentPawn();
	}

	ApplyGUIHelpPanelState();
}

void AMetaAgentPlayerController::ToggleRecordingFromGUI()
{
	if (!IsMetaAgentRuntimeActive() || !IsModularRuntimeEnabled(EMetaAgentModularRuntime::Recording) || !IsLocalPlayerController())
	{
		return;
	}

	if (Recording.bTakeRecordingActive)
	{
		StopAutopilotTakeRecording();
	}
	else
	{
		StartAutopilotTakeRecording();
	}

	ApplyGUIHelpPanelState();
}

void AMetaAgentPlayerController::ReportRecordingStatusFromGUI()
{
	if (!IsMetaAgentRuntimeActive() || !IsModularRuntimeEnabled(EMetaAgentModularRuntime::Recording))
	{
		return;
	}

	ReportRuntimeCaptureStatus();
	ApplyGUIHelpPanelState();
}

metaagent::runtime::RecordingSnapshot AMetaAgentPlayerController::BuildRecordingHostSnapshot() const
{
	metaagent::runtime::RecordingSnapshot Snapshot;
	Snapshot.runtime_enabled = IsModularRuntimeEnabled(EMetaAgentModularRuntime::Recording);
	Snapshot.capture_active = Recording.bTakeRecordingActive;
	const FTCHARToUTF8 OutputPathConverter(*Recording.RuntimeCaptureOutputDirectory);
	Snapshot.last_output_path = std::string(
		OutputPathConverter.Get(),
		static_cast<size_t>(OutputPathConverter.Length()));
	const FTCHARToUTF8 StatusConverter(*GUI.RecordingStatusLine);
	Snapshot.status_text = std::string(
		StatusConverter.Get(),
		static_cast<size_t>(StatusConverter.Length()));
	return Snapshot;
}

metaagent::runtime::AiSnapshot AMetaAgentPlayerController::BuildAiHostSnapshot() const
{
	metaagent::runtime::AiSnapshot Snapshot;
	Snapshot.runtime_enabled = IsModularRuntimeEnabled(EMetaAgentModularRuntime::AI);
	Snapshot.autopilot_enabled = Autopilot.bEnabled;
	Snapshot.status_text = Autopilot.bEnabled ? "Autopilot: ON" : "Autopilot: OFF";
	return Snapshot;
}

void AMetaAgentPlayerController::HandleToggleAutopilotPressed()
{
	if (!IsMetaAgentRuntimeActive() || !IsModularRuntimeEnabled(EMetaAgentModularRuntime::AI))
	{
		return;
	}

	if (!IsLocalPlayerController())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float CurrentTimeSeconds = World->GetTimeSeconds();
	const float EffectiveDebounceSeconds = FMath::Max(0.0f, Autopilot.ToggleDebounceSeconds);
	if ((CurrentTimeSeconds - Autopilot.LastToggleTimeSeconds) < EffectiveDebounceSeconds)
	{
		return;
	}

	Autopilot.LastToggleTimeSeconds = CurrentTimeSeconds;

	if (Autopilot.bEnabled)
	{
		DisableAutopilotAndRepossess();
	}
	else
	{
		EnableAutopilotForCurrentPawn();
	}
}

void AMetaAgentPlayerController::EnableAutopilotForCurrentPawn()
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (Autopilot.bEnabled)
	{
		UE_LOG(LogMetaAgent, Verbose, TEXT("Autopilot: enable requested while already enabled."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("Autopilot: world is null; cannot enable."));
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!IsValid(ControlledPawn))
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("Autopilot: no currently possessed pawn to hand to AI."));
		return;
	}

	if (ControlledPawn->GetController() != this)
	{
		UE_LOG(LogMetaAgent, Warning,
			TEXT("Autopilot: current pawn '%s' is not controlled by this player controller."),
			*GetNameSafe(ControlledPawn));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UClass* DesiredAIControllerClass = Autopilot.AIControllerClass.Get();
	if (!DesiredAIControllerClass || !DesiredAIControllerClass->IsChildOf(AAIController::StaticClass()))
	{
		if (DesiredAIControllerClass)
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("Autopilot: configured class '%s' is not an AAIController; falling back to AMetaAgentWanderAIController."),
				*GetNameSafe(DesiredAIControllerClass));
		}
		DesiredAIControllerClass = AMetaAgentWanderAIController::StaticClass();
	}

	AAIController* AIController = World->SpawnActor<AAIController>(
		DesiredAIControllerClass,
		ControlledPawn->GetActorLocation(),
		ControlledPawn->GetActorRotation(),
		SpawnParams);

	if (!AIController)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("Autopilot: failed to spawn AI controller '%s'."), *GetNameSafe(DesiredAIControllerClass));
		return;
	}

	UnPossess();
	AIController->Possess(ControlledPawn);
	if (AIController->GetPawn() != ControlledPawn)
	{
		UE_LOG(LogMetaAgent, Error,
			TEXT("Autopilot: AI controller '%s' failed to possess pawn '%s'. Restoring player control."),
			*GetNameSafe(AIController),
			*GetNameSafe(ControlledPawn));

		AIController->Destroy();
		Possess(ControlledPawn);
		SetViewTargetWithBlend(ControlledPawn, 0.0f);
		ResetAutopilotRuntimeState(Autopilot);
		return;
	}

	Autopilot.Pawn = ControlledPawn;
	Autopilot.Controller = AIController;
	Autopilot.bEnabled = true;
	SetViewTargetWithBlend(ControlledPawn, 0.0f);

	UE_LOG(LogMetaAgent, Log, TEXT("Autopilot: ENABLED for pawn '%s'. Press I again to return to player control."), *GetNameSafe(ControlledPawn));

	EnableCinematicCameraMode();
}

void AMetaAgentPlayerController::DisableAutopilotAndRepossess()
{
	if (!IsMetaAgentRuntimeActive())
	{
		ResetAutopilotRuntimeState(Autopilot);
		return;
	}

	AAIController* CachedAIController = Autopilot.Controller.Get();
	APawn* PawnToRepossess = Autopilot.Pawn.Get();
	if (!PawnToRepossess && CachedAIController)
	{
		PawnToRepossess = CachedAIController->GetPawn();
	}

	if (!PawnToRepossess)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("Autopilot: no cached pawn to repossess."));
		ResetAutopilotRuntimeState(Autopilot);
		DisableCinematicCameraMode();
		return;
	}

	AController* PawnController = PawnToRepossess->GetController();
	if (PawnController && PawnController != this)
	{
		PawnController->UnPossess();
		PawnController->Destroy();
	}
	else if (IsValid(CachedAIController))
	{
		CachedAIController->UnPossess();
		CachedAIController->Destroy();
	}

	if (IsValid(PawnToRepossess))
	{
		Possess(PawnToRepossess);
		SetViewTargetWithBlend(PawnToRepossess, 0.0f);
	}
	else
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("Autopilot: cached pawn became invalid before repossession."));
	}

	ResetAutopilotRuntimeState(Autopilot);

	UE_LOG(LogMetaAgent, Log, TEXT("Autopilot: DISABLED. Player control restored."));

	DisableCinematicCameraMode();
}


// ===== MetaAgentPlayerControllerRecording.cpp =====
#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#endif

namespace MetaAgentRecording
{
	static const FName SceneViewportTypeName(TEXT("SceneViewport"));

	static TSharedPtr<FSceneViewport> ToSharedSceneViewport(FSceneViewport* SceneViewport)
	{
		if (!SceneViewport)
		{
			return nullptr;
		}

		if (const UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
		{
			if (GameEngine->SceneViewport.IsValid() && GameEngine->SceneViewport.Get() == SceneViewport)
			{
				return GameEngine->SceneViewport;
			}
		}

		// Viewport lifetime is owned by the active game/PIE session.
		return TSharedPtr<FSceneViewport>(SceneViewport, [](FSceneViewport*) {});
	}

	static FSceneViewport* ResolveRawSceneViewport(const AMetaAgentPlayerController& Controller)
	{
		if (const UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
		{
			if (GameEngine->SceneViewport.IsValid())
			{
				return GameEngine->SceneViewport.Get();
			}
		}

		if (const ULocalPlayer* LocalPlayer = Controller.GetLocalPlayer())
		{
			if (UGameViewportClient* ViewportClient = LocalPlayer->ViewportClient)
			{
				if (FSceneViewport* SceneViewport = ViewportClient->GetGameViewport())
				{
					return SceneViewport;
				}
			}
		}

		if (const UWorld* World = Controller.GetWorld())
		{
			if (UGameViewportClient* ViewportClient = World->GetGameViewport())
			{
				if (FSceneViewport* SceneViewport = ViewportClient->GetGameViewport())
				{
					return SceneViewport;
				}
			}
		}

		if (GEngine && GEngine->GameViewport)
		{
			if (FSceneViewport* SceneViewport = GEngine->GameViewport->GetGameViewport())
			{
				return SceneViewport;
			}
		}

#if WITH_EDITOR
		if (UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine))
		{
			if (FViewport* PieViewport = EditorEngine->GetPIEViewport())
			{
				if (PieViewport->GetViewportType() == SceneViewportTypeName)
				{
					return static_cast<FSceneViewport*>(PieViewport);
				}
			}
		}
#endif

		return nullptr;
	}

	static TSharedPtr<FSceneViewport> ResolveLocalSceneViewport(const AMetaAgentPlayerController& Controller)
	{
		return ToSharedSceneViewport(ResolveRawSceneViewport(Controller));
	}
}

void AMetaAgentPlayerController::HandleReportRecordingStatusPressed()
{
	if (!IsModularRuntimeEnabled(EMetaAgentModularRuntime::Recording))
	{
		return;
	}

	ReportRuntimeCaptureStatus();
}

void AMetaAgentPlayerController::HandleToggleRecordingPressed()
{
	if (!IsLocalPlayerController() || !IsModularRuntimeEnabled(EMetaAgentModularRuntime::Recording))
	{
		return;
	}

	if (Recording.bTakeRecordingActive)
	{
		StopAutopilotTakeRecording();
	}
	else
	{
		StartAutopilotTakeRecording();
	}
}

void AMetaAgentPlayerController::StartAutopilotTakeRecording()
{
	if (Recording.bTakeRecordingActive || !IsLocalPlayerController())
	{
		return;
	}

	const UMetaAgentPluginSettings* PluginSettings = GetDefault<UMetaAgentPluginSettings>();
	if (!PluginSettings || !PluginSettings->bEnableRecordingSystems)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("RecordingRuntime: recording systems are disabled in plugin settings."));
		return;
	}

	const TSharedPtr<FSceneViewport> SceneViewport = MetaAgentRecording::ResolveLocalSceneViewport(*this);
	if (!SceneViewport.IsValid())
	{
		UE_LOG(LogMetaAgent, Error,
			TEXT("RecordingRuntime: no scene viewport available for capture (world='%s' localPlayer=%s gameViewport=%s)."),
			*GetNameSafe(GetWorld()),
			GetLocalPlayer() ? TEXT("yes") : TEXT("no"),
			(GEngine && GEngine->GameViewport) ? TEXT("yes") : TEXT("no"));
		return;
	}

	Recording.ActiveMovieSceneCapture = nullptr;
	Recording.RuntimeCapturedFrameCount = 0;
	Recording.ActiveCaptureWidth = 0;
	Recording.ActiveCaptureHeight = 0;
	Recording.RuntimeCaptureOutputDirectory = TEXT("");

	const FString SessionSuffix = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	Recording.RuntimeCaptureOutputDirectory = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Renders") / FString::Printf(TEXT("Capture_%s"), *SessionSuffix));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.CreateDirectoryTree(*Recording.RuntimeCaptureOutputDirectory))
	{
		UE_LOG(LogMetaAgent, Error, TEXT("RecordingRuntime: failed to create capture directory '%s'."), *Recording.RuntimeCaptureOutputDirectory);
		return;
	}

	if (Recording.CaptureWidth > 0 && Recording.CaptureHeight > 0)
	{
		SceneViewport->SetViewportSize(Recording.CaptureWidth, Recording.CaptureHeight);
	}

	FIntPoint CaptureResolution = SceneViewport->GetSize();
	if (CaptureResolution == FIntPoint::ZeroValue)
	{
		CaptureResolution = FIntPoint(GSystemResolution.ResX, GSystemResolution.ResY);
	}

	UMovieSceneCapture* Capture = NewObject<UMovieSceneCapture>(GetTransientPackage());
	if (!Capture)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("RecordingRuntime: failed to create Movie Scene Capture object."));
		return;
	}

	const int32 CaptureFps = FMath::Clamp(FMath::RoundToInt(Recording.CaptureFps), 1, 120);
	Capture->Settings.OutputDirectory.Path = Recording.RuntimeCaptureOutputDirectory;
	Capture->Settings.OutputFormat = TEXT("Capture_{world}_{time}");
	Capture->Settings.bOverwriteExisting = true;
	Capture->Settings.bUseCustomFrameRate = true;
	Capture->Settings.CustomFrameRate = FFrameRate(CaptureFps, 1);
	Capture->Settings.Resolution.ResX = CaptureResolution.X;
	Capture->Settings.Resolution.ResY = CaptureResolution.Y;
	Capture->Settings.bCinematicMode = false;
	Capture->Settings.bCinematicEngineScalability = false;
	Capture->Settings.bShowHUD = false;
	Capture->Settings.bEnableTextureStreaming = true;
	Capture->Settings.MovieExtension = TEXT(".avi");
	Capture->SetImageCaptureProtocolType(UVideoCaptureProtocol::StaticClass());
	Capture->SetAudioCaptureProtocolType(UNullAudioCaptureProtocol::StaticClass());

	Capture->Initialize(SceneViewport);

	if (UVideoCaptureProtocol* VideoProtocol = Cast<UVideoCaptureProtocol>(Capture->GetImageCaptureProtocol()))
	{
		VideoProtocol->bUseCompression = Recording.bUseVideoCompression;
		VideoProtocol->CompressionQuality = Recording.VideoCompressionQuality;
	}

	Capture->StartCapture();

	Recording.ActiveMovieSceneCapture = Capture;
	Recording.bTakeRecordingActive = true;
	Recording.ActiveCaptureWidth = CaptureResolution.X;
	Recording.ActiveCaptureHeight = CaptureResolution.Y;
	Recording.RenderStatusText = FString::Printf(
		TEXT("Video capture active (%dx%d @ %d FPS)"),
		Recording.ActiveCaptureWidth,
		Recording.ActiveCaptureHeight,
		CaptureFps);
	Recording.RenderStatusColor = FColor::Cyan;

	UE_LOG(LogMetaAgent, Log,
		TEXT("RecordingRuntime: video capture started. Output='%s' Res=%dx%d FPS=%d Compression=%s Quality=%0.0f"),
		*Recording.RuntimeCaptureOutputDirectory,
		Recording.ActiveCaptureWidth,
		Recording.ActiveCaptureHeight,
		CaptureFps,
		Recording.bUseVideoCompression ? TEXT("On") : TEXT("Off"),
		Recording.VideoCompressionQuality);
	UpdateRecordingStatusHud();
}

void AMetaAgentPlayerController::StopAutopilotTakeRecording()
{
	if (!Recording.bTakeRecordingActive || !IsLocalPlayerController())
	{
		return;
	}

	if (Recording.ActiveMovieSceneCapture)
	{
		Recording.RuntimeCapturedFrameCount = Recording.ActiveMovieSceneCapture->GetMetrics().Frame;
		IMovieSceneCaptureModule::Get().DestroyMovieSceneCapture(Recording.ActiveMovieSceneCapture->GetHandle());
		Recording.ActiveMovieSceneCapture = nullptr;
	}

	Recording.bTakeRecordingActive = false;
	Recording.RenderStatusText = FString::Printf(
		TEXT("Video capture stopped (%d frames)"),
		Recording.RuntimeCapturedFrameCount);
	Recording.RenderStatusColor = FColor::Silver;

	UE_LOG(LogMetaAgent, Log,
		TEXT("RecordingRuntime: video capture stopped. Frames=%d Output='%s'"),
		Recording.RuntimeCapturedFrameCount,
		*Recording.RuntimeCaptureOutputDirectory);
	UpdateRecordingStatusHud();
}

void AMetaAgentPlayerController::ReportRuntimeCaptureStatus()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (Recording.bTakeRecordingActive)
	{
		StopAutopilotTakeRecording();
	}

	if (Recording.RuntimeCapturedFrameCount > 0)
	{
		Recording.RenderStatusText = FString::Printf(
			TEXT("Video saved (%d frames)"),
			Recording.RuntimeCapturedFrameCount);
		Recording.RenderStatusColor = FColor::Green;
		UpdateRecordingStatusHud();

		UE_LOG(LogMetaAgent, Log,
			TEXT("RecordingRuntime: video saved to '%s' (%d frames)."),
			*Recording.RuntimeCaptureOutputDirectory,
			Recording.RuntimeCapturedFrameCount);
		return;
	}

	UE_LOG(LogMetaAgent, Warning, TEXT("RecordingRuntime: no captured video available yet. Press J to start/stop capture first."));
}

void AMetaAgentPlayerController::UpdateRecordingCaptureStatus()
{
	if (!Recording.bTakeRecordingActive || !Recording.ActiveMovieSceneCapture)
	{
		return;
	}

	const FCachedMetrics& Metrics = Recording.ActiveMovieSceneCapture->GetMetrics();
	if (Metrics.Frame == Recording.RuntimeCapturedFrameCount)
	{
		return;
	}

	Recording.RuntimeCapturedFrameCount = Metrics.Frame;
	Recording.RenderStatusText = FString::Printf(TEXT("Recording video: %d frames"), Recording.RuntimeCapturedFrameCount);
	Recording.RenderStatusColor = FColor::Cyan;
	UpdateRecordingStatusHud();
}

void AMetaAgentPlayerController::UpdateRecordingStatusHud()
{
	const TCHAR* RecordingState = Recording.bTakeRecordingActive ? TEXT("ON") : TEXT("OFF");
	GUI.RecordingStatusLine = FString::Printf(TEXT("Recording: %s (Video/AVI) | %s"), RecordingState, *Recording.RenderStatusText);
	ApplyGUIHelpPanelState();
}

TArray<FString> AMetaAgentPlayerController::BuildRecordingRuntimePanelLines() const
{
	TArray<FString> Lines;
	Lines.Add(TEXT("Recording Runtime"));
	Lines.Add(FString::Printf(TEXT("State         : %s"), Recording.bTakeRecordingActive ? TEXT("ON") : TEXT("OFF")));
	Lines.Add(TEXT("Backend       : MovieSceneCapture (AVI video)"));
	if (Recording.ActiveCaptureWidth > 0 && Recording.ActiveCaptureHeight > 0)
	{
		Lines.Add(FString::Printf(TEXT("Resolution    : %dx%d"), Recording.ActiveCaptureWidth, Recording.ActiveCaptureHeight));
	}
	else if (Recording.CaptureWidth > 0 && Recording.CaptureHeight > 0)
	{
		Lines.Add(FString::Printf(TEXT("Resolution    : %dx%d (override)"), Recording.CaptureWidth, Recording.CaptureHeight));
	}
	else
	{
		Lines.Add(TEXT("Resolution    : Viewport"));
	}
	Lines.Add(FString::Printf(TEXT("FPS           : %0.0f"), Recording.CaptureFps));
	Lines.Add(FString::Printf(
		TEXT("Compression   : %s (%0.0f)"),
		Recording.bUseVideoCompression ? TEXT("On") : TEXT("Off"),
		Recording.VideoCompressionQuality));
	Lines.Add(FString::Printf(TEXT("Frames        : %d"), Recording.RuntimeCapturedFrameCount));
	Lines.Add(FString::Printf(TEXT("Render        : %s"), *Recording.RenderStatusText));
	Lines.Add(FString::Printf(
		TEXT("Output Dir    : %s"),
		Recording.RuntimeCaptureOutputDirectory.IsEmpty() ? TEXT("Saved/Renders") : *Recording.RuntimeCaptureOutputDirectory));
	return Lines;
}

TArray<FString> AMetaAgentPlayerController::BuildAiRuntimePanelLines() const
{
	TArray<FString> Lines;
	Lines.Add(TEXT("AI Runtime"));
	Lines.Add(FString::Printf(TEXT("Autopilot      : %s"), Autopilot.bEnabled ? TEXT("ON") : TEXT("OFF")));
	if (UClass* AIControllerClass = Autopilot.AIControllerClass.Get())
	{
		Lines.Add(FString::Printf(TEXT("Controller     : %s"), *GetNameSafe(AIControllerClass)));
	}
	else
	{
		Lines.Add(TEXT("Controller     : Default wander AI"));
	}
	Lines.Add(FString::Printf(TEXT("Debounce       : %0.2fs"), Autopilot.ToggleDebounceSeconds));
	return Lines;
}

// ===== MetaAgentPlayerControllerParticles.cpp =====
namespace MetaAgentParticlePatternConsole
{
	bool AreParticlePatternTagsBlocked(
		const FGameplayTagContainer& BlockedTags,
		const FGameplayTagContainer& PatternTags)
	{
		if (BlockedTags.IsEmpty())
		{
			return false;
		}

		if (BlockedTags.HasTag(MetaAgentParticleTags::Pattern_Blocked))
		{
			return true;
		}

		return !PatternTags.IsEmpty() && BlockedTags.HasAny(PatternTags);
	}

	AMetaAgentPlayerController* ResolveLocalMetaAgentController()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			UWorld* World = WorldContext.World();
			if (!World || !World->IsGameWorld())
			{
				continue;
			}

			if (APlayerController* PlayerController = World->GetFirstPlayerController())
			{
				return Cast<AMetaAgentPlayerController>(PlayerController);
			}
		}

		return nullptr;
	}

	void ShowTransientPatternMessage(AMetaAgentPlayerController* Controller, const FString& Message, const FColor Color)
	{
		if (!Controller)
		{
			return;
		}

		if (AMetaAgentHUD* MetaAgentHUD = Controller->GetHUD<AMetaAgentHUD>())
		{
			MetaAgentHUD->AddTransientMessage(Message, Color, 2.5f);
		}
	}

	void ExecSetForm(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Form <seconds>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Form: no local MetaAgent player controller found."));
			return;
		}

		const FMetaAgentParticlePatternConfig CurrentConfig = Controller->GetParticlePatternConfig();
		const float FormSeconds = FMath::Max(0.1f, FCString::Atof(*Args[0]));
		Controller->SetParticlePatternTimings(
			FormSeconds,
			CurrentConfig.HoldDurationSeconds,
			CurrentConfig.ReturnDurationSeconds);

		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Particle pattern form duration set to %.1fs."), FormSeconds),
			FColor::Cyan);
	}

	void ExecSetHold(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Hold <seconds>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Hold: no local MetaAgent player controller found."));
			return;
		}

		const FMetaAgentParticlePatternConfig CurrentConfig = Controller->GetParticlePatternConfig();
		const float HoldSeconds = FMath::Max(0.0f, FCString::Atof(*Args[0]));
		Controller->SetParticlePatternTimings(
			CurrentConfig.FormDurationSeconds,
			HoldSeconds,
			CurrentConfig.ReturnDurationSeconds);

		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Particle pattern hold duration set to %.1fs."), HoldSeconds),
			FColor::Cyan);
	}

	void ExecSetReturn(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Return <seconds>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Return: no local MetaAgent player controller found."));
			return;
		}

		const FMetaAgentParticlePatternConfig CurrentConfig = Controller->GetParticlePatternConfig();
		const float ReturnSeconds = FMath::Max(0.1f, FCString::Atof(*Args[0]));
		Controller->SetParticlePatternTimings(
			CurrentConfig.FormDurationSeconds,
			CurrentConfig.HoldDurationSeconds,
			ReturnSeconds);

		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Particle pattern return duration set to %.1fs."), ReturnSeconds),
			FColor::Cyan);
	}

	void ExecApplyPreset(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Preset Normal|Slow|Dramatic|Snappy|Dreamy"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Preset: no local MetaAgent player controller found."));
			return;
		}

		const FString PresetName = Args[0].ToLower();
		EMetaAgentParticlePatternPreset Preset = EMetaAgentParticlePatternPreset::Normal;
		if (PresetName == TEXT("slow"))
		{
			Preset = EMetaAgentParticlePatternPreset::Slow;
		}
		else if (PresetName == TEXT("dramatic"))
		{
			Preset = EMetaAgentParticlePatternPreset::Dramatic;
		}
		else if (PresetName == TEXT("snappy"))
		{
			Preset = EMetaAgentParticlePatternPreset::Snappy;
		}
		else if (PresetName == TEXT("dreamy"))
		{
			Preset = EMetaAgentParticlePatternPreset::Dreamy;
		}
		else if (PresetName != TEXT("normal"))
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Preset: unknown preset '%s'."), *Args[0]);
			return;
		}

		Controller->ApplyParticlePatternPreset(Preset);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Particle pattern preset applied: %s."), *Controller->GetParticlePatternTimingsText()),
			FColor::Cyan);
	}

	void ExecStatus()
	{
		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Status: no local MetaAgent player controller found."));
			return;
		}

		UE_LOG(LogMetaAgent, Log, TEXT("%s"), *Controller->GetParticlePatternTimingsText());
		UE_LOG(LogMetaAgent, Log, TEXT("%s"), *Controller->GetParticlePatternShapeText());
		UE_LOG(LogMetaAgent, Log, TEXT("%s"), *Controller->GetParticlePatternStatusText());
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(
				TEXT("%s | %s | %s"),
				*Controller->GetParticlePatternTimingsText(),
				*Controller->GetParticlePatternShapeText(),
				*Controller->GetParticlePatternStatusText()),
			FColor::Silver);
	}

	static FAutoConsoleCommand MetaAgentPatternFormCmd(
		TEXT("MetaAgent.Pattern.Form"),
		TEXT("Set particle pattern Forming duration in seconds."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetForm));

	static FAutoConsoleCommand MetaAgentPatternHoldCmd(
		TEXT("MetaAgent.Pattern.Hold"),
		TEXT("Set particle pattern Holding duration in seconds."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetHold));

	static FAutoConsoleCommand MetaAgentPatternReturnCmd(
		TEXT("MetaAgent.Pattern.Return"),
		TEXT("Set particle pattern Returning duration in seconds."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetReturn));

	static FAutoConsoleCommand MetaAgentPatternPresetCmd(
		TEXT("MetaAgent.Pattern.Preset"),
		TEXT("Apply particle pattern preset: Normal, Slow, or Dramatic."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecApplyPreset));

	static FAutoConsoleCommand MetaAgentPatternStatusCmd(
		TEXT("MetaAgent.Pattern.Status"),
		TEXT("Print active particle pattern timings, shape, and live state."),
		FConsoleCommandDelegate::CreateStatic(&ExecStatus));

	void ExecSetShape(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Shape SquareGrid|ImageSilhouette|SplinePath|MeshSilhouette"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const FString ShapeName = Args[0].ToLower();
		if (ShapeName == TEXT("imagesilhouette") || ShapeName == TEXT("image"))
		{
			Controller->SetParticlePatternShape(EMetaAgentParticlePatternShape::ImageSilhouette);
		}
		else if (ShapeName == TEXT("squaregrid") || ShapeName == TEXT("square") || ShapeName == TEXT("grid"))
		{
			Controller->SetParticlePatternShape(EMetaAgentParticlePatternShape::SquareGrid);
		}
		else if (ShapeName == TEXT("splinepath") || ShapeName == TEXT("spline"))
		{
			Controller->SetParticlePatternShape(EMetaAgentParticlePatternShape::SplinePath);
		}
		else if (ShapeName == TEXT("meshsilhouette") || ShapeName == TEXT("mesh"))
		{
			Controller->SetParticlePatternShape(EMetaAgentParticlePatternShape::MeshSilhouette);
		}
		else
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Shape: unknown shape '%s'."), *Args[0]);
			return;
		}

		ShowTransientPatternMessage(Controller, Controller->GetParticlePatternShapeText(), FColor::Cyan);
	}

	void ExecSetImageThreshold(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.ImageThreshold <0-1>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const float Threshold = FMath::Clamp(FCString::Atof(*Args[0]), 0.0f, 1.0f);
		Controller->SetParticlePatternImageThreshold(Threshold);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Image threshold set to %.2f."), Threshold),
			FColor::Cyan);
	}

	void ExecSetShapeWidth(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.ShapeWidth <cm>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const float WidthCm = FMath::Max(10.0f, FCString::Atof(*Args[0]));
		Controller->SetParticlePatternShapeWidth(WidthCm);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Shape width set to %.0f cm."), WidthCm),
			FColor::Cyan);
	}

	static FAutoConsoleCommand MetaAgentPatternShapeCmd(
		TEXT("MetaAgent.Pattern.Shape"),
		TEXT("Set particle pattern shape: SquareGrid, ImageSilhouette, SplinePath, or MeshSilhouette."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetShape));

	static FAutoConsoleCommand MetaAgentPatternImageThresholdCmd(
		TEXT("MetaAgent.Pattern.ImageThreshold"),
		TEXT("Set image silhouette alpha/luminance threshold (0-1)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetImageThreshold));

	static FAutoConsoleCommand MetaAgentPatternShapeWidthCmd(
		TEXT("MetaAgent.Pattern.ShapeWidth"),
		TEXT("Set image shape width in centimeters when not aligned to preview plane."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetShapeWidth));

	void ExecSetImageSampling(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.ImageSampling Gray|Sobel"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const FString ModeName = Args[0].ToLower();
		if (ModeName == TEXT("sobel") || ModeName == TEXT("edges") || ModeName == TEXT("sobeledges"))
		{
			Controller->SetParticlePatternImageSamplingMode(EMetaAgentParticleImageSamplingMode::SobelEdges);
		}
		else if (ModeName == TEXT("gray") || ModeName == TEXT("grey") || ModeName == TEXT("density")
			|| ModeName == TEXT("grayscale") || ModeName == TEXT("grayscaledensity"))
		{
			Controller->SetParticlePatternImageSamplingMode(EMetaAgentParticleImageSamplingMode::GrayscaleDensity);
		}
		else
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.ImageSampling: unknown mode '%s'."), *Args[0]);
			return;
		}

		ShowTransientPatternMessage(Controller, Controller->GetParticlePatternShapeText(), FColor::Cyan);
	}

	void ExecSetScatterGrid(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.ScatterGrid <1-16>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const float GridScale = FMath::Clamp(FCString::Atof(*Args[0]), 1.0f, 16.0f);
		Controller->SetParticlePatternDensityGridScale(GridScale);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Scatter grid scale set to %.1f. Press F then V to rebuild."), GridScale),
			FColor::Cyan);
	}

	void ExecSetReturning(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("Usage: MetaAgent.Pattern.Returning Lerp|Arc|Spiral|Dissipate|Cycle|<0-3>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const FString ModeName = Args[0].ToLower();
		EMetaAgentParticleReturnMode NewMode = EMetaAgentParticleReturnMode::DirectLerp;

		if (ModeName == TEXT("lerp") || ModeName == TEXT("direct") || ModeName == TEXT("directlerp") || ModeName == TEXT("0"))
		{
			NewMode = EMetaAgentParticleReturnMode::DirectLerp;
		}
		else if (ModeName == TEXT("arc") || ModeName == TEXT("arclift") || ModeName == TEXT("lift") || ModeName == TEXT("1"))
		{
			NewMode = EMetaAgentParticleReturnMode::ArcLift;
		}
		else if (ModeName == TEXT("spiral") || ModeName == TEXT("spiralin") || ModeName == TEXT("2"))
		{
			NewMode = EMetaAgentParticleReturnMode::SpiralIn;
		}
		else if (ModeName == TEXT("dissipate") || ModeName == TEXT("dissipatetocenter") || ModeName == TEXT("center") || ModeName == TEXT("3"))
		{
			NewMode = EMetaAgentParticleReturnMode::DissipateToCenter;
		}
		else if (ModeName == TEXT("cycle") || ModeName == TEXT("next"))
		{
			Controller->CycleParticlePatternReturnMode();
			ShowTransientPatternMessage(Controller, Controller->GetParticlePatternTimingsText(), FColor::Cyan);
			return;
		}
		else
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Returning: unknown mode '%s'."), *Args[0]);
			return;
		}

		Controller->SetParticlePatternReturnMode(NewMode);
		ShowTransientPatternMessage(Controller, Controller->GetParticlePatternTimingsText(), FColor::Cyan);
	}

	void ExecSetForming(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("Usage: MetaAgent.Pattern.Forming Lerp|Arc|Spiral|Wave|Spring|Cycle|<0-4>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const FString ModeName = Args[0].ToLower();
		EMetaAgentParticleFormingMode NewMode = EMetaAgentParticleFormingMode::DirectLerp;

		if (ModeName == TEXT("lerp") || ModeName == TEXT("direct") || ModeName == TEXT("directlerp") || ModeName == TEXT("0"))
		{
			NewMode = EMetaAgentParticleFormingMode::DirectLerp;
		}
		else if (ModeName == TEXT("arc") || ModeName == TEXT("arclift") || ModeName == TEXT("lift") || ModeName == TEXT("1"))
		{
			NewMode = EMetaAgentParticleFormingMode::ArcLift;
		}
		else if (ModeName == TEXT("spiral") || ModeName == TEXT("spiralin") || ModeName == TEXT("2"))
		{
			NewMode = EMetaAgentParticleFormingMode::SpiralIn;
		}
		else if (ModeName == TEXT("wave") || ModeName == TEXT("staggered") || ModeName == TEXT("staggeredwave") || ModeName == TEXT("3"))
		{
			NewMode = EMetaAgentParticleFormingMode::StaggeredWave;
		}
		else if (ModeName == TEXT("spring") || ModeName == TEXT("springchase") || ModeName == TEXT("chase") || ModeName == TEXT("4"))
		{
			NewMode = EMetaAgentParticleFormingMode::SpringChase;
		}
		else if (ModeName == TEXT("cycle") || ModeName == TEXT("next"))
		{
			Controller->CycleParticlePatternFormingMode();
			ShowTransientPatternMessage(Controller, Controller->GetParticlePatternTimingsText(), FColor::Cyan);
			return;
		}
		else
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Forming: unknown mode '%s'."), *Args[0]);
			return;
		}

		Controller->SetParticlePatternFormingMode(NewMode);
		ShowTransientPatternMessage(Controller, Controller->GetParticlePatternTimingsText(), FColor::Cyan);
	}

	void ExecSetScatterJitter(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.ScatterJitter <0-1>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const float Jitter = FMath::Clamp(FCString::Atof(*Args[0]), 0.0f, 1.0f);
		Controller->SetParticlePatternTargetJitter(Jitter);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Scatter jitter set to %.2f. Press F then V to rebuild."), Jitter),
			FColor::Cyan);
	}

	void ExecSetEdgeThreshold(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.EdgeThreshold <0-1>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const float Threshold = FMath::Clamp(FCString::Atof(*Args[0]), 0.01f, 1.0f);
		Controller->SetParticlePatternEdgeThreshold(Threshold);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Edge threshold set to %.3f."), Threshold),
			FColor::Cyan);
	}

	void ExecCancel(const TArray<FString>& Args)
	{
		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const bool bSkipReturn = Args.Num() > 0 && Args[0].Equals(TEXT("SkipReturn"), ESearchCase::IgnoreCase);
		if (Controller->RequestParticlePatternCancel(bSkipReturn))
		{
			ShowTransientPatternMessage(Controller, TEXT("Particle pattern cancelled."), FColor::Orange);
		}
	}

	void ExecSkipHold()
	{
		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		if (Controller->RequestParticleSkipHold())
		{
			ShowTransientPatternMessage(Controller, TEXT("Particle pattern skip hold."), FColor::Cyan);
		}
	}

	void ExecDissipate()
	{
		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const FMetaAgentParticleEffectResult Result = Controller->TriggerParticleEffect(
			MetaAgentParticleEffectIds::DissipateToCenter);
		if (AMetaAgentHUD* HUD = Controller->GetHUD<AMetaAgentHUD>())
		{
			const FColor Color = Result.bSuccess ? FColor::Cyan : FColor::Orange;
			HUD->AddTransientMessage(Result.UserMessage.ToString(), Color, 3.0f);
		}
	}

	void ExecReady(const TArray<FString>& Args)
	{
		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		FString ImagePath;
		if (Args.Num() > 0)
		{
			ImagePath = Args[0];
		}
		else
		{
			ImagePath = Controller->GetLastLoadedPreviewImagePath();
		}

		const bool bReady = Controller->IsParticlePatternReady(ImagePath);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Pattern mask ready: %s"), bReady ? TEXT("TRUE") : TEXT("FALSE")),
			bReady ? FColor::Green : FColor::Orange);
	}

	static FAutoConsoleCommand MetaAgentPatternCancelCmd(
		TEXT("MetaAgent.Pattern.Cancel"),
		TEXT("Cancel active particle pattern. Optional arg: SkipReturn"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecCancel));

	static FAutoConsoleCommand MetaAgentPatternSkipHoldCmd(
		TEXT("MetaAgent.Pattern.SkipHold"),
		TEXT("Skip Holding and begin Returning immediately."),
		FConsoleCommandDelegate::CreateStatic(&ExecSkipHold));

	static FAutoConsoleCommand MetaAgentPatternDissipateCmd(
		TEXT("MetaAgent.Pattern.Dissipate"),
		TEXT("Collapse particles toward pattern center and fade out (Forming/Holding/Returning)."),
		FConsoleCommandDelegate::CreateStatic(&ExecDissipate));

	static FAutoConsoleCommand MetaAgentPatternReadyCmd(
		TEXT("MetaAgent.Pattern.Ready"),
		TEXT("Check whether image mask is cached. Optional: image path."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecReady));

	static FAutoConsoleCommand MetaAgentPatternImageSamplingCmd(
		TEXT("MetaAgent.Pattern.ImageSampling"),
		TEXT("Set image sampling: Gray (default) or Sobel (edges)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetImageSampling));

	static FAutoConsoleCommand MetaAgentPatternEdgeThresholdCmd(
		TEXT("MetaAgent.Pattern.EdgeThreshold"),
		TEXT("Set Sobel edge magnitude threshold (0.01-1). Lower = denser outlines."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetEdgeThreshold));

	static FAutoConsoleCommand MetaAgentPatternScatterGridCmd(
		TEXT("MetaAgent.Pattern.ScatterGrid"),
		TEXT("Stratification grid scale (1-16). Higher = particles spread across more of the image."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetScatterGrid));

	static FAutoConsoleCommand MetaAgentPatternScatterJitterCmd(
		TEXT("MetaAgent.Pattern.ScatterJitter"),
		TEXT("Per-particle scatter jitter within grid cells (0-1). Higher = more random offset."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetScatterJitter));

	static FAutoConsoleCommand MetaAgentPatternFormingCmd(
		TEXT("MetaAgent.Pattern.Forming"),
		TEXT("Set forming mode: Lerp, Arc, Spiral, or Cycle."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetForming));

	static FAutoConsoleCommand MetaAgentPatternReturningCmd(
		TEXT("MetaAgent.Pattern.Returning"),
		TEXT("Set returning mode: Lerp, Arc, Spiral, Dissipate, or Cycle."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetReturning));
}

namespace MetaAgentParticleControllerInternal
{
	void NotifyEffectResult(AMetaAgentPlayerController* Controller, const FMetaAgentParticleEffectResult& Result)
	{
		if (!Controller)
		{
			return;
		}

		if (AMetaAgentHUD* HUD = Controller->GetHUD<AMetaAgentHUD>())
		{
			const FColor Color = Result.bSuccess ? FColor::Cyan : FColor::Orange;
			const FString Message = Result.UserMessage.IsEmpty()
				? (Result.bSuccess ? TEXT("Particle effect applied.") : TEXT("Particle effect unavailable."))
				: Result.UserMessage.ToString();
			HUD->AddTransientMessage(Message, Color, 3.0f);
		}
	}

	void EnsureParticleRuntimeEnabled(AMetaAgentPlayerController* Controller)
	{
		if (Controller && !Controller->IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
		{
			Controller->SetModularRuntimeEnabled(EMetaAgentModularRuntime::Particle, true);
		}
	}

	bool TryExecuteParticleEffect(AMetaAgentPlayerController* Controller, const FName EffectId)
	{
		if (!Controller)
		{
			return false;
		}

		if (!IsMetaAgentRuntimeActive())
		{
			FMetaAgentParticleEffectResult FailureResult;
			FailureResult.bSuccess = false;
			FailureResult.EffectId = EffectId;
			FailureResult.UserMessage = FText::FromString(TEXT("MetaAgent runtime is inactive."));
			NotifyEffectResult(Controller, FailureResult);
			return false;
		}

		EnsureParticleRuntimeEnabled(Controller);

		if (!Controller->IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
		{
			FMetaAgentParticleEffectResult FailureResult;
			FailureResult.bSuccess = false;
			FailureResult.EffectId = EffectId;
			FailureResult.UserMessage = FText::FromString(TEXT("Particle runtime is disabled."));
			NotifyEffectResult(Controller, FailureResult);
			return false;
		}

		NotifyEffectResult(Controller, Controller->TriggerParticleEffect(EffectId));
		return true;
	}

	void TriggerEffectOnController(AMetaAgentPlayerController* Controller, const FName EffectId)
	{
		(void)TryExecuteParticleEffect(Controller, EffectId);
	}

	bool ToggleStateEffectOnController(
		AMetaAgentPlayerController* Controller,
		const metaagent::app::CommandId Command,
		const metaagent::core::String& EffectId)
	{
		if (!Controller
			|| !IsMetaAgentRuntimeActive()
			|| !Controller->IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle)
			|| !Controller->CanExecuteAppCommand(Command))
		{
			return false;
		}

		Controller->EnsureParticleOrchestrator();
		UMetaAgentParticleOrchestrator* Orchestrator = Controller->GetParticleOrchestrator();
		if (!Orchestrator)
		{
			return false;
		}

		UMetaAgentParticleRuntime* Runtime = Orchestrator->GetParticleRuntime();
		if (!Runtime)
		{
			if (AMetaAgentHUD* HUD = Controller->GetHUD<AMetaAgentHUD>())
			{
				HUD->AddTransientMessage(TEXT("Particle runtime unavailable."), FColor::Yellow, 2.0f);
			}
			return false;
		}

		const metaagent::particle::StateEffectTriggerResult Result =
			MetaAgentParticleCoreBridge::toggle_state_effect(*Runtime, EffectId);

		if (Result.success)
		{
			Runtime->ApplyPatternRepresentation();
		}

		if (AMetaAgentHUD* HUD = Controller->GetHUD<AMetaAgentHUD>())
		{
			const FColor Color = Result.success ? FColor::Cyan : FColor::Yellow;
			HUD->AddTransientMessage(FString(UTF8_TO_TCHAR(Result.user_message.c_str())), Color, 2.5f);
		}

		return Result.success;
	}
}

bool AMetaAgentPlayerController::ExecuteGuiParticleAction(const FName ActionId)
{
	if (!IsMetaAgentRuntimeActive())
	{
		return false;
	}

	if (!IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
	{
		SetModularRuntimeEnabled(EMetaAgentModularRuntime::Particle, true);
	}

	if (!IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
	{
		return false;
	}

	const FTCHARToUTF8 ActionUtf8(*ActionId.ToString());
	const metaagent::particle::ParticleGuiActionSpec* Spec =
		metaagent::particle::find_particle_gui_action(
			std::string(ActionUtf8.Get(), static_cast<size_t>(ActionUtf8.Length())));

	if (!Spec)
	{
		if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
		{
			HUD->AddTransientMessage(TEXT("Unknown particle panel action."), FColor::Yellow, 2.0f);
		}
		return false;
	}

	EnsureParticleOrchestrator();
	if (!ParticleOrchestrator)
	{
		if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
		{
			HUD->AddTransientMessage(TEXT("Particle orchestrator unavailable."), FColor::Yellow, 2.0f);
		}
		return false;
	}

	if (Spec->dispatch_kind == metaagent::particle::ParticleGuiDispatchKind::LoadPreviewPng)
	{
		FString Message;
		const bool bLoaded = ParticleOrchestrator->LoadDefaultPreviewPng(Message);
		if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
		{
			HUD->AddTransientMessage(
				Message,
				bLoaded ? FColor::Green : FColor::Yellow,
				bLoaded ? 3.0f : 2.5f);
		}
		return bLoaded;
	}

	if (Spec->dispatch_kind == metaagent::particle::ParticleGuiDispatchKind::ToggleStateEffect)
	{
		const metaagent::app::CommandId Command = metaagent::app::command_for_gui_action(
			std::string(ActionUtf8.Get(), static_cast<size_t>(ActionUtf8.Length())));
		return MetaAgentParticleControllerInternal::ToggleStateEffectOnController(
			this,
			Command,
			Spec->effect_id);
	}

	const FName EffectId(UTF8_TO_TCHAR(Spec->effect_id.c_str()));
	const FMetaAgentParticleEffectResult Result = TriggerParticleEffect(EffectId);
	if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
	{
		const FColor Color = Result.bSuccess ? FColor::Cyan : FColor::Orange;
		const FString Message = Result.UserMessage.IsEmpty()
			? (Result.bSuccess ? TEXT("Particle action applied.") : TEXT("Particle action unavailable."))
			: Result.UserMessage.ToString();
		HUD->AddTransientMessage(Message, Color, 3.0f);
	}
	return Result.bSuccess;
}

void AMetaAgentPlayerController::EnsureParticleOrchestrator()
{
	if (ParticleOrchestrator)
	{
		return;
	}

	const TSubclassOf<UMetaAgentParticleOrchestrator> OrchestratorClass =
		ParticleOrchestratorClass
			? ParticleOrchestratorClass
			: TSubclassOf<UMetaAgentParticleOrchestrator>(UMetaAgentDefaultParticleOrchestrator::StaticClass());
	ParticleOrchestrator = NewObject<UMetaAgentParticleOrchestrator>(this, OrchestratorClass, TEXT("MetaAgentParticleOrchestrator"));
	SyncOrchestratorFromControllerDefaults();
	ParticleOrchestrator->InitializeOrchestrator(this, GetWorld());
	BindParticleRuntimeDelegates();
}

void AMetaAgentPlayerController::SyncOrchestratorFromControllerDefaults()
{
	if (!ParticleOrchestrator)
	{
		return;
	}

	ParticleOrchestrator->ApplyPatternConfig(ParticlePatternConfig);
	ParticleOrchestrator->SetActuationMode(ParticleActuationMode);
	ParticleOrchestrator->SetBlockedPatternTags(BlockedPatternTags);
	ParticleOrchestrator->SetDefaultPatternAsset(DefaultParticlePatternAsset);
}

UMetaAgentParticleRuntime* AMetaAgentPlayerController::GetParticleRuntime() const
{
	return ParticleOrchestrator ? ParticleOrchestrator->GetParticleRuntime() : nullptr;
}

FMetaAgentParticleEffectResult AMetaAgentPlayerController::TriggerParticleEffect(const FName EffectId)
{
	EnsureParticleOrchestrator();
	if (!ParticleOrchestrator)
	{
		return FMetaAgentParticleEffectResult();
	}

	const FMetaAgentParticleEffectResult Result = ParticleOrchestrator->TriggerEffect(EffectId);
	if (Result.bSuccess)
	{
		ParticlePatternConfig = ParticleOrchestrator->GetPatternConfig();
	}

	return Result;
}

void AMetaAgentPlayerController::HandleParticleLoadPreviewPressed()
{
	if (!CanExecuteAppCommand(metaagent::app::CommandId::LoadPreviewImage))
	{
		return;
	}

	EnsureParticleOrchestrator();
	if (!ParticleOrchestrator)
	{
		return;
	}

	FString Message;
	if (ParticleOrchestrator->LoadDefaultPreviewPng(Message))
	{
		if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
		{
			HUD->AddTransientMessage(Message, FColor::Green, 3.0f);
		}
		ApplyGUIHelpPanelState();
	}
	else if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
	{
		HUD->AddTransientMessage(Message, FColor::Yellow, 2.5f);
	}
}

void AMetaAgentPlayerController::HandleParticlePlayFullCyclePressed()
{
	if (!IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
	{
		return;
	}

	EnsureParticleOrchestrator();
	if (!ParticleOrchestrator)
	{
		return;
	}

	const FMetaAgentParticleEffectResult Result = ParticleOrchestrator->PlayFullImageRevealCycle();
	if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
	{
		HUD->AddTransientMessage(
			Result.UserMessage.ToString(),
			Result.bSuccess ? FColor::Cyan : FColor::Yellow,
			2.5f);
	}
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleStepPatternBackwardPressed()
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (!IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
	{
		SetModularRuntimeEnabled(EMetaAgentModularRuntime::Particle, true);
	}

	FString BlockMessage;
	if (!CanExecuteAppCommand(metaagent::app::CommandId::PatternStepBackward, &BlockMessage))
	{
		if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
		{
			HUD->AddTransientMessage(
				BlockMessage.IsEmpty() ? TEXT("Pattern step backward unavailable.") : BlockMessage,
				FColor::Yellow,
				2.5f);
		}
		return;
	}

	MetaAgentParticleControllerInternal::TriggerEffectOnController(
		this,
		MetaAgentParticleEffectIds::PatternStepBackward);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleStepPatternForwardPressed()
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (!IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
	{
		SetModularRuntimeEnabled(EMetaAgentModularRuntime::Particle, true);
	}

	FString BlockMessage;
	if (!CanExecuteAppCommand(metaagent::app::CommandId::PatternStepForward, &BlockMessage))
	{
		if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
		{
			HUD->AddTransientMessage(
				BlockMessage.IsEmpty() ? TEXT("Pattern step forward unavailable.") : BlockMessage,
				FColor::Yellow,
				2.5f);
		}
		return;
	}

	MetaAgentParticleControllerInternal::TriggerEffectOnController(
		this,
		MetaAgentParticleEffectIds::PatternStepForward);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleCyclePresetPressed()
{
	MetaAgentParticleControllerInternal::TriggerEffectOnController(this, MetaAgentParticleEffectIds::CyclePreset);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleCycleSamplingPressed()
{
	MetaAgentParticleControllerInternal::TriggerEffectOnController(this, MetaAgentParticleEffectIds::CycleSampling);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleCycleFormingPressed()
{
	MetaAgentParticleControllerInternal::TriggerEffectOnController(this, MetaAgentParticleEffectIds::CycleForming);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleCycleReturningPressed()
{
	MetaAgentParticleControllerInternal::TriggerEffectOnController(this, MetaAgentParticleEffectIds::CycleReturning);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleCycleOverlayPressed()
{
	MetaAgentParticleControllerInternal::TriggerEffectOnController(this, MetaAgentParticleEffectIds::CycleOverlay);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

bool AMetaAgentPlayerController::StartParticleSquarePattern()
{
	return StartParticlePattern();
}

bool AMetaAgentPlayerController::StartParticlePattern()
{
	return TriggerParticleEffect(MetaAgentParticleEffectIds::ImageReveal).bSuccess;
}

bool AMetaAgentPlayerController::RequestParticlePatternStart(UMetaAgentParticlePatternAsset* PatternAsset)
{
	EnsureParticleOrchestrator();
	if (!ParticleOrchestrator || !PatternAsset)
	{
		return false;
	}

	return ParticleOrchestrator->StartPatternWithAsset(PatternAsset).bSuccess;
}

bool AMetaAgentPlayerController::RequestParticlePatternCancel(const bool bSkipReturn)
{
	EnsureParticleOrchestrator();
	return ParticleOrchestrator ? ParticleOrchestrator->RequestPatternCancel(bSkipReturn) : false;
}

bool AMetaAgentPlayerController::RequestParticleSkipHold()
{
	EnsureParticleOrchestrator();
	return ParticleOrchestrator ? ParticleOrchestrator->RequestSkipHold() : false;
}

bool AMetaAgentPlayerController::RequestParticlePatternQueue(UMetaAgentParticlePatternAsset* PatternAsset)
{
	EnsureParticleOrchestrator();
	return ParticleOrchestrator ? ParticleOrchestrator->RequestPatternQueue(PatternAsset) : false;
}

bool AMetaAgentPlayerController::CanStartParticlePattern() const
{
	return GetParticleRuntime() ? GetParticleRuntime()->CanStartPattern() : false;
}

bool AMetaAgentPlayerController::IsParticlePatternReady(const FString& ImagePath) const
{
	return GetParticleRuntime() ? GetParticleRuntime()->IsPatternReady(ImagePath) : false;
}

int32 AMetaAgentPlayerController::GetParticlePatternQueueDepth() const
{
	return GetParticleRuntime() ? GetParticleRuntime()->GetPatternQueueDepth() : 0;
}

void AMetaAgentPlayerController::BindParticleRuntimeDelegates()
{
	UMetaAgentParticleRuntime* Runtime = GetParticleRuntime();
	if (!Runtime)
	{
		return;
	}

	Runtime->OnPatternStateEntered.RemoveAll(this);
	Runtime->OnPatternCompleted.RemoveAll(this);
	Runtime->OnPatternStateEntered.AddDynamic(this, &AMetaAgentPlayerController::HandleParticlePatternStateEntered);
	Runtime->OnPatternCompleted.AddDynamic(this, &AMetaAgentPlayerController::HandleParticlePatternCompleted);
}

void AMetaAgentPlayerController::HandleParticlePatternStateEntered(
	const EMetaAgentParticlePatternState NewState,
	const EMetaAgentParticlePatternState PreviousState)
{
	OnParticlePatternStateEntered.Broadcast(NewState, PreviousState);
}

void AMetaAgentPlayerController::HandleParticlePatternCompleted()
{
	OnParticlePatternCompleted.Broadcast();
}

FString AMetaAgentPlayerController::GetParticlePatternStatusText() const
{
	if (ParticleOrchestrator)
	{
		return ParticleOrchestrator->BuildPatternStatusText();
	}

	return TEXT("Pattern State: Idle | Phase: 0.00 | Particles: 0");
}

FString AMetaAgentPlayerController::GetParticlePatternTimingsText() const
{
	if (ParticleOrchestrator)
	{
		return ParticleOrchestrator->BuildPatternTimingsText();
	}

	return FString::Printf(
		TEXT("Pattern Preset: %s | Form=%.1fs Hold=%.1fs Return=%.1fs | Forming=%s"),
		*ParticlePatternConfig.GetPresetDisplayName(),
		ParticlePatternConfig.FormDurationSeconds,
		ParticlePatternConfig.HoldDurationSeconds,
		ParticlePatternConfig.ReturnDurationSeconds,
		*ParticlePatternConfig.Forming.GetModeDisplayName());
}

TArray<FString> AMetaAgentPlayerController::BuildParticleRuntimePanelStatusLines() const
{
	TArray<FString> Lines;
	const FMetaAgentParticlePatternConfig& Config = ParticlePatternConfig;

	Lines.Add(FString::Printf(
		TEXT("Callback=%s | Capture=%s | Particles=%d"),
		HasReceivedParticleCallback() ? TEXT("yes") : TEXT("no"),
		IsParticleCaptureActive() ? TEXT("yes") : TEXT("no"),
		GetCapturedParticleCount()));

	if (const UMetaAgentParticleRuntime* Runtime = GetParticleRuntime())
	{
		const FString MaskSuffix = Runtime->IsAwaitingAsyncMask() ? TEXT(" | loading mask") : FString();
		Lines.Add(FString::Printf(
			TEXT("State=%s | Phase=%.2f | Queue=%d%s"),
			*Runtime->GetPatternStateDisplayName(),
			Runtime->GetPatternPhase(),
			GetParticlePatternQueueDepth(),
			*MaskSuffix));
	}
	else
	{
		Lines.Add(TEXT("State=unavailable | Phase=0.00 | Queue=0"));
	}

	Lines.Add(FString::Printf(
		TEXT("Preset=%s | Form=%.1fs | Hold=%.1fs | Return=%.1fs"),
		*Config.GetPresetDisplayName(),
		Config.FormDurationSeconds,
		Config.HoldDurationSeconds,
		Config.ReturnDurationSeconds));

	const bool bImageLoaded = ParticleOrchestrator && ParticleOrchestrator->GetPreviewTexture() != nullptr;
	Lines.Add(FString::Printf(
		TEXT("Shape=%s | Sampling=%s | Forming=%s | Returning=%s | Image=%s"),
		*Config.Shape.GetShapeDisplayName(),
		*Config.Shape.GetImageSamplingDisplayName(),
		*Config.Forming.GetModeDisplayName(),
		*Config.Return.GetModeDisplayName(),
		bImageLoaded ? TEXT("loaded") : TEXT("none")));

	Lines.Add(FString::Printf(
		TEXT("Res=%dpx | Grid=%.1f | Jitter=%.2f"),
		Config.Shape.SampleResolution,
		Config.Shape.DensityGridScale,
		Config.Shape.TargetJitterNormalized));

	return Lines;
}

void AMetaAgentPlayerController::ApplyParticlePatternPreset(const EMetaAgentParticlePatternPreset Preset)
{
	ParticlePatternConfig.ApplyPreset(Preset);
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::SetParticlePatternTimings(
	const float FormDurationSeconds,
	const float HoldDurationSeconds,
	const float ReturnDurationSeconds)
{
	ParticlePatternConfig.FormDurationSeconds = FMath::Max(0.1f, FormDurationSeconds);
	ParticlePatternConfig.HoldDurationSeconds = FMath::Max(0.0f, HoldDurationSeconds);
	ParticlePatternConfig.ReturnDurationSeconds = FMath::Max(0.1f, ReturnDurationSeconds);
	ParticlePatternConfig.ActivePreset = EMetaAgentParticlePatternPreset::Custom;
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::SyncParticlePatternConfigToRuntime()
{
	SyncOrchestratorFromControllerDefaults();
}

bool AMetaAgentPlayerController::PrepareParticlePatternShapeContext()
{
	EnsureParticleOrchestrator();
	return ParticleOrchestrator ? ParticleOrchestrator->PrepareShapeContextForPlay() : false;
}

void AMetaAgentPlayerController::RequestParticleImageMaskBuild()
{
	PrepareParticlePatternShapeContext();
}

void AMetaAgentPlayerController::SetParticlePatternShape(const EMetaAgentParticlePatternShape ShapeType)
{
	ParticlePatternConfig.Shape.ShapeType = ShapeType;
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::SetParticlePatternImageThreshold(const float Threshold)
{
	ParticlePatternConfig.Shape.AlphaThreshold = FMath::Clamp(Threshold, 0.0f, 1.0f);
	SyncParticlePatternConfigToRuntime();
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
}

void AMetaAgentPlayerController::SetParticlePatternImageSamplingMode(
	const EMetaAgentParticleImageSamplingMode SamplingMode)
{
	ParticlePatternConfig.Shape.ImageSamplingMode =
		FMetaAgentParticleShapeDefinition::SanitizeImageSamplingMode(SamplingMode);
	SyncParticlePatternConfigToRuntime();
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
}

void AMetaAgentPlayerController::SetParticlePatternEdgeThreshold(const float Threshold)
{
	ParticlePatternConfig.Shape.EdgeThreshold = FMath::Clamp(Threshold, 0.01f, 1.0f);
	SyncParticlePatternConfigToRuntime();
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
}

void AMetaAgentPlayerController::SetParticlePatternShapeWidth(const float WidthCm)
{
	ParticlePatternConfig.Shape.ShapeWidthCm = FMath::Max(10.0f, WidthCm);
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::SetParticlePatternDensityGridScale(const float GridScale)
{
	ParticlePatternConfig.Shape.DensityGridScale = FMath::Clamp(GridScale, 1.0f, 16.0f);
	SyncParticlePatternConfigToRuntime();
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
}

void AMetaAgentPlayerController::SetParticlePatternTargetJitter(const float JitterNormalized)
{
	ParticlePatternConfig.Shape.TargetJitterNormalized = FMath::Clamp(JitterNormalized, 0.0f, 1.0f);
	SyncParticlePatternConfigToRuntime();
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
}

void AMetaAgentPlayerController::SetParticlePatternFormingMode(const EMetaAgentParticleFormingMode FormingMode)
{
	ParticlePatternConfig.Forming.Mode = FMetaAgentParticleFormingSettings::SanitizeMode(FormingMode);
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::CycleParticlePatternFormingMode()
{
	ParticlePatternConfig.Forming.CycleMode();
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::SetParticlePatternReturnMode(const EMetaAgentParticleReturnMode ReturnMode)
{
	ParticlePatternConfig.Return.Mode = FMetaAgentParticleReturnSettings::SanitizeMode(ReturnMode);
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::CycleParticlePatternReturnMode()
{
	ParticlePatternConfig.Return.CycleMode();
	SyncParticlePatternConfigToRuntime();
}

FString AMetaAgentPlayerController::GetParticlePatternShapeText() const
{
	return ParticleOrchestrator
		? ParticleOrchestrator->BuildPatternShapeText()
		: FString::Printf(
			TEXT("Pattern Shape: %s | Sampling=%s | ImageLoaded=FALSE"),
			*ParticlePatternConfig.Shape.GetShapeDisplayName(),
			*ParticlePatternConfig.Shape.GetImageSamplingDisplayName());
}

FMetaAgentParticleShapeContext AMetaAgentPlayerController::BuildParticleShapeContext()
{
	EnsureParticleOrchestrator();
	PrepareParticlePatternShapeContext();
	return FMetaAgentParticleShapeContext();
}

bool AMetaAgentPlayerController::EnsureParticlePreviewTextureLoaded(FString& OutResolvedPath)
{
	return FMetaAgentImagePreviewRuntime::EnsurePreviewTextureLoaded(*this, OutResolvedPath);
}

UTexture2D* AMetaAgentPlayerController::GetLatestPngPreviewTexture() const
{
	return ParticleOrchestrator ? ParticleOrchestrator->GetPreviewTexture() : nullptr;
}

void AMetaAgentPlayerController::SetLatestPngPreviewTexture(UTexture2D* Texture)
{
	EnsureParticleOrchestrator();
	if (ParticleOrchestrator)
	{
		ParticleOrchestrator->SetPreviewSource(Texture, GetLastLoadedPreviewImagePath());
	}
}

FString AMetaAgentPlayerController::GetLastLoadedPreviewImagePath() const
{
	return ParticleOrchestrator ? ParticleOrchestrator->GetPreviewImagePath() : FString();
}

void AMetaAgentPlayerController::SetLastLoadedPreviewImagePath(const FString& Path)
{
	EnsureParticleOrchestrator();
	if (ParticleOrchestrator)
	{
		ParticleOrchestrator->SetPreviewSource(GetLatestPngPreviewTexture(), Path);
	}
}

UStaticMeshComponent* AMetaAgentPlayerController::GetExistingPreviewPlaneMesh() const
{
	return ParticleOrchestrator ? ParticleOrchestrator->GetCachedPreviewPlaneMesh() : nullptr;
}

void AMetaAgentPlayerController::CacheExistingPreviewPlaneMesh(UStaticMeshComponent* Mesh)
{
	EnsureParticleOrchestrator();
	if (ParticleOrchestrator)
	{
		ParticleOrchestrator->SetCachedPreviewPlaneMesh(Mesh);
	}
}