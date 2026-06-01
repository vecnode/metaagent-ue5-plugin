// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "MovieRenderPipelineDataTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "MetaAgentPlayerController.generated.h"

class AAIController;
class UInputMappingContext;
class ULevelSequence;
class UUserWidget;
class USkeletalMeshComponent;

UENUM()
enum class EMetaAgentCameraMode : uint8
{
	ThirdPerson,
	CloseOverShoulder,
	SideCinematicClose,
	FirstPerson
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

	UPROPERTY(EditAnywhere, Category = "Camera|Mode")
	EMetaAgentCameraMode ActiveMode = EMetaAgentCameraMode::ThirdPerson;

	UPROPERTY(EditAnywhere, Category = "Camera|Mode", meta=(ClampMin="0.0", ClampMax="1500.0"))
	float ThirdPersonArmLength = 400.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Mode", meta=(ClampMin="0.0", ClampMax="600.0"))
	float CloseOverShoulderArmLength = 165.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Mode", meta=(ClampMin="-200.0", ClampMax="300.0"))
	float CloseOverShoulderHeightOffset = 58.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Mode", meta=(ClampMin="-120.0", ClampMax="120.0"))
	float CloseOverShoulderLateralOffset = 18.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Mode", meta=(ClampMin="0.0", ClampMax="600.0"))
	float SideCinematicCloseArmLength = 118.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Mode", meta=(ClampMin="-200.0", ClampMax="300.0"))
	float SideCinematicCloseHeightOffset = 66.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Mode", meta=(ClampMin="-120.0", ClampMax="120.0"))
	float SideCinematicCloseLateralOffset = 46.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Mode", meta=(ClampMin="0.0", ClampMax="200.0"))
	float FirstPersonEyeHeightOffset = 12.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Mode", meta=(ClampMin="-60.0", ClampMax="120.0"))
	float FirstPersonForwardOffset = 16.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Mode", meta=(ClampMin="-60.0", ClampMax="80.0"))
	float FirstPersonVerticalOffset = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Mode")
	bool bPreferHeadSocketForFirstPerson = true;

	UPROPERTY(Transient)
	float LastThirdPersonArmLength = 400.0f;

	UPROPERTY(Transient)
	FName PreferredFirstPersonSocket = NAME_None;
};

USTRUCT()
struct FMetaAgentInputFallbackState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Input|Fallback")
	bool bEnableKeyboardMovement = true;

	UPROPERTY(EditAnywhere, Category = "Input|Fallback")
	bool bEnableMouseLook = true;

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
	bool bRenderInProgress = false;

	UPROPERTY(Transient)
	FString RenderStatusText = TEXT("Rendering: idle");

	UPROPERTY(Transient)
	FColor RenderStatusColor = FColor::Silver;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording")
	bool bRenderAt4K = false;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording")
	bool bRenderPngSequenceAlongsideMp4 = true;

	UPROPERTY(Transient)
	bool bRecordingUsesTakeRecorder = false;

	UPROPERTY(Transient)
	bool bRuntimeFrameCaptureActive = false;

	UPROPERTY(Transient)
	float RuntimeCaptureAccumulatedSeconds = 0.0f;

	UPROPERTY(Transient)
	int32 RuntimeCapturedFrameCount = 0;

	UPROPERTY(Transient)
	int32 RuntimeCaptureFrameIndex = 0;

	UPROPERTY(Transient)
	FString RuntimeCaptureOutputDirectory;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording", meta=(ClampMin="1.0", ClampMax="60.0"))
	float RuntimeCaptureFps = 15.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording", meta=(ClampMin="128", ClampMax="7680"))
	int32 RuntimeCaptureWidth = 3840;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording", meta=(ClampMin="128", ClampMax="4320"))
	int32 RuntimeCaptureHeight = 2160;
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
	float OscillationYawAmplitudeDegrees = 8.0f;

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
	float SwayHorizontalAmplitude = 8.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="0.0", ClampMax="80.0"))
	float SwayVerticalAmplitude = 4.0f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic", meta=(ClampMin="0.1", ClampMax="5.0"))
	float SwayFrequency = 0.85f;

	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic")
	bool bDisablePlayerInput = false;

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
};

/**
 * Runtime player controller that owns input setup, camera zoom behavior,
 * possession diagnostics, and optional AI autopilot handoff.
 */
UCLASS()
class AMetaAgentPlayerController : public APlayerController
{
	GENERATED_BODY()
	
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

	/** Called when this controller possesses a new pawn. Sets up third-person camera. */
	virtual void OnPossess(APawn* InPawn) override;

