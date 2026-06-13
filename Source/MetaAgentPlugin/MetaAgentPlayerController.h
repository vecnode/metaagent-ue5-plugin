// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "MetaAgentHUD.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "UObject/SoftObjectPtr.h"
#include "MetaAgentParticleTypes.h"
#include "MetaAgentPlugin.h"
#include "MetaAgentParticleControl.h"
#include "Host/MetaAgentHostSession.h"
#include "metaagent/app/commands.hpp"
#include "metaagent/camera/types.hpp"
#include "MetaAgentPlayerController.generated.h"

class AAIController;
class UInputMappingContext;
class UStaticMeshComponent;
class UTexture2D;
class UUserWidget;
class USkeletalMeshComponent;
class UNiagaraComponent;
class UMetaAgentParticleOrchestrator;
class UMetaAgentParticleRuntime;
class UMetaAgentParticlePatternAsset;
class UMetaAgentNiagaraExportHandler;
class UMovieSceneCapture;

UENUM()
enum class EMetaAgentCameraMode : uint8
{
	// Environment-only viewer mode
	FreeLook = 0
};

USTRUCT()
struct FMetaAgentCameraZoomState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Camera|Zoom")
	float MinDistance = 40.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Zoom")
	float MaxDistance = 1500.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Zoom")
	float MouseWheelStep = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Zoom")
	float InterpSpeed = 10.0f;

	UPROPERTY(Transient)
	float DesiredDistance = -1.0f;
};

USTRUCT()
struct FMetaAgentCameraModeState
{
	GENERATED_BODY()

	// Simplified for environment-only viewing: single free-look mode
	UPROPERTY(EditAnywhere, Category = "Camera|Mode")
	EMetaAgentCameraMode ActiveMode = EMetaAgentCameraMode::FreeLook;
};

USTRUCT()
struct FMetaAgentInputFallbackState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Input|Fallback")
	bool bEnableKeyboardMovement = false;

	UPROPERTY(EditAnywhere, Category = "Input|Fallback")
	bool bEnableMouseLook = false;

	UPROPERTY(EditAnywhere, Category = "Input|Fallback", meta=(ClampMin="0.001", ClampMax="5.0"))
	float MouseSensitivity = 0.05f;

	UPROPERTY(EditAnywhere, Category = "Input|Fallback", meta=(ClampMin="1.0", ClampMax="1200.0"))
	float WalkSpeed = 350.0f;

	UPROPERTY(EditAnywhere, Category = "Input|Fallback", meta=(ClampMin="1.0", ClampMax="2400.0"))
	float SprintSpeed = 600.0f;

	UPROPERTY(Transient)
	bool bAddedAnyMappingContext = false;

	UPROPERTY(Transient)
	bool bUtilityKeysBound = false;
};

USTRUCT()
struct FMetaAgentMovementDiagnosticsState
{
	GENERATED_BODY()

	// Emergency override. Keep disabled by default and let AnimBlueprint drive locomotion first.
	UPROPERTY(EditAnywhere, Category = "Animation|Fallback")
	bool bEnableEmergencySingleNodeLocomotion = false;

	// Preferred runtime mode: try AnimBlueprint locomotion first.
	UPROPERTY(EditAnywhere, Category = "Animation|Fallback")
	bool bPreferAnimBlueprintLocomotion = true;

