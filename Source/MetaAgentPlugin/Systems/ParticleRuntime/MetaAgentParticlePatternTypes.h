// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "MetaAgentParticlePatternTypes.generated.h"

UENUM(BlueprintType)
enum class EMetaAgentParticlePatternState : uint8
{
	Idle,
	Forming,
	Holding,
	Returning
};

UENUM(BlueprintType)
enum class EMetaAgentParticlePatternPreset : uint8
{
	Normal,
	Slow,
	Dramatic,
	Custom
};

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

	/** Timings frozen when the current pattern run started. */
	UPROPERTY()
	FMetaAgentParticlePatternConfig ActiveConfig;

	UPROPERTY()
	TArray<FVector> BaselineWorldPositions;

	UPROPERTY()
	TArray<FVector> PatternWorldTargets;
};
