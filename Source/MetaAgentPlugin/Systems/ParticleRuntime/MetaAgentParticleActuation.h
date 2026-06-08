// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"
#include "MetaAgentParticleActuation.generated.h"

class UNiagaraComponent;
class UMetaAgentParticleRuntime;

USTRUCT(BlueprintType)
struct FMetaAgentTrackedParticleBlock
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FName SourceActorName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FName SourceComponentName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	int32 GlobalStartIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	int32 ParticleCount = 0;
};

struct FMetaAgentParticleActuationRequest
{
	const TArray<FVector>* BaselineWorldPositions = nullptr;
	const TArray<FVector>* PatternWorldTargets = nullptr;
	const TArray<FMetaAgentTrackedParticleBlock>* ParticleBlocks = nullptr;
	TArray<TWeakObjectPtr<UNiagaraComponent>> TrackedComponents;
	float BlendAlpha = 0.0f;
	float HoldPulseScale = 1.0f;
	FVector PatternCenter = FVector::ZeroVector;
	bool bPatternActive = false;
	/** Returning: lerp ReturnRestPositions ← ReturnHoldPositions by BlendAlpha (phase 1 = hold, 0 = rest). */
	bool bUseReturnHoldBlend = false;
	const TArray<FVector>* ReturnHoldPositions = nullptr;
	const TArray<FVector>* ReturnRestPositions = nullptr;
};

class METAAGENTPLUGIN_API FMetaAgentParticleActuation
{
public:
	static int32 ApplyDirect(
		const FMetaAgentParticleActuationRequest& Request,
		TArray<FVector>& OutAppliedWorldPositions);

	static void ApplyParameters(const FMetaAgentParticleActuationRequest& Request);

	static EMetaAgentParticleActuationMode ResolveEffectiveMode(
		EMetaAgentParticleActuationMode ConfiguredMode);
};
