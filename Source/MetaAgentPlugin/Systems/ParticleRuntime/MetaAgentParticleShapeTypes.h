// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "MetaAgentParticleShapeTypes.generated.h"

class UStaticMeshComponent;
class UTexture2D;
class UWorld;

UENUM(BlueprintType)
enum class EMetaAgentParticlePatternShape : uint8
{
	SquareGrid,
	ImageSilhouette,
	SplinePath,
	MeshSilhouette,
	/** Random 3D box inside the particle bounding sphere (C-key sculpt). */
	RandomParallelepiped
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

	/** Half-extents (cm) for volume shapes (RandomParallelepiped). */
	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	FVector VolumeHalfExtentsCm = FVector(50.0f, 50.0f, 50.0f);

	/** When true, local shape points use VolumeHalfExtentsCm on all three axes. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Shape")
	bool bUseVolumeFrame = false;
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

	/** Exponent on grayscale concentration (>1 = more weight on dark/dense regions). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "0.25", ClampMax = "4.0"))
	float GrayscaleGamma = 1.2f;

	/** Grid density multiplier for stratified scatter (higher = more spread). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float DensityGridScale = 2.5f;

	/** Sub-cell jitter as a fraction of one grid cell (breaks exact overlaps). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TargetJitterNormalized = 0.35f;

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

	/** RandomParallelepiped: minimum half-axis as a fraction of cloud bounding radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape|RandomBox", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float BoxMinSizeFractionOfSphere = 0.35f;

	/** RandomParallelepiped: maximum half-axis as a fraction of cloud bounding radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape|RandomBox", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float BoxMaxSizeFractionOfSphere = 0.85f;

	/** RandomParallelepiped: max box corner distance as a fraction of bounding radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape|RandomBox", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float BoxMaxCornerFractionOfSphere = 0.90f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape|RandomBox", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BoxVolumeSampleFraction = 0.40f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape|RandomBox", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BoxSurfaceSampleFraction = 0.40f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape|RandomBox", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BoxHaloSampleFraction = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape|RandomBox", meta = (ClampMin = "1.0", ClampMax = "1.5"))
	float BoxHaloOutwardScaleMin = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape|RandomBox", meta = (ClampMin = "1.0", ClampMax = "1.5"))
	float BoxHaloOutwardScaleMax = 1.35f;

	/** 0 = pick a new random seed each pattern start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape|RandomBox")
	int32 BoxRandomSeed = 0;

	FString GetShapeDisplayName() const;

	FString GetImageSamplingDisplayName() const;
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
