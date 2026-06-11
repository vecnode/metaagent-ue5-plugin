// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Systems/ParticleRuntime/MetaAgentParticleActuation.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeTypes.h"
#include "UObject/Object.h"
#include "MetaAgentParticleRuntime.generated.h"

class UNiagaraComponent;
class UMetaAgentParticlePatternAsset;
class UCurveFloat;

USTRUCT(BlueprintType)
struct FMetaAgentTrackedNiagaraComponent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FString ComponentName;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FVector ComponentLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FVector BoundsOrigin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FVector BoundsExtent = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FMetaAgentParticleSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	int32 FrameCounter = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	float WorldTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	int32 ExportedParticleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	int32 CallbackEventCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FName LastExportSourceActor = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FName LastExportSourceComponent = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	TArray<FMetaAgentTrackedNiagaraComponent> TrackedComponents;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	TArray<FVector> ExportedParticlePositions;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	TArray<FMetaAgentTrackedParticleBlock> ParticleBlocks;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Steering")
	bool bSteeringTargetEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Steering")
	FVector SteeringTargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Steering")
	float SteeringStrength = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles|Steering")
	TArray<FVector> SuggestedSteeringDirections;
};

UCLASS(BlueprintType)
class METAAGENTPLUGIN_API UMetaAgentParticleRuntime : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "MetaAgent|Particles|Pattern")
	FOnMetaAgentPatternStateChanged OnPatternStateEntered;

	UPROPERTY(BlueprintAssignable, Category = "MetaAgent|Particles|Pattern")
	FOnMetaAgentPatternCompleted OnPatternCompleted;

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void InitializeRuntime(UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void TickRuntime(float DeltaTimeSeconds);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void DiscoverNiagaraComponents(bool bLogSummary = false);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void ForceCaptureParticles();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void SubmitExportedParticlePositions(
		const TArray<FVector>& ParticlePositions,
		FName SourceActorName = NAME_None,
		FName SourceComponentName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void SubmitAggregatedParticleCapture(
		const TArray<FVector>& ParticlePositions,
		const TArray<FMetaAgentTrackedParticleBlock>& ParticleBlocks,
		FName SourceActorName = NAME_None,
		FName SourceComponentName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles")
	void ClearExportedParticlePositions();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Steering")
	void SetSteeringTarget(const FVector& TargetLocation, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Steering")
	void ClearSteeringTarget();

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	const FMetaAgentParticleSnapshot& GetLatestSnapshot() const { return LatestSnapshot; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	TArray<FVector> GetKnownParticlePositions() const { return LatestSnapshot.ExportedParticlePositions; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	int32 GetKnownParticleCount() const { return LatestSnapshot.ExportedParticleCount; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	FString BuildStatusText() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	bool HasKnownParticleData() const { return LatestSnapshot.ExportedParticleCount > 0; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles")
	bool HasReceivedAnyCallback() const { return LatestSnapshot.CallbackEventCount > 0; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Steering")
	bool HasSteeringTarget() const { return LatestSnapshot.bSteeringTargetEnabled; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Steering")
	TArray<FVector> GetSuggestedSteeringDirections() const { return LatestSnapshot.SuggestedSteeringDirections; }

	TArray<UNiagaraComponent*> GetTrackedNiagaraComponents() const;

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern", meta = (DeprecatedFunction, DeprecationMessage = "Use StartPattern"))
	bool StartSquarePattern();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	bool StartPattern();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	bool RequestPatternStart(UMetaAgentParticlePatternAsset* PatternAsset);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	bool RequestPatternCancel(bool bSkipReturn = false);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	bool RequestSkipHold();

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	bool RequestPatternQueue(UMetaAgentParticlePatternAsset* PatternAsset);

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	bool CanStartPattern() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	bool IsPatternReady(const FString& ImagePath) const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	int32 GetPatternQueueDepth() const { return PendingPatternAssets.Num(); }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	bool IsPatternActive() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	EMetaAgentParticlePatternState GetPatternState() const { return PatternRuntime.State; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	float GetPatternPhase() const { return PatternRuntime.Phase; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	FString GetPatternStateDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	FString BuildPatternStatusText() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	FString BuildPatternTimingsText() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	FString BuildPatternShapeText() const;

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetPatternShapeContext(const FMetaAgentParticleShapeContext& ShapeContext);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void ApplyPatternConfig(const FMetaAgentParticlePatternConfig& Config);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetPatternTimings(
		float FormDurationSeconds,
		float HoldDurationSeconds,
		float ReturnDurationSeconds);

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void ApplyPatternPreset(EMetaAgentParticlePatternPreset Preset);

	bool AdvancePatternStateForward();

	bool RetreatPatternStateBackward();

	void SetManualPatternStateAdvance(bool bEnabled) { bManualPatternStateAdvance = bEnabled; }

	bool IsAwaitingAsyncMask() const { return PatternRuntime.bAwaitingAsyncMask; }

	bool IsManualPatternStateAdvance() const { return bManualPatternStateAdvance; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	FMetaAgentParticlePatternConfig GetPatternConfig() const { return PatternConfig; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	float GetActiveStateDurationSeconds() const;

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	float GetActiveStateTimeRemainingSeconds() const;

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Pattern")
	void SetActuationMode(EMetaAgentParticleActuationMode NewMode) { ActuationMode = NewMode; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Pattern")
	EMetaAgentParticleActuationMode GetActuationMode() const { return ActuationMode; }

private:
	bool PassesNameFilter(const AActor* OwnerActor, const UNiagaraComponent* NiagaraComponent) const;
	void BuildComponentSnapshot();
	void RebuildSuggestedSteeringDirections();
	void CaptureParticlesDirectly();
	void CaptureLiveSimPositions();
	void RefreshTrajectoryBaselineAtHoldStart();
	void BeginReturnFromHold();
	void EnsureNiagaraComponentReadable(UNiagaraComponent* NiagaraComponent);
	void TickPatternRuntime(float DeltaTimeSeconds);
	bool BuildPatternTargets();
	void ApplyPatternActuation();
	float EvaluatePhaseForState(EMetaAgentParticlePatternState State, float NormalizedTimeInState) const;
	float ComputeActuationBlendAlpha() const;
	bool BeginPatternStart();
	bool ApplyPatternAsset(UMetaAgentParticlePatternAsset* PatternAsset);
	void TryStartQueuedPattern();
	void CompletePatternRun();
	void ResetPatternRuntime();
	void EnterPatternState(EMetaAgentParticlePatternState NewState);
	const FMetaAgentParticlePatternConfig& GetTimingConfigForTick() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<UWorld> CachedWorld;

	UPROPERTY(Transient)
	FMetaAgentParticleSnapshot LatestSnapshot;

	TArray<TWeakObjectPtr<UNiagaraComponent>> TrackedNiagaraComponents;

	int32 DiscoveryFrameCounter = 0;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles")
	int32 DiscoveryEveryNFrames = 30;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles")
	bool bFilterToNiagaraNamedActorsOrComponents = true;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles")
	FString NameFilter = TEXT("NIAGARA");

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles")
	bool bEnableDirectParticleCapture = true;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles", meta = (ClampMin = "1"))
	int32 DirectCaptureEveryNFrames = 2;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Pattern", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxPatternQueueSize = 3;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Pattern")
	EMetaAgentParticleActuationMode ActuationMode = EMetaAgentParticleActuationMode::Hybrid;

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Pattern", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FormingSteeringBlendDurationSeconds = 0.2f;

	/** Below this return phase, stop Direct buffer writes and release Niagara sim control. */
	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Pattern", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float ReturnReleaseAuthorityThreshold = 0.08f;

	/** When true, pattern states animate but only advance on manual step input (>> / <<). */
	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Pattern")
	bool bManualPatternStateAdvance = true;

	TArray<FVector> LiveSimWorldPositions;
	TArray<FVector> LastAppliedWorldPositions;

	float ActiveHoldPulseAmplitude = 0.0f;
	float FormingSteeringBlendElapsedSeconds = 0.0f;
	float LastPatternTickDeltaSeconds = 0.0f;
	bool bLoggedPatternStart = false;

	int32 DirectCaptureFrameCounter = 0;
	bool bLoggedDirectCaptureSuccess = false;
	bool bLoggedDirectCaptureMiss = false;

	TSet<TWeakObjectPtr<UNiagaraComponent>> ReadableNiagaraComponents;

	UPROPERTY(Transient)
	FMetaAgentParticlePatternRuntime PatternRuntime;

	UPROPERTY(Transient)
	FMetaAgentParticlePatternConfig PatternConfig;

	UPROPERTY(Transient)
	FMetaAgentParticleShapeContext PatternShapeContext;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMetaAgentParticlePatternAsset>> PendingPatternAssets;

	UPROPERTY(Transient)
	TObjectPtr<UCurveFloat> ActiveFormCurve = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCurveFloat> ActiveReturnCurve = nullptr;
};
