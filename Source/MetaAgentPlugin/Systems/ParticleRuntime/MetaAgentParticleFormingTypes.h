// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "MetaAgentParticleFormingTypes.generated.h"

UENUM(BlueprintType)
enum class EMetaAgentParticleFormingMode : uint8
{
	/** Straight baseline → target lerp (default). */
	DirectLerp UMETA(DisplayName = "Direct Lerp"),
	/** Lift along world up mid-form, then settle on target. */
	ArcLift UMETA(DisplayName = "Arc Lift"),
	/** Spiral inward toward the pattern center. */
	SpiralIn UMETA(DisplayName = "Spiral In"),
	/** Per-particle delayed wave across the cloud. */
	StaggeredWave UMETA(DisplayName = "Staggered Wave"),
	/** Spring-damper chase with C++ velocity state. */
	SpringChase UMETA(DisplayName = "Spring Chase"),
	/** Push forming params to Niagara; motion authored in GPU modules. */
	NiagaraForces UMETA(DisplayName = "Niagara Forces")
};

USTRUCT(BlueprintType)
struct FMetaAgentParticleFormingSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming")
	EMetaAgentParticleFormingMode Mode = EMetaAgentParticleFormingMode::DirectLerp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.0"))
	float ArcLiftHeightCm = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.0"))
	float SpiralTurns = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.1"))
	float StaggerWaveCycles = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.1"))
	float SpringStiffness = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.0"))
	float SpringDamping = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.0"))
	float NiagaraForceStrength = 500.0f;

	METAAGENTPLUGIN_API FString GetModeDisplayName() const;

	METAAGENTPLUGIN_API void CycleMode();
};
