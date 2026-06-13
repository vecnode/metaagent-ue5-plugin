// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleShapeBuilder.h"

#include "Core/MetaAgent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Systems/ParticleRuntime/MetaAgentImagePreviewRuntime.h"
#include "Systems/ParticleRuntime/MetaAgentParticleImageMaskProcessor.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeCache.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeRegistry.h"

#include "Bridge/MetaAgentTypeBridge.h"

#include "metaagent/particle/shape_builder.hpp"

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
		Result.bSuccess = false;
		return Result;
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

	FMetaAgentParticleShapeFrame ShapeFrame = ResolveShapeFrame(PatternConfig, ShapeContext, SourceTexture);

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

	AssignParticlesToShapePoints(
		Baseline,
		LocalShapePointsCm,
		ShapeFrame,
		PatternConfig.Shape.AssignmentMode,
		OutResult.PatternWorldTargets);

	OutResult.ShapeFrame = ShapeFrame;
	OutResult.PatternCenter = ShapeFrame.Origin;
	OutResult.PatternColumns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(ParticleCount))));
	OutResult.ShapePointCount = LocalShapePointsCm.Num();
	OutResult.ResolvedShape = EMetaAgentParticlePatternShape::ImageSilhouette;
	OutResult.bSuccess = true;
	OutResult.DebugInfo = FString::Printf(
		TEXT("%s | frame=%.0fx%.0fcm anchor=%s autoFit=%s"),
		*ExtractionDebug,
		ShapeFrame.ExtentsCm.X,
		ShapeFrame.ExtentsCm.Y,
		PatternConfig.Shape.ShapeAnchor == EMetaAgentParticleShapeAnchor::PreviewPlane
			? TEXT("PreviewPlane")
			: TEXT("ParticleCentroid"),
		PatternConfig.Shape.bAutoFitShapeToParticleSphere ? TEXT("on") : TEXT("off"));

	return true;
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
	TArray<FVector> LocalShapePointsCm;
	LocalShapePointsCm.Reserve(SampleCount);

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& Position : Baseline)
	{
		Centroid += Position;
	}
	Centroid /= static_cast<float>(ParticleCount);

	const float ExtentCm = FMath::Max(10.0f, PatternConfig.Shape.ShapeWidthCm);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const float Distance = SampleCount > 1
			? (static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1)) * SplineLength
			: 0.0f;
		const FVector WorldPoint = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		const FVector Delta = WorldPoint - Centroid;
		LocalShapePointsCm.Add(FVector(Delta.X / ExtentCm, Delta.Y / ExtentCm, 0.0f));
	}

	FMetaAgentParticleShapeFrame ShapeFrame = ResolveShapeFrame(PatternConfig, ShapeContext, nullptr);
	AssignParticlesToShapePoints(
		Baseline,
		LocalShapePointsCm,
		ShapeFrame,
		PatternConfig.Shape.AssignmentMode,
		OutResult.PatternWorldTargets);

	OutResult.ShapeFrame = ShapeFrame;
	OutResult.PatternCenter = ShapeFrame.Origin;
	OutResult.ShapePointCount = LocalShapePointsCm.Num();
	OutResult.ResolvedShape = EMetaAgentParticlePatternShape::SplinePath;
	OutResult.bSuccess = true;
	OutResult.DebugInfo = FString::Printf(
		TEXT("SplinePath samples=%d length=%.0fcm actor=%s"),
		SampleCount,
		SplineLength,
		*SourceActor->GetName());
	return true;
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
	const int32 GridSide = FMath::Clamp(
		FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FMath::Max(ParticleCount, PatternConfig.Shape.ProceduralSampleCount)))),
		2,
		32);

	TArray<FVector> LocalShapePointsCm;
	LocalShapePointsCm.Reserve(GridSide * GridSide);
	for (int32 Row = 0; Row < GridSide; ++Row)
	{
		for (int32 Column = 0; Column < GridSide; ++Column)
		{
			const float U = GridSide > 1 ? static_cast<float>(Column) / static_cast<float>(GridSide - 1) : 0.5f;
			const float V = GridSide > 1 ? static_cast<float>(Row) / static_cast<float>(GridSide - 1) : 0.5f;
			LocalShapePointsCm.Add(FVector(U - 0.5f, 0.5f - V, 0.0f));
		}
	}

	FMetaAgentImagePreviewRuntime::FShapeFrameBuildParams FrameParams;
	FrameParams.ShapeWidthCm = WorldBounds.BoxExtent.X * 2.0f;
	FrameParams.ShapeHeightCm = WorldBounds.BoxExtent.Y * 2.0f;
	FrameParams.ZOffsetCm = PatternConfig.Shape.ZOffsetCm;
	FrameParams.bAutoFitToParticleSphere = false;
	FrameParams.bOrientShapeToView = PatternConfig.Shape.bOrientShapeToView;
	FrameParams.ViewOrigin = ShapeContext.ViewOrigin;
	FrameParams.bHasViewOrigin = ShapeContext.bHasViewOrigin;

	FMetaAgentParticleShapeFrame ShapeFrame = FMetaAgentImagePreviewRuntime::BuildShapeFrameFromCentroid(
		Baseline,
		FrameParams);
	ShapeFrame.Origin = WorldBounds.Origin;

	AssignParticlesToShapePoints(
		Baseline,
		LocalShapePointsCm,
		ShapeFrame,
		PatternConfig.Shape.AssignmentMode,
		OutResult.PatternWorldTargets);

	OutResult.ShapeFrame = ShapeFrame;
	OutResult.PatternCenter = ShapeFrame.Origin;
	OutResult.ShapePointCount = LocalShapePointsCm.Num();
	OutResult.ResolvedShape = EMetaAgentParticlePatternShape::MeshSilhouette;
	OutResult.bSuccess = true;
	OutResult.DebugInfo = FString::Printf(
		TEXT("MeshSilhouette grid=%dx%d mesh=%s"),
		GridSide,
		GridSide,
		*MeshComponent->GetStaticMesh()->GetName());
	return true;
}
