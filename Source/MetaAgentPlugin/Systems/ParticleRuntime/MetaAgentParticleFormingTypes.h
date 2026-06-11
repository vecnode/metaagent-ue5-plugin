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
	SpiralIn UMETA(DisplayName = "Spiral In")
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

	METAAGENTPLUGIN_API FString GetModeDisplayName() const;

	METAAGENTPLUGIN_API void CycleMode();

	METAAGENTPLUGIN_API static EMetaAgentParticleFormingMode SanitizeMode(EMetaAgentParticleFormingMode Mode);
};
