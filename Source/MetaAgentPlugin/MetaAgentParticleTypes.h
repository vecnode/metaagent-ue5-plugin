// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MetaAgentParticleTypes.generated.h"

class UCurveFloat;
class UTexture2D;
class UMetaAgentParticlePatternAsset;

UENUM(BlueprintType)
enum class EMetaAgentParticleFormingMode : uint8
{
	/** Straight baseline → target lerp (default). */
	DirectLerp UMETA(DisplayName = "Direct Lerp"),
	/** Lift along world up mid-form, then settle on target. */
	ArcLift UMETA(DisplayName = "Arc Lift"),
	/** Spiral inward toward the pattern center. */
	SpiralIn UMETA(DisplayName = "Spiral In"),
	/** Per-particle phase offset so the shape fills in as a wave. */
	StaggeredWave UMETA(DisplayName = "Staggered Wave"),
	/** Overshoot + settle easing on arrival. */
	SpringChase UMETA(DisplayName = "Spring Chase")
};

USTRUCT(BlueprintType)
struct FMetaAgentParticleFormingSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming")
	EMetaAgentParticleFormingMode Mode = EMetaAgentParticleFormingMode::DirectLerp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.0"))
	float ArcLiftHeightCm = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.0"))
	float SpiralTurns = 1.5f;

	/** StaggeredWave: fraction of global forming time used as per-particle delay spread (0–0.9). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float WaveSpread = 0.45f;

	/** SpringChase: peak overshoot past the target (0 = none, ~0.2 = soft bounce). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float SpringOvershoot = 0.12f;

	METAAGENTPLUGIN_API FString GetModeDisplayName() const;
	METAAGENTPLUGIN_API void CycleMode();
	METAAGENTPLUGIN_API static EMetaAgentParticleFormingMode SanitizeMode(EMetaAgentParticleFormingMode Mode);
};

/** Per-particle input for metaagent forming solvers (bridge-only, not Blueprint). */
struct FMetaAgentParticleFormingContext
{
	int32 GlobalIndex = INDEX_NONE;
	int32 TotalParticleCount = 0;
	FVector Baseline = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	FVector PatternCenter = FVector::ZeroVector;
	float BlendAlpha = 0.0f;
	float StateElapsedSeconds = 0.0f;
	float FormDurationSeconds = 1.0f;
	float DeltaTimeSeconds = 0.0f;
	const FMetaAgentParticleFormingSettings* Settings = nullptr;
	float FormingSteeringWeight = 0.0f;
	FVector FormingSteeringOffset = FVector::ZeroVector;
};

class UCurveFloat;

UENUM(BlueprintType)
enum class EMetaAgentParticleReturnMode : uint8
{
	/** Straight rest → hold lerp (default). */
	DirectLerp UMETA(DisplayName = "Direct Lerp"),
	/** Lift along world up mid-return, then settle on rest. */
	ArcLift UMETA(DisplayName = "Arc Lift"),
	/** Spiral outward from pattern center while returning to rest. */
	SpiralIn UMETA(DisplayName = "Spiral In"),
	/** Collapse toward pattern center and fade out instead of a positional return. */
	DissipateToCenter UMETA(DisplayName = "Dissipate To Center")
};

USTRUCT(BlueprintType)
struct FMetaAgentParticleReturnSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning")
	EMetaAgentParticleReturnMode Mode = EMetaAgentParticleReturnMode::DirectLerp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning", meta = (ClampMin = "0.0"))
	float ArcLiftHeightCm = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning", meta = (ClampMin = "0.0"))
	float SpiralTurns = 1.5f;

	/** Optional easing curve for DirectLerp return (normalized return time 0–1 → blend weight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning|Curves")
	TObjectPtr<UCurveFloat> DirectLerpReturnCurve = nullptr;

	/** Optional easing curve for ArcLift return. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning|Curves")
	TObjectPtr<UCurveFloat> ArcLiftReturnCurve = nullptr;

	/** Optional easing curve for SpiralIn return. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning|Curves")
	TObjectPtr<UCurveFloat> SpiralInReturnCurve = nullptr;

	METAAGENTPLUGIN_API FString GetModeDisplayName() const;

	METAAGENTPLUGIN_API const UCurveFloat* GetReturnCurveForMode() const;

	METAAGENTPLUGIN_API void CycleMode();

	METAAGENTPLUGIN_API bool UsesMotionSolver() const;

	METAAGENTPLUGIN_API FMetaAgentParticleFormingSettings AsFormingSettings() const;

	METAAGENTPLUGIN_API static EMetaAgentParticleReturnMode SanitizeMode(EMetaAgentParticleReturnMode Mode);
};

class UStaticMeshComponent;
class UTexture2D;
class UWorld;

UENUM(BlueprintType)
enum class EMetaAgentParticlePatternShape : uint8
{
	SquareGrid,
	ImageSilhouette,
	SplinePath,
	MeshSilhouette
};

UENUM(BlueprintType)
enum class EMetaAgentParticleShapeAssignmentMode : uint8
{
	NearestNeighbor,
	Ordered,
	/** One-to-one polar angle matching — reduces target stacking. */
	PolarMatched
};

