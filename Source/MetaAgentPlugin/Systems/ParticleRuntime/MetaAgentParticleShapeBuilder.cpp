// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleShapeBuilder.h"

#include "Core/MetaAgent.h"
#include "Engine/Texture2D.h"
#include "Systems/ParticleRuntime/MetaAgentImagePreviewRuntime.h"
#include "Systems/ParticleRuntime/MetaAgentParticleImageMaskProcessor.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeCache.h"
FString FMetaAgentParticleShapeDefinition::GetShapeDisplayName() const
{
	switch (ShapeType)
	{
	case EMetaAgentParticlePatternShape::ImageSilhouette:
		return TEXT("ImageSilhouette");
	case EMetaAgentParticlePatternShape::SquareGrid:
	default:
		return TEXT("SquareGrid");
	}
}

FString FMetaAgentParticleShapeDefinition::GetImageSamplingDisplayName() const
{
	switch (ImageSamplingMode)
	{
	case EMetaAgentParticleImageSamplingMode::FilledSilhouette:
		return TEXT("FilledSilhouette");
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

	switch (PatternConfig.Shape.ShapeType)
	{
	case EMetaAgentParticlePatternShape::ImageSilhouette:
		if (BuildImageSilhouetteTargets(PatternConfig, ShapeContext, Result))
		{
			return Result;
		}

		if (Result.bAwaitingAsyncMask)
		{
			Result.bSuccess = false;
			return Result;
		}

		{
			const FString FailedReason = Result.DebugInfo;
			UE_LOG(LogMetaAgent, Warning,
				TEXT("ParticleShapeBuilder: image silhouette failed (%s), falling back to square grid."),
				*FailedReason);
			Result.ResolvedShape = EMetaAgentParticlePatternShape::SquareGrid;
			BuildSquareGridTargets(PatternConfig, ShapeContext, Result);
			Result.DebugInfo = FString::Printf(TEXT("Fallback SquareGrid | %s"), *FailedReason);
		}
		return Result;

	case EMetaAgentParticlePatternShape::SquareGrid:
	default:
		BuildSquareGridTargets(PatternConfig, ShapeContext, Result);
		return Result;
	}
}

bool FMetaAgentParticleShapeBuilder::BuildSquareGridTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	FMetaAgentParticleShapeBuildResult& OutResult)
{
	const TArray<FVector>& Baseline = ShapeContext.BaselineWorldPositions;
	const int32 ParticleCount = Baseline.Num();
	OutResult.PatternWorldTargets.Reset();

	if (ParticleCount <= 0)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("No baseline particles.");
		return false;
	}

	const int32 PatternColumns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(ParticleCount))));
	const int32 RowCount = FMath::DivideAndRoundUp(ParticleCount, PatternColumns);

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& Position : Baseline)
	{
		Centroid += Position;
	}
	Centroid /= static_cast<float>(ParticleCount);

	const float GridSpacingCm = FMath::Max(1.0f, PatternConfig.GridSpacingCm);
	const FVector GridOrigin = Centroid - FVector(
		(PatternColumns - 1) * GridSpacingCm * 0.5f,
		(RowCount - 1) * GridSpacingCm * 0.5f,
		0.0f);

	OutResult.PatternWorldTargets.SetNum(ParticleCount);
	for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
	{
		const int32 Column = ParticleIndex % PatternColumns;
		const int32 Row = ParticleIndex / PatternColumns;
		OutResult.PatternWorldTargets[ParticleIndex] = GridOrigin + FVector(
			Column * GridSpacingCm,
			Row * GridSpacingCm,
			0.0f);
	}

	OutResult.ShapeFrame = FMetaAgentImagePreviewRuntime::BuildShapeFrameFromCentroid(
		Baseline,
		PatternColumns * GridSpacingCm,
		RowCount * GridSpacingCm,
		PatternConfig.Shape.ZOffsetCm);
	OutResult.ShapeFrame.Origin = Centroid;
	OutResult.PatternCenter = Centroid;
	OutResult.PatternColumns = PatternColumns;
	OutResult.ShapePointCount = ParticleCount;
	OutResult.ResolvedShape = EMetaAgentParticlePatternShape::SquareGrid;
	OutResult.bSuccess = true;
	OutResult.DebugInfo = FString::Printf(
		TEXT("SquareGrid columns=%d spacing=%.1f"),
		PatternColumns,
		GridSpacingCm);

	return true;
}