	// Automatically switch to emergency single-node fallback if AnimBlueprint appears stalled while moving.
	UPROPERTY(EditAnywhere, Category = "Animation|Fallback")
	bool bEnableAutoFallbackOnAnimStall = true;

	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="1.0", ClampMax="600.0"))
	float AutoFallbackMinSpeed = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="0.05", ClampMax="5.0"))
	float AutoFallbackStallSeconds = 0.15f;

	// Approximate authored world speed of the emergency in-place walk clip.
	// Used to compute play-rate so visual stride matches movement speed better.
	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="1.0", ClampMax="600.0"))
	float EmergencySingleNodeAuthoredWalkSpeed = 45.0f;

	// Approximate authored world speed of the emergency run clip.
	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="1.0", ClampMax="1200.0"))
	float EmergencySingleNodeAuthoredRunSpeed = 260.0f;

	// Enter/exit hysteresis thresholds (cm/s) used to avoid abrupt idle/walk toggles.
	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="0.1", ClampMax="600.0"))
	float EmergencyWalkEnterSpeed = 24.0f;

	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="0.1", ClampMax="600.0"))
	float EmergencyWalkExitSpeed = 12.0f;

	// Enter/exit hysteresis thresholds (cm/s) for switching between walk and run fallback clips.
	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="1.0", ClampMax="2400.0"))
	float EmergencyRunEnterSpeed = 280.0f;

	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="1.0", ClampMax="2400.0"))
	float EmergencyRunExitSpeed = 200.0f;

	// Blend timing for smoothing transitions between idle and walk fallback clips.
	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="0.01", ClampMax="2.0"))
	float EmergencyWalkBlendInSeconds = 0.18f;

	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="0.01", ClampMax="2.0"))
	float EmergencyWalkBlendOutSeconds = 0.24f;

	// Asset switch thresholds over the transition alpha.
	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="0.0", ClampMax="1.0"))
	float EmergencyWalkEnterAssetAlpha = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="0.0", ClampMax="1.0"))
	float EmergencyWalkExitAssetAlpha = 0.15f;

	// Eases initial walk movement by ramping play-rate up from this value.
	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="0.1", ClampMax="2.0"))
	float EmergencyWalkStartPlayRate = 0.45f;

	// Near-zero speed tolerance used to settle exactly onto idle and avoid visible end-of-blend gaps.
	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="0.0", ClampMax="30.0"))
	float EmergencyIdleSnapSpeed = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Animation|Fallback", meta=(ClampMin="0.001", ClampMax="10.0"))
	float AutoFallbackMinBoneDelta = 0.02f;

	UPROPERTY(Transient)
	bool bLoggedMovementAnimDiagnostics = false;

	UPROPERTY(Transient)
	bool bMovementProbeActive = false;

	UPROPERTY(Transient)
	float ProbeElapsedSeconds = 0.0f;

	UPROPERTY(Transient)
	float ProbePeakSpeed2D = 0.0f;

	UPROPERTY(Transient)
	float ProbePeakAcceleration2D = 0.0f;

	UPROPERTY(Transient)
	int32 ProbeSampleCount = 0;

	UPROPERTY(Transient)
	bool bAutoFallbackActivated = false;

	UPROPERTY(Transient)
	float MovingWithoutPoseChangeSeconds = 0.0f;

	UPROPERTY(Transient)
	FVector LastProbeBoneLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	bool bHasLastProbeBoneLocation = false;

	UPROPERTY(Transient)
	bool bEmergencyWalkActive = false;

	UPROPERTY(Transient)
	bool bEmergencyRunActive = false;

	UPROPERTY(Transient)
	float EmergencyWalkBlendAlpha = 0.0f;

	UPROPERTY(Transient)
	float EmergencyRunBlendAlpha = 0.0f;
};

USTRUCT()
struct FMetaAgentAutopilotState
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	bool bEnabled = false;

	UPROPERTY(Transient)
	float LastToggleTimeSeconds = -1000.0f;

	UPROPERTY(EditAnywhere, Category = "AI|Autopilot", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ToggleDebounceSeconds = 0.2f;

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> Pawn;

	UPROPERTY(Transient)
	TWeakObjectPtr<AAIController> Controller;

	UPROPERTY(EditAnywhere, Category = "AI|Autopilot")
	TSubclassOf<AAIController> AIControllerClass;
};

