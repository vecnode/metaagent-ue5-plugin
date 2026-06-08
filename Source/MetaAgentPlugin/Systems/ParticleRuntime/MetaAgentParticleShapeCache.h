// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleImageMaskProcessor.h"

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
};
