// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MetaAgentHUD.h"
#include "MetaAgentParticleTypes.h"
#include "NiagaraDataInterfaceExport.h"
#include "UObject/Object.h"
#include "MetaAgentParticleControl.generated.h"

class UNiagaraComponent;
class UMetaAgentParticleRuntime;
class UMetaAgentNiagaraSystemProfile;
class AMetaAgentPlayerController;
class UNiagaraSystem;

/** Pluggable actuation backend (Direct buffer write or Niagara user parameters). */
class METAAGENTPLUGIN_API IMetaAgentParticleActuator
{
public:
	virtual ~IMetaAgentParticleActuator() = default;

	virtual EMetaAgentParticleActuationMode GetActuationMode() const = 0;

	virtual bool SupportsComponent(const UNiagaraComponent& NiagaraComponent) const;

	virtual int32 ApplyPhase(
		const struct FMetaAgentParticleActuationRequest& Request,
		TArray<FVector>& OutAppliedWorldPositions);

	virtual void ApplyParameters(const struct FMetaAgentParticleActuationRequest& Request);

	virtual void Reset();
};

USTRUCT(BlueprintType)
struct FMetaAgentTrackedParticleBlock
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FName SourceActorName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	FName SourceComponentName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	int32 GlobalStartIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaAgent|Particles")
	int32 ParticleCount = 0;
};

struct FMetaAgentParticleActuationRequest
{
	const TArray<FVector>* BaselineWorldPositions = nullptr;
	const TArray<FVector>* PatternWorldTargets = nullptr;
	const TArray<FMetaAgentTrackedParticleBlock>* ParticleBlocks = nullptr;
	TArray<TWeakObjectPtr<UNiagaraComponent>> TrackedComponents;
	float BlendAlpha = 0.0f;
	float HoldPulseScale = 1.0f;
	FVector PatternCenter = FVector::ZeroVector;
	bool bPatternActive = false;
	bool bUseReturnHoldBlend = false;
	const TArray<FVector>* ReturnHoldPositions = nullptr;
	const TArray<FVector>* ReturnRestPositions = nullptr;
	const TArray<FVector>* DissipateStartPositions = nullptr;
	const TArray<FVector>* FormingSteeringOffsets = nullptr;
	float FormingSteeringWeight = 0.0f;
	EMetaAgentParticlePatternState PatternState = EMetaAgentParticlePatternState::Idle;
	const FMetaAgentParticleFormingSettings* FormingSettings = nullptr;
	float FormingStateElapsedSeconds = 0.0f;
	float FormingDurationSeconds = 1.0f;
	float FormingDeltaTimeSeconds = 0.0f;
	bool bAnticipatingMotion = false;
	float AnticipationElapsedSeconds = 0.0f;
	float AnticipationAmplitudeCm = 12.0f;
	float AnticipationFrequencyHz = 1.2f;
	float AnticipationIdleBlendDurationSeconds = 0.35f;
	const TArray<FVector>* IdleBaselineWorldPositions = nullptr;
	float AnticipationHandoffElapsedSeconds = -1.0f;
	float FormingAnticipationCarryoverDurationSeconds = 0.35f;
	bool bDissipatingMotion = false;
	float DissipateVisibility = 1.0f;
	const TArray<FVector>* StateEffectOffsets = nullptr;
};

class METAAGENTPLUGIN_API FMetaAgentParticleActuation
{
public:
	static FVector ComputeAnticipationWorldPosition(
		const FVector& IdleBaseline,
		const int32 GlobalIndex,
		const FVector& PatternCenter,
		const float AnticipationElapsedSeconds,
		const float AnticipationAmplitudeCm,
		const float AnticipationFrequencyHz,
		const float AnticipationIdleBlendDurationSeconds = 0.35f);

	static void BuildAnticipationWorldPositions(
		const TArray<FVector>& IdleBaselineWorldPositions,
		const FVector& PatternCenter,
		const float AnticipationElapsedSeconds,
		const float AnticipationAmplitudeCm,
		const float AnticipationFrequencyHz,
		TArray<FVector>& OutWorldPositions,
		const float AnticipationIdleBlendDurationSeconds = 0.35f);