USTRUCT()
struct FMetaAgentRecordingState
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	bool bTakeRecordingActive = false;

	UPROPERTY(Transient)
	FString RenderStatusText = TEXT("Capture: idle");

	UPROPERTY(Transient)
	FColor RenderStatusColor = FColor::Silver;

	UPROPERTY(Transient)
	TObjectPtr<UMovieSceneCapture> ActiveMovieSceneCapture;

	UPROPERTY(Transient)
	int32 RuntimeCapturedFrameCount = 0;

	UPROPERTY(Transient)
	int32 ActiveCaptureWidth = 0;

	UPROPERTY(Transient)
	int32 ActiveCaptureHeight = 0;

	UPROPERTY(Transient)
	FString RuntimeCaptureOutputDirectory;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording", meta=(ClampMin="1.0", ClampMax="120.0"))
	float CaptureFps = 30.0f;

	/** When zero, capture uses the current viewport resolution. */
	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording", meta=(ClampMin="0", ClampMax="7680"))
	int32 CaptureWidth = 0;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording", meta=(ClampMin="0", ClampMax="4320"))
	int32 CaptureHeight = 0;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording")
	bool bUseVideoCompression = true;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording", meta=(ClampMin="1", ClampMax="100", EditCondition="bUseVideoCompression"))
	float VideoCompressionQuality = 75.0f;
};

UENUM()
enum class EMetaAgentCinematicCameraStyle : uint8
{
	OscillatingHold
};

USTRUCT()
struct FMetaAgentCinematicCameraState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic")
	EMetaAgentCinematicCameraStyle ActiveStyle = EMetaAgentCinematicCameraStyle::OscillatingHold;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="0.0", ClampMax="5.0"))
	float BlendInSeconds = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="0.0", ClampMax="5.0"))
	float BlendOutSeconds = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="1.0", ClampMax="360.0"))
	float PanDegrees = 180.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="0.1", ClampMax="30.0"))
	float PanDurationSeconds = 4.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="0.1", ClampMax="45.0"))
	float OscillationYawAmplitudeDegrees = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="-200.0", ClampMax="400.0"))
	float LookAtZOffset = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic")
	bool bContinuousOrbit = true;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="0.05", ClampMax="3.0"))
	float OrbitSpeedScale = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="1", ClampMax="20"))
	int32 TurnsPerDirection = 1;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="80.0", ClampMax="500.0"))
	float CloseOrbitRadius = 105.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="0.0", ClampMax="1.0"))
	float HeadFocusAlpha = 0.80f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="0.0", ClampMax="80.0"))
	float SwayHorizontalAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="0.0", ClampMax="80.0"))
	float SwayVerticalAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="0.1", ClampMax="5.0"))
	float SwayFrequency = 0.85f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic")
	bool bDisablePlayerInput = true;

	UPROPERTY(Transient)
	bool bModeEnabled = false;

	UPROPERTY(Transient)
	float PanElapsedSeconds = 0.0f;

	UPROPERTY(Transient)
	float OrbitRadius = 400.0f;

	UPROPERTY(Transient)
	float StartOrbitYawDegrees = 0.0f;

	UPROPERTY(Transient)
	float OrbitAccumulatedYawDegrees = 0.0f;

	UPROPERTY(Transient)
	int32 OrbitDirectionSign = 1;

	UPROPERTY(Transient)
	float DirectionTravelDegrees = 0.0f;

	UPROPERTY(Transient)
	int32 CompletedTurnsThisDirection = 0;

	UPROPERTY(Transient)
	float CameraHeightOffset = 120.0f;

	UPROPERTY(Transient)
	float SwayPhaseOffset = 0.0f;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> TargetActor;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> PreViewTarget;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> RuntimeCameraActor;
};

