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

	UPROPERTY()
	TArray<FVector> BaselineWorldPositions;

	UPROPERTY()
	TArray<FVector> PatternWorldTargets;
};