FMetaAgentParticleShapeFrame FMetaAgentParticleShapeBuilder::ResolveShapeFrame(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	const UTexture2D* SourceTexture)
{
	const FMetaAgentParticleShapeDefinition& ShapeDef = PatternConfig.Shape;

	if (ShapeDef.bAlignToPreviewPlane && ShapeContext.PreviewPlaneMesh)
	{
		FMetaAgentParticleShapeFrame Frame = FMetaAgentImagePreviewRuntime::BuildShapeFrameFromPreviewPlane(
			ShapeContext.PreviewPlaneMesh,
			ShapeDef.ZOffsetCm);
		return Frame;
	}

	float ShapeHeightCm = ShapeDef.ShapeHeightCm;
	if (ShapeHeightCm <= 0.0f && SourceTexture)
	{
		const int32 TexWidth = SourceTexture->GetSizeX();
		const int32 TexHeight = SourceTexture->GetSizeY();
		if (TexWidth > 0 && TexHeight > 0)
		{
			ShapeHeightCm = ShapeDef.ShapeWidthCm * (static_cast<float>(TexHeight) / static_cast<float>(TexWidth));
		}
	}
	if (ShapeHeightCm <= 0.0f)
	{
		ShapeHeightCm = ShapeDef.ShapeWidthCm;
	}

	return FMetaAgentImagePreviewRuntime::BuildShapeFrameFromCentroid(
		ShapeContext.BaselineWorldPositions,
		ShapeDef.ShapeWidthCm,
		ShapeHeightCm,
		ShapeDef.ZOffsetCm);
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
	const FVector ScaledLocal(
		LocalPointCm.X * ShapeFrame.ExtentsCm.X,
		LocalPointCm.Y * ShapeFrame.ExtentsCm.Y,
		ShapeFrame.ZOffsetCm);

	const FVector RotatedLocal = ShapeFrame.Orientation.RotateVector(ScaledLocal);
	return ShapeFrame.Origin + RotatedLocal;
}

