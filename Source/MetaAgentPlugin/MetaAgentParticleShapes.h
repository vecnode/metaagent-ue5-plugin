// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "MetaAgentParticleTypes.h"

class AMetaAgentPlayerController;
class UStaticMeshComponent;
class UTexture2D;
class UWorld;

struct FMetaAgentImageMaskBuildParams
{
	FString SourceImagePath;
	FDateTime SourceFileTimestamp;
	int64 SourceFileSize = 0;
	EMetaAgentParticleImageSamplingMode ImageSamplingMode = EMetaAgentParticleImageSamplingMode::SobelEdges;
	float AlphaThreshold = 0.35f;
	float EdgeThreshold = 0.12f;
	bool bUseLuminance = true;
	int32 SampleResolution = 1024;
	float GrayscaleGamma = 1.0f;
	float DensityGridScale = 5.0f;
	float TargetJitterNormalized = 0.7f;
	int32 DesiredPointCount = 0;
};

struct FMetaAgentImageMaskBuildOutput
{
	bool bSuccess = false;
	TArray<FVector> LocalPointsCm;
	FString DebugInfo;
};

namespace MetaAgentImageMask
{
	bool GetImageFileIdentity(const FString& SourceImagePath, FDateTime& OutTimestamp, int64& OutFileSize);

	FMetaAgentImageMaskBuildParams MakeBuildParams(
		const FString& SourceImagePath,
		const FMetaAgentParticleShapeDefinition& ShapeDefinition,
		const int32 DesiredPointCount);

	bool BuildMaskOnWorkerThread(const FMetaAgentImageMaskBuildParams& Params, FMetaAgentImageMaskBuildOutput& OutOutput);
}

/** Pluggable shape backend. Higher priority wins when multiple providers share a shape type. */
class METAAGENTPLUGIN_API IMetaAgentParticleShapeProvider
{
public:
	virtual ~IMetaAgentParticleShapeProvider() = default;

	virtual EMetaAgentParticlePatternShape GetShapeType() const = 0;

	virtual FName GetShapeId() const = 0;

	virtual int32 GetPriority() const { return 0; }

	virtual bool CanBuild(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext) const = 0;

	virtual bool RequiresAsyncPrepare(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext) const
	{
		return false;
	}

	virtual bool BuildTargets(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext,
		FMetaAgentParticleShapeBuildResult& OutResult) = 0;
};

/**
 * Builds pattern world targets from shape definitions and resolved runtime context.
 */
class METAAGENTPLUGIN_API FMetaAgentParticleShapeBuilder
{
public:
	static FMetaAgentParticleShapeBuildResult BuildPatternTargets(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext);

	static void InvalidateImageMaskCache();

	static void RequestImageMaskBuild(
		const FString& SourceImagePath,
		const FMetaAgentParticleShapeDefinition& ShapeDefinition,
		const int32 DesiredPointCount);

	static bool BuildSquareGridTargets(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext,
		FMetaAgentParticleShapeBuildResult& OutResult);

	static bool BuildImageSilhouetteTargets(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext,
		FMetaAgentParticleShapeBuildResult& OutResult);

	static bool BuildSplinePathTargets(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext,
		FMetaAgentParticleShapeBuildResult& OutResult);

	static bool BuildMeshSilhouetteTargets(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext,
		FMetaAgentParticleShapeBuildResult& OutResult);

	static FMetaAgentParticleShapeFrame ResolveShapeFrame(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext,
		const UTexture2D* SourceTexture);

	static bool ExtractSilhouetteLocalPoints(
		const FString& SourceImagePath,
		const FMetaAgentParticleShapeDefinition& ShapeDefinition,
		const int32 DesiredPointCount,
		TArray<FVector>& OutLocalPointsCm,
		FString& OutDebugInfo,
		bool& bOutAwaitingAsyncMask);

