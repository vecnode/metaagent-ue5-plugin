// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleFormingTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticleReturnTypes.h"
#include "MetaAgentParticleRepresentationTypes.generated.h"

/** Macro phases for the representation scheduler (maps many micro-states). */
UENUM(BlueprintType)
enum class EMetaAgentRepresentationMacroPhase : uint8
{
	Idle,
	/** Anticipating / async prepare. */
	Prepare,
	/** Forming / morph-in. */
	Express,
	/** Holding / sustain. */
	Sustain,
	/** Returning / dissipating. */
	Release
};

UENUM(BlueprintType)
enum class EMetaAgentPatternTransitionTrigger : uint8
{
	Start,
	Advance,
	Retreat,
	Timeout,
	Cancel,
	Morph,
	Dissipate,
	/** Async mask / targets became ready during Prepare. */
	Ready
};

USTRUCT(BlueprintType)
struct FMetaAgentRepresentationPhase
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float NormalizedTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float BlendAlpha = 0.0f;

	/** 1 = C++ owns positions; 0 = Niagara owns sim. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AuthorityWeight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float Visibility = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float Emphasis = 1.0f;
};

/**
 * Snapshot emitted each active-pattern tick by the representation scheduler.
 * Drivers consume this frame to push Niagara parameters and/or direct positions.
 */
USTRUCT(BlueprintType)
struct FMetaAgentParticleRepresentationFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	EMetaAgentRepresentationMacroPhase MacroPhase = EMetaAgentRepresentationMacroPhase::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	EMetaAgentParticlePatternState PatternState = EMetaAgentParticlePatternState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	FMetaAgentRepresentationPhase Phase;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	FVector PatternCenter = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	bool bPatternActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	bool bAnticipatingMotion = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	bool bDissipatingMotion = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	bool bUseReturnHoldBlend = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AnticipationElapsedSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AnticipationAmplitudeCm = 12.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AnticipationFrequencyHz = 1.2f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AnticipationIdleBlendDurationSeconds = 0.35f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float AnticipationHandoffElapsedSeconds = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float FormingAnticipationCarryoverDurationSeconds = 0.35f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float FormingStateElapsedSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float FormingDurationSeconds = 1.5f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float FormingDeltaTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float FormingSteeringWeight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	float DissipateVisibility = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	FMetaAgentParticleFormingSettings FormingSettings;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	bool bReturnUsesMotionSolver = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	FMetaAgentParticleFormingSettings ReturnMotionSettings;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> BaselineWorldPositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> PatternWorldTargets;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> IdleBaselineWorldPositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> ReturnHoldPositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> ReturnRestPositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> DissipateStartPositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Representation")
	TArray<FVector> FormingSteeringOffsets;
};

class METAAGENTPLUGIN_API FMetaAgentParticleRepresentationMapping
{
public:
	static EMetaAgentRepresentationMacroPhase MacroPhaseFromPatternState(EMetaAgentParticlePatternState State);
	static FString GetMacroPhaseDisplayName(EMetaAgentRepresentationMacroPhase MacroPhase);
};