void FMetaAgentParticleShapeBuilder::AssignParticlesToShapePoints(
	const TArray<FVector>& BaselineWorldPositions,
	const TArray<FVector>& LocalShapePointsCm,
	const FMetaAgentParticleShapeFrame& ShapeFrame,
	const EMetaAgentParticleShapeAssignmentMode AssignmentMode,
	TArray<FVector>& OutWorldTargets)
{
	const int32 ParticleCount = BaselineWorldPositions.Num();
	OutWorldTargets.Reset();
	if (ParticleCount <= 0 || LocalShapePointsCm.Num() <= 0)
	{
		return;
	}

	TArray<FVector> ShapeWorldPoints;
	ShapeWorldPoints.Reserve(LocalShapePointsCm.Num());
	for (const FVector& LocalPoint : LocalShapePointsCm)
	{
		ShapeWorldPoints.Add(LocalPointToWorld(LocalPoint, ShapeFrame));
	}

	OutWorldTargets.SetNum(ParticleCount);

	if (AssignmentMode == EMetaAgentParticleShapeAssignmentMode::Ordered)
	{
		for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
		{
			const int32 ShapeIndex = ParticleIndex % ShapeWorldPoints.Num();
			OutWorldTargets[ParticleIndex] = ShapeWorldPoints[ShapeIndex];
		}
		return;
	}

	if (AssignmentMode == EMetaAgentParticleShapeAssignmentMode::PolarMatched)
	{
		FVector BaselineCentroid = FVector::ZeroVector;
		for (const FVector& Position : BaselineWorldPositions)
		{
			BaselineCentroid += Position;
		}
		BaselineCentroid /= static_cast<float>(ParticleCount);

		FVector ShapeCentroid = FVector::ZeroVector;
		for (const FVector& Position : ShapeWorldPoints)
		{
			ShapeCentroid += Position;
		}
		ShapeCentroid /= static_cast<float>(ShapeWorldPoints.Num());

		TArray<int32> ParticleOrder;
		ParticleOrder.Reserve(ParticleCount);
		for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
		{
			ParticleOrder.Add(ParticleIndex);
		}
		ParticleOrder.Sort([&BaselineWorldPositions, BaselineCentroid](const int32 A, const int32 B)
		{
			const FVector DeltaA = BaselineWorldPositions[A] - BaselineCentroid;
			const FVector DeltaB = BaselineWorldPositions[B] - BaselineCentroid;
			return FMath::Atan2(DeltaA.Y, DeltaA.X) < FMath::Atan2(DeltaB.Y, DeltaB.X);
		});

		TArray<int32> ShapeOrder;
		ShapeOrder.Reserve(ShapeWorldPoints.Num());
		for (int32 ShapeIndex = 0; ShapeIndex < ShapeWorldPoints.Num(); ++ShapeIndex)
		{
			ShapeOrder.Add(ShapeIndex);
		}
		ShapeOrder.Sort([&ShapeWorldPoints, ShapeCentroid](const int32 A, const int32 B)
		{
			const FVector DeltaA = ShapeWorldPoints[A] - ShapeCentroid;
			const FVector DeltaB = ShapeWorldPoints[B] - ShapeCentroid;
			return FMath::Atan2(DeltaA.Y, DeltaA.X) < FMath::Atan2(DeltaB.Y, DeltaB.X);
		});

		for (int32 OrderIndex = 0; OrderIndex < ParticleCount; ++OrderIndex)
		{
			const int32 ParticleIndex = ParticleOrder[OrderIndex];
			const int32 ShapeIndex = ShapeOrder[OrderIndex % ShapeOrder.Num()];
			OutWorldTargets[ParticleIndex] = ShapeWorldPoints[ShapeIndex];
		}
		return;
	}

	TArray<bool> UsedShapePoints;
	UsedShapePoints.Init(false, ShapeWorldPoints.Num());

	TArray<int32> ParticleOrder;
	ParticleOrder.Reserve(ParticleCount);
	for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
	{
		ParticleOrder.Add(ParticleIndex);
	}
	ParticleOrder.Sort([&BaselineWorldPositions](const int32 A, const int32 B)
	{
		const FVector PosA = BaselineWorldPositions[A];
		const FVector PosB = BaselineWorldPositions[B];
		if (!FMath::IsNearlyEqual(PosA.Y, PosB.Y, 0.01f))
		{
			return PosA.Y < PosB.Y;
		}
		return PosA.X < PosB.X;
	});

	for (const int32 ParticleIndex : ParticleOrder)
	{
		const FVector& BaselinePosition = BaselineWorldPositions[ParticleIndex];

		float BestDistanceSq = TNumericLimits<float>::Max();
		int32 BestShapeIndex = 0;
		for (int32 ShapeIndex = 0; ShapeIndex < ShapeWorldPoints.Num(); ++ShapeIndex)
		{
			if (UsedShapePoints.IsValidIndex(ShapeIndex) && UsedShapePoints[ShapeIndex])
			{
				continue;
			}

			const float DistanceSq = FVector::DistSquared(BaselinePosition, ShapeWorldPoints[ShapeIndex]);
			if (DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				BestShapeIndex = ShapeIndex;
			}
		}

		if (ShapeWorldPoints.IsValidIndex(BestShapeIndex))
		{
			UsedShapePoints[BestShapeIndex] = true;
			OutWorldTargets[ParticleIndex] = ShapeWorldPoints[BestShapeIndex];
		}
		else
		{
			OutWorldTargets[ParticleIndex] = ShapeWorldPoints[ParticleIndex % ShapeWorldPoints.Num()];
		}
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

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& Position : Baseline)
	{
		Centroid += Position;
	}
	Centroid /= static_cast<float>(ParticleCount);

	OutResult.ShapeFrame = ShapeFrame;
	OutResult.PatternCenter = ShapeFrame.Origin;
	OutResult.PatternColumns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(ParticleCount))));
	OutResult.ShapePointCount = LocalShapePointsCm.Num();
	OutResult.ResolvedShape = EMetaAgentParticlePatternShape::ImageSilhouette;
	OutResult.bSuccess = true;
	OutResult.DebugInfo = FString::Printf(
		TEXT("%s | frame=%.0fx%.0fcm plane=%s"),
		*ExtractionDebug,
		ShapeFrame.ExtentsCm.X,
		ShapeFrame.ExtentsCm.Y,
		ShapeContext.PreviewPlaneMesh ? TEXT("yes") : TEXT("no"));

	return true;
}
