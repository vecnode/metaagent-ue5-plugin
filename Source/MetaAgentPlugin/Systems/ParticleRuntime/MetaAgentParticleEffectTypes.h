// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"
#include "MetaAgentParticleEffectTypes.generated.h"

class UMetaAgentParticlePatternAsset;

/** Built-in effect identifiers (extend in subclasses via PopulateEffectSpec). */
namespace MetaAgentParticleEffectIds
{
	METAAGENTPLUGIN_API extern const FName ImageReveal;
	METAAGENTPLUGIN_API extern const FName SplinePath;
	METAAGENTPLUGIN_API extern const FName MeshSilhouette;
	METAAGENTPLUGIN_API extern const FName GridSquare;
	METAAGENTPLUGIN_API extern const FName PresetSlow;
	METAAGENTPLUGIN_API extern const FName PresetDramatic;
	METAAGENTPLUGIN_API extern const FName PlayNormal;
	METAAGENTPLUGIN_API extern const FName PlaySlow;
	METAAGENTPLUGIN_API extern const FName PlayDramatic;
	METAAGENTPLUGIN_API extern const FName ReplayLast;
	METAAGENTPLUGIN_API extern const FName CycleSampling;
	METAAGENTPLUGIN_API extern const FName CycleForming;
	METAAGENTPLUGIN_API extern const FName AttractToView;
}

USTRUCT(BlueprintType)
struct FMetaAgentParticleEffectResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Effect")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Effect")
	FName EffectId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Effect")
	FText UserMessage;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Effect")
	bool bAwaitingAsyncPrepare = false;
};

/** Config applied when an effect is triggered (FSM choreography unchanged). */
USTRUCT(BlueprintType)
struct FMetaAgentParticleEffectSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	FMetaAgentParticlePatternConfig PatternConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	bool bOverridePatternConfig = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	bool bStartPattern = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	bool bSteerTowardViewOnForm = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	float SteeringStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Effect")
	TObjectPtr<UMetaAgentParticlePatternAsset> PatternAsset = nullptr;
};
