// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleShapeRegistry.h"

#include "Systems/ParticleRuntime/MetaAgentParticleShapeBuilder.h"

namespace
{
	class FMetaAgentSquareGridShapeProvider final : public FMetaAgentParticleShapeProviderBase
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::SquareGrid;
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildSquareGridTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

	class FMetaAgentImageSilhouetteShapeProvider final : public FMetaAgentParticleShapeProviderBase
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::ImageSilhouette;
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildImageSilhouetteTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

	class FMetaAgentSplinePathShapeProvider final : public FMetaAgentParticleShapeProviderBase
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::SplinePath;
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildSplinePathTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

	class FMetaAgentMeshSilhouetteShapeProvider final : public FMetaAgentParticleShapeProviderBase
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::MeshSilhouette;
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildMeshSilhouetteTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

	class FMetaAgentRandomParallelepipedShapeProvider final : public FMetaAgentParticleShapeProviderBase
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::RandomParallelepiped;
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildRandomParallelepipedTargets(PatternConfig, ShapeContext, OutResult);
		}
	};
}

TArray<TUniquePtr<FMetaAgentParticleShapeProviderBase>>& FMetaAgentParticleShapeRegistry::GetProviders()
{
	static TArray<TUniquePtr<FMetaAgentParticleShapeProviderBase>> Providers;
	return Providers;
}

void FMetaAgentParticleShapeRegistry::RegisterDefaults()
{
	TArray<TUniquePtr<FMetaAgentParticleShapeProviderBase>>& Providers = GetProviders();
	if (Providers.Num() > 0)
	{
		return;
	}

	Providers.Add(MakeUnique<FMetaAgentSquareGridShapeProvider>());
	Providers.Add(MakeUnique<FMetaAgentImageSilhouetteShapeProvider>());
	Providers.Add(MakeUnique<FMetaAgentSplinePathShapeProvider>());
	Providers.Add(MakeUnique<FMetaAgentMeshSilhouetteShapeProvider>());
	Providers.Add(MakeUnique<FMetaAgentRandomParallelepipedShapeProvider>());
}

bool FMetaAgentParticleShapeRegistry::BuildPatternTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	FMetaAgentParticleShapeBuildResult& OutResult)
{
	RegisterDefaults();

	for (const TUniquePtr<FMetaAgentParticleShapeProviderBase>& Provider : GetProviders())
	{
		if (!Provider || Provider->GetShapeType() != PatternConfig.Shape.ShapeType)
		{
			continue;
		}

		if (Provider->BuildTargets(PatternConfig, ShapeContext, OutResult))
		{
			return true;
		}

		return false;
	}

	OutResult.bSuccess = false;
	OutResult.DebugInfo = FString::Printf(
		TEXT("No shape provider registered for %s."),
		*PatternConfig.Shape.GetShapeDisplayName());
	return false;
}
