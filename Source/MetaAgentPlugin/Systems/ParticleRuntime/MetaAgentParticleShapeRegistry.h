// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeProvider.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"

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
