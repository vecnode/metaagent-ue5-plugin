// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleActuation.h"
#include "Systems/ParticleRuntime/MetaAgentParticleRepresentationTypes.h"

class UNiagaraComponent;
class UMetaAgentNiagaraSystemProfile;

class METAAGENTPLUGIN_API IMetaAgentParticleRepresentationDriver
{
public:
	virtual ~IMetaAgentParticleRepresentationDriver() = default;

	virtual FName GetDriverId() const = 0;
	virtual EMetaAgentParticleActuationMode GetActuationMode() const = 0;

	virtual bool SupportsComponent(
		const UNiagaraComponent& NiagaraComponent,
		const UMetaAgentNiagaraSystemProfile* Profile) const = 0;

	virtual int32 ApplyFrame(
		const FMetaAgentParticleRepresentationFrame& Frame,
		FMetaAgentParticleActuationRequest& Request,
		const UMetaAgentNiagaraSystemProfile* Profile,
		TArray<FVector>& OutAppliedWorldPositions) const = 0;
};

class METAAGENTPLUGIN_API FMetaAgentParticleRepresentationDriverRegistry
{
public:
	static void RegisterDefaults();

	static void RegisterDriver(TUniquePtr<IMetaAgentParticleRepresentationDriver> Driver);

	static const IMetaAgentParticleRepresentationDriver& ResolveDriver(
		EMetaAgentParticleActuationMode EffectiveMode);

	static int32 ApplyRepresentationFrame(
		const FMetaAgentParticleRepresentationFrame& Frame,
		FMetaAgentParticleActuationRequest& Request,
		const UMetaAgentNiagaraSystemProfile* Profile,
		EMetaAgentParticleActuationMode ConfiguredMode,
		float ReturnReleaseAuthorityThreshold,
		class UMetaAgentNiagaraTargetData* SharedTargetData,
		TArray<FVector>& OutAppliedWorldPositions);

	static void BuildActuationRequestFromFrame(
		const FMetaAgentParticleRepresentationFrame& Frame,
		FMetaAgentParticleActuationRequest& OutRequest);

private:
	static TArray<TUniquePtr<IMetaAgentParticleRepresentationDriver>>& GetDrivers();
};