USTRUCT()
struct FMetaAgentGUIState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "UI|Runtime")
	bool bHelpPanelVisible = false;

	UPROPERTY(EditAnywhere, Category = "UI|Runtime")
	bool bShowTransientToggleMessage = true;

	UPROPERTY(Transient)
	bool bHelpPanelInitialized = false;

	UPROPERTY(Transient)
	TArray<FString> BaseHelpPanelLines;

	UPROPERTY(Transient)
	TArray<FString> HelpPanelLines;

	UPROPERTY(Transient)
	FString RecordingStatusLine = TEXT("Recording: OFF");

	UPROPERTY(Transient)
	bool bCameraRuntimeEnabled = false;

	UPROPERTY(Transient)
	bool bAIRuntimeEnabled = false;

	UPROPERTY(Transient)
	bool bRecordingRuntimeEnabled = false;

	UPROPERTY(Transient)
	bool bNetworkingRuntimeEnabled = false;

	UPROPERTY(Transient)
	bool bParticleRuntimeEnabled = true;

	UPROPERTY(Transient)
	bool bCharacterInputRuntimeEnabled = false;

	UPROPERTY(Transient)
	TArray<FMetaAgentGUIRuntimeSection> RuntimeSections;

	UPROPERTY(Transient)
	TMap<FName, bool> SectionExpandedStates;
};

/**
 * Runtime player controller that owns input setup, camera zoom behavior,
 * possession diagnostics, and optional AI autopilot handoff.
 */
UCLASS()
class AMetaAgentPlayerController : public APlayerController
{
	GENERATED_BODY()

	friend struct FMetaAgentGUIRuntime;
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts skipped when touch controls are active */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Input Mapping Contexts */
	TSoftObjectPtr<UInputMappingContext> DefaultMappingContextAsset;
	UPROPERTY(EditDefaultsOnly, Category="Input|Input Mappings")
	TSoftObjectPtr<UInputMappingContext> MouseLookMappingContextAsset;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Called when this controller possesses a new pawn. Sets up third-person camera. */
	virtual void OnPossess(APawn* InPawn) override;

	/** Per-frame input fallback, diagnostics, and camera zoom updates. */
	virtual void PlayerTick(float DeltaTime) override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Requests application quit when Escape is pressed. */
	void HandleEscapePressed();

	/** Bound to H: sends a COMMS HTTP request with command 'start audio'. */
	void HandleStartAudioPressed();

	/** Bound to G: sends a COMMS HTTP request with command 'start image'. */
	void HandleStartImagePressed();

	/** Toggles player possession between manual control and runtime AI autopilot. */
	void HandleToggleAutopilotPressed();

	/** Reports output status for the current/last runtime capture session. */
	void HandleReportRecordingStatusPressed();

	/** Toggles recording on/off using the runtime recording backend. */
	void HandleToggleRecordingPressed();

	/** Bound to O: toggles cinematic camera mode on/off. */
	void HandleToggleCinematicCameraPressed();

	/** Bound to P: focus cinematic camera on live particle positions. */
	void HandleFocusParticlesCameraPressed();

	/** Bound to Q: toggles runtime controls help panel on/off. */
	void HandleToggleHelpPanelPressed();

	/** Bound to left mouse button while the controls panel is open. */
	void HandleGUIPanelMousePressed();

	/** Dispatches a runtime-panel button action (keyboard shortcut mirror). */
	void DispatchGUIAction(FName ActionId);

	/** Enables cinematic orbit camera mode around the active character. */
	void EnableCinematicCameraMode();

	/** Disables cinematic orbit camera mode and restores normal camera behavior. */
	void DisableCinematicCameraMode();

	/** Returns the best character actor to orbit (player pawn, autopilot pawn, or current target). */
	AActor* ResolveCinematicTargetActor() const;

	/** Resolves the preferred cinematic look-at point (head/chest when available). */
	FVector ResolveCinematicFocusLocation(AActor* TargetActor) const;

	/** Updates runtime cinematic camera transform each tick while mode is active. */
	void UpdateCinematicCamera(float DeltaTime);

	bool IsCinematicFocusParticlesEnabled() const { return bCinematicFocusParticles; }

	bool HasLockedParticleFocusTarget() const { return bHasLockedParticleFocus; }

	/** Captures a stable particle focus frame for observation mode. */
	bool TryLockParticleFocusTarget(bool bForceRelock = false);

	void BuildLockedParticleFocusTarget(metaagent::camera::FocusTarget& OutFocus) const;

	/** Applies locked orbit radius/height to the active cinematic camera state. */
	void ApplyLockedParticleFocusToCinematicCamera();

