// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleRuntime.h"

#include "NiagaraComponent.h"
#include "Core/MetaAgent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

void UMetaAgentParticleRuntime::InitializeRuntime(UObject* WorldContextObject)
{
	CachedWorld = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	DiscoveryFrameCounter = 0;
	LatestSnapshot = FMetaAgentParticleSnapshot();
	TrackedNiagaraComponents.Reset();

	DiscoverNiagaraComponents(true);
}

void UMetaAgentParticleRuntime::TickRuntime(const float DeltaTimeSeconds)
{
	if (!CachedWorld.IsValid())
	{
		return;
	}

	LatestSnapshot.WorldTimeSeconds = CachedWorld->GetTimeSeconds();
	LatestSnapshot.FrameCounter++;

	DiscoveryFrameCounter++;
	const int32 DiscoveryInterval = FMath::Max(1, DiscoveryEveryNFrames);
	if (DiscoveryFrameCounter >= DiscoveryInterval)
	{
		DiscoveryFrameCounter = 0;
		DiscoverNiagaraComponents(false);
	}

	BuildComponentSnapshot();
	RebuildSuggestedSteeringDirections();

	if (DeltaTimeSeconds < 0.0f)
	{
		UE_LOG(LogMetaAgent, Verbose, TEXT("ParticleRuntime received negative delta, which should not happen in runtime tick."));
	}
}

void UMetaAgentParticleRuntime::DiscoverNiagaraComponents(const bool bLogSummary)
{
	if (!CachedWorld.IsValid())
	{
		return;
	}

	TArray<TWeakObjectPtr<UNiagaraComponent>> DiscoveredComponents;

	for (TActorIterator<AActor> ActorIt(CachedWorld.Get()); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!Actor)
		{
			continue;
		}

		TArray<UNiagaraComponent*> NiagaraComponents;
		Actor->GetComponents<UNiagaraComponent>(NiagaraComponents);

		for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
		{
			if (!NiagaraComponent || !PassesNameFilter(Actor, NiagaraComponent))
			{
				continue;
			}

			DiscoveredComponents.Add(NiagaraComponent);
		}
	}

	TrackedNiagaraComponents = MoveTemp(DiscoveredComponents);
	BuildComponentSnapshot();

	if (bLogSummary)
	{
		UE_LOG(LogMetaAgent, Log,
			TEXT("ParticleRuntime: discovered %d Niagara component(s). Filter=%s NameFilter='%s'"),
			TrackedNiagaraComponents.Num(),
			bFilterToNiagaraNamedActorsOrComponents ? TEXT("On") : TEXT("Off"),
			*NameFilter);
	}
}

void UMetaAgentParticleRuntime::SubmitExportedParticlePositions(
	const TArray<FVector>& ParticlePositions,
	const FName SourceActorName,
	const FName SourceComponentName)
{
	LatestSnapshot.ExportedParticlePositions = ParticlePositions;
	LatestSnapshot.ExportedParticleCount = ParticlePositions.Num();
	LatestSnapshot.CallbackEventCount++;
	LatestSnapshot.LastExportSourceActor = SourceActorName;
	LatestSnapshot.LastExportSourceComponent = SourceComponentName;

	if (CachedWorld.IsValid())
	{
		LatestSnapshot.WorldTimeSeconds = CachedWorld->GetTimeSeconds();
	}

	RebuildSuggestedSteeringDirections();
}

void UMetaAgentParticleRuntime::ClearExportedParticlePositions()
{
	LatestSnapshot.ExportedParticlePositions.Reset();
	LatestSnapshot.ExportedParticleCount = 0;
	LatestSnapshot.LastExportSourceActor = NAME_None;
	LatestSnapshot.LastExportSourceComponent = NAME_None;
	LatestSnapshot.SuggestedSteeringDirections.Reset();
}

void UMetaAgentParticleRuntime::SetSteeringTarget(const FVector& TargetLocation, const float Strength)
{
	LatestSnapshot.bSteeringTargetEnabled = true;
	LatestSnapshot.SteeringTargetLocation = TargetLocation;
	LatestSnapshot.SteeringStrength = FMath::Max(0.0f, Strength);
	RebuildSuggestedSteeringDirections();
}

