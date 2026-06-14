// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Async/Async.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "MetaAgentPlugin.h"
#include "MetaAgentTypeBridge.h"
#include "media/decode.hpp"
#include "media/mask_cache.hpp"
#include "media/pipeline.hpp"
#include "media/store.hpp"
#include "particle/image_mask_processor.hpp"
#include "particle/shape_builder.hpp"
#include "MetaAgentParticleShapes.h"
#include "MetaAgentParticleShapes.h"
#include "MetaAgentParticleShapes.h"
#include "MetaAgentParticleShapes.h"
#include "MetaAgentParticleShapes.h"
#include "MetaAgentPlayerController.h"
#include "MetaAgentTypeBridge.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "TextureResource.h"

// ===== MetaAgentParticleShapeBuilder.cpp =====
namespace
{
metaagent::particle::ShapeAssignmentMode ToCoreAssignmentMode(const EMetaAgentParticleShapeAssignmentMode Mode)
{
	switch (Mode)
	{
	case EMetaAgentParticleShapeAssignmentMode::Ordered: return metaagent::particle::ShapeAssignmentMode::Ordered;
	case EMetaAgentParticleShapeAssignmentMode::PolarMatched: return metaagent::particle::ShapeAssignmentMode::PolarMatched;
	case EMetaAgentParticleShapeAssignmentMode::NearestNeighbor:
	default:
		return metaagent::particle::ShapeAssignmentMode::NearestNeighbor;
	}
}

void CopyVec3ArrayToCore(const TArray<FVector>& Source, metaagent::core::Array<metaagent::core::Vec3>& Destination)
{
	Destination.clear();
	Destination.reserve(static_cast<size_t>(Source.Num()));
	for (const FVector& Value : Source)
	{
		Destination.push_back(metaagent::core::Vec3(Value.X, Value.Y, Value.Z));
	}
}
}
FString FMetaAgentParticleShapeDefinition::GetShapeDisplayName() const
{
	switch (ShapeType)
	{
	case EMetaAgentParticlePatternShape::ImageSilhouette:
		return TEXT("ImageSilhouette");
	case EMetaAgentParticlePatternShape::SplinePath:
		return TEXT("SplinePath");
	case EMetaAgentParticlePatternShape::MeshSilhouette:
		return TEXT("MeshSilhouette");
	case EMetaAgentParticlePatternShape::SquareGrid:
	default:
		return TEXT("SquareGrid");
	}
}

EMetaAgentParticleImageSamplingMode FMetaAgentParticleShapeDefinition::SanitizeImageSamplingMode(
	const EMetaAgentParticleImageSamplingMode Mode)
{
	switch (Mode)
	{
	case EMetaAgentParticleImageSamplingMode::GrayscaleDensity:
	case EMetaAgentParticleImageSamplingMode::SobelEdges:
		return Mode;
	case EMetaAgentParticleImageSamplingMode::FilledSilhouette:
	default:
		return EMetaAgentParticleImageSamplingMode::GrayscaleDensity;
	}
}

FString FMetaAgentParticleShapeDefinition::GetImageSamplingDisplayName() const
{
	switch (SanitizeImageSamplingMode(ImageSamplingMode))
	{
	case EMetaAgentParticleImageSamplingMode::GrayscaleDensity:
		return TEXT("GrayscaleDensity");
	case EMetaAgentParticleImageSamplingMode::SobelEdges:
	default:
		return TEXT("SobelEdges");
	}
}

void FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache()
{
	FMetaAgentParticleShapeCache::InvalidateAll();
	metaagent::media::MaskBuildCache::instance().invalidate_all();
	metaagent::media::MediaStore::instance().invalidate_all();
}

void FMetaAgentParticleShapeBuilder::RequestImageMaskBuild(
	const FString& SourceImagePath,
	const FMetaAgentParticleShapeDefinition& ShapeDefinition,
	const int32 DesiredPointCount)
{
	if (SourceImagePath.IsEmpty() || DesiredPointCount <= 0)
	{
		return;
	}

	const FMetaAgentImageMaskBuildParams Params = MetaAgentImageMask::MakeBuildParams(
		SourceImagePath,
		ShapeDefinition,
		DesiredPointCount);
	FMetaAgentParticleShapeCache::RequestBuild(Params);
}

FMetaAgentParticleShapeBuildResult FMetaAgentParticleShapeBuilder::BuildPatternTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext)
{
	FMetaAgentParticleShapeBuildResult Result;
	Result.ResolvedShape = PatternConfig.Shape.ShapeType;

	FMetaAgentParticleShapeRegistry::RegisterDefaults();
	if (FMetaAgentParticleShapeRegistry::BuildPatternTargets(PatternConfig, ShapeContext, Result))
	{
		return Result;
	}

	if (Result.bAwaitingAsyncMask)
	{
		const int32 BaselineCount = FMath::Max(1, ShapeContext.BaselineWorldPositions.Num());
		const FMetaAgentImageMaskBuildParams MaskParams = MetaAgentImageMask::MakeBuildParams(
			ShapeContext.SourceImagePath,
			PatternConfig.Shape,
			BaselineCount);
		if (FMetaAgentParticleShapeCache::IsMaskBuildInFlight(MaskParams))
		{
			Result.bSuccess = false;
			return Result;
		}

		// Stale awaiting flag (e.g. failed/empty cache entry) — fall through to SquareGrid fallback.
		Result.bAwaitingAsyncMask = false;
	}

	const FString FailedReason = Result.DebugInfo;
	UE_LOG(LogMetaAgent, Warning,
		TEXT("ParticleShapeBuilder: shape build failed (%s), falling back to square grid."),
		*FailedReason);
	Result.ResolvedShape = EMetaAgentParticlePatternShape::SquareGrid;
	BuildSquareGridTargets(PatternConfig, ShapeContext, Result);
	Result.DebugInfo = FString::Printf(TEXT("Fallback SquareGrid | %s"), *FailedReason);
	return Result;
}

bool FMetaAgentParticleShapeBuilder::BuildSquareGridTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	FMetaAgentParticleShapeBuildResult& OutResult)
{
	metaagent::particle::PatternConfig CoreConfig;
	metaagent::particle::ShapeContext CoreContext;
	MetaAgentTypeBridge::copy_pattern_config_to_core(PatternConfig, CoreConfig);
	MetaAgentTypeBridge::copy_shape_context_to_core(ShapeContext, CoreContext);

	metaagent::particle::ShapeBuildResult CoreResult;
	const bool bSuccess = metaagent::particle::ShapeBuilder::build_square_grid_targets(
		CoreConfig,
		CoreContext,
		CoreResult);
	MetaAgentTypeBridge::copy_shape_build_result_from_core(CoreResult, OutResult);
	return bSuccess;
}

FMetaAgentParticleShapeFrame FMetaAgentParticleShapeBuilder::ResolveShapeFrame(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	const UTexture2D* SourceTexture)
{
	metaagent::particle::PatternConfig CoreConfig;
	metaagent::particle::ShapeContext CoreContext;
	MetaAgentTypeBridge::copy_pattern_config_to_core(PatternConfig, CoreConfig);
	MetaAgentTypeBridge::copy_shape_context_to_core(ShapeContext, CoreContext);

	const int32 TextureWidth = SourceTexture ? SourceTexture->GetSizeX() : CoreContext.source_texture_width;
	const int32 TextureHeight = SourceTexture ? SourceTexture->GetSizeY() : CoreContext.source_texture_height;
	const metaagent::particle::ShapeFrame CoreFrame = metaagent::particle::ShapeBuilder::resolve_shape_frame(
		CoreConfig,
		CoreContext,
		TextureWidth,
		TextureHeight);

	FMetaAgentParticleShapeBuildResult TempResult;
	MetaAgentTypeBridge::copy_shape_build_result_from_core(
		metaagent::particle::ShapeBuildResult {.shape_frame = CoreFrame},
		TempResult);
	return TempResult.ShapeFrame;
}

