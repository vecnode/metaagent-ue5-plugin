// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MetaAgentNiagaraTargetData.generated.h"

/** CPU-readable target field pushed to Niagara via SetVariableObject when array User params are unavailable. */
UCLASS(BlueprintType)
class METAAGENTPLUGIN_API UMetaAgentNiagaraTargetData : public UObject
{
	GENERATED_BODY()

public:
	void SetTargets(const TArray<FVector>& InPatternTargets, const TArray<FVector>& InBaselines);

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Niagara")
	const TArray<FVector>& GetPatternTargets() const { return PatternTargets; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Niagara")
	const TArray<FVector>& GetBaselines() const { return Baselines; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Niagara")
	int32 GetTargetCount() const { return PatternTargets.Num(); }

private:
	UPROPERTY(Transient)
	TArray<FVector> PatternTargets;

	UPROPERTY(Transient)
	TArray<FVector> Baselines;
};
