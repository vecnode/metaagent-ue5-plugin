// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleFormingTypes.h"
#include "MetaAgentParticleReturnTypes.generated.h"

UENUM(BlueprintType)
enum class EMetaAgentParticleReturnMode : uint8
{
	/** Straight rest → hold lerp (default). */
	DirectLerp UMETA(DisplayName = "Direct Lerp"),
	/** Lift along world up mid-return, then settle on rest. */
	ArcLift UMETA(DisplayName = "Arc Lift"),
	/** Spiral outward from pattern center while returning to rest. */
	SpiralIn UMETA(DisplayName = "Spiral In"),
	/** Collapse toward pattern center and fade out instead of a positional return. */
	DissipateToCenter UMETA(DisplayName = "Dissipate To Center")
};

USTRUCT(BlueprintType)
struct FMetaAgentParticleReturnSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning")
	EMetaAgentParticleReturnMode Mode = EMetaAgentParticleReturnMode::DirectLerp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning", meta = (ClampMin = "0.0"))
	float ArcLiftHeightCm = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning", meta = (ClampMin = "0.0"))
	float SpiralTurns = 1.5f;

	METAAGENTPLUGIN_API FString GetModeDisplayName() const;

	METAAGENTPLUGIN_API void CycleMode();

	METAAGENTPLUGIN_API bool UsesMotionSolver() const;

	METAAGENTPLUGIN_API FMetaAgentParticleFormingSettings AsFormingSettings() const;

	METAAGENTPLUGIN_API static EMetaAgentParticleReturnMode SanitizeMode(EMetaAgentParticleReturnMode Mode);
};
