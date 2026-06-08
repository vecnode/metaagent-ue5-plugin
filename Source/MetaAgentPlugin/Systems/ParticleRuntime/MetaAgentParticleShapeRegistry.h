// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"

class FMetaAgentParticleShapeProviderBase
{
public:
	virtual ~FMetaAgentParticleShapeProviderBase() = default;

	virtual EMetaAgentParticlePatternShape GetShapeType() const = 0;

	virtual bool BuildTargets(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext,
		FMetaAgentParticleShapeBuildResult& OutResult) = 0;
};

class METAAGENTPLUGIN_API FMetaAgentParticleShapeRegistry
{
public:
	static void RegisterDefaults();

	static bool BuildPatternTargets(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext,
		FMetaAgentParticleShapeBuildResult& OutResult);

private:
	static TArray<TUniquePtr<FMetaAgentParticleShapeProviderBase>>& GetProviders();
};