bool FMetaAgentParticleShapeBuilder::ExtractSilhouetteLocalPoints(
	const FString& SourceImagePath,
	const FMetaAgentParticleShapeDefinition& ShapeDefinition,
	const int32 DesiredPointCount,
	TArray<FVector>& OutLocalPointsCm,
	FString& OutDebugInfo,
	bool& bOutAwaitingAsyncMask)
{
	OutLocalPointsCm.Reset();
	bOutAwaitingAsyncMask = false;

	const FMetaAgentImageMaskBuildParams Params = MetaAgentImageMask::MakeBuildParams(
		SourceImagePath,
		ShapeDefinition,
		DesiredPointCount);
	const FMetaAgentImageMaskLookupResult Lookup = FMetaAgentParticleShapeCache::ResolveMask(Params);

	switch (Lookup.Availability)
	{
	case EMetaAgentImageMaskAvailability::Ready:
		OutLocalPointsCm = Lookup.LocalPointsCm;
		OutDebugInfo = Lookup.DebugInfo;
		return OutLocalPointsCm.Num() > 0;

	case EMetaAgentImageMaskAvailability::Building:
		bOutAwaitingAsyncMask = true;
		OutDebugInfo = Lookup.DebugInfo;
		return false;

	case EMetaAgentImageMaskAvailability::Failed:
		OutDebugInfo = Lookup.DebugInfo;
		return false;

	case EMetaAgentImageMaskAvailability::Unavailable:
	default:
		OutDebugInfo = Lookup.DebugInfo.IsEmpty()
			? TEXT("Image mask unavailable.")
			: Lookup.DebugInfo;
		return false;
	}
}

FVector FMetaAgentParticleShapeBuilder::LocalPointToWorld(
	const FVector& LocalPointCm,
	const FMetaAgentParticleShapeFrame& ShapeFrame)
{
	metaagent::particle::ShapeFrame CoreFrame;
	CoreFrame.origin = metaagent::core::Vec3(ShapeFrame.Origin.X, ShapeFrame.Origin.Y, ShapeFrame.Origin.Z);
	CoreFrame.orientation.pitch_deg = ShapeFrame.Orientation.Pitch;
	CoreFrame.orientation.yaw_deg = ShapeFrame.Orientation.Yaw;
	CoreFrame.orientation.roll_deg = ShapeFrame.Orientation.Roll;
	CoreFrame.extents_cm = metaagent::core::Vec2(ShapeFrame.ExtentsCm.X, ShapeFrame.ExtentsCm.Y);
	CoreFrame.z_offset_cm = ShapeFrame.ZOffsetCm;

	const metaagent::core::Vec3 Result = metaagent::particle::ShapeBuilder::local_point_to_world(
		metaagent::core::Vec3(LocalPointCm.X, LocalPointCm.Y, LocalPointCm.Z),
		CoreFrame);
	return FVector(Result.x, Result.y, Result.z);
}

void FMetaAgentParticleShapeBuilder::AssignParticlesToShapePoints(
	const TArray<FVector>& BaselineWorldPositions,
	const TArray<FVector>& LocalShapePointsCm,
	const FMetaAgentParticleShapeFrame& ShapeFrame,
	const EMetaAgentParticleShapeAssignmentMode AssignmentMode,
	TArray<FVector>& OutWorldTargets)
{
	metaagent::core::Array<metaagent::core::Vec3> CoreBaselines;
	metaagent::core::Array<metaagent::core::Vec3> CoreLocalPoints;
	CopyVec3ArrayToCore(BaselineWorldPositions, CoreBaselines);
	CopyVec3ArrayToCore(LocalShapePointsCm, CoreLocalPoints);

	metaagent::particle::ShapeFrame CoreFrame;
	CoreFrame.origin = metaagent::core::Vec3(ShapeFrame.Origin.X, ShapeFrame.Origin.Y, ShapeFrame.Origin.Z);
	CoreFrame.orientation.pitch_deg = ShapeFrame.Orientation.Pitch;
	CoreFrame.orientation.yaw_deg = ShapeFrame.Orientation.Yaw;
	CoreFrame.orientation.roll_deg = ShapeFrame.Orientation.Roll;
	CoreFrame.extents_cm = metaagent::core::Vec2(ShapeFrame.ExtentsCm.X, ShapeFrame.ExtentsCm.Y);
	CoreFrame.z_offset_cm = ShapeFrame.ZOffsetCm;

	metaagent::core::Array<metaagent::core::Vec3> CoreTargets;
	metaagent::particle::ShapeBuilder::assign_particles_to_shape_points(
		CoreBaselines,
		CoreLocalPoints,
		CoreFrame,
		ToCoreAssignmentMode(AssignmentMode),
		CoreTargets);

	OutWorldTargets.Reset(static_cast<int32>(CoreTargets.size()));
	OutWorldTargets.Reserve(static_cast<int32>(CoreTargets.size()));
	for (const metaagent::core::Vec3& Point : CoreTargets)
	{
		OutWorldTargets.Add(FVector(Point.x, Point.y, Point.z));
	}
}

bool FMetaAgentParticleShapeBuilder::BuildImageSilhouetteTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	FMetaAgentParticleShapeBuildResult& OutResult)
{
	const TArray<FVector>& Baseline = ShapeContext.BaselineWorldPositions;
	const int32 ParticleCount = Baseline.Num();
	if (ParticleCount <= 0)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("No baseline particles.");
		return false;
	}

	UTexture2D* SourceTexture = ShapeContext.SourceTexture;
	if (!SourceTexture && ShapeContext.SourceImagePath.IsEmpty())
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("No source texture or image path for image silhouette.");
		return false;
	}

	TArray<FVector> LocalShapePointsCm;
	FString ExtractionDebug;
	bool bAwaitingAsyncMask = false;
	if (!ExtractSilhouetteLocalPoints(
		ShapeContext.SourceImagePath,
		PatternConfig.Shape,
		ParticleCount,
		LocalShapePointsCm,
		ExtractionDebug,
		bAwaitingAsyncMask))
	{
		OutResult.bSuccess = false;
		OutResult.bAwaitingAsyncMask = bAwaitingAsyncMask;
		OutResult.DebugInfo = ExtractionDebug;
		return false;
	}

	metaagent::particle::PatternConfig CoreConfig;
	metaagent::particle::ShapeContext CoreContext;
	MetaAgentTypeBridge::copy_pattern_config_to_core(PatternConfig, CoreConfig);
	MetaAgentTypeBridge::copy_shape_context_to_core(ShapeContext, CoreContext);

	metaagent::core::Array<metaagent::core::Vec3> CoreLocalPoints;
	CopyVec3ArrayToCore(LocalShapePointsCm, CoreLocalPoints);

	const int32 TextureWidth = SourceTexture ? SourceTexture->GetSizeX() : CoreContext.source_texture_width;
	const int32 TextureHeight = SourceTexture ? SourceTexture->GetSizeY() : CoreContext.source_texture_height;

	metaagent::particle::ShapeBuildResult CoreResult;
	const bool bSuccess = metaagent::particle::ShapeBuilder::build_silhouette_from_local_points(
		CoreConfig,
		CoreContext,
		CoreLocalPoints,
		TextureWidth,
		TextureHeight,
		CoreResult,
		metaagent::core::String(TCHAR_TO_UTF8(*ExtractionDebug)));
	MetaAgentTypeBridge::copy_shape_build_result_from_core(CoreResult, OutResult);
	return bSuccess;
}

namespace MetaAgentParticleShapeInternal
{
	AActor* FindTaggedShapeSourceActor(const UWorld* World, const FName Tag)
	{
		if (!World || Tag.IsNone())
		{
			return nullptr;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && Actor->ActorHasTag(Tag))
			{
				return Actor;
			}
		}

		return nullptr;
	}
}

