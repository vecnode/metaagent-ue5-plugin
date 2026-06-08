// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "MetaAgentParticleShapeTypes.generated.h"

class UStaticMeshComponent;
class UTexture2D;

UENUM(BlueprintType)
enum class EMetaAgentParticlePatternShape : uint8
{
	SquareGrid,
	ImageSilhouette
};

UENUM(BlueprintType)
enum class EMetaAgentParticleShapeAssignmentMode : uint8
{
	NearestNeighbor,
	Ordered
};

/** How image pixels are turned into particle target points. */
UENUM(BlueprintType)
enum class EMetaAgentParticleImageSamplingMode : uint8
{
	/** Strong edges only (Sobel) — outlines look like the image. */
	SobelEdges,
	/** All pixels above alpha/luminance threshold — filled plane look. */
	FilledSilhouette
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
	EMetaAgentParticleImageSamplingMode ImageSamplingMode = EMetaAgentParticleImageSamplingMode::SobelEdges;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AlphaThreshold = 0.35f;

	/** Sobel gradient magnitude threshold (normalized 0-1). Lower = more edge pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float EdgeThreshold = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	bool bUseLuminance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "32", ClampMax = "1024"))
	int32 SampleResolution = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	bool bUseLoadedPreviewTexture = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	bool bAlignToPreviewPlane = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "10.0"))
	float ShapeWidthCm = 200.0f;

	/** When zero, height is derived from image aspect ratio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape", meta = (ClampMin = "0.0"))
	float ShapeHeightCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	float ZOffsetCm = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Shape")
	EMetaAgentParticleShapeAssignmentMode AssignmentMode = EMetaAgentParticleShapeAssignmentMode::NearestNeighbor;

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
	bool bHasResolvedImage = false;
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

	UPROPERTY()
	TArray<FVector> PatternWorldTargets;
};
