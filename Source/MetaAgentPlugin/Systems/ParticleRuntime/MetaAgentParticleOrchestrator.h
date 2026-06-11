// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Systems/GUIRuntime/MetaAgentRuntimePanelTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticleEffectTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"
#include "UObject/Object.h"
#include "MetaAgentParticleOrchestrator.generated.h"

class APlayerController;
class UMetaAgentParticlePatternAsset;
class UMetaAgentParticleRuntime;
class UStaticMeshComponent;
class UTexture2D;
class UWorld;

/**
 * Reusable particle effect orchestrator: owns capture runtime, pattern config, preview state,
 * and routes TriggerEffect requests through the shared Form/Hold/Return FSM.
 * Subclass to register custom effect ids via PopulateEffectSpec.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class METAAGENTPLUGIN_API UMetaAgentParticleOrchestrator : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator")
	virtual void InitializeOrchestrator(APlayerController* InHostController, UWorld* InWorld);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator")
	virtual void TickOrchestrator(float DeltaTimeSeconds);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Effects")
	virtual FMetaAgentParticleEffectResult TriggerEffect(FName EffectId);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Effects")
	FMetaAgentParticleEffectResult ReplayLastEffect();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Effects")
	FMetaAgentParticleEffectResult CycleImageSamplingMode();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Effects")
	FMetaAgentParticleEffectResult CycleFormingMode();

	FMetaAgentParticleEffectResult StepPatternStateForward();

	FMetaAgentParticleEffectResult StepPatternStateBackward();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Preview")
	bool LoadDefaultPreviewPng(FString& OutUserMessage);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator")
	UMetaAgentParticleRuntime* GetParticleRuntime() const { return ParticleRuntime; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Orchestrator")
	FMetaAgentParticlePatternConfig GetPatternConfig() const { return PatternConfig; }

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator")
	void ApplyPatternConfig(const FMetaAgentParticlePatternConfig& Config);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator")
	void SetActuationMode(EMetaAgentParticleActuationMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator")
	void SetBlockedPatternTags(const FGameplayTagContainer& Tags) { BlockedPatternTags = Tags; }

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator")
	void SetDefaultPatternAsset(UMetaAgentParticlePatternAsset* Asset) { DefaultPatternAsset = Asset; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Orchestrator")
	FName GetLastTriggeredEffectId() const { return LastTriggeredEffectId; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Orchestrator|Preview")
	UTexture2D* GetPreviewTexture() const { return LatestPngPreviewTexture; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Orchestrator|Preview")
	FString GetPreviewImagePath() const { return LastLoadedPreviewImagePath; }

	const TArray<FMetaAgentGUIPreviewThumbnail>& GetPanelPreviewThumbnails() const { return PanelPreviewThumbnails; }

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator")
	void SubmitExportedParticlePositions(
		const TArray<FVector>& ParticlePositions,
		FName SourceActorName = NAME_None,
		FName SourceComponentName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator")
	void DiscoverNiagaraComponents(bool bLogSummary = false);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Steering")
	void SetSteeringTarget(FVector TargetLocation, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Steering")
	void ClearSteeringTarget();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	bool RequestPatternCancel(bool bSkipReturn = false);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	bool RequestSkipHold();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	bool RequestPatternQueue(UMetaAgentParticlePatternAsset* PatternAsset);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	FMetaAgentParticleEffectResult StartPatternWithAsset(UMetaAgentParticlePatternAsset* PatternAsset);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator")
	bool PrepareShapeContextForPlay();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Preview")
	void SetPreviewSource(UTexture2D* Texture, const FString& ImagePath);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Preview")
	void SetCachedPreviewPlaneMesh(UStaticMeshComponent* Mesh);

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Orchestrator|Preview")
	UStaticMeshComponent* GetCachedPreviewPlaneMesh() const { return CachedPreviewPlaneMesh.Get(); }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	FString BuildPatternStatusText() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	FString BuildPatternTimingsText() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	FString BuildPatternShapeText() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	FString BuildRuntimeStatusText() const;

protected:
	virtual bool PopulateEffectSpec(FName EffectId, FMetaAgentParticleEffectSpec& OutSpec) const;
	virtual FMetaAgentParticleShapeContext BuildShapeContext() const;
	virtual bool ArePatternTagsBlocked(const FGameplayTagContainer& PatternTags) const;
	virtual FMetaAgentParticleEffectResult ApplyEffectSpec(const FMetaAgentParticleEffectSpec& Spec, FName EffectId);
	virtual void SyncConfigToRuntime();
	virtual void RequestImageMaskBuild();
	void RefreshPanelPreviewThumbnails();

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Orchestrator|Preview")
	FName PreviewPlaneActorName = TEXT("Plane");

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Orchestrator|Preview")
	FName PreviewPlaneComponentName = TEXT("Plane");

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Orchestrator|Preview", meta = (ClampMin = "0.1"))
	float PreviewPlaneBrightness = 3.5f;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	FMetaAgentParticlePatternConfig PatternConfig;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	FGameplayTagContainer BlockedPatternTags;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	EMetaAgentParticleActuationMode ActuationMode = EMetaAgentParticleActuationMode::Hybrid;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Orchestrator|Pattern")
	TObjectPtr<UMetaAgentParticlePatternAsset> DefaultPatternAsset = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMetaAgentParticleRuntime> ParticleRuntime = nullptr;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> HostController;

	UPROPERTY(Transient)
	TWeakObjectPtr<UWorld> CachedWorld;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LatestPngPreviewTexture = nullptr;

	UPROPERTY(Transient)
	FString LastLoadedPreviewImagePath;

	UPROPERTY(Transient)
	TArray<FMetaAgentGUIPreviewThumbnail> PanelPreviewThumbnails;

	UPROPERTY(Transient)
	TWeakObjectPtr<UStaticMeshComponent> CachedPreviewPlaneMesh;

	UPROPERTY(Transient)
	FName LastTriggeredEffectId = NAME_None;

	UPROPERTY(Transient)
	FMetaAgentParticleEffectSpec LastEffectSpec;

	bool bHasLastEffectSpec = false;
};

/** Default orchestrator used by MetaAgent player controller. */
UCLASS(Blueprintable, BlueprintType)
class METAAGENTPLUGIN_API UMetaAgentDefaultParticleOrchestrator : public UMetaAgentParticleOrchestrator
{
	GENERATED_BODY()
};
