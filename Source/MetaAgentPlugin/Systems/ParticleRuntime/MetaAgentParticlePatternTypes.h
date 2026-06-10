// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeTypes.h"
#include "MetaAgentParticlePatternTypes.generated.h"

class UCurveFloat;
class UMetaAgentParticlePatternAsset;

UENUM(BlueprintType)
enum class EMetaAgentParticlePatternState : uint8
{
	Idle,
	Preparing,
	Forming,
	Holding,
	Returning,
	/** Deprecated: unused; return now follows live sim directly. Kept for Blueprint compat. */
	Releasing UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EMetaAgentParticlePatternPreset : uint8
{
	Normal,
	Slow,
	Dramatic,
	/** Tuned for C-key random box sculpt (form / hold / return). */
	Sculpt,
	Custom
};

UENUM(BlueprintType)
enum class EMetaAgentParticleActuationMode : uint8
{
	/** Direct Niagara Position buffer read/write. */
	Direct,
	/** Push phase/center/active via Niagara user parameters. */
	Parameters,
	/** Direct in editor/PIE; Parameters in packaged builds. */
	Hybrid
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnMetaAgentPatternStateChanged,
	EMetaAgentParticlePatternState, NewState,
	EMetaAgentParticlePatternState, PreviousState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMetaAgentPatternCompleted);

USTRUCT(BlueprintType)
struct FMetaAgentParticlePatternConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern", meta = (ClampMin = "0.1"))
	float FormDurationSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern", meta = (ClampMin = "0.0"))
	float HoldDurationSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern", meta = (ClampMin = "0.1"))
	float ReturnDurationSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern", meta = (ClampMin = "1.0"))
	float GridSpacingCm = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern")
	FMetaAgentParticleShapeDefinition Shape;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	EMetaAgentParticlePatternPreset ActivePreset = EMetaAgentParticlePatternPreset::Normal;

	static FMetaAgentParticlePatternConfig MakeFromPreset(EMetaAgentParticlePatternPreset Preset);

	void ApplyPreset(EMetaAgentParticlePatternPreset Preset);

	FString GetPresetDisplayName() const;
};

USTRUCT(BlueprintType)
struct FMetaAgentParticlePatternRuntime
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	EMetaAgentParticlePatternState State = EMetaAgentParticlePatternState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	float Phase = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	float StateElapsedSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	int32 PatternColumns = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FVector PatternCenter = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	EMetaAgentParticlePatternShape ActiveShape = EMetaAgentParticlePatternShape::SquareGrid;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FMetaAgentParticleShapeFrame ActiveShapeFrame;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FString ShapeDebugInfo;

	/** Timings frozen when the current pattern run started. */
	UPROPERTY()
	FMetaAgentParticlePatternConfig ActiveConfig;

	UPROPERTY()
	TArray<FVector> BaselineWorldPositions;

	UPROPERTY()
	TArray<FVector> PatternWorldTargets;

	/** Positions frozen when Returning begins (end of Holding actuation). */
	UPROPERTY()
	TArray<FVector> ReturnHoldPositions;

	/** Frozen idle rest positions for return (captured once when Returning begins). */
	UPROPERTY()
	TArray<FVector> ReturnRestPositions;

	/** Positions when Holding began (formed shape reference). */
	UPROPERTY()
	TArray<FVector> TrajectoryWorldPositions;

	UPROPERTY()
	bool bAwaitingAsyncMask = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FGameplayTagContainer ActivePatternTags;
};