	/** Per-frame input fallback, diagnostics, and camera zoom updates. */
	virtual void PlayerTick(float DeltaTime) override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Requests application quit when Escape is pressed. */
	void HandleEscapePressed();

	/** Shows a quick HUD debug message when Y is pressed. */
	void HandleYPressed();

	/** Toggles between the generated third-person and first-person camera modes. */
	void HandleToggleCameraModePressed();

	/** Toggles player possession between manual control and runtime AI autopilot. */
	void HandleToggleAutopilotPressed();

	/** Submits the most recently recorded autopilot take to Movie Render Queue. */
	void HandleRenderRecordedTakePressed();

	/** Toggles recording on/off using the runtime recording backend. */
	void HandleToggleRecordingPressed();

	/** Bound to O: toggles cinematic camera mode on/off. */
	void HandleToggleCinematicCameraPressed();

	/** Bound to H: toggles runtime controls help panel on/off. */
	void HandleToggleHelpPanelPressed();

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

	/** Applies the runtime GUI help panel state to the HUD. */
	void ApplyGUIHelpPanelState();

	/** Enables AI autopilot over the currently possessed pawn. */
	void EnableAutopilotForCurrentPawn();

	/** Disables AI autopilot and repossesses the last autopilot pawn. */
	void DisableAutopilotAndRepossess();

	/** Starts high-resolution frame capture to disk. */
	void StartAutopilotTakeRecording();

	/** Stops an in-progress high-resolution frame capture. */
	void StopAutopilotTakeRecording();

	/** Reports status for captured frame output (legacy U action path). */
	void SubmitLastRecordedTakeToRenderQueue();

	/** Pushes current recording/take/render state into the persistent HUD status panel. */
	void UpdateRecordingStatusHud();

	/** Updates standalone/shipping frame capture recording at a fixed rate while active. */
	void UpdateRuntimeFrameCapture(float DeltaTime);

	/** Applies the active camera mode to the currently possessed pawn. */
	void ApplyCameraModeToPawn(APawn* InPawn);

	/** Enables the generated first-person camera mode. */
	void EnableFirstPersonCameraMode();

	/** Enables the generated close over-shoulder camera mode. */
	void EnableCloseOverShoulderCameraMode();

	/** Enables the generated tight side cinematic camera mode. */
	void EnableSideCinematicCloseCameraMode();

	/** Enables the generated third-person camera mode. */
	void EnableThirdPersonCameraMode();

	/** Receives completion callback for runtime MRQ renders. */
	UFUNCTION()
	void HandleRuntimeRenderFinished(FMoviePipelineOutputData Results);

public:

	/** Blueprint entry point so a UI button can toggle the same cinematic camera mode as V. */
	UFUNCTION(BlueprintCallable, Category = "Camera|Cinematic")
	void ToggleCinematicCameraMode();

	/** Builds lines for the dedicated recording runtime GUI panel. */
	TArray<FString> BuildRecordingRuntimePanelLines() const;

protected:

	/** Resets one-shot movement diagnostics after each possession change. */
	void ResetMovementDiagnosticsState();

	/**
	 * Configures spring arm/camera after possession.
	 * Keep camera defaults centralized here for easier camera behavior iteration.
	 */
	void ConfigureCameraForPawn(APawn* InPawn);

	/**
	 * Applies mouse-wheel camera zoom with clamping and interpolation.
	 * This is the primary extension point for camera distance behavior.
	 */
	void ApplyMouseWheelZoom(APawn* ControlledPawn, float DeltaTime);

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

	/** Applies raw keyboard fallback movement when enhanced input is unavailable. */
	void ApplyFallbackMovementInput(APawn* ControlledPawn);

	/** Applies raw mouse fallback look when enhanced input is unavailable. */
	void ApplyFallbackLookInput();

	/** Emits one-shot animation and movement diagnostics after motion begins. */
	void LogMovementAnimationDiagnostics(APawn* ControlledPawn);

	/** Tracks a short movement telemetry window while input is active. */
	void UpdateMovementProbe(APawn* ControlledPawn, float DeltaTime);

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

	/** Take Recorder and Movie Render Queue state. */
	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic|Recording")
	FMetaAgentRecordingState Recording;

	/** Cinematic orbit camera tuning and runtime state. */
	UPROPERTY(EditAnywhere, Category = "Camera|Cinematic")
	FMetaAgentCinematicCameraState CinematicCamera;

	/** Runtime GUI panel visibility and keybind help lines. */
	UPROPERTY(EditAnywhere, Category = "UI|Runtime")
	FMetaAgentGUIState GUI;

};