bool FMetaAgentParticleShapeBuilder::BuildSplinePathTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	FMetaAgentParticleShapeBuildResult& OutResult)
{
	const TArray<FVector>& Baseline = ShapeContext.BaselineWorldPositions;
	const int32 ParticleCount = Baseline.Num();
	if (ParticleCount <= 0)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("No baseline particles for spline path.");
		return false;
	}

	UWorld* World = ShapeContext.World;
	if (!World)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("SplinePath requires World in shape context.");
		return false;
	}

	AActor* SourceActor = MetaAgentParticleShapeInternal::FindTaggedShapeSourceActor(
		World,
		PatternConfig.Shape.ShapeSourceActorTag);
	if (!SourceActor)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = FString::Printf(
			TEXT("SplinePath: no actor with tag '%s'."),
			*PatternConfig.Shape.ShapeSourceActorTag.ToString());
		return false;
	}

	USplineComponent* Spline = SourceActor->FindComponentByClass<USplineComponent>();
	if (!Spline)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("SplinePath: tagged actor has no USplineComponent.");
		return false;
	}

	const int32 SampleCount = FMath::Clamp(
		FMath::Max(ParticleCount, PatternConfig.Shape.ProceduralSampleCount),
		4,
		512);
	const float SplineLength = Spline->GetSplineLength();
	TArray<FVector> PolylineWorldPoints;
	PolylineWorldPoints.Reserve(SampleCount);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const float Distance = SampleCount > 1
			? (static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1)) * SplineLength
			: 0.0f;
		PolylineWorldPoints.Add(
			Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World));
	}

	metaagent::particle::PatternConfig CoreConfig;
	metaagent::particle::ShapeContext CoreContext;
	MetaAgentTypeBridge::copy_pattern_config_to_core(PatternConfig, CoreConfig);
	MetaAgentTypeBridge::copy_shape_context_to_core(ShapeContext, CoreContext);

	metaagent::core::Array<metaagent::core::Vec3> CorePolyline;
	CopyVec3ArrayToCore(PolylineWorldPoints, CorePolyline);

	metaagent::particle::ShapeBuildResult CoreResult;
	const bool bSuccess = metaagent::particle::ShapeBuilder::build_polyline_path_targets(
		CoreConfig,
		CoreContext,
		CorePolyline,
		CoreResult);
	MetaAgentTypeBridge::copy_shape_build_result_from_core(CoreResult, OutResult);
	if (bSuccess)
	{
		OutResult.DebugInfo += FString::Printf(
			TEXT(" length=%.0fcm actor=%s"),
			SplineLength,
			*SourceActor->GetName());
	}
	return bSuccess;
}

bool FMetaAgentParticleShapeBuilder::BuildMeshSilhouetteTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	FMetaAgentParticleShapeBuildResult& OutResult)
{
	const TArray<FVector>& Baseline = ShapeContext.BaselineWorldPositions;
	const int32 ParticleCount = Baseline.Num();
	if (ParticleCount <= 0)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("No baseline particles for mesh silhouette.");
		return false;
	}

	UWorld* World = ShapeContext.World;
	if (!World)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("MeshSilhouette requires World in shape context.");
		return false;
	}

	AActor* SourceActor = MetaAgentParticleShapeInternal::FindTaggedShapeSourceActor(
		World,
		PatternConfig.Shape.ShapeSourceActorTag);
	UStaticMeshComponent* MeshComponent = SourceActor
		? SourceActor->FindComponentByClass<UStaticMeshComponent>()
		: nullptr;
	if (!MeshComponent || !MeshComponent->GetStaticMesh())
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = FString::Printf(
			TEXT("MeshSilhouette: no static mesh on tag '%s'."),
			*PatternConfig.Shape.ShapeSourceActorTag.ToString());
		return false;
	}

	const FBoxSphereBounds WorldBounds = MeshComponent->Bounds;

	metaagent::particle::PatternConfig CoreConfig;
	metaagent::particle::ShapeContext CoreContext;
	MetaAgentTypeBridge::copy_pattern_config_to_core(PatternConfig, CoreConfig);
	MetaAgentTypeBridge::copy_shape_context_to_core(ShapeContext, CoreContext);

	metaagent::particle::ShapeBuildResult CoreResult;
	const bool bSuccess = metaagent::particle::ShapeBuilder::build_bounds_grid_targets(
		CoreConfig,
		CoreContext,
		metaagent::core::Vec3(WorldBounds.Origin.X, WorldBounds.Origin.Y, WorldBounds.Origin.Z),
		WorldBounds.BoxExtent.X * 2.0f,
		WorldBounds.BoxExtent.Y * 2.0f,
		CoreResult);
	MetaAgentTypeBridge::copy_shape_build_result_from_core(CoreResult, OutResult);
	if (bSuccess)
	{
		OutResult.DebugInfo += FString::Printf(
			TEXT(" mesh=%s"),
			*MeshComponent->GetStaticMesh()->GetName());
	}
	return bSuccess;
}

// ===== MetaAgentParticleShapeCache.cpp =====
namespace
{
	struct FMetaAgentImageMaskCacheKey
	{
		FString SourceImagePath;
		FDateTime SourceFileTimestamp;
		int64 SourceFileSize = 0;
		EMetaAgentParticleImageSamplingMode ImageSamplingMode = EMetaAgentParticleImageSamplingMode::SobelEdges;
		float AlphaThreshold = 0.0f;
		float EdgeThreshold = 0.0f;
		bool bUseLuminance = false;
		int32 SampleResolution = 0;
		float GrayscaleGamma = 0.0f;
		float DensityGridScale = 0.0f;
		float TargetJitterNormalized = 0.0f;
		int32 DesiredPointCount = 0;

		bool operator==(const FMetaAgentImageMaskCacheKey& Other) const
		{
			return SourceImagePath == Other.SourceImagePath
				&& SourceFileTimestamp == Other.SourceFileTimestamp
				&& SourceFileSize == Other.SourceFileSize
				&& ImageSamplingMode == Other.ImageSamplingMode
				&& FMath::IsNearlyEqual(AlphaThreshold, Other.AlphaThreshold)
				&& FMath::IsNearlyEqual(EdgeThreshold, Other.EdgeThreshold)
				&& bUseLuminance == Other.bUseLuminance
				&& SampleResolution == Other.SampleResolution
				&& FMath::IsNearlyEqual(GrayscaleGamma, Other.GrayscaleGamma)
				&& FMath::IsNearlyEqual(DensityGridScale, Other.DensityGridScale)
				&& FMath::IsNearlyEqual(TargetJitterNormalized, Other.TargetJitterNormalized)
				&& DesiredPointCount == Other.DesiredPointCount;
		}
	};

	uint32 GetImageMaskCacheKeyHash(const FMetaAgentImageMaskCacheKey& Key)
	{
		uint32 Hash = FCrc::StrCrc32(*Key.SourceImagePath);
		Hash = HashCombine(Hash, ::GetTypeHash(Key.SourceFileTimestamp.GetTicks()));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.SourceFileSize));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.ImageSamplingMode));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.AlphaThreshold));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.EdgeThreshold));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.bUseLuminance));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.SampleResolution));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.GrayscaleGamma));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.DensityGridScale));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.TargetJitterNormalized));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.DesiredPointCount));
		return Hash;
	}

	FORCEINLINE uint32 GetTypeHash(const FMetaAgentImageMaskCacheKey& Key)
	{
		return GetImageMaskCacheKeyHash(Key);
	}

	FMetaAgentImageMaskCacheKey MakeCacheKey(const FMetaAgentImageMaskBuildParams& Params)
	{
		FMetaAgentImageMaskCacheKey Key;
		Key.SourceImagePath = Params.SourceImagePath;
		Key.SourceFileTimestamp = Params.SourceFileTimestamp;
		Key.SourceFileSize = Params.SourceFileSize;
		Key.ImageSamplingMode = Params.ImageSamplingMode;
		Key.AlphaThreshold = Params.AlphaThreshold;
		Key.EdgeThreshold = Params.EdgeThreshold;
		Key.bUseLuminance = Params.bUseLuminance;
		Key.SampleResolution = Params.SampleResolution;
		Key.GrayscaleGamma = Params.GrayscaleGamma;
		Key.DensityGridScale = Params.DensityGridScale;
		Key.TargetJitterNormalized = Params.TargetJitterNormalized;
		Key.DesiredPointCount = Params.DesiredPointCount;
		return Key;
	}

	struct FMetaAgentImageMaskCacheEntry
	{
		bool bSuccess = false;
		TArray<FVector> LocalPointsCm;
		FString DebugInfo;
	};

	bool IsMaskCacheEntryUsable(const FMetaAgentImageMaskCacheEntry& Entry)
	{
		return Entry.bSuccess && Entry.LocalPointsCm.Num() > 0;
	}

	struct FMetaAgentImageMaskAsyncJob
	{
		FMetaAgentImageMaskCacheKey Key;
		TFuture<FMetaAgentImageMaskBuildOutput> Future;
	};

	FCriticalSection GMaskCacheMutex;
	TMap<FMetaAgentImageMaskCacheKey, FMetaAgentImageMaskCacheEntry> GMaskCache;
	TMap<FMetaAgentImageMaskCacheKey, TSharedPtr<FMetaAgentImageMaskAsyncJob>> GInFlightJobs;
}