	static void AssignParticlesToShapePoints(
		const TArray<FVector>& BaselineWorldPositions,
		const TArray<FVector>& LocalShapePointsCm,
		const FMetaAgentParticleShapeFrame& ShapeFrame,
		const EMetaAgentParticleShapeAssignmentMode AssignmentMode,
		TArray<FVector>& OutWorldTargets);

	static FVector LocalPointToWorld(
		const FVector& LocalPointCm,
		const FMetaAgentParticleShapeFrame& ShapeFrame);
};

enum class EMetaAgentImageMaskAvailability : uint8
{
	Ready,
	Building,
	Failed,
	Unavailable
};

struct FMetaAgentImageMaskLookupResult
{
	EMetaAgentImageMaskAvailability Availability = EMetaAgentImageMaskAvailability::Unavailable;
	TArray<FVector> LocalPointsCm;
	FString DebugInfo;
};

/**
 * Thread-safe async cache for image silhouette mask points.
 * Heavy PNG decode / Sobel work runs on a worker thread; game thread polls via Tick().
 */
class METAAGENTPLUGIN_API FMetaAgentParticleShapeCache
{
public:
	static void InvalidateAll();
	static void InvalidateForPath(const FString& SourceImagePath);

	static void Tick();

	static EMetaAgentImageMaskAvailability RequestBuild(
		const FMetaAgentImageMaskBuildParams& Params);

	static FMetaAgentImageMaskLookupResult ResolveMask(
		const FMetaAgentImageMaskBuildParams& Params);

	/** Returns true when a successful mask exists for the given build params (does not start builds). */
	static bool IsMaskReady(const FMetaAgentImageMaskBuildParams& Params);
};

class METAAGENTPLUGIN_API FMetaAgentParticleShapeRegistry
{
public:
	static void RegisterDefaults();

	static void RegisterProvider(TUniquePtr<IMetaAgentParticleShapeProvider> Provider);

	static bool BuildPatternTargets(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext,
		FMetaAgentParticleShapeBuildResult& OutResult);

private:
	static TArray<TUniquePtr<IMetaAgentParticleShapeProvider>>& GetProviders();
};

class AMetaAgentPlayerController;
class UStaticMeshComponent;
class UTexture2D;
class UWorld;

/**
 * Shared image preview utilities used by the F-key plane preview and particle image shapes.
 */
class METAAGENTPLUGIN_API FMetaAgentImagePreviewRuntime
{
public:
	static FString ResolveDefaultSdxlPngPath();

	static UStaticMeshComponent* FindPreviewPlaneMesh(
		UWorld* World,
		FName ActorName,
		FName ComponentName,
		UStaticMeshComponent* CachedMesh = nullptr);

	static FMetaAgentParticleShapeFrame BuildShapeFrameFromPreviewPlane(
		const UStaticMeshComponent* PreviewMesh,
		float ZOffsetCm);

	struct FShapeFrameBuildParams
	{
		float ShapeWidthCm = 200.0f;
		float ShapeHeightCm = 0.0f;
		float ZOffsetCm = 2.0f;
		bool bAutoFitToParticleSphere = true;
		bool bOrientShapeToView = true;
		FVector ViewOrigin = FVector::ZeroVector;
		bool bHasViewOrigin = false;
	};

	static FMetaAgentParticleShapeFrame BuildShapeFrameFromCentroid(
		const TArray<FVector>& BaselineWorldPositions,
		const FShapeFrameBuildParams& Params);

	static UTexture2D* ImportPngTexture(const FString& PngPath);

	static bool EnsurePreviewTextureLoaded(AMetaAgentPlayerController& Controller, FString& OutResolvedPath);

	struct FPanelPreviewThumbnail
	{
		TObjectPtr<UTexture2D> Texture = nullptr;
		FString Label;
	};

	static bool BuildPanelPreviewThumbnails(
		const FString& PngPath,
		int32 PreviewSize,
		TArray<FPanelPreviewThumbnail>& OutThumbnails);
};