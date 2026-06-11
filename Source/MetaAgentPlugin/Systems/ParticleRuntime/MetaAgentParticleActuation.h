// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleActuatorTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticleFormingTypes.h"
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
	/** Returning: lerp live sim (ReturnRestPositions) → held shape (ReturnHoldPositions) by BlendAlpha (phase 1 = hold, 0 = live). */
	bool bUseReturnHoldBlend = false;
	const TArray<FVector>* ReturnHoldPositions = nullptr;
	const TArray<FVector>* ReturnRestPositions = nullptr;
	const TArray<FVector>* DissipateStartPositions = nullptr;
	/** Optional forming steering offsets (first ~0.2s when steering target is active). */
	const TArray<FVector>* FormingSteeringOffsets = nullptr;
	float FormingSteeringWeight = 0.0f;
	EMetaAgentParticlePatternState PatternState = EMetaAgentParticlePatternState::Idle;
	const FMetaAgentParticleFormingSettings* FormingSettings = nullptr;
	float FormingStateElapsedSeconds = 0.0f;
	float FormingDurationSeconds = 1.0f;
	float FormingDeltaTimeSeconds = 0.0f;
	bool bAnticipatingMotion = false;
	float AnticipationElapsedSeconds = 0.0f;
	float AnticipationAmplitudeCm = 12.0f;
	float AnticipationFrequencyHz = 1.2f;
	float AnticipationIdleBlendDurationSeconds = 0.35f;
	/** Idle baseline for anticipation carryover during early Forming. */
	const TArray<FVector>* IdleBaselineWorldPositions = nullptr;
	float AnticipationHandoffElapsedSeconds = -1.0f;
	float FormingAnticipationCarryoverDurationSeconds = 0.35f;
	bool bDissipatingMotion = false;
	float DissipateVisibility = 1.0f;
};

class METAAGENTPLUGIN_API FMetaAgentParticleActuation
{
public:
	/** World position for one particle during Anticipating (idle baseline + center pull / orbit). */
	static FVector ComputeAnticipationWorldPosition(
		const FVector& IdleBaseline,
		const int32 GlobalIndex,
		const FVector& PatternCenter,
		const float AnticipationElapsedSeconds,
		const float AnticipationAmplitudeCm,
		const float AnticipationFrequencyHz,
		const float AnticipationIdleBlendDurationSeconds = 0.35f);

	static void BuildAnticipationWorldPositions(
		const TArray<FVector>& IdleBaselineWorldPositions,
		const FVector& PatternCenter,
		const float AnticipationElapsedSeconds,
		const float AnticipationAmplitudeCm,
		const float AnticipationFrequencyHz,
		TArray<FVector>& OutWorldPositions,
		const float AnticipationIdleBlendDurationSeconds = 0.35f);

	static IMetaAgentParticleActuator& GetActuator(EMetaAgentParticleActuationMode Mode);

	static int32 ApplyDirect(
		const FMetaAgentParticleActuationRequest& Request,
		TArray<FVector>& OutAppliedWorldPositions);

	static void ApplyParameters(const FMetaAgentParticleActuationRequest& Request);

	static EMetaAgentParticleActuationMode ResolveEffectiveMode(
		EMetaAgentParticleActuationMode ConfiguredMode);
};