void UMetaAgentParticleRuntime::ClearSteeringTarget()
{
	LatestSnapshot.bSteeringTargetEnabled = false;
	LatestSnapshot.SteeringTargetLocation = FVector::ZeroVector;
	LatestSnapshot.SteeringStrength = 1.0f;
	LatestSnapshot.SuggestedSteeringDirections.Reset();
}

FString UMetaAgentParticleRuntime::BuildStatusText() const
{
	return FString::Printf(
		TEXT("ParticleRuntime: NiagaraComponents=%d ExportedParticles=%d CallbackEvents=%d Steering=%s SourceActor=%s SourceComp=%s"),
		TrackedNiagaraComponents.Num(),
		LatestSnapshot.ExportedParticleCount,
		LatestSnapshot.CallbackEventCount,
		LatestSnapshot.bSteeringTargetEnabled ? TEXT("On") : TEXT("Off"),
		*LatestSnapshot.LastExportSourceActor.ToString(),
		*LatestSnapshot.LastExportSourceComponent.ToString());
}

bool UMetaAgentParticleRuntime::PassesNameFilter(const AActor* OwnerActor, const UNiagaraComponent* NiagaraComponent) const
{
	if (!OwnerActor || !NiagaraComponent)
	{
		return false;
	}

	if (!bFilterToNiagaraNamedActorsOrComponents)
	{
		return true;
	}

	const FString EffectiveFilter = NameFilter.IsEmpty() ? TEXT("NIAGARA") : NameFilter;
	return OwnerActor->GetName().Contains(EffectiveFilter, ESearchCase::IgnoreCase)
		|| OwnerActor->GetActorNameOrLabel().Contains(EffectiveFilter, ESearchCase::IgnoreCase)
		|| NiagaraComponent->GetName().Contains(EffectiveFilter, ESearchCase::IgnoreCase);
}

void UMetaAgentParticleRuntime::BuildComponentSnapshot()
{
	LatestSnapshot.TrackedComponents.Reset();

	for (int32 Index = TrackedNiagaraComponents.Num() - 1; Index >= 0; --Index)
	{
		UNiagaraComponent* NiagaraComponent = TrackedNiagaraComponents[Index].Get();
		if (!NiagaraComponent)
		{
			TrackedNiagaraComponents.RemoveAtSwap(Index);
			continue;
		}

		AActor* OwnerActor = NiagaraComponent->GetOwner();
		if (!OwnerActor)
		{
			continue;
		}

		const FBoxSphereBounds Bounds = NiagaraComponent->Bounds;

		FMetaAgentTrackedNiagaraComponent Entry;
		Entry.ActorName = OwnerActor->GetActorNameOrLabel();
		Entry.ComponentName = NiagaraComponent->GetName();
		Entry.ComponentLocation = NiagaraComponent->GetComponentLocation();
		Entry.BoundsOrigin = Bounds.Origin;
		Entry.BoundsExtent = Bounds.BoxExtent;

		LatestSnapshot.TrackedComponents.Add(MoveTemp(Entry));
	}
}

void UMetaAgentParticleRuntime::RebuildSuggestedSteeringDirections()
{
	LatestSnapshot.SuggestedSteeringDirections.Reset();

	if (!LatestSnapshot.bSteeringTargetEnabled)
	{
		return;
	}

	LatestSnapshot.SuggestedSteeringDirections.Reserve(LatestSnapshot.ExportedParticlePositions.Num());

	for (const FVector& ParticlePosition : LatestSnapshot.ExportedParticlePositions)
	{
		FVector Direction = LatestSnapshot.SteeringTargetLocation - ParticlePosition;
		if (!Direction.Normalize())
		{
			Direction = FVector::ZeroVector;
		}

		Direction *= LatestSnapshot.SteeringStrength;
		LatestSnapshot.SuggestedSteeringDirections.Add(Direction);
	}
}