	static IMetaAgentParticleActuator& GetActuator(EMetaAgentParticleActuationMode Mode);

	static int32 ApplyDirect(
		const FMetaAgentParticleActuationRequest& Request,
		TArray<FVector>& OutAppliedWorldPositions);

	/** Composes logical world positions from actuation inputs without touching Niagara buffers. */
	static int32 ComposeWorldPositionsFromRequest(
		const FMetaAgentParticleActuationRequest& Request,
		TArray<FVector>& OutAppliedWorldPositions);

	static void ApplyParameters(const FMetaAgentParticleActuationRequest& Request);

	static EMetaAgentParticleActuationMode ResolveEffectiveMode(
		EMetaAgentParticleActuationMode ConfiguredMode);

	static FVector SolveFormingPosition(FMetaAgentParticleFormingContext& Context);
};

UCLASS()
class METAAGENTPLUGIN_API UMetaAgentNiagaraExportHandler : public UObject, public INiagaraParticleCallbackHandler
{
	GENERATED_BODY()

public:
	void Initialize(AMetaAgentPlayerController* InOwnerController);

	virtual void ReceiveParticleData_Implementation(
		const TArray<FBasicParticleData>& Data,
		UNiagaraSystem* NiagaraSystem,
		const FVector& SimulationPositionOffset) override;

private:
	TWeakObjectPtr<AMetaAgentPlayerController> OwnerController;
};

UCLASS(BlueprintType)
class METAAGENTPLUGIN_API UMetaAgentNiagaraTargetData : public UObject
{
	GENERATED_BODY()

public:
	void SetTargets(const TArray<FVector>& InPatternTargets, const TArray<FVector>& InBaselines)
	{
		PatternTargets = InPatternTargets;
		Baselines = InBaselines;
	}

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Niagara")
	const TArray<FVector>& GetPatternTargets() const { return PatternTargets; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Niagara")
	const TArray<FVector>& GetBaselines() const { return Baselines; }

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Niagara")
	int32 GetTargetCount() const { return PatternTargets.Num(); }

private:
	UPROPERTY(Transient)
	TArray<FVector> PatternTargets;

	UPROPERTY(Transient)
	TArray<FVector> Baselines;
};

UENUM(BlueprintType)
enum class EMetaAgentNiagaraDriverCapability : uint8
{
	None = 0,
	ParameterPhase = 1 << 0,
	DirectPositionWrite = 1 << 1,
	TargetArrayUpload = 1 << 2,
	DissipateVisibility = 1 << 3
};
ENUM_CLASS_FLAGS(EMetaAgentNiagaraDriverCapability)

UCLASS(BlueprintType)
class METAAGENTPLUGIN_API UMetaAgentNiagaraSystemProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara")
	EMetaAgentParticleActuationMode PreferredActuationMode = EMetaAgentParticleActuationMode::Hybrid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara", meta = (Bitmask, BitmaskEnum = "/Script/MetaAgentPlugin.EMetaAgentNiagaraDriverCapability"))
	int32 Capabilities = static_cast<int32>(
		EMetaAgentNiagaraDriverCapability::ParameterPhase
		| EMetaAgentNiagaraDriverCapability::DissipateVisibility);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara")
	TArray<FName> RequiredUserParameters;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara")
	FName TargetDataParameterName = TEXT("MetaAgentPatternTargetData");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MetaAgent|Particles|Niagara")
	FName TargetCountParameterName = TEXT("MetaAgentPatternTargetCount");

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Niagara")
	bool HasCapability(EMetaAgentNiagaraDriverCapability Capability) const;

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Niagara")
	bool ValidateComponent(UNiagaraComponent* NiagaraComponent, FString& OutMissingParameter) const;

	static bool ComponentExposesUserParameter(const UNiagaraComponent& NiagaraComponent, FName ParameterName);

	static const UMetaAgentNiagaraSystemProfile* GetDefaultProfile();
};

class APlayerController;
class UInputComponent;
class UMetaAgentParticlePatternAsset;
class UMetaAgentParticleRuntime;
class UStaticMeshComponent;
class UTexture2D;
class UWorld;
class AMetaAgentPlayerController;

