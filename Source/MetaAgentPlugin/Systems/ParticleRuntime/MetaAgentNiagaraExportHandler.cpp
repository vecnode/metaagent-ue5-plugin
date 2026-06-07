// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentNiagaraExportHandler.h"

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "NiagaraSystem.h"

void UMetaAgentNiagaraExportHandler::Initialize(AMetaAgentPlayerController* InOwnerController)
{
	OwnerController = InOwnerController;
}

void UMetaAgentNiagaraExportHandler::ReceiveParticleData_Implementation(
	const TArray<FBasicParticleData>& Data,
	UNiagaraSystem* NiagaraSystem,
	const FVector& SimulationPositionOffset)
{
	AMetaAgentPlayerController* Controller = OwnerController.Get();
	if (!Controller)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("ParticleRuntime: export handler has no owner controller."));
		return;
	}

	TArray<FVector> ParticlePositions;
	ParticlePositions.Reserve(Data.Num());

	for (const FBasicParticleData& ParticleData : Data)
	{
		ParticlePositions.Add(ParticleData.Position + SimulationPositionOffset);
	}

	const FName SourceSystemName = NiagaraSystem ? NiagaraSystem->GetFName() : NAME_None;
	UE_LOG(LogMetaAgent, Log,
		TEXT("ParticleRuntime: ReceiveParticleData from system '%s' with %d particle(s)."),
		*SourceSystemName.ToString(),
		ParticlePositions.Num());

	Controller->SubmitNiagaraParticlePositions(ParticlePositions, SourceSystemName, NAME_None);
}