/** Where the grayscale image shape is anchored in world space. */
UENUM(BlueprintType)
enum class EMetaAgentParticleShapeAnchor : uint8
{
	/** Center on the particle cloud; preview plane is texture-only (F-key). */
	ParticleCentroid,
	/** Match the F-key preview plane transform (legacy compositing). */
	PreviewPlane
};

/** How image pixels are turned into particle target points. */
UENUM(BlueprintType)
enum class EMetaAgentParticleImageSamplingMode : uint8
{
	/** Strong edges only (Sobel) — outlines look like the image. */
	SobelEdges,
	/** All pixels above alpha/luminance threshold — filled plane look. */
	FilledSilhouette,
	/** Grayscale-weighted stratified scatter — default image look. */
	GrayscaleDensity
};

/** World-space placement frame for a 2D shape extruded on a plane. */
USTRUCT(BlueprintType)
struct FMetaAgentParticleShapeFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	FRotator Orientation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	FVector2D ExtentsCm = FVector2D(200.0f, 200.0f);

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	float ZOffsetCm = 2.0f;

};

/** Shape-specific tuning carried on the pattern config. */
USTRUCT(BlueprintType)
struct FMetaAgentParticleShapeDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	EMetaAgentParticlePatternShape ShapeType = EMetaAgentParticlePatternShape::ImageSilhouette;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	EMetaAgentParticleImageSamplingMode ImageSamplingMode = EMetaAgentParticleImageSamplingMode::GrayscaleDensity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AlphaThreshold = 0.08f;

	/** Sobel gradient magnitude threshold (normalized 0-1). Lower = more edge pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float EdgeThreshold = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	bool bUseLuminance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "32", ClampMax = "4096"))
	int32 SampleResolution = 1024;

	/** Exponent on grayscale concentration (>1 = clusters in dark regions; lower = flatter scatter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "0.25", ClampMax = "4.0"))
	float GrayscaleGamma = 1.0f;

	/** Stratification grid scale: grid cells ≈ sqrt(particleCount * this). Applies to Gray, Sobel, and Fill sampling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "1.0", ClampMax = "16.0"))
	float DensityGridScale = 5.0f;

	/** Sub-cell jitter as a fraction of one stratification cell. Applies to Gray, Sobel, and Fill sampling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TargetJitterNormalized = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	bool bUseLoadedPreviewTexture = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	EMetaAgentParticleShapeAnchor ShapeAnchor = EMetaAgentParticleShapeAnchor::ParticleCentroid;

	/** Scale the image to fit the particle cloud bounding sphere (ignores ShapeWidthCm when enabled). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	bool bAutoFitShapeToParticleSphere = true;

	/** Rotate the image plane to face the active view origin (player camera). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	bool bOrientShapeToView = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "10.0", EditCondition = "!bAutoFitShapeToParticleSphere"))
	float ShapeWidthCm = 200.0f;

	/** When zero, height is derived from image aspect ratio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "0.0"))
	float ShapeHeightCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	float ZOffsetCm = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	EMetaAgentParticleShapeAssignmentMode AssignmentMode = EMetaAgentParticleShapeAssignmentMode::PolarMatched;

	/** Actor/component tag used by SplinePath and MeshSilhouette providers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape|Procedural")
	FName ShapeSourceActorTag = TEXT("MetaAgentPatternShapeSource");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape|Procedural", meta = (ClampMin = "4", ClampMax = "512"))
	int32 ProceduralSampleCount = 64;

	FString GetShapeDisplayName() const;

	FString GetImageSamplingDisplayName() const;

	METAAGENTPLUGIN_API static EMetaAgentParticleImageSamplingMode SanitizeImageSamplingMode(
		EMetaAgentParticleImageSamplingMode Mode);
};

/** Inputs resolved at pattern start (texture, plane, baselines). */
USTRUCT(BlueprintType)
struct FMetaAgentParticleShapeContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	TArray<FVector> BaselineWorldPositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	TObjectPtr<UTexture2D> SourceTexture = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	TObjectPtr<UStaticMeshComponent> PreviewPlaneMesh = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	FString SourceImagePath;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	FVector ViewOrigin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	bool bHasViewOrigin = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	bool bHasResolvedImage = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	TObjectPtr<UWorld> World = nullptr;
};