class METAAGENTPLUGIN_API FMetaAgentParticleInputRouter
{
public:
	static void BindKeyboardInput(
		AMetaAgentPlayerController* Controller,
		UInputComponent* InputComponent,
		class UMetaAgentParticleOrchestrator* Orchestrator);

	static TArray<FString> GetParticleKeyHelpLines();
	static TArray<FMetaAgentGUIActionRow> GetParticleGUIActionRows();
};

class METAAGENTPLUGIN_API IMetaAgentParticleRepresentationDriver
{
public:
	virtual ~IMetaAgentParticleRepresentationDriver() = default;

	virtual FName GetDriverId() const = 0;
	virtual EMetaAgentParticleActuationMode GetActuationMode() const = 0;

	virtual bool SupportsComponent(
		const UNiagaraComponent& NiagaraComponent,
		const UMetaAgentNiagaraSystemProfile* Profile) const = 0;

	virtual int32 ApplyFrame(
		const FMetaAgentParticleRepresentationFrame& Frame,
		FMetaAgentParticleActuationRequest& Request,
		const UMetaAgentNiagaraSystemProfile* Profile,
		TArray<FVector>& OutAppliedWorldPositions) const = 0;
};

class METAAGENTPLUGIN_API FMetaAgentParticleRepresentationDriverRegistry
{
public:
	static void RegisterDefaults();

	static void RegisterDriver(TUniquePtr<IMetaAgentParticleRepresentationDriver> Driver);

	static const IMetaAgentParticleRepresentationDriver& ResolveDriver(
		EMetaAgentParticleActuationMode EffectiveMode);

	static int32 ApplyRepresentationFrame(
		const FMetaAgentParticleRepresentationFrame& Frame,
		FMetaAgentParticleActuationRequest& Request,
		const UMetaAgentNiagaraSystemProfile* Profile,
		EMetaAgentParticleActuationMode ConfiguredMode,
		float ReturnReleaseAuthorityThreshold,
		UMetaAgentNiagaraTargetData* SharedTargetData,
		TArray<FVector>& OutAppliedWorldPositions);

	static void BuildActuationRequestFromFrame(
		const FMetaAgentParticleRepresentationFrame& Frame,
		FMetaAgentParticleActuationRequest& OutRequest);

private:
	static TArray<TUniquePtr<IMetaAgentParticleRepresentationDriver>>& GetDrivers();
};

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

	FMetaAgentParticleEffectResult CycleReturningMode();
	FMetaAgentParticleEffectResult CyclePatternPreset();
	FMetaAgentParticleEffectResult CycleOverlayEffects();
	FMetaAgentParticleEffectResult StepPatternStateForward();
	FMetaAgentParticleEffectResult StepPatternStateBackward();
	FMetaAgentParticleEffectResult PlayFullImageRevealCycle();
	FMetaAgentParticleEffectResult DissipateToCenterEffect();
	FMetaAgentParticleEffectResult MorphPatternEffect();

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

	UFUNCTION(BlueprintCallable, Category = "MetaAgent|Particles|Orchestrator|Niagara")
	void SetNiagaraSystemProfile(UMetaAgentNiagaraSystemProfile* Profile);

	UFUNCTION(BlueprintPure, Category = "MetaAgent|Particles|Orchestrator|Niagara")
	UMetaAgentNiagaraSystemProfile* GetNiagaraSystemProfile() const { return NiagaraSystemProfile; }

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
	bool PrepareShapeContextForPlay(bool bRequestMaskBuild = true);

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

	UPROPERTY(EditAnywhere, Category = "MetaAgent|Particles|Orchestrator|Niagara")
	TObjectPtr<UMetaAgentNiagaraSystemProfile> NiagaraSystemProfile = nullptr;

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

	int32 OverlayCycleIndex = -1;
};

UCLASS(Blueprintable, BlueprintType)
class METAAGENTPLUGIN_API UMetaAgentDefaultParticleOrchestrator : public UMetaAgentParticleOrchestrator
{
	GENERATED_BODY()
};
