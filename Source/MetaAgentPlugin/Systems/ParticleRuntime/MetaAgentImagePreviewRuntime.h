// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeTypes.h"

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

	static FMetaAgentParticleShapeFrame BuildShapeFrameFromCentroid(
		const TArray<FVector>& BaselineWorldPositions,
		float ShapeWidthCm,
		float ShapeHeightCm,
		float ZOffsetCm);

	static UTexture2D* ImportPngTexture(const FString& PngPath);

	static bool EnsurePreviewTextureLoaded(AMetaAgentPlayerController& Controller, FString& OutResolvedPath);
};
