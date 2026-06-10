// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeTypes.h"

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
