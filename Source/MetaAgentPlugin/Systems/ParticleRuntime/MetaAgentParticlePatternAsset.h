// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"
#include "MetaAgentParticlePatternAsset.generated.h"

class UCurveFloat;
class UTexture2D;

UCLASS(BlueprintType)
class METAAGENTPLUGIN_API UMetaAgentParticlePatternAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FMetaAgentParticlePatternConfig PatternConfig;

	/** When set, replaces PatternConfig.Shape for this asset only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Shape")
	bool bOverrideShape = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Shape", meta = (EditCondition = "bOverrideShape"))
	FMetaAgentParticleShapeDefinition ShapeOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern")
	FGameplayTagContainer PatternTags;

	/** Optional disk path override for image silhouette shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Image")
	FString SourceImagePathOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Image")
	TSoftObjectPtr<UTexture2D> SourceImageTexture;

	/** Remaps normalized forming time (0–1) to blend phase. Defaults to smoothstep when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Motion")
	TObjectPtr<UCurveFloat> FormCurve = nullptr;

	/** Remaps normalized return time (0–1) to blend phase (inverted at actuation). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Motion")
	TObjectPtr<UCurveFloat> ReturnCurve = nullptr;

	/** Sinusoidal hold pulse amplitude (0 = use PatternConfig default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Motion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HoldPulseAmplitude = 0.0f;

	/** Hold breathe frequency in Hz (0 = use PatternConfig default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Pattern|Motion", meta = (ClampMin = "0.0"))
	float HoldPulseFrequencyHz = 0.0f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