	/** Keeps player input disabled unless the GUI panel is open. */
	void EnforceObservationInputLock();

	/** Enables AI autopilot over the currently possessed pawn. */
	void EnableAutopilotForCurrentPawn();

	/** Disables AI autopilot and repossesses the last autopilot pawn. */
	void DisableAutopilotAndRepossess();


	/** Starts viewport movie-scene capture to disk. */
	void StartAutopilotTakeRecording();

	/** Stops an in-progress viewport movie-scene capture. */
	void StopAutopilotTakeRecording();

	/** Reports status for captured frame output. */
	void ReportRuntimeCaptureStatus();

	/** Pushes current recording/take/render state into the persistent HUD status panel. */
	void UpdateRecordingStatusHud();

	/** Refreshes live capture metrics while Movie Scene Capture is active. */
	void UpdateRecordingCaptureStatus();

	/** Applies the active camera mode to the currently possessed pawn. */
	void ApplyCameraModeToPawn(APawn* InPawn);

public:

	void HandleParticleLoadPreviewPressed();
	void HandleParticlePlayFullCyclePressed();
	void HandleParticleStepPatternBackwardPressed();
	void HandleParticleStepPatternForwardPressed();
	void HandleParticleSlowPresetPressed();
	void HandleParticleDramaticPresetPressed();
	void HandleParticleSnappyPresetPressed();
	void HandleParticleDreamyPresetPressed();
	void HandleParticleMorphPressed();
	void HandleParticleCycleSamplingPressed();
	void HandleParticleCycleFormingPressed();
	void HandleParticleCycleReturningPressed();

	/** Blueprint entry point so a UI button can toggle the same cinematic camera mode as V. */
	UFUNCTION(BlueprintCallable, Category = "Camera|Cinematic")
	void ToggleCinematicCameraMode();

	/** Executes a particle-panel row after GUI validation (skips keyboard command gates). */
	bool ExecuteGuiParticleAction(FName ActionId);

	/** C++ entry point for Niagara particle export data forwarded by the export handler. */
	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void SubmitNiagaraParticlePositions(
		const TArray<FVector>& ParticlePositions,
		FName SourceActorName = NAME_None,
		FName SourceComponentName = NAME_None);

	/** Manual debug trigger to rescan and print particle runtime status. */
	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void RefreshParticleRuntimeTracking();

	/** Sets a global steering target used to build per-particle direction vectors. */
	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Steering")
	void SetParticleSteeringTarget(FVector TargetLocation, float Strength = 1.0f);

	/** Clears steering target and generated direction vectors. */
	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Steering")
	void ClearParticleSteeringTarget();

	/** Returns per-particle suggested steering direction vectors. */
	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Steering")
	TArray<FVector> GetParticleSteeringDirections() const;

	/** True when exported particle data is currently available in runtime cache. */
	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	bool IsParticleCaptureActive() const;

	/** Number of particles currently cached from Niagara export callbacks. */
	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	int32 GetCapturedParticleCount() const;

	/** True once at least one Niagara export callback reached runtime. */
	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	bool HasReceivedParticleCallback() const;

	/** Starts square particle pattern choreography when capture data is available. */
	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern", meta = (DeprecatedFunction, DeprecationMessage = "Use StartParticlePattern"))
	bool StartParticleSquarePattern();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	bool StartParticlePattern();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	bool RequestParticlePatternStart(UMetaAgentParticlePatternAsset* PatternAsset);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	bool RequestParticlePatternCancel(bool bSkipReturn = false);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	bool RequestParticleSkipHold();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	bool RequestParticlePatternQueue(UMetaAgentParticlePatternAsset* PatternAsset);

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	bool CanStartParticlePattern() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	bool IsParticlePatternReady(const FString& ImagePath) const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	int32 GetParticlePatternQueueDepth() const;

	UPROPERTY(BlueprintAssignable, Category = "MetaAgent|Particles|Pattern")
	FOnMetaAgentPatternStateChanged OnParticlePatternStateEntered;