USTRUCT(BlueprintType)
struct FMetaAgentParticleShapeBuildResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	EMetaAgentParticlePatternShape ResolvedShape = EMetaAgentParticlePatternShape::SquareGrid;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	FMetaAgentParticleShapeFrame ShapeFrame;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	int32 ShapePointCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	int32 PatternColumns = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	FVector PatternCenter = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	FString DebugInfo;

	/** True when silhouette points are still being built on a worker thread. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	bool bAwaitingAsyncMask = false;

	UPROPERTY()
	TArray<FVector> PatternWorldTargets;
};

class UCurveFloat;

UENUM(BlueprintType)
enum class EMetaAgentParticlePatternState : uint8
{
	Idle,
	Preparing,
	/** Small attraction/orbit motion on baselines before forming begins. */
	Anticipating,
	Forming,
	Holding,
	Returning,
	/** Collapse held particles toward pattern center and fade out. */
	Dissipating,
	/** Deprecated: unused; return now follows live sim directly. Kept for Blueprint compat. */
	Releasing UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EMetaAgentParticlePatternPreset : uint8
{
	Normal,
	Slow,
	Dramatic,
	Snappy,
	Dreamy,
	Custom
};

UENUM(BlueprintType)
enum class EMetaAgentParticleActuationMode : uint8
{
	/** Direct Niagara Position buffer read/write. */
	Direct,
	/** Push phase/center/active via Niagara user parameters. */
	Parameters,
	/** Direct in editor/PIE; Parameters in packaged builds. */
	Hybrid
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnMetaAgentPatternStateChanged,
	EMetaAgentParticlePatternState, NewState,
	EMetaAgentParticlePatternState, PreviousState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMetaAgentPatternCompleted);

USTRUCT(BlueprintType)
struct FMetaAgentParticlePatternConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern", meta = (ClampMin = "0.1"))
	float FormDurationSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern", meta = (ClampMin = "0.0"))
	float HoldDurationSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern", meta = (ClampMin = "0.1"))
	float ReturnDurationSeconds = 1.5f;

	/** Duration for DissipateToCenter collapse toward pattern center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Dissipating", meta = (ClampMin = "0.1"))
	float DissipateDurationSeconds = 1.2f;

	/** Peak pull toward pattern center during Anticipating (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Anticipating", meta = (ClampMin = "0.0"))
	float AnticipationAmplitudeCm = 12.0f;

	/** Orbit/twitch frequency during Anticipating (Hz). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Anticipating", meta = (ClampMin = "0.1"))
	float AnticipationFrequencyHz = 1.2f;

	/** Seconds to ramp anticipation motion in from idle at pattern start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Anticipating", meta = (ClampMin = "0.05"))
	float AnticipationIdleBlendDurationSeconds = 0.35f;

	/** Seconds to crossfade continuing anticipation motion into forming after handoff. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Anticipating", meta = (ClampMin = "0.05"))
	float FormingAnticipationCarryoverDurationSeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern", meta = (ClampMin = "1.0"))
	float GridSpacingCm = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern")
	FMetaAgentParticleShapeDefinition Shape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming")
	FMetaAgentParticleFormingSettings Forming;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning")
	FMetaAgentParticleReturnSettings Return;

	/** Sinusoidal hold breathe amplitude during Sustain (0 = off, ~0.04 = subtle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Hold", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HoldPulseAmplitude = 0.04f;

	/** Hold breathe frequency in Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Hold", meta = (ClampMin = "0.1"))
	float HoldPulseFrequencyHz = 0.75f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	EMetaAgentParticlePatternPreset ActivePreset = EMetaAgentParticlePatternPreset::Normal;

	METAAGENTPLUGIN_API static FMetaAgentParticlePatternConfig MakeFromPreset(EMetaAgentParticlePatternPreset Preset);

	METAAGENTPLUGIN_API void ApplyPreset(EMetaAgentParticlePatternPreset Preset);

	METAAGENTPLUGIN_API FString GetPresetDisplayName() const;
};

USTRUCT(BlueprintType)
struct FMetaAgentParticlePatternRuntime
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	EMetaAgentParticlePatternState State = EMetaAgentParticlePatternState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	float Phase = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	float StateElapsedSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	int32 PatternColumns = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FVector PatternCenter = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	EMetaAgentParticlePatternShape ActiveShape = EMetaAgentParticlePatternShape::SquareGrid;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FMetaAgentParticleShapeFrame ActiveShapeFrame;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FString ShapeDebugInfo;

	/** Timings frozen when the current pattern run started. */
	UPROPERTY()
	FMetaAgentParticlePatternConfig ActiveConfig;

	UPROPERTY()
	TArray<FVector> BaselineWorldPositions;

	/** Idle snapshot at pattern start; used for return rest targets after forming baselines are updated. */
	UPROPERTY()
	TArray<FVector> IdleBaselineWorldPositions;

	UPROPERTY()
	TArray<FVector> PatternWorldTargets;

	/** Mask targets from the last successful shape build; not overwritten by manual hold freezes. */
	UPROPERTY()
	TArray<FVector> CanonicalPatternWorldTargets;

	/** Positions frozen when Returning begins (end of Holding actuation). */
	UPROPERTY()
	TArray<FVector> ReturnHoldPositions;

	/** Frozen idle rest positions for return (captured once when Returning begins). */
	UPROPERTY()
	TArray<FVector> ReturnRestPositions;

	/** Positions when Holding began (formed shape reference). */
	UPROPERTY()
	TArray<FVector> TrajectoryWorldPositions;

	/** Positions frozen when Dissipating begins. */
	UPROPERTY()
	TArray<FVector> DissipateStartPositions;

	UPROPERTY()
	bool bAwaitingAsyncMask = false;

	/** Anticipation elapsed time at Anticipating→Forming handoff; negative when carryover is inactive. */
	float AnticipationHandoffElapsedSeconds = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FGameplayTagContainer ActivePatternTags;
};

/** Macro phases for the representation scheduler (maps many micro-states). */
UENUM(BlueprintType)
enum class EMetaAgentRepresentationMacroPhase : uint8
{
	Idle,
	/** Anticipating / async prepare. */
	Prepare,
	/** Forming / morph-in. */
	Express,
	/** Holding / sustain. */
	Sustain,
	/** Returning / dissipating. */
	Release
};

UENUM(BlueprintType)
enum class EMetaAgentPatternTransitionTrigger : uint8
{
	Start,
	Advance,
	Retreat,
	Timeout,
	Cancel,
	Morph,
	Dissipate,
	/** Async mask / targets became ready during Prepare. */
	Ready
};

USTRUCT(BlueprintType)
struct FMetaAgentRepresentationPhase
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float NormalizedTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float BlendAlpha = 0.0f;

	/** 1 = C++ owns positions; 0 = Niagara owns sim. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AuthorityWeight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float Visibility = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float Emphasis = 1.0f;
};

/**
 * Snapshot emitted each active-pattern tick by the representation scheduler.
 * Drivers consume this frame to push Niagara parameters and/or direct positions.
 */
USTRUCT(BlueprintType)
struct FMetaAgentParticleRepresentationFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	EMetaAgentRepresentationMacroPhase MacroPhase = EMetaAgentRepresentationMacroPhase::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	EMetaAgentParticlePatternState PatternState = EMetaAgentParticlePatternState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	FMetaAgentRepresentationPhase Phase;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	FVector PatternCenter = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	bool bPatternActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	bool bAnticipatingMotion = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	bool bDissipatingMotion = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	bool bUseReturnHoldBlend = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AnticipationElapsedSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AnticipationAmplitudeCm = 12.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AnticipationFrequencyHz = 1.2f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AnticipationIdleBlendDurationSeconds = 0.35f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AnticipationHandoffElapsedSeconds = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float FormingAnticipationCarryoverDurationSeconds = 0.35f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float FormingStateElapsedSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float FormingDurationSeconds = 1.5f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float FormingDeltaTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float FormingSteeringWeight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float DissipateVisibility = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	FMetaAgentParticleFormingSettings FormingSettings;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	bool bReturnUsesMotionSolver = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	FMetaAgentParticleFormingSettings ReturnMotionSettings;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> BaselineWorldPositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> PatternWorldTargets;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> IdleBaselineWorldPositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> ReturnHoldPositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> ReturnRestPositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> DissipateStartPositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> FormingSteeringOffsets;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> StateEffectOffsets;
};

