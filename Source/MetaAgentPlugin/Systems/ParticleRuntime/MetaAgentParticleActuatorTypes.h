// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"

class UNiagaraComponent;

/** Pluggable actuation backend (Direct buffer write or Niagara user parameters). */
class METAAGENTPLUGIN_API IMetaAgentParticleActuator
{
public:
	virtual ~IMetaAgentParticleActuator() = default;

	virtual EMetaAgentParticleActuationMode GetActuationMode() const = 0;

	virtual bool SupportsComponent(const UNiagaraComponent& NiagaraComponent) const;

	virtual int32 ApplyPhase(
		const struct FMetaAgentParticleActuationRequest& Request,
		TArray<FVector>& OutAppliedWorldPositions);

	virtual void ApplyParameters(const struct FMetaAgentParticleActuationRequest& Request);

	virtual void Reset();
};