	UPROPERTY(BlueprintAssignable, Category = "MetaAgent|Particles|Pattern")
	FOnMetaAgentPatternCompleted OnParticlePatternCompleted;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	FString GetParticlePatternStatusText() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	FString GetParticlePatternTimingsText() const;

	TArray<FString> BuildParticleRuntimePanelStatusLines() const;

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void ApplyParticlePatternPreset(EMetaAgentParticlePatternPreset Preset);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetParticlePatternTimings(float FormDurationSeconds, float HoldDurationSeconds, float ReturnDurationSeconds);

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	FMetaAgentParticlePatternConfig GetParticlePatternConfig() const { return ParticlePatternConfig; }

	/** Pushes controller pattern config into the transient particle runtime object. */
	void SyncParticlePatternConfigToRuntime();
	void EnsureParticleOrchestrator();
	void SyncOrchestratorFromControllerDefaults();

	/** Resolves and pushes shape context (texture, plane, baselines) into particle runtime. */
	bool PrepareParticlePatternShapeContext();

	/** Starts async full-resolution mask extraction when image silhouettes are enabled. */
	void RequestParticleImageMaskBuild();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetParticlePatternShape(EMetaAgentParticlePatternShape ShapeType);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetParticlePatternImageThreshold(float Threshold);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetParticlePatternImageSamplingMode(EMetaAgentParticleImageSamplingMode SamplingMode);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetParticlePatternEdgeThreshold(float Threshold);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetParticlePatternShapeWidth(float WidthCm);

	/** Stratification grid scale (higher = particles scatter across more of the image). */
	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetParticlePatternDensityGridScale(float GridScale);

	/** Per-particle jitter within a stratification cell (0-1, higher = more offset). */
	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetParticlePatternTargetJitter(float JitterNormalized);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetParticlePatternFormingMode(EMetaAgentParticleFormingMode FormingMode);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void CycleParticlePatternFormingMode();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetParticlePatternReturnMode(EMetaAgentParticleReturnMode ReturnMode);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void CycleParticlePatternReturnMode();

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	FString GetParticlePatternShapeText() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Orchestrator")
	UMetaAgentParticleOrchestrator* GetParticleOrchestrator() const { return ParticleOrchestrator; }

	UMetaAgentParticleRuntime* GetParticleRuntime() const;

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator")
	FMetaAgentParticleEffectResult TriggerParticleEffect(FName EffectId);

	/** Resolves shape inputs (texture, plane, baselines) for the next pattern run. */
	FMetaAgentParticleShapeContext BuildParticleShapeContext();

	/** Loads sdxl_latest.png when needed for image silhouette shapes. */
	bool EnsureParticlePreviewTextureLoaded(FString& OutResolvedPath);

	UTexture2D* GetLatestPngPreviewTexture() const;
	void SetLatestPngPreviewTexture(UTexture2D* Texture);
	FString GetLastLoadedPreviewImagePath() const;
	void SetLastLoadedPreviewImagePath(const FString& Path);
	UStaticMeshComponent* GetExistingPreviewPlaneMesh() const;
	void CacheExistingPreviewPlaneMesh(UStaticMeshComponent* Mesh);

	/** Builds lines for the dedicated recording runtime GUI panel. */
	TArray<FString> BuildRecordingRuntimePanelLines() const;

	/** Applies the runtime GUI help panel state to the HUD. */
	void ApplyGUIHelpPanelState();

	/** Switches between game-only and game+UI input while the controls panel is open. */
	void ApplyGUIInteractionInputModeFromPanelState();

	bool IsModularRuntimeEnabled(EMetaAgentModularRuntime Runtime) const;
	bool IsGUIInteractionModeActive() const;
	void SetModularRuntimeEnabled(EMetaAgentModularRuntime Runtime, bool bEnabled);
	void ToggleModularRuntime(EMetaAgentModularRuntime Runtime);

	/** Validates an app command against the live host session via metaagent core. */
	bool CanExecuteAppCommand(metaagent::app::CommandId Command, FString* OutUserMessage = nullptr) const;

