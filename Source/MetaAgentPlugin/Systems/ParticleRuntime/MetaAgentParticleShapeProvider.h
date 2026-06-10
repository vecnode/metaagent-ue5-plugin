// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"

/** Pluggable shape backend. Higher priority wins when multiple providers share a shape type. */
class METAAGENTPLUGIN_API IMetaAgentParticleShapeProvider
{
public:
	virtual ~IMetaAgentParticleShapeProvider() = default;

	virtual EMetaAgentParticlePatternShape GetShapeType() const = 0;

	virtual FName GetShapeId() const = 0;

	virtual int32 GetPriority() const { return 0; }

	virtual bool CanBuild(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext) const = 0;

	virtual bool RequiresAsyncPrepare(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext) const
	{
		return false;
	}

	virtual bool BuildTargets(
		const FMetaAgentParticlePatternConfig& PatternConfig,
		const FMetaAgentParticleShapeContext& ShapeContext,
		FMetaAgentParticleShapeBuildResult& OutResult) = 0;
};
