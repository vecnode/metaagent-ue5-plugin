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
};