class METAAGENTPLUGIN_API FMetaAgentParticleRepresentationMapping
{
public:
	static EMetaAgentRepresentationMacroPhase MacroPhaseFromPatternState(EMetaAgentParticlePatternState State);
	static FString GetMacroPhaseDisplayName(EMetaAgentRepresentationMacroPhase MacroPhase);
};

/** Built-in effect identifiers (extend in subclasses via PopulateEffectSpec). */
namespace MetaAgentParticleEffectIds
{
	METAAGENTPLUGIN_API extern const FName ImageReveal;
	METAAGENTPLUGIN_API extern const FName SplinePath;
	METAAGENTPLUGIN_API extern const FName MeshSilhouette;
	METAAGENTPLUGIN_API extern const FName GridSquare;
	METAAGENTPLUGIN_API extern const FName PresetSlow;
	METAAGENTPLUGIN_API extern const FName PresetDramatic;
	METAAGENTPLUGIN_API extern const FName PresetSnappy;
	METAAGENTPLUGIN_API extern const FName PresetDreamy;
	METAAGENTPLUGIN_API extern const FName PlayNormal;
	METAAGENTPLUGIN_API extern const FName PlaySlow;
	METAAGENTPLUGIN_API extern const FName PlayDramatic;
	METAAGENTPLUGIN_API extern const FName PlaySnappy;
	METAAGENTPLUGIN_API extern const FName PlayDreamy;
	METAAGENTPLUGIN_API extern const FName PatternMorph;
	METAAGENTPLUGIN_API extern const FName ReplayLast;
	METAAGENTPLUGIN_API extern const FName CycleSampling;
	METAAGENTPLUGIN_API extern const FName CycleForming;
	METAAGENTPLUGIN_API extern const FName CycleReturning;
	METAAGENTPLUGIN_API extern const FName PatternStepForward;
	METAAGENTPLUGIN_API extern const FName PatternStepBackward;
	METAAGENTPLUGIN_API extern const FName DissipateToCenter;
	METAAGENTPLUGIN_API extern const FName AttractToView;
}

USTRUCT(BlueprintType)
struct FMetaAgentParticleEffectResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Effect")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Effect")
	FName EffectId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Effect")
	FText UserMessage;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Effect")
	bool bAwaitingAsyncPrepare = false;
};

/** Config applied when an effect is triggered (FSM choreography unchanged). */
USTRUCT(BlueprintType)
struct FMetaAgentParticleEffectSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	FMetaAgentParticlePatternConfig PatternConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	bool bOverridePatternConfig = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	bool bStartPattern = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	bool bSteerTowardViewOnForm = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	float SteeringStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	TObjectPtr<UMetaAgentParticlePatternAsset> PatternAsset = nullptr;
};

UCLASS(BlueprintType)
class METAAGENTPLUGIN_API UMetaAgentParticlePatternAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FMetaAgentParticlePatternConfig PatternConfig;

	/** When set, replaces PatternConfig.Shape for this asset only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Shape")
	bool bOverrideShape = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Shape", meta = (EditCondition = "bOverrideShape"))
	FMetaAgentParticleShapeDefinition ShapeOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FGameplayTagContainer PatternTags;

	/** Optional disk path override for image silhouette shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Image")
	FString SourceImagePathOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Image")
	TSoftObjectPtr<UTexture2D> SourceImageTexture;

	/** Remaps normalized forming time (0–1) to blend phase. Defaults to smoothstep when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Motion")
	TObjectPtr<UCurveFloat> FormCurve = nullptr;

	/** Remaps normalized return time (0–1) to blend phase (inverted at actuation). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Motion")
	TObjectPtr<UCurveFloat> ReturnCurve = nullptr;

	/** Sinusoidal hold pulse amplitude (0 = use PatternConfig default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Motion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HoldPulseAmplitude = 0.0f;

	/** Hold breathe frequency in Hz (0 = use PatternConfig default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Motion", meta = (ClampMin = "0.0"))
	float HoldPulseFrequencyHz = 0.0f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("MetaAgentParticlePattern"), GetFName());
	}
};