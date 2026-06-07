// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraDataInterfaceExport.h"
#include "UObject/Object.h"
#include "MetaAgentNiagaraExportHandler.generated.h"

class AMetaAgentPlayerController;

/**
 * Dedicated C++ Niagara export callback handler.
 * Niagara binds to this UObject via the system user parameter; it forwards particle data to the player controller runtime.
 */
UCLASS()
class METAAGENTPLUGIN_API UMetaAgentNiagaraExportHandler : public UObject, public INiagaraParticleCallbackHandler
{
	GENERATED_BODY()

public:
	void Initialize(AMetaAgentPlayerController* InOwnerController);

	virtual void ReceiveParticleData_Implementation(
		const TArray<FBasicParticleData>& Data,
		UNiagaraSystem* NiagaraSystem,
		const FVector& SimulationPositionOffset) override;

private:
	TWeakObjectPtr<AMetaAgentPlayerController> OwnerController;
};