void FMetaAgentParticleShapeCache::InvalidateAll()
{
	FScopeLock Lock(&GMaskCacheMutex);
	GMaskCache.Reset();
	GInFlightJobs.Reset();
}

void FMetaAgentParticleShapeCache::InvalidateForPath(const FString& SourceImagePath)
{
	if (SourceImagePath.IsEmpty())
	{
		return;
	}

	FScopeLock Lock(&GMaskCacheMutex);

	for (auto It = GMaskCache.CreateIterator(); It; ++It)
	{
		if (It.Key().SourceImagePath == SourceImagePath)
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = GInFlightJobs.CreateIterator(); It; ++It)
	{
		if (It.Key().SourceImagePath == SourceImagePath)
		{
			It.RemoveCurrent();
		}
	}
}

void FMetaAgentParticleShapeCache::Tick()
{
	TArray<TPair<FMetaAgentImageMaskCacheKey, FMetaAgentImageMaskBuildOutput>> CompletedJobs;
	CompletedJobs.Reserve(4);

	TArray<TPair<FMetaAgentImageMaskCacheKey, TSharedPtr<FMetaAgentImageMaskAsyncJob>>> ReadyJobs;
	ReadyJobs.Reserve(4);

	{
		FScopeLock Lock(&GMaskCacheMutex);
		for (auto It = GInFlightJobs.CreateIterator(); It; ++It)
		{
			const TSharedPtr<FMetaAgentImageMaskAsyncJob>& Job = It.Value();
			if (!Job.IsValid() || !Job->Future.IsReady())
			{
				continue;
			}

			ReadyJobs.Add(TPair<FMetaAgentImageMaskCacheKey, TSharedPtr<FMetaAgentImageMaskAsyncJob>>(It.Key(), Job));
			It.RemoveCurrent();
		}
	}

	for (const TPair<FMetaAgentImageMaskCacheKey, TSharedPtr<FMetaAgentImageMaskAsyncJob>>& ReadyJob : ReadyJobs)
	{
		if (ReadyJob.Value.IsValid())
		{
			CompletedJobs.Add(TPair<FMetaAgentImageMaskCacheKey, FMetaAgentImageMaskBuildOutput>(
				ReadyJob.Key,
				ReadyJob.Value->Future.Get()));
		}
	}

	for (const TPair<FMetaAgentImageMaskCacheKey, FMetaAgentImageMaskBuildOutput>& CompletedJob : CompletedJobs)
	{
		FMetaAgentImageMaskCacheEntry Entry;
		Entry.bSuccess = CompletedJob.Value.bSuccess && CompletedJob.Value.LocalPointsCm.Num() > 0;
		Entry.LocalPointsCm = CompletedJob.Value.LocalPointsCm;
		Entry.DebugInfo = CompletedJob.Value.DebugInfo;
		if (!Entry.bSuccess && Entry.DebugInfo.IsEmpty())
		{
			Entry.DebugInfo = TEXT("Image mask build produced no silhouette points.");
		}

		UE_LOG(LogMetaAgent, Log,
			TEXT("ParticleShapeCache: async mask build finished success=%s points=%d | %s"),
			Entry.bSuccess ? TEXT("true") : TEXT("false"),
			Entry.LocalPointsCm.Num(),
			*Entry.DebugInfo);

		FScopeLock Lock(&GMaskCacheMutex);
		GMaskCache.Add(CompletedJob.Key, MoveTemp(Entry));
	}
}

EMetaAgentImageMaskAvailability FMetaAgentParticleShapeCache::RequestBuild(
	const FMetaAgentImageMaskBuildParams& Params)
{
	if (Params.SourceImagePath.IsEmpty() || Params.DesiredPointCount <= 0)
	{
		return EMetaAgentImageMaskAvailability::Unavailable;
	}

	FDateTime CurrentTimestamp = Params.SourceFileTimestamp;
	int64 CurrentFileSize = Params.SourceFileSize;
	if (!MetaAgentImageMask::GetImageFileIdentity(Params.SourceImagePath, CurrentTimestamp, CurrentFileSize))
	{
		return EMetaAgentImageMaskAvailability::Unavailable;
	}

	FMetaAgentImageMaskBuildParams FreshParams = Params;
	FreshParams.SourceFileTimestamp = CurrentTimestamp;
	FreshParams.SourceFileSize = CurrentFileSize;
	const FMetaAgentImageMaskCacheKey Key = MakeCacheKey(FreshParams);

	{
		FScopeLock Lock(&GMaskCacheMutex);
		if (const FMetaAgentImageMaskCacheEntry* CachedEntry = GMaskCache.Find(Key))
		{
			return IsMaskCacheEntryUsable(*CachedEntry)
				? EMetaAgentImageMaskAvailability::Ready
				: EMetaAgentImageMaskAvailability::Failed;
		}

		if (GInFlightJobs.Contains(Key))
		{
			return EMetaAgentImageMaskAvailability::Building;
		}
	}

	TSharedPtr<FMetaAgentImageMaskAsyncJob> Job = MakeShared<FMetaAgentImageMaskAsyncJob>();
	Job->Key = Key;
	Job->Future = Async(
		EAsyncExecution::LargeThreadPool,
		[FreshParams]()
		{
			FMetaAgentImageMaskBuildOutput Output;
			MetaAgentImageMask::BuildMaskOnWorkerThread(FreshParams, Output);
			return Output;
		});

	{
		FScopeLock Lock(&GMaskCacheMutex);
		if (const FMetaAgentImageMaskCacheEntry* CachedEntry = GMaskCache.Find(Key))
		{
			return IsMaskCacheEntryUsable(*CachedEntry)
				? EMetaAgentImageMaskAvailability::Ready
				: EMetaAgentImageMaskAvailability::Failed;
		}

		GInFlightJobs.Add(Key, Job);
	}

	UE_LOG(LogMetaAgent, Log,
		TEXT("ParticleShapeCache: async mask build started for '%s' (%d points @ %dpx)."),
		*FPaths::GetCleanFilename(FreshParams.SourceImagePath),
		FreshParams.DesiredPointCount,
		FreshParams.SampleResolution);

	return EMetaAgentImageMaskAvailability::Building;
}

FMetaAgentImageMaskLookupResult FMetaAgentParticleShapeCache::ResolveMask(
	const FMetaAgentImageMaskBuildParams& Params)
{
	FMetaAgentImageMaskLookupResult Result;
	Tick();

	if (Params.SourceImagePath.IsEmpty() || Params.DesiredPointCount <= 0)
	{
		Result.Availability = EMetaAgentImageMaskAvailability::Unavailable;
		Result.DebugInfo = TEXT("No image path or particle count for mask build.");
		return Result;
	}

	FDateTime CurrentTimestamp = Params.SourceFileTimestamp;
	int64 CurrentFileSize = Params.SourceFileSize;
	if (!MetaAgentImageMask::GetImageFileIdentity(Params.SourceImagePath, CurrentTimestamp, CurrentFileSize))
	{
		Result.Availability = EMetaAgentImageMaskAvailability::Unavailable;
		Result.DebugInfo = TEXT("Image file not found on disk.");
		return Result;
	}

	FMetaAgentImageMaskBuildParams FreshParams = Params;
	FreshParams.SourceFileTimestamp = CurrentTimestamp;
	FreshParams.SourceFileSize = CurrentFileSize;
	const FMetaAgentImageMaskCacheKey Key = MakeCacheKey(FreshParams);

	{
		FScopeLock Lock(&GMaskCacheMutex);
		if (const FMetaAgentImageMaskCacheEntry* CachedEntry = GMaskCache.Find(Key))
		{
			Result.LocalPointsCm = CachedEntry->LocalPointsCm;
			Result.DebugInfo = CachedEntry->DebugInfo;
			Result.Availability = IsMaskCacheEntryUsable(*CachedEntry)
				? EMetaAgentImageMaskAvailability::Ready
				: EMetaAgentImageMaskAvailability::Failed;
			return Result;
		}

		if (GInFlightJobs.Contains(Key))
		{
			Result.Availability = EMetaAgentImageMaskAvailability::Building;
			Result.DebugInfo = TEXT("Building image mask on background thread...");
			return Result;
		}
	}

	const EMetaAgentImageMaskAvailability RequestedAvailability = RequestBuild(FreshParams);
	Result.Availability = RequestedAvailability;
	if (RequestedAvailability == EMetaAgentImageMaskAvailability::Building)
	{
		Result.DebugInfo = TEXT("Building image mask on background thread...");
	}
	else if (RequestedAvailability == EMetaAgentImageMaskAvailability::Unavailable)
	{
		Result.DebugInfo = TEXT("Mask build unavailable.");
	}

	return Result;
}

bool FMetaAgentParticleShapeCache::IsMaskReady(const FMetaAgentImageMaskBuildParams& Params)
{
	if (Params.SourceImagePath.IsEmpty() || Params.DesiredPointCount <= 0)
	{
		return false;
	}

	FDateTime CurrentTimestamp = Params.SourceFileTimestamp;
	int64 CurrentFileSize = Params.SourceFileSize;
	if (!MetaAgentImageMask::GetImageFileIdentity(Params.SourceImagePath, CurrentTimestamp, CurrentFileSize))
	{
		return false;
	}

	FMetaAgentImageMaskBuildParams FreshParams = Params;
	FreshParams.SourceFileTimestamp = CurrentTimestamp;
	FreshParams.SourceFileSize = CurrentFileSize;
	const FMetaAgentImageMaskCacheKey Key = MakeCacheKey(FreshParams);

	FScopeLock Lock(&GMaskCacheMutex);
	if (const FMetaAgentImageMaskCacheEntry* CachedEntry = GMaskCache.Find(Key))
	{
		return IsMaskCacheEntryUsable(*CachedEntry);
	}

	return false;
}

bool FMetaAgentParticleShapeCache::IsMaskBuildInFlight(const FMetaAgentImageMaskBuildParams& Params)
{
	if (Params.SourceImagePath.IsEmpty() || Params.DesiredPointCount <= 0)
	{
		return false;
	}

	FDateTime CurrentTimestamp = Params.SourceFileTimestamp;
	int64 CurrentFileSize = Params.SourceFileSize;
	if (!MetaAgentImageMask::GetImageFileIdentity(Params.SourceImagePath, CurrentTimestamp, CurrentFileSize))
	{
		return false;
	}

	FMetaAgentImageMaskBuildParams FreshParams = Params;
	FreshParams.SourceFileTimestamp = CurrentTimestamp;
	FreshParams.SourceFileSize = CurrentFileSize;
	const FMetaAgentImageMaskCacheKey Key = MakeCacheKey(FreshParams);

	FScopeLock Lock(&GMaskCacheMutex);
	return GInFlightJobs.Contains(Key);
}

// ===== MetaAgentParticleShapeRegistry.cpp =====
namespace
{
	class FMetaAgentSquareGridShapeProvider final : public IMetaAgentParticleShapeProvider
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::SquareGrid;
		}

		virtual FName GetShapeId() const override
		{
			return TEXT("SquareGrid");
		}

		virtual int32 GetPriority() const override { return 0; }

		virtual bool CanBuild(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext) const override
		{
			return ShapeContext.BaselineWorldPositions.Num() > 0;
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildSquareGridTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

	class FMetaAgentImageSilhouetteShapeProvider final : public IMetaAgentParticleShapeProvider
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::ImageSilhouette;
		}

		virtual FName GetShapeId() const override
		{
			return TEXT("ImageSilhouette");
		}

		virtual int32 GetPriority() const override { return 0; }

		virtual bool CanBuild(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext) const override
		{
			return ShapeContext.BaselineWorldPositions.Num() > 0
				&& (!ShapeContext.SourceImagePath.IsEmpty() || ShapeContext.bHasResolvedImage);
		}

		virtual bool RequiresAsyncPrepare(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext) const override
		{
			if (!CanBuild(PatternConfig, ShapeContext) || ShapeContext.SourceImagePath.IsEmpty())
			{
				return false;
			}

			const int32 DesiredPointCount = FMath::Max(1, ShapeContext.BaselineWorldPositions.Num());
			const FMetaAgentImageMaskBuildParams Params = MetaAgentImageMask::MakeBuildParams(
				ShapeContext.SourceImagePath,
				PatternConfig.Shape,
				DesiredPointCount);
			const FMetaAgentImageMaskLookupResult Lookup = FMetaAgentParticleShapeCache::ResolveMask(Params);
			if (Lookup.Availability == EMetaAgentImageMaskAvailability::Failed
				|| Lookup.Availability == EMetaAgentImageMaskAvailability::Unavailable)
			{
				// Let the sync build path run so ShapeBuilder can fall back (e.g. Sobel produced no edges).
				return false;
			}

			if (Lookup.Availability == EMetaAgentImageMaskAvailability::Building)
			{
				return true;
			}

			if (Lookup.Availability == EMetaAgentImageMaskAvailability::Ready)
			{
				return false;
			}

			return FMetaAgentParticleShapeCache::IsMaskBuildInFlight(Params);
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildImageSilhouetteTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

	class FMetaAgentSplinePathShapeProvider final : public IMetaAgentParticleShapeProvider
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::SplinePath;
		}

		virtual FName GetShapeId() const override
		{
			return TEXT("SplinePath");
		}

		virtual int32 GetPriority() const override { return 0; }

		virtual bool CanBuild(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext) const override
		{
			return ShapeContext.BaselineWorldPositions.Num() > 0 && ShapeContext.World != nullptr;
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildSplinePathTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

	class FMetaAgentMeshSilhouetteShapeProvider final : public IMetaAgentParticleShapeProvider
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::MeshSilhouette;
		}

		virtual FName GetShapeId() const override
		{
			return TEXT("MeshSilhouette");
		}

		virtual int32 GetPriority() const override { return 0; }

		virtual bool CanBuild(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext) const override
		{
			return ShapeContext.BaselineWorldPositions.Num() > 0 && ShapeContext.World != nullptr;
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildMeshSilhouetteTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

}

TArray<TUniquePtr<IMetaAgentParticleShapeProvider>>& FMetaAgentParticleShapeRegistry::GetProviders()
{
	static TArray<TUniquePtr<IMetaAgentParticleShapeProvider>> Providers;
	return Providers;
}

void FMetaAgentParticleShapeRegistry::RegisterProvider(TUniquePtr<IMetaAgentParticleShapeProvider> Provider)
{
	RegisterDefaults();
	if (Provider)
	{
		GetProviders().Add(MoveTemp(Provider));
	}
}