	/** Builds a host session snapshot from plugin settings and live modular runtime flags. */
	FMetaAgentHostSessionSnapshot BuildHostSessionSnapshot() const;

	/** Applies MetaAgent-owned cinematic view focused on particles (startup default). */
	void ApplyStartupParticleFocusView();

	/** Waits for particles to spawn, then locks a stable observation focus once. */
	void TickStartupParticleFocusLock();

	/** Toggles autopilot from GUI clicks (skips keyboard debounce). */
	void ToggleAutopilotFromGUI();

	/** Applies character-input runtime enable/disable to Enhanced Input and ignore flags. */
	void ApplyCharacterInputRuntimeState();

	/** Applies default modular runtime OFF/ON state when play begins. */
	void ApplyInitialModularRuntimeStates();

	/** Expands or collapses a runtime section body in the controls panel. */
	void ToggleRuntimeSectionExpanded(FName RuntimeId);

protected:

	/** Resets one-shot movement diagnostics after each possession change. */
	void ResetMovementDiagnosticsState();

	/**
	 * Applies mouse-wheel camera zoom with clamping and interpolation.
	 * This is the primary extension point for camera distance behavior.
	 */
	void ApplyMouseWheelZoom(APawn* ControlledPawn, float DeltaTime);

	/** Mouse-wheel zoom for locked cinematic particle observation. */
	void ApplyCinematicMouseWheelZoom();

	/** Reads mouse position in HUD canvas space for panel hit-testing. */
	bool GetViewportCanvasMousePos(float& OutX, float& OutY) const;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

	/** Applies raw keyboard fallback movement when enhanced input is unavailable. */
	void ApplyFallbackMovementInput(APawn* ControlledPawn);

	/** Applies raw mouse fallback look when enhanced input is unavailable. */
	void ApplyFallbackLookInput();

	void EnsureEnhancedInputMappingContexts();
	void RemoveEnhancedInputMappingContexts();

	/** Emits one-shot animation and movement diagnostics after motion begins. */
	void LogMovementAnimationDiagnostics(APawn* ControlledPawn);

	/** Tracks a short movement telemetry window while input is active. */
	void UpdateMovementProbe(APawn* ControlledPawn, float DeltaTime);

	/** Ensures discovered Niagara components route export callback events to this controller. */
	void EnsureParticleExportCallbackBindings(bool bLogBindings = false);

	/** Camera zoom configuration and current zoom target. */
	UPROPERTY(EditAnywhere, Category = "Camera|Zoom")
	FMetaAgentCameraZoomState CameraZoom;

	/** Generated camera mode configuration and runtime state. */
	UPROPERTY(EditAnywhere, Category = "Camera|Mode")
	FMetaAgentCameraModeState CameraMode;

	/** Raw keyboard/mouse fallback configuration plus input setup runtime flags. */
	UPROPERTY(EditAnywhere, Category = "Input|Fallback")
	FMetaAgentInputFallbackState InputFallback;

	/** One-shot and probe-style movement diagnostics runtime state. */
	UPROPERTY(Transient)
	FMetaAgentMovementDiagnosticsState MovementDiagnostics;

	/** Runtime AI handoff state and autopilot configuration. */
	UPROPERTY(EditAnywhere, Category = "AI|Autopilot")
	FMetaAgentAutopilotState Autopilot;

	/** Runtime frame-capture recording state. */
	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording")
	FMetaAgentRecordingState Recording;

	/** Cinematic orbit camera tuning and runtime state. */
	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic")
	FMetaAgentCinematicCameraState CinematicCamera;

	UPROPERTY(Transient)
	bool bCinematicFocusParticles = true;

	/** Runtime GUI panel visibility and keybind help lines. */
	UPROPERTY(EditAnywhere, Category = "UI|Runtime")
	FMetaAgentGUIState GUI;

	/** Orchestrator class (subclass to extend particle effects). */
	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Orchestrator")
	TSubclassOf<UMetaAgentParticleOrchestrator> ParticleOrchestratorClass;

