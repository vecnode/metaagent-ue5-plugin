// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeTypes.h"

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
