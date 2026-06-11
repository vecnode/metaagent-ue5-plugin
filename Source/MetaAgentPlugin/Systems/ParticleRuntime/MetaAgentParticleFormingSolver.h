// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleFormingTypes.h"

struct FMetaAgentParticleFormingContext
{
	int32 GlobalIndex = INDEX_NONE;
	int32 TotalParticleCount = 0;
	FVector Baseline = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	FVector PatternCenter = FVector::ZeroVector;
	float BlendAlpha = 0.0f;
	float StateElapsedSeconds = 0.0f;
	float FormDurationSeconds = 1.0f;
	float DeltaTimeSeconds = 0.0f;
	const FMetaAgentParticleFormingSettings* Settings = nullptr;
	float FormingSteeringWeight = 0.0f;
	FVector FormingSteeringOffset = FVector::ZeroVector;
};

class METAAGENTPLUGIN_API IMetaAgentParticleFormingSolver
{
public:
	virtual ~IMetaAgentParticleFormingSolver() = default;

	virtual EMetaAgentParticleFormingMode GetMode() const = 0;
	virtual FVector SolvePosition(FMetaAgentParticleFormingContext& Context) const = 0;
};

class METAAGENTPLUGIN_API FMetaAgentParticleFormingSolverRegistry
{
public:
	static void RegisterDefaults();

	static void RegisterSolver(TUniquePtr<IMetaAgentParticleFormingSolver> Solver);

	static const IMetaAgentParticleFormingSolver& GetSolver(EMetaAgentParticleFormingMode Mode);
	static FVector SolveFormingPosition(FMetaAgentParticleFormingContext& Context);

private:
	static TArray<TUniquePtr<IMetaAgentParticleFormingSolver>>& GetSolvers();
};