	/** Default pattern config copied into the orchestrator at startup. */
	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Pattern")
	FMetaAgentParticlePatternConfig ParticlePatternConfig;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Pattern")
	FGameplayTagContainer BlockedPatternTags;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Pattern")
	EMetaAgentParticleActuationMode ParticleActuationMode = EMetaAgentParticleActuationMode::Hybrid;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Pattern")
	TObjectPtr<UMetaAgentParticlePatternAsset> DefaultParticlePatternAsset = nullptr;

	void BindParticleRuntimeDelegates();

	UFUNCTION()
	void HandleParticlePatternStateEntered(
		EMetaAgentParticlePatternState NewState,
		EMetaAgentParticlePatternState PreviousState);

	UFUNCTION()
	void HandleParticlePatternCompleted();

	/** Particle capture, FSM choreography, and effect routing. */
	UPROPERTY(Transient)
	TObjectPtr<UMetaAgentParticleOrchestrator> ParticleOrchestrator;

	/** Niagara user object parameter bound to the C++ export handler (BP_NIAGARA_1 uses User.Export particle data). */
	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles")
	FName NiagaraExportUserVariableName = TEXT("User.Export particle data");

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Particles", meta=(ClampMin="1.0", ClampMax="6.0"))
	float ParticleObservationPaddingScale = 3.5f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Particles", meta=(ClampMin="200.0", ClampMax="5000.0"))
	float ParticleObservationMinOrbitRadius = 450.0f;

	UPROPERTY(Transient)
	bool bHasLockedParticleFocus = false;

	UPROPERTY(Transient)
	FVector LockedParticleFocusPoint = FVector::ZeroVector;

	UPROPERTY(Transient)
	float LockedParticleFocusOrbitRadius = 450.0f;

	UPROPERTY(Transient)
	float LockedParticleFocusHeightOffset = 100.0f;

	/** Frames left to capture a stable particle focus target after startup. */
	UPROPERTY(Transient)
	int32 StartupParticleFocusFramesRemaining = 0;

	/** Rebind every frame for this many ticks after startup to win over level blueprint BeginPlay. */
	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles", meta=(ClampMin="0", ClampMax="600"))
	int32 ParticleCallbackAggressiveRebindFrames = 120;

	/** Rebind callback handler object every N frames after the aggressive startup window. */
	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles", meta=(ClampMin="1", ClampMax="600"))
	int32 ParticleCallbackRebindEveryNFrames = 5;

	UPROPERTY(Transient)
	TObjectPtr<UMetaAgentNiagaraExportHandler> ParticleExportHandler;

	UPROPERTY(Transient)
	int32 ParticleCallbackRebindFrameCounter = 0;

	UPROPERTY(Transient)
	int32 ParticleCallbackStartupFrameCounter = 0;

	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<UNiagaraComponent>> NiagaraExportBoundComponents;

	friend struct FMetaAgentCameraRuntime;
};

namespace metaagent::camera
{
struct FocusTarget;
}

struct FMetaAgentCameraRuntime
{
	static metaagent::camera::FocusTarget ResolveFocusTarget(const AMetaAgentPlayerController& Controller);

	static void RunEnvironmentZoomSequence(
		AMetaAgentPlayerController& Controller,
		float DeltaTime,
		FMetaAgentCameraZoomState& CameraZoom);

	static void RunToggleCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentCinematicCameraState& CinematicCamera);

	static void RunEnableCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentCinematicCameraState& CinematicCamera);

	static void RunDisableCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		FMetaAgentCinematicCameraState& CinematicCamera);

	static void RunUpdateCinematicCameraSequence(
		AMetaAgentPlayerController& Controller,
		float DeltaTime,
		FMetaAgentCinematicCameraState& CinematicCamera);

	static void RunRefreshCinematicFocus(
		AMetaAgentPlayerController& Controller,
		FMetaAgentCinematicCameraState& CinematicCamera);

	static const TCHAR* GetCinematicStyleLabel(EMetaAgentCinematicCameraStyle Style);
};

