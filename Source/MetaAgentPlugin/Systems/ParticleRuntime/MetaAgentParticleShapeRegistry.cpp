// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleShapeRegistry.h"

#include "Systems/ParticleRuntime/MetaAgentParticleImageMaskProcessor.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeBuilder.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeCache.h"

namespace
{
	class FMetaAgentSquareGridShapeProvider final : public IMetaAgentParticleShapeProvider
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::SquareGrid;
		}

		virtual FName GetShapeId() const override
		{
			return TEXT("SquareGrid");
		}

		virtual int32 GetPriority() const override { return 0; }

		virtual bool CanBuild(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext) const override
		{
			return ShapeContext.BaselineWorldPositions.Num() > 0;
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildSquareGridTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

	class FMetaAgentImageSilhouetteShapeProvider final : public IMetaAgentParticleShapeProvider
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::ImageSilhouette;
		}

		virtual FName GetShapeId() const override
		{
			return TEXT("ImageSilhouette");
		}

		virtual int32 GetPriority() const override { return 0; }

		virtual bool CanBuild(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext) const override
		{
			return ShapeContext.BaselineWorldPositions.Num() > 0
				&& (!ShapeContext.SourceImagePath.IsEmpty() || ShapeContext.bHasResolvedImage);
		}

		virtual bool RequiresAsyncPrepare(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext) const override
		{
			if (!CanBuild(PatternConfig, ShapeContext) || ShapeContext.SourceImagePath.IsEmpty())
			{
				return false;
			}

			const int32 DesiredPointCount = FMath::Max(1, ShapeContext.BaselineWorldPositions.Num());
			const FMetaAgentImageMaskBuildParams Params = MetaAgentImageMask::MakeBuildParams(
				ShapeContext.SourceImagePath,
				PatternConfig.Shape,
				DesiredPointCount);
			return !FMetaAgentParticleShapeCache::IsMaskReady(Params);
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildImageSilhouetteTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

	class FMetaAgentSplinePathShapeProvider final : public IMetaAgentParticleShapeProvider
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::SplinePath;
		}

		virtual FName GetShapeId() const override
		{
			return TEXT("SplinePath");
		}

		virtual int32 GetPriority() const override { return 0; }

		virtual bool CanBuild(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext) const override
		{
			return ShapeContext.BaselineWorldPositions.Num() > 0 && ShapeContext.World != nullptr;
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildSplinePathTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

	class FMetaAgentMeshSilhouetteShapeProvider final : public IMetaAgentParticleShapeProvider
	{
	public:
		virtual EMetaAgentParticlePatternShape GetShapeType() const override
		{
			return EMetaAgentParticlePatternShape::MeshSilhouette;
		}

		virtual FName GetShapeId() const override
		{
			return TEXT("MeshSilhouette");
		}

		virtual int32 GetPriority() const override { return 0; }

		virtual bool CanBuild(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext) const override
		{
			return ShapeContext.BaselineWorldPositions.Num() > 0 && ShapeContext.World != nullptr;
		}

		virtual bool BuildTargets(
			const FMetaAgentParticlePatternConfig& PatternConfig,
			const FMetaAgentParticleShapeContext& ShapeContext,
			FMetaAgentParticleShapeBuildResult& OutResult) override
		{
			return FMetaAgentParticleShapeBuilder::BuildMeshSilhouetteTargets(PatternConfig, ShapeContext, OutResult);
		}
	};

}

TArray<TUniquePtr<IMetaAgentParticleShapeProvider>>& FMetaAgentParticleShapeRegistry::GetProviders()
{
	static TArray<TUniquePtr<IMetaAgentParticleShapeProvider>> Providers;
	return Providers;
}

void FMetaAgentParticleShapeRegistry::RegisterProvider(TUniquePtr<IMetaAgentParticleShapeProvider> Provider)
{
	RegisterDefaults();
	if (Provider)
	{
		GetProviders().Add(MoveTemp(Provider));
	}
}

void FMetaAgentParticleShapeRegistry::RegisterDefaults()
{
	TArray<TUniquePtr<IMetaAgentParticleShapeProvider>>& Providers = GetProviders();
	if (Providers.Num() > 0)
	{
		return;
	}

	Providers.Add(MakeUnique<FMetaAgentSquareGridShapeProvider>());
	Providers.Add(MakeUnique<FMetaAgentImageSilhouetteShapeProvider>());
	Providers.Add(MakeUnique<FMetaAgentSplinePathShapeProvider>());
	Providers.Add(MakeUnique<FMetaAgentMeshSilhouetteShapeProvider>());
}

bool FMetaAgentParticleShapeRegistry::BuildPatternTargets(
	const FMetaAgentParticlePatternConfig& PatternConfig,
	const FMetaAgentParticleShapeContext& ShapeContext,
	FMetaAgentParticleShapeBuildResult& OutResult)
{
	RegisterDefaults();

	const EMetaAgentParticlePatternShape RequestedShape = PatternConfig.Shape.ShapeType;
	IMetaAgentParticleShapeProvider* BestProvider = nullptr;
	int32 BestPriority = MIN_int32;

	for (const TUniquePtr<IMetaAgentParticleShapeProvider>& Provider : GetProviders())
	{
		if (!Provider || Provider->GetShapeType() != RequestedShape)
		{
			continue;
		}

		if (!Provider->CanBuild(PatternConfig, ShapeContext))
		{
			continue;
		}

		const int32 ProviderPriority = Provider->GetPriority();
		if (!BestProvider || ProviderPriority > BestPriority)
		{
			BestProvider = Provider.Get();
			BestPriority = ProviderPriority;
		}
	}

	if (!BestProvider)
	{
		OutResult.bSuccess = false;
		OutResult.DebugInfo = FString::Printf(
			TEXT("No shape provider can build %s."),
			*PatternConfig.Shape.GetShapeDisplayName());
		return false;
	}

	if (BestProvider->RequiresAsyncPrepare(PatternConfig, ShapeContext))
	{
		OutResult.bAwaitingAsyncMask = true;
		OutResult.bSuccess = false;
		OutResult.DebugInfo = TEXT("Awaiting async mask prepare.");
		return false;
	}

	return BestProvider->BuildTargets(PatternConfig, ShapeContext, OutResult);
}