void FMetaAgentParticleShapeRegistry::RegisterDefaults()
{
	TArray<TUniquePtr<IMetaAgentParticleShapeProvider>>& Providers = GetProviders();
	if (Providers.Num() > 0)
	{
		return;
	}

	Providers.Add(MakeUnique<FMetaAgentSquareGridShapeProvider>());
	Providers.Add(MakeUnique<FMetaAgentImageSilhouetteShapeProvider>());
	Providers.Add(MakeUnique<FMetaAgentSplinePathShapeProvider>());
	Providers.Add(MakeUnique<FMetaAgentMeshSilhouetteShapeProvider>());
}

bool FMetaAgentParticleShapeRegistry::BuildPatternTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	FMetaAgentParticleShapeBuildResult& OutResult)
{
	RegisterDefaults();

	const EMetaAgentParticlePatternShape RequestedShape = PatternConfig.Shape.ShapeType;
	IMetaAgentParticleShapeProvider* BestProvider = nullptr;
	int32 BestPriority = MIN_int32;

	for (const TUniquePtr<IMetaAgentParticleShapeProvider>& Provider : GetProviders())
	{
		if (!Provider || Provider->GetShapeType() != RequestedShape)
		{
			continue;
		}

		if (!Provider->CanBuild(PatternConfig, ShapeContext))
		{
			continue;
		}

		const int32 ProviderPriority = Provider->GetPriority();
		if (!BestProvider || ProviderPriority > BestPriority)
		{
			BestProvider = Provider.Get();
			BestPriority = ProviderPriority;
		}
	}

	if (!BestProvider)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = FString::Printf(
			TEXT("No shape provider can build %s."),
			*PatternConfig.Shape.GetShapeDisplayName());
		return false;
	}

	if (BestProvider->RequiresAsyncPrepare(PatternConfig, ShapeContext))
	{
		OutResult.bAwaitingAsyncMask = true;
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("Awaiting async mask prepare.");
		return false;
	}

	return BestProvider->BuildTargets(PatternConfig, ShapeContext, OutResult);
}

// ===== MetaAgentParticleImageMaskProcessor.cpp =====
namespace MetaAgentImageMask
{
namespace
{
bool CopyFImageToFColorPixels(const FImage& InImage, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
	OutWidth = InImage.SizeX;
	OutHeight = InImage.SizeY;
	if (OutWidth <= 0 || OutHeight <= 0)
	{
		return false;
	}

	if (InImage.Format == ERawImageFormat::BGRA8)
	{
		const int64 ExpectedBytes = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4;
		if (InImage.RawData.Num() < ExpectedBytes)
		{
			return false;
		}

		OutPixels.SetNum(OutWidth * OutHeight);
		FMemory::Memcpy(OutPixels.GetData(), InImage.RawData.GetData(), ExpectedBytes);
		return true;
	}

	FImage BGRAImage;
	BGRAImage.Init(OutWidth, OutHeight, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	FImageCore::CopyImage(InImage, BGRAImage);

	const int64 ExpectedBytes = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4;
	if (BGRAImage.RawData.Num() < ExpectedBytes)
	{
		return false;
	}

	OutPixels.SetNum(OutWidth * OutHeight);
	FMemory::Memcpy(OutPixels.GetData(), BGRAImage.RawData.GetData(), ExpectedBytes);
	return true;
}

bool ReadTexturePixelsFromPngFile(
	const FString& SourceImagePath,
	TArray<FColor>& OutPixels,
	int32& OutWidth,
	int32& OutHeight)
{
	OutPixels.Reset();
	OutWidth = 0;
	OutHeight = 0;

	if (SourceImagePath.IsEmpty() || !FPaths::FileExists(SourceImagePath))
	{
		return false;
	}

	FImage LoadedImage;
	if (!FImageUtils::LoadImage(*SourceImagePath, LoadedImage))
	{
		return false;
	}

	return CopyFImageToFColorPixels(LoadedImage, OutPixels, OutWidth, OutHeight);
}

metaagent::particle::RgbaImage ToCoreRgbaImage(const TArray<FColor>& Pixels, const int32 Width, const int32 Height)
{
	metaagent::particle::RgbaImage Image;
	Image.width = Width;
	Image.height = Height;
	Image.pixels.resize(static_cast<size_t>(Width * Height));
	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		const FColor& Pixel = Pixels[Index];
		Image.pixels[static_cast<size_t>(Index)] = {
			Pixel.R,
			Pixel.G,
			Pixel.B,
			Pixel.A};
	}
	return Image;
}
} // namespace

bool GetImageFileIdentity(const FString& SourceImagePath, FDateTime& OutTimestamp, int64& OutFileSize)
{
	OutTimestamp = FDateTime::MinValue();
	OutFileSize = 0;

	if (SourceImagePath.IsEmpty())
	{
		return false;
	}

	const FString FullPath = FPaths::ConvertRelativePathToFull(SourceImagePath);
	FTCHARToUTF8 Utf8Path(*FullPath);
	const metaagent::core::String CorePath(Utf8Path.Get(), static_cast<size_t>(Utf8Path.Length()));

	int64 ModifiedUnix = 0;
	if (!metaagent::media::get_file_identity(CorePath, OutFileSize, ModifiedUnix))
	{
		return false;
	}

	OutTimestamp = FDateTime::FromUnixTimestamp(ModifiedUnix);
	return OutFileSize > 0;
}

FMetaAgentImageMaskBuildParams MakeBuildParams(
	const FString& SourceImagePath,
	const FMetaAgentParticleShapeDefinition& ShapeDefinition,
	const int32 DesiredPointCount)
{
	FMetaAgentImageMaskBuildParams Params;
	Params.SourceImagePath = SourceImagePath.IsEmpty()
		? SourceImagePath
		: FPaths::ConvertRelativePathToFull(SourceImagePath);
	GetImageFileIdentity(Params.SourceImagePath, Params.SourceFileTimestamp, Params.SourceFileSize);
	Params.ImageSamplingMode = ShapeDefinition.ImageSamplingMode;
	Params.AlphaThreshold = ShapeDefinition.AlphaThreshold;
	Params.EdgeThreshold = ShapeDefinition.EdgeThreshold;
	Params.bUseLuminance = ShapeDefinition.bUseLuminance;
	Params.SampleResolution = ShapeDefinition.SampleResolution;
	Params.GrayscaleGamma = ShapeDefinition.GrayscaleGamma;
	Params.DensityGridScale = ShapeDefinition.DensityGridScale;
	Params.TargetJitterNormalized = ShapeDefinition.TargetJitterNormalized;
	Params.DesiredPointCount = FMath::Max(1, DesiredPointCount);
	return Params;
}

bool BuildMaskOnWorkerThread(const FMetaAgentImageMaskBuildParams& Params, FMetaAgentImageMaskBuildOutput& OutOutput)
{
	OutOutput = FMetaAgentImageMaskBuildOutput();

	FTCHARToUTF8 Utf8Path(*Params.SourceImagePath);
	const metaagent::core::String CorePath(Utf8Path.Get(), static_cast<size_t>(Utf8Path.Length()));

	metaagent::particle::ImageMaskBuildParams CoreParams;
	MetaAgentTypeBridge::copy_image_mask_params_to_core(Params, CoreParams);

	metaagent::media::MaskBuildResult Built;
	if (!metaagent::media::build_mask_from_file(CorePath, CoreParams, Built))
	{
		OutOutput.DebugInfo = FString(UTF8_TO_TCHAR(Built.mask.debug_info.c_str()));
		if (OutOutput.DebugInfo.IsEmpty())
		{
			OutOutput.DebugInfo = FString::Printf(
				TEXT("Failed to build mask for '%s'."),
				Params.SourceImagePath.IsEmpty() ? TEXT("<none>") : *FPaths::GetCleanFilename(Params.SourceImagePath));
		}
		return false;
	}

	MetaAgentTypeBridge::copy_image_mask_output_from_core(Built.mask, OutOutput);
	OutOutput.bSuccess = OutOutput.bSuccess && OutOutput.LocalPointsCm.Num() > 0;
	if (!OutOutput.bSuccess && OutOutput.DebugInfo.IsEmpty())
	{
		OutOutput.DebugInfo = TEXT("Image mask build produced no silhouette points.");
	}
	return OutOutput.bSuccess;
}
} // namespace MetaAgentImageMask

