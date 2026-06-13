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
	/** Per-particle phase offset so the shape fills in as a wave. */
	StaggeredWave UMETA(DisplayName = "Staggered Wave"),
	/** Overshoot + settle easing on arrival. */
	SpringChase UMETA(DisplayName = "Spring Chase")
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

	/** StaggeredWave: fraction of global forming time used as per-particle delay spread (0–0.9). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float WaveSpread = 0.45f;

	/** SpringChase: peak overshoot past the target (0 = none, ~0.2 = soft bounce). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaAgent|Particles|Pattern|Forming", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float SpringOvershoot = 0.12f;

	METAAGENTPLUGIN_API FString GetModeDisplayName() const;
	METAAGENTPLUGIN_API void CycleMode();
	METAAGENTPLUGIN_API static EMetaAgentParticleFormingMode SanitizeMode(EMetaAgentParticleFormingMode Mode);
};

/** Per-particle input for metaagent forming solvers (bridge-only, not Blueprint). */
struct FMetaAgentParticleFormingContext
{
	int32 GlobalIndex = INDEX_NONE;
	int32 TotalParticleCount = 0;
	FVector Baseline = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	FVector PatternCenter = FVector::ZeroVector;
	float BlendAlpha = 0.0f;
	float StateElapsedSeconds = 0.0f;
	float FormDurationSeconds = 1.0f;
	float DeltaTimeSeconds = 0.0f;
	const FMetaAgentParticleFormingSettings* Settings = nullptr;
	float FormingSteeringWeight = 0.0f;
	FVector FormingSteeringOffset = FVector::ZeroVector;
};
