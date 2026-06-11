// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentNiagaraTargetData.h"

void UMetaAgentNiagaraTargetData::SetTargets(
	const TArray<FVector>& InPatternTargets,
	const TArray<FVector>& InBaselines)
{
	PatternTargets = InPatternTargets;
	Baselines = InBaselines;
}