// ===== MetaAgentImagePreviewRuntime.cpp =====
namespace
{
	bool NameMatches(const FString& CandidateName, const FString& WantedName)
	{
		if (CandidateName.Equals(WantedName, ESearchCase::IgnoreCase))
		{
			return true;
		}

		const FString Prefix = WantedName + TEXT("_");
		return CandidateName.StartsWith(Prefix, ESearchCase::IgnoreCase);
	}

	FString ResolveDesktopDirectory()
	{
		const FString UserProfileDir = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
		if (!UserProfileDir.IsEmpty())
		{
			const FString DesktopDir = FPaths::Combine(UserProfileDir, TEXT("Desktop"));
			if (FPaths::DirectoryExists(DesktopDir))
			{
				return DesktopDir;
			}
		}

		const FString OneDriveDir = FPlatformMisc::GetEnvironmentVariable(TEXT("OneDrive"));
		if (!OneDriveDir.IsEmpty())
		{
			const FString OneDriveDesktopDir = FPaths::Combine(OneDriveDir, TEXT("Desktop"));
			if (FPaths::DirectoryExists(OneDriveDesktopDir))
			{
				return OneDriveDesktopDir;
			}
		}

		return FPaths::ProjectDir();
	}

	bool CopyFImageToFColorPixels(const FImage& InImage, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		OutWidth = InImage.SizeX;
		OutHeight = InImage.SizeY;
		if (OutWidth <= 0 || OutHeight <= 0)
		{
			return false;
		}

		if (InImage.Format == ERawImageFormat::BGRA8)
		{
			const int64 ExpectedBytes = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4;
			if (InImage.RawData.Num() < ExpectedBytes)
			{
				return false;
			}

			OutPixels.SetNum(OutWidth * OutHeight);
			FMemory::Memcpy(OutPixels.GetData(), InImage.RawData.GetData(), ExpectedBytes);
			return true;
		}

		FImage BGRAImage;
		BGRAImage.Init(OutWidth, OutHeight, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FImageCore::CopyImage(InImage, BGRAImage);

		const int64 ExpectedBytes = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4;
		if (BGRAImage.RawData.Num() < ExpectedBytes)
		{
			return false;
		}

		OutPixels.SetNum(OutWidth * OutHeight);
		FMemory::Memcpy(OutPixels.GetData(), BGRAImage.RawData.GetData(), ExpectedBytes);
		return true;
	}

	bool ReadTexturePixelsFromPngFile(
		const FString& SourceImagePath,
		TArray<FColor>& OutPixels,
		int32& OutWidth,
		int32& OutHeight)
	{
		OutPixels.Reset();
		OutWidth = 0;
		OutHeight = 0;

		if (SourceImagePath.IsEmpty() || !FPaths::FileExists(SourceImagePath))
		{
			return false;
		}

		FImage LoadedImage;
		if (!FImageUtils::LoadImage(*SourceImagePath, LoadedImage))
		{
			return false;
		}

		return CopyFImageToFColorPixels(LoadedImage, OutPixels, OutWidth, OutHeight);
	}

	void DownsampleToSize(
		const TArray<FColor>& SourcePixels,
		const int32 SourceWidth,
		const int32 SourceHeight,
		const int32 TargetWidth,
		const int32 TargetHeight,
		TArray<FColor>& OutPixels)
	{
		OutPixels.SetNum(TargetWidth * TargetHeight);
		for (int32 Y = 0; Y < TargetHeight; ++Y)
		{
			for (int32 X = 0; X < TargetWidth; ++X)
			{
				const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(TargetWidth);
				const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(TargetHeight);
				const int32 SrcX = FMath::Clamp(
					FMath::FloorToInt(U * static_cast<float>(SourceWidth - 1)),
					0,
					SourceWidth - 1);
				const int32 SrcY = FMath::Clamp(
					FMath::FloorToInt(V * static_cast<float>(SourceHeight - 1)),
					0,
					SourceHeight - 1);
				OutPixels[Y * TargetWidth + X] = SourcePixels[SrcY * SourceWidth + SrcX];
			}
		}
	}

	void BuildGrayscalePixels(
		const TArray<FColor>& SourcePixels,
		const int32 Width,
		const int32 Height,
		TArray<FColor>& OutPixels)
	{
		OutPixels.SetNum(Width * Height);
		for (int32 Index = 0; Index < SourcePixels.Num(); ++Index)
		{
			const FColor& Pixel = SourcePixels[Index];
			const uint8 Gray = static_cast<uint8>(
				FMath::Clamp(
					(0.2126f * Pixel.R + 0.7152f * Pixel.G + 0.0722f * Pixel.B),
					0.0f,
					255.0f));
			OutPixels[Index] = FColor(Gray, Gray, Gray, 255);
		}
	}

	float SampleGrayAt(const TArray<float>& Gray, const int32 Width, const int32 Height, const int32 X, const int32 Y)
	{
		const int32 ClampedX = FMath::Clamp(X, 0, Width - 1);
		const int32 ClampedY = FMath::Clamp(Y, 0, Height - 1);
		return Gray[ClampedY * Width + ClampedX];
	}

	void ComputeSobelMagnitudes(
		const TArray<float>& Gray,
		const int32 Width,
		const int32 Height,
		TArray<float>& OutMagnitudes)
	{
		OutMagnitudes.Init(0.0f, Width * Height);
		if (Width < 3 || Height < 3)
		{
			return;
		}

		for (int32 Y = 1; Y < Height - 1; ++Y)
		{
			for (int32 X = 1; X < Width - 1; ++X)
			{
				const float Gx =
					-1.0f * SampleGrayAt(Gray, Width, Height, X - 1, Y - 1) +
					1.0f * SampleGrayAt(Gray, Width, Height, X + 1, Y - 1) +
					-2.0f * SampleGrayAt(Gray, Width, Height, X - 1, Y) +
					2.0f * SampleGrayAt(Gray, Width, Height, X + 1, Y) +
					-1.0f * SampleGrayAt(Gray, Width, Height, X - 1, Y + 1) +
					1.0f * SampleGrayAt(Gray, Width, Height, X + 1, Y + 1);
				const float Gy =
					-1.0f * SampleGrayAt(Gray, Width, Height, X - 1, Y - 1) +
					-2.0f * SampleGrayAt(Gray, Width, Height, X, Y - 1) +
					-1.0f * SampleGrayAt(Gray, Width, Height, X + 1, Y - 1) +
					1.0f * SampleGrayAt(Gray, Width, Height, X - 1, Y + 1) +
					2.0f * SampleGrayAt(Gray, Width, Height, X, Y + 1) +
					1.0f * SampleGrayAt(Gray, Width, Height, X + 1, Y + 1);
				OutMagnitudes[Y * Width + X] = FMath::Sqrt(Gx * Gx + Gy * Gy);
			}
		}
	}

	void MagnitudesToPixels(
		const TArray<float>& Magnitudes,
		const int32 Width,
		const int32 Height,
		TArray<FColor>& OutPixels)
	{
		float MaxMagnitude = 0.0f;
		for (const float Magnitude : Magnitudes)
		{
			MaxMagnitude = FMath::Max(MaxMagnitude, Magnitude);
		}

		const float InvMax = MaxMagnitude > KINDA_SMALL_NUMBER ? (1.0f / MaxMagnitude) : 1.0f;
		OutPixels.SetNum(Width * Height);
		for (int32 Index = 0; Index < Magnitudes.Num(); ++Index)
		{
			const uint8 Value = static_cast<uint8>(FMath::Clamp(Magnitudes[Index] * InvMax, 0.0f, 1.0f) * 255.0f);
			OutPixels[Index] = FColor(Value, Value, Value, 255);
		}
	}

	UTexture2D* CreateTextureFromPixels(const TArray<FColor>& Pixels, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
		{
			return nullptr;
		}

		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}

		Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->SRGB = true;
		Texture->NeverStream = true;

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}
}

