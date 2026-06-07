// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MetaAgentParticleRuntime.generated.h"

class UNiagaraComponent;

USTRUCT(BlueprintType)
struct FMetaAgentTrackedNiagaraComponent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FString ComponentName;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FVector ComponentLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FVector BoundsOrigin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FVector BoundsExtent = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FMetaAgentParticleSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	int32 FrameCounter = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	float WorldTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	int32 ExportedParticleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	int32 CallbackEventCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FName LastExportSourceActor = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FName LastExportSourceComponent = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	TArray<FMetaAgentTrackedNiagaraComponent> TrackedComponents;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	TArray<FVector> ExportedParticlePositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Steering")
	bool bSteeringTargetEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Steering")
	FVector SteeringTargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Steering")
	float SteeringStrength = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Steering")
	TArray<FVector> SuggestedSteeringDirections;
};

/**
 * Runtime skeleton for Niagara introspection.
 * Stage A tracks Niagara components in the active world and their transforms/bounds.
 * Stage B accepts per-particle positions exported from Niagara graphs/blueprints.
 */
UCLASS(BlueprintType)
class METAAGENTPLUGIN_API UMetaAgentParticleRuntime : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void InitializeRuntime(UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void TickRuntime(float DeltaTimeSeconds);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void DiscoverNiagaraComponents(bool bLogSummary = false);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void SubmitExportedParticlePositions(
		const TArray<FVector>& ParticlePositions,
		FName SourceActorName = NAME_None,
		FName SourceComponentName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void ClearExportedParticlePositions();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Steering")
	void SetSteeringTarget(const FVector& TargetLocation, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Steering")
	void ClearSteeringTarget();

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	const FMetaAgentParticleSnapshot& GetLatestSnapshot() const { return LatestSnapshot; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	TArray<FVector> GetKnownParticlePositions() const { return LatestSnapshot.ExportedParticlePositions; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	int32 GetKnownParticleCount() const { return LatestSnapshot.ExportedParticleCount; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	FString BuildStatusText() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	bool HasKnownParticleData() const { return LatestSnapshot.ExportedParticleCount > 0; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	bool HasReceivedAnyCallback() const { return LatestSnapshot.CallbackEventCount > 0; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Steering")
	bool HasSteeringTarget() const { return LatestSnapshot.bSteeringTargetEnabled; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Steering")
	TArray<FVector> GetSuggestedSteeringDirections() const { return LatestSnapshot.SuggestedSteeringDirections; }

private:
	bool PassesNameFilter(const AActor* OwnerActor, const UNiagaraComponent* NiagaraComponent) const;
	void BuildComponentSnapshot();
	void RebuildSuggestedSteeringDirections();

	UPROPERTY(Transient)
	TWeakObjectPtr<UWorld> CachedWorld;

	UPROPERTY(Transient)
	FMetaAgentParticleSnapshot LatestSnapshot;

	TArray<TWeakObjectPtr<UNiagaraComponent>> TrackedNiagaraComponents;

	int32 DiscoveryFrameCounter = 0;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles")
	int32 DiscoveryEveryNFrames = 30;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles")
	bool bFilterToNiagaraNamedActorsOrComponents = true;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles")
	FString NameFilter = TEXT("NIAGARA");
};
