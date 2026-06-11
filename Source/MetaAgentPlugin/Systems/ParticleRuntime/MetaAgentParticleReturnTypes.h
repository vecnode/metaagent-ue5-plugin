// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleFormingTypes.h"
#include "MetaAgentParticleReturnTypes.generated.h"

class UCurveFloat;

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

	/** Optional easing curve for DirectLerp return (normalized return time 0–1 → blend weight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning|Curves")
	TObjectPtr<UCurveFloat> DirectLerpReturnCurve = nullptr;

	/** Optional easing curve for ArcLift return. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning|Curves")
	TObjectPtr<UCurveFloat> ArcLiftReturnCurve = nullptr;

	/** Optional easing curve for SpiralIn return. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Returning|Curves")
	TObjectPtr<UCurveFloat> SpiralInReturnCurve = nullptr;

	METAAGENTPLUGIN_API FString GetModeDisplayName() const;

	METAAGENTPLUGIN_API const UCurveFloat* GetReturnCurveForMode() const;

	METAAGENTPLUGIN_API void CycleMode();

	METAAGENTPLUGIN_API bool UsesMotionSolver() const;

	METAAGENTPLUGIN_API FMetaAgentParticleFormingSettings AsFormingSettings() const;

	METAAGENTPLUGIN_API static EMetaAgentParticleReturnMode SanitizeMode(EMetaAgentParticleReturnMode Mode);
};