FString FMetaAgentImagePreviewRuntime::ResolveDefaultSdxlPngPath()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(ResolveDesktopDirectory(), TEXT("github/agent-sequencer-app/output/sdxl_latest.png")));
}

UStaticMeshComponent* FMetaAgentImagePreviewRuntime::FindPreviewPlaneMesh(
	UWorld* World,
	const FName ActorName,
	const FName ComponentName,
	UStaticMeshComponent* CachedMesh)
{
	if (CachedMesh)
	{
		return CachedMesh;
	}

	if (!World)
	{
		return nullptr;
	}

	const FString WantedActorName = ActorName.ToString();
	const FString WantedComponentName = ComponentName.ToString();

	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* CandidateActor = *It;
		if (!CandidateActor)
		{
			continue;
		}

		UStaticMeshComponent* CandidateMesh = CandidateActor->GetStaticMeshComponent();
		if (!CandidateMesh)
		{
			continue;
		}

		const bool bActorNameMatches =
			NameMatches(CandidateActor->GetActorNameOrLabel(), WantedActorName) ||
			NameMatches(CandidateActor->GetName(), WantedActorName);

		const bool bComponentNameMatches =
			NameMatches(CandidateMesh->GetName(), WantedComponentName);

		if (bActorNameMatches || bComponentNameMatches)
		{
			return CandidateMesh;
		}
	}

	return nullptr;
}

FMetaAgentParticleShapeFrame FMetaAgentImagePreviewRuntime::BuildShapeFrameFromPreviewPlane(
	const UStaticMeshComponent* PreviewMesh,
	const float ZOffsetCm)
{
	FMetaAgentParticleShapeFrame Frame;
	if (!PreviewMesh)
	{
		return Frame;
	}

	const FBoxSphereBounds WorldBounds = PreviewMesh->Bounds;
	Frame.Origin = WorldBounds.Origin;
	Frame.Orientation = PreviewMesh->GetComponentRotation();
	Frame.ZOffsetCm = ZOffsetCm;

	FVector LocalMin = FVector::ZeroVector;
	FVector LocalMax = FVector::ZeroVector;
	PreviewMesh->GetLocalBounds(LocalMin, LocalMax);
	const FVector LocalExtent = (LocalMax - LocalMin) * 0.5f;
	const FVector Scale = PreviewMesh->GetComponentScale();
	Frame.ExtentsCm = FVector2D(
		FMath::Max(10.0f, LocalExtent.X * Scale.X * 2.0f),
		FMath::Max(10.0f, LocalExtent.Y * Scale.Y * 2.0f));

	return Frame;
}

FMetaAgentParticleShapeFrame FMetaAgentImagePreviewRuntime::BuildShapeFrameFromCentroid(
	const TArray<FVector>& BaselineWorldPositions,
	const FShapeFrameBuildParams& Params)
{
	return MetaAgentTypeBridge::build_shape_frame_from_centroid(
		BaselineWorldPositions,
		Params.ShapeWidthCm,
		Params.ShapeHeightCm,
		Params.ZOffsetCm,
		Params.bAutoFitToParticleSphere,
		Params.bOrientShapeToView,
		Params.bHasViewOrigin,
		Params.ViewOrigin);
}

UTexture2D* FMetaAgentImagePreviewRuntime::ImportPngTexture(const FString& PngPath)
{
	if (!FPaths::FileExists(PngPath))
	{
		return nullptr;
	}

	FTCHARToUTF8 Utf8Path(*PngPath);
	const metaagent::core::String CorePath(Utf8Path.Get(), static_cast<size_t>(Utf8Path.Length()));

	metaagent::media::RgbaImage Image;
	if (!metaagent::media::MediaStore::instance().load_file(CorePath, Image))
	{
		return nullptr;
	}

	return MetaAgentTypeBridge::create_texture2d_from_rgba(Image);
}

bool FMetaAgentImagePreviewRuntime::EnsurePreviewTextureLoaded(
	AMetaAgentPlayerController& Controller,
	FString& OutResolvedPath)
{
	OutResolvedPath.Reset();

	if (Controller.GetLatestPngPreviewTexture())
	{
		OutResolvedPath = Controller.GetLastLoadedPreviewImagePath();
		if (OutResolvedPath.IsEmpty())
		{
			OutResolvedPath = ResolveDefaultSdxlPngPath();
		}
		return true;
	}

	const FString PngPath = ResolveDefaultSdxlPngPath();
	OutResolvedPath = PngPath;

	UTexture2D* ImportedTexture = ImportPngTexture(PngPath);
	if (!ImportedTexture)
	{
		return false;
	}

	Controller.SetLatestPngPreviewTexture(ImportedTexture);
	Controller.SetLastLoadedPreviewImagePath(PngPath);
	return true;
}

bool FMetaAgentImagePreviewRuntime::BuildPanelPreviewThumbnails(
	const FString& PngPath,
	const int32 PreviewSize,
	TArray<FPanelPreviewThumbnail>& OutThumbnails)
{
	OutThumbnails.Reset();

	const int32 ClampedSize = FMath::Clamp(PreviewSize, 16, 256);
	FTCHARToUTF8 Utf8Path(*PngPath);
	const metaagent::core::String CorePath(Utf8Path.Get(), static_cast<size_t>(Utf8Path.Length()));

	metaagent::media::RgbaImage Image;
	if (!metaagent::media::MediaStore::instance().load_file(CorePath, Image))
	{
		return false;
	}

	metaagent::media::MaskPreviewBuffers Previews;
	metaagent::media::build_preview_thumbnails(Image, ClampedSize, Previews);

	auto GrayBytesToTexture = [ClampedSize](const metaagent::core::Array<uint8_t>& GrayBytes) -> UTexture2D*
	{
		if (static_cast<int32>(GrayBytes.size()) != ClampedSize * ClampedSize)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNum(ClampedSize * ClampedSize);
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			const uint8 Value = GrayBytes[static_cast<size_t>(Index)];
			Pixels[Index] = FColor(Value, Value, Value, 255);
		}

		return CreateTextureFromPixels(Pixels, ClampedSize, ClampedSize);
	};

	auto AddThumbnail = [&OutThumbnails](UTexture2D* Texture, const FString& Label)
	{
		if (!Texture)
		{
			return;
		}

		FPanelPreviewThumbnail Thumbnail;
		Thumbnail.Texture = Texture;
		Thumbnail.Label = Label;
		OutThumbnails.Add(Thumbnail);
	};

	metaagent::media::RgbaImage SourceThumb;
	SourceThumb.width = ClampedSize;
	SourceThumb.height = ClampedSize;
	SourceThumb.pixels = Previews.source_color;
	if (SourceThumb.pixels.empty())
	{
		SourceThumb.pixels.resize(static_cast<size_t>(ClampedSize * ClampedSize));
		for (int32 Index = 0; Index < ClampedSize * ClampedSize; ++Index)
		{
			const uint8 Value = Previews.source_gray[static_cast<size_t>(Index)];
			SourceThumb.pixels[static_cast<size_t>(Index)] = {Value, Value, Value, 255};
		}
	}

	AddThumbnail(MetaAgentTypeBridge::create_texture2d_from_rgba(SourceThumb), TEXT("Source"));
	AddThumbnail(GrayBytesToTexture(Previews.gray_preview), TEXT("Gray"));
	AddThumbnail(GrayBytesToTexture(Previews.sobel_preview), TEXT("Sobel"));
	return OutThumbnails.Num() > 0;
}