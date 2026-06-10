// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleRuntime.h"

#include "Curves/CurveFloat.h"
#include "NiagaraComponent.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraTypes.h"
#include "Core/MetaAgent.h"
#include "Systems/ParticleRuntime/MetaAgentParticleActuation.h"
#include "Systems/ParticleRuntime/MetaAgentParticleGameplayTags.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternAsset.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeBuilder.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeCache.h"
#include "Systems/ParticleRuntime/MetaAgentParticleImageMaskProcessor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/LargeWorldRenderPosition.h"

namespace
{
	float SmoothStep01(const float Alpha)
	{
		const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		return ClampedAlpha * ClampedAlpha * (3.0f - 2.0f * ClampedAlpha);
	}

	FString PatternStateToString(const EMetaAgentParticlePatternState State)
	{
		switch (State)
		{
		case EMetaAgentParticlePatternState::Preparing: return TEXT("Preparing");
		case EMetaAgentParticlePatternState::Forming: return TEXT("Forming");
		case EMetaAgentParticlePatternState::Holding: return TEXT("Holding");
		case EMetaAgentParticlePatternState::Returning: return TEXT("Returning");
		case EMetaAgentParticlePatternState::Releasing: return TEXT("Releasing");
		case EMetaAgentParticlePatternState::Idle:
		default:
			return TEXT("Idle");
		}
	}
}

namespace MetaAgentParticleCapture
{
	static const FName PositionAttributeName(TEXT("Position"));

	FVector NiagaraPositionToWorld(
		const FNiagaraPosition& SimPosition,
		const FNiagaraEmitterInstance& EmitterInstance,
		const UNiagaraComponent& NiagaraComponent,
		const FNiagaraSystemInstance& SystemInstance)
	{
		FVector Position = FVector(SimPosition);
		const FVector LWCOffset = FVector(SystemInstance.GetLWCTile()) * FLargeWorldRenderScalar::GetTileSize();
		Position += LWCOffset;

		if (EmitterInstance.IsLocalSpace())
		{
			Position = NiagaraComponent.GetComponentTransform().TransformPosition(Position);
		}

		return Position;
	}

	void ExtractPositionsFromDataSet(
		const FNiagaraDataSet& DataSet,
		const FNiagaraEmitterInstance& EmitterInstance,
		const UNiagaraComponent& NiagaraComponent,
		const FNiagaraSystemInstance& SystemInstance,
		TArray<FVector>& OutPositions)
	{
		const FNiagaraDataBuffer* ParticleDataBuffer = DataSet.GetCurrentData();
		if (!ParticleDataBuffer || ParticleDataBuffer->GetNumInstances() <= 0)
		{
			return;
		}

		const FNiagaraDataSetReaderFloat<FNiagaraPosition> PositionReader =
			FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(DataSet, PositionAttributeName);
		if (!PositionReader.IsValid())
		{
			return;
		}

		const int32 NumInstances = ParticleDataBuffer->GetNumInstances();
		OutPositions.Reserve(OutPositions.Num() + NumInstances);

		for (int32 ParticleIndex = 0; ParticleIndex < NumInstances; ++ParticleIndex)
		{
			OutPositions.Add(NiagaraPositionToWorld(PositionReader[ParticleIndex], EmitterInstance, NiagaraComponent, SystemInstance));
		}
	}

	void CaptureEmitterPositions(
		FNiagaraEmitterInstance& EmitterInstance,
		const UNiagaraComponent& NiagaraComponent,
		const FNiagaraSystemInstance& SystemInstance,
		TArray<FVector>& OutPositions)
	{
		if (FNiagaraComputeExecutionContext* GPUContext = EmitterInstance.GetGPUContext())
		{
			if (FNiagaraDataSet* GPUDataSet = GPUContext->MainDataSet)
			{
				FScopedNiagaraDataSetGPUReadback ScopedGPUReadback;
				ScopedGPUReadback.ReadbackData(SystemInstance.GetComputeDispatchInterface(), GPUDataSet);
				if (ScopedGPUReadback.GetNumInstances() > 0)
				{
					ExtractPositionsFromDataSet(*GPUDataSet, EmitterInstance, NiagaraComponent, SystemInstance, OutPositions);
				}
			}

			return;
		}

		ExtractPositionsFromDataSet(EmitterInstance.GetParticleData(), EmitterInstance, NiagaraComponent, SystemInstance, OutPositions);
	}
}

void UMetaAgentParticleRuntime::InitializeRuntime(UObject* WorldContextObject)
{
	CachedWorld = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	DiscoveryFrameCounter = 0;
	LatestSnapshot = FMetaAgentParticleSnapshot();
	TrackedNiagaraComponents.Reset();
	ResetPatternRuntime();

	DiscoverNiagaraComponents(true);
}

void UMetaAgentParticleRuntime::TickRuntime(const float DeltaTimeSeconds)
{
	if (!CachedWorld.IsValid())
	{
		return;
	}

	LatestSnapshot.WorldTimeSeconds = CachedWorld->GetTimeSeconds();
	LatestSnapshot.FrameCounter++;

	FMetaAgentParticleShapeCache::Tick();

	DiscoveryFrameCounter++;
	const int32 DiscoveryInterval = FMath::Max(1, DiscoveryEveryNFrames);
	if (DiscoveryFrameCounter >= DiscoveryInterval)
	{
		DiscoveryFrameCounter = 0;
		DiscoverNiagaraComponents(false);
	}

	BuildComponentSnapshot();

	TickPatternRuntime(DeltaTimeSeconds);

	if (PatternRuntime.State != EMetaAgentParticlePatternState::Idle
		&& PatternRuntime.State != EMetaAgentParticlePatternState::Preparing)
	{
		ApplyPatternActuation();
	}
	else if (bEnableDirectParticleCapture)
	{
		CaptureParticlesDirectly();
	}

	RebuildSuggestedSteeringDirections();

	if (DeltaTimeSeconds < 0.0f)
	{
		UE_LOG(LogMetaAgent, Verbose, TEXT("ParticleRuntime received negative delta, which should not happen in runtime tick."));
	}
}

void UMetaAgentParticleRuntime::ForceCaptureParticles()
{
	DirectCaptureFrameCounter = FMath::Max(0, DirectCaptureEveryNFrames - 1);
	CaptureParticlesDirectly();
}

void UMetaAgentParticleRuntime::CaptureLiveSimPositions()
{
	DirectCaptureFrameCounter = FMath::Max(0, DirectCaptureEveryNFrames - 1);
	CaptureParticlesDirectly();
	LiveSimWorldPositions = LatestSnapshot.ExportedParticlePositions;
}

void UMetaAgentParticleRuntime::RefreshTrajectoryBaselineAtHoldStart()
{
	if (LastAppliedWorldPositions.Num() > 0)
	{
		PatternRuntime.TrajectoryWorldPositions = LastAppliedWorldPositions;
	}
	else
	{
		CaptureLiveSimPositions();
		PatternRuntime.TrajectoryWorldPositions = LiveSimWorldPositions;
	}

	UE_LOG(LogMetaAgent, Verbose,
		TEXT("ParticleRuntime: trajectory baseline captured at Holding start (%d particles)."),
		PatternRuntime.TrajectoryWorldPositions.Num());
}

void UMetaAgentParticleRuntime::BeginReturnFromHold()
{
	PatternRuntime.ReturnHoldPositions = LastAppliedWorldPositions;
	if (PatternRuntime.ReturnHoldPositions.Num() <= 0)
	{
		PatternRuntime.ReturnHoldPositions = PatternRuntime.PatternWorldTargets;
	}

	// ReturnRestPositions is refreshed from live Niagara sim each Returning tick.
	PatternRuntime.ReturnRestPositions.Reset();

	UE_LOG(LogMetaAgent, Verbose,
		TEXT("ParticleRuntime: return started hold=%d (live rest refreshed each tick)."),
		PatternRuntime.ReturnHoldPositions.Num());
}

void UMetaAgentParticleRuntime::DiscoverNiagaraComponents(const bool bLogSummary)
{
	if (!CachedWorld.IsValid())
	{
		return;
	}

	TArray<TWeakObjectPtr<UNiagaraComponent>> DiscoveredComponents;

	for (TActorIterator<AActor> ActorIt(CachedWorld.Get()); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!Actor)
		{
			continue;
		}

		TArray<UNiagaraComponent*> NiagaraComponents;
		Actor->GetComponents<UNiagaraComponent>(NiagaraComponents);

		for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
		{
			if (!NiagaraComponent || !PassesNameFilter(Actor, NiagaraComponent))
			{
				continue;
			}

			DiscoveredComponents.Add(NiagaraComponent);
		}
	}

	TrackedNiagaraComponents = MoveTemp(DiscoveredComponents);
	BuildComponentSnapshot();

	if (bLogSummary)
	{
		UE_LOG(LogMetaAgent, Log,
			TEXT("ParticleRuntime: discovered %d Niagara component(s). Filter=%s NameFilter='%s'"),
			TrackedNiagaraComponents.Num(),
			bFilterToNiagaraNamedActorsOrComponents ? TEXT("On") : TEXT("Off"),
			*NameFilter);
	}
}

void UMetaAgentParticleRuntime::SubmitExportedParticlePositions(
	const TArray<FVector>& ParticlePositions,
	const FName SourceActorName,
	const FName SourceComponentName)
{
	LatestSnapshot.ExportedParticlePositions = ParticlePositions;
	LatestSnapshot.ExportedParticleCount = ParticlePositions.Num();
	LatestSnapshot.CallbackEventCount++;
	LatestSnapshot.LastExportSourceActor = SourceActorName;
	LatestSnapshot.LastExportSourceComponent = SourceComponentName;
	LatestSnapshot.ParticleBlocks.Reset();

	if (ParticlePositions.Num() > 0)
	{
		FMetaAgentTrackedParticleBlock Block;
		Block.SourceActorName = SourceActorName;
		Block.SourceComponentName = SourceComponentName;
		Block.GlobalStartIndex = 0;
		Block.ParticleCount = ParticlePositions.Num();
		LatestSnapshot.ParticleBlocks.Add(Block);
	}

	if (CachedWorld.IsValid())
	{
		LatestSnapshot.WorldTimeSeconds = CachedWorld->GetTimeSeconds();
	}

	RebuildSuggestedSteeringDirections();
}

void UMetaAgentParticleRuntime::SubmitAggregatedParticleCapture(
	const TArray<FVector>& ParticlePositions,
	const TArray<FMetaAgentTrackedParticleBlock>& ParticleBlocks,
	const FName SourceActorName,
	const FName SourceComponentName)
{
	LatestSnapshot.ExportedParticlePositions = ParticlePositions;
	LatestSnapshot.ExportedParticleCount = ParticlePositions.Num();
	LatestSnapshot.ParticleBlocks = ParticleBlocks;
	LatestSnapshot.CallbackEventCount++;
	LatestSnapshot.LastExportSourceActor = SourceActorName;
	LatestSnapshot.LastExportSourceComponent = SourceComponentName;

	if (CachedWorld.IsValid())
	{
		LatestSnapshot.WorldTimeSeconds = CachedWorld->GetTimeSeconds();
	}

	RebuildSuggestedSteeringDirections();
}

void UMetaAgentParticleRuntime::ClearExportedParticlePositions()
{
	LatestSnapshot.ExportedParticlePositions.Reset();
	LatestSnapshot.ExportedParticleCount = 0;
	LatestSnapshot.LastExportSourceActor = NAME_None;
	LatestSnapshot.LastExportSourceComponent = NAME_None;
	LatestSnapshot.SuggestedSteeringDirections.Reset();
}

void UMetaAgentParticleRuntime::SetSteeringTarget(const FVector& TargetLocation, const float Strength)
{
	LatestSnapshot.bSteeringTargetEnabled = true;
	LatestSnapshot.SteeringTargetLocation = TargetLocation;
	LatestSnapshot.SteeringStrength = FMath::Max(0.0f, Strength);
	RebuildSuggestedSteeringDirections();
}

void UMetaAgentParticleRuntime::ClearSteeringTarget()
{
	LatestSnapshot.bSteeringTargetEnabled = false;
	LatestSnapshot.SteeringTargetLocation = FVector::ZeroVector;
	LatestSnapshot.SteeringStrength = 1.0f;
	LatestSnapshot.SuggestedSteeringDirections.Reset();
}

FString UMetaAgentParticleRuntime::BuildStatusText() const
{
	return FString::Printf(
		TEXT("ParticleRuntime: NiagaraComponents=%d ExportedParticles=%d CallbackEvents=%d Steering=%s SourceActor=%s SourceComp=%s"),
		TrackedNiagaraComponents.Num(),
		LatestSnapshot.ExportedParticleCount,
		LatestSnapshot.CallbackEventCount,
		LatestSnapshot.bSteeringTargetEnabled ? TEXT("On") : TEXT("Off"),
		*LatestSnapshot.LastExportSourceActor.ToString(),
		*LatestSnapshot.LastExportSourceComponent.ToString());
}

bool UMetaAgentParticleRuntime::PassesNameFilter(const AActor* OwnerActor, const UNiagaraComponent* NiagaraComponent) const
{
	if (!OwnerActor || !NiagaraComponent)
	{
		return false;
	}

	if (!bFilterToNiagaraNamedActorsOrComponents)
	{
		return true;
	}

	const FString EffectiveFilter = NameFilter.IsEmpty() ? TEXT("NIAGARA") : NameFilter;
	return OwnerActor->GetName().Contains(EffectiveFilter, ESearchCase::IgnoreCase)
		|| OwnerActor->GetActorNameOrLabel().Contains(EffectiveFilter, ESearchCase::IgnoreCase)
		|| NiagaraComponent->GetName().Contains(EffectiveFilter, ESearchCase::IgnoreCase);
}

void UMetaAgentParticleRuntime::BuildComponentSnapshot()
{
	LatestSnapshot.TrackedComponents.Reset();

	for (int32 Index = TrackedNiagaraComponents.Num() - 1; Index >= 0; --Index)
	{
		UNiagaraComponent* NiagaraComponent = TrackedNiagaraComponents[Index].Get();
		if (!NiagaraComponent)
		{
			TrackedNiagaraComponents.RemoveAtSwap(Index);
			continue;
		}

		AActor* OwnerActor = NiagaraComponent->GetOwner();
		if (!OwnerActor)
		{
			continue;
		}

		const FBoxSphereBounds Bounds = NiagaraComponent->Bounds;

		FMetaAgentTrackedNiagaraComponent Entry;
		Entry.ActorName = OwnerActor->GetActorNameOrLabel();
		Entry.ComponentName = NiagaraComponent->GetName();
		Entry.ComponentLocation = NiagaraComponent->GetComponentLocation();
		Entry.BoundsOrigin = Bounds.Origin;
		Entry.BoundsExtent = Bounds.BoxExtent;

		LatestSnapshot.TrackedComponents.Add(MoveTemp(Entry));
	}
}

TArray<UNiagaraComponent*> UMetaAgentParticleRuntime::GetTrackedNiagaraComponents() const
{
	TArray<UNiagaraComponent*> Result;
	Result.Reserve(TrackedNiagaraComponents.Num());

	for (const TWeakObjectPtr<UNiagaraComponent>& WeakComponent : TrackedNiagaraComponents)
	{
		if (UNiagaraComponent* NiagaraComponent = WeakComponent.Get())
		{
			Result.Add(NiagaraComponent);
		}
	}

	return Result;
}

void UMetaAgentParticleRuntime::EnsureNiagaraComponentReadable(UNiagaraComponent* NiagaraComponent)
{
	if (!NiagaraComponent || ReadableNiagaraComponents.Contains(NiagaraComponent))
	{
		return;
	}

	if (!NiagaraComponent->GetForceSolo())
	{
		NiagaraComponent->SetForceSolo(true);
	}

	ReadableNiagaraComponents.Add(NiagaraComponent);
}

void UMetaAgentParticleRuntime::CaptureParticlesDirectly()
{
	using namespace MetaAgentParticleCapture;

	DirectCaptureFrameCounter++;
	const int32 CaptureInterval = FMath::Max(1, DirectCaptureEveryNFrames);
	if (DirectCaptureFrameCounter < CaptureInterval)
	{
		return;
	}

	DirectCaptureFrameCounter = 0;

	TArray<FVector> AggregatedPositions;
	TArray<FMetaAgentTrackedParticleBlock> AggregatedBlocks;
	FName LastActorName = NAME_None;
	FName LastComponentName = NAME_None;

	for (const TWeakObjectPtr<UNiagaraComponent>& WeakComponent : TrackedNiagaraComponents)
	{
		UNiagaraComponent* NiagaraComponent = WeakComponent.Get();
		if (!NiagaraComponent || !NiagaraComponent->IsActive())
		{
			continue;
		}

		EnsureNiagaraComponentReadable(NiagaraComponent);

		const FNiagaraSystemInstanceControllerConstPtr InstanceController = NiagaraComponent->GetSystemInstanceController();
		if (!InstanceController.IsValid() || !InstanceController->IsValid())
		{
			continue;
		}

		FNiagaraSystemInstance* SystemInstance = InstanceController->GetSoloSystemInstance();
		if (!SystemInstance)
		{
			SystemInstance = InstanceController->GetSystemInstance_Unsafe();
		}

		if (!SystemInstance)
		{
			continue;
		}

		TArray<FVector> CapturedPositions;
		int32 TotalReportedParticles = 0;

		for (const FNiagaraEmitterInstanceRef& EmitterRef : SystemInstance->GetEmitters())
		{
			FNiagaraEmitterInstance& EmitterInstance = EmitterRef.Get();
			if (EmitterInstance.IsDisabled() || EmitterInstance.IsComplete())
			{
				continue;
			}

			TotalReportedParticles += EmitterInstance.GetNumParticles();
			CaptureEmitterPositions(EmitterInstance, *NiagaraComponent, *SystemInstance, CapturedPositions);
		}

		if (CapturedPositions.Num() <= 0)
		{
			if (!bLoggedDirectCaptureMiss && TotalReportedParticles > 0)
			{
				bLoggedDirectCaptureMiss = true;
				UE_LOG(LogMetaAgent, Warning,
					TEXT("ParticleRuntime: direct C++ capture found %d simulated particle(s) on '%s' but could not read Position attribute buffers."),
					TotalReportedParticles,
					*NiagaraComponent->GetName());
			}
			continue;
		}

		AActor* OwnerActor = NiagaraComponent->GetOwner();
		LastActorName = OwnerActor ? OwnerActor->GetFName() : NAME_None;
		LastComponentName = NiagaraComponent->GetFName();

		FMetaAgentTrackedParticleBlock Block;
		Block.SourceActorName = LastActorName;
		Block.SourceComponentName = LastComponentName;
		Block.GlobalStartIndex = AggregatedPositions.Num();
		Block.ParticleCount = CapturedPositions.Num();
		AggregatedBlocks.Add(Block);
		AggregatedPositions.Append(CapturedPositions);

		if (!bLoggedDirectCaptureSuccess)
		{
			bLoggedDirectCaptureSuccess = true;
			UE_LOG(LogMetaAgent, Log,
				TEXT("ParticleRuntime: direct C++ capture read %d position(s) from component '%s' (emitters report %d particle(s))."),
				CapturedPositions.Num(),
				*LastComponentName.ToString(),
				TotalReportedParticles);
		}
	}

	if (AggregatedPositions.Num() > 0)
	{
		SubmitAggregatedParticleCapture(AggregatedPositions, AggregatedBlocks, LastActorName, LastComponentName);
	}
}

void UMetaAgentParticleRuntime::RebuildSuggestedSteeringDirections()
{
	LatestSnapshot.SuggestedSteeringDirections.Reset();

	if (!LatestSnapshot.bSteeringTargetEnabled)
	{
		return;
	}

	LatestSnapshot.SuggestedSteeringDirections.Reserve(LatestSnapshot.ExportedParticlePositions.Num());

	for (const FVector& ParticlePosition : LatestSnapshot.ExportedParticlePositions)
	{
		FVector Direction = LatestSnapshot.SteeringTargetLocation - ParticlePosition;
		if (!Direction.Normalize())
		{
			Direction = FVector::ZeroVector;
		}

		Direction *= LatestSnapshot.SteeringStrength;
		LatestSnapshot.SuggestedSteeringDirections.Add(Direction);
	}
}

bool UMetaAgentParticleRuntime::StartSquarePattern()
{
	return StartPattern();
}

bool UMetaAgentParticleRuntime::StartPattern()
{
	if (!CanStartPattern())
	{
		return false;
	}

	return BeginPatternStart();
}

bool UMetaAgentParticleRuntime::RequestPatternStart(UMetaAgentParticlePatternAsset* PatternAsset)
{
	if (!PatternAsset)
	{
		return StartPattern();
	}

	if (!CanStartPattern())
	{
		return false;
	}

	if (!ApplyPatternAsset(PatternAsset))
	{
		return false;
	}

	return BeginPatternStart();
}

bool UMetaAgentParticleRuntime::RequestPatternCancel(const bool bSkipReturn)
{
	if (PatternRuntime.State == EMetaAgentParticlePatternState::Idle)
	{
		return false;
	}

	if (bSkipReturn
		|| PatternRuntime.State == EMetaAgentParticlePatternState::Preparing)
	{
		CompletePatternRun();
		return true;
	}

	EnterPatternState(EMetaAgentParticlePatternState::Returning);
	PatternRuntime.Phase = 1.0f;
	return true;
}

bool UMetaAgentParticleRuntime::RequestSkipHold()
{
	if (PatternRuntime.State != EMetaAgentParticlePatternState::Holding)
	{
		return false;
	}

	EnterPatternState(EMetaAgentParticlePatternState::Returning);
	PatternRuntime.Phase = 1.0f;
	return true;
}

bool UMetaAgentParticleRuntime::RequestPatternQueue(UMetaAgentParticlePatternAsset* PatternAsset)
{
	if (!PatternAsset || PendingPatternAssets.Num() >= MaxPatternQueueSize)
	{
		return false;
	}

	PendingPatternAssets.Add(PatternAsset);
	return true;
}

bool UMetaAgentParticleRuntime::CanStartPattern() const
{
	return PatternRuntime.State == EMetaAgentParticlePatternState::Idle
		&& LatestSnapshot.ExportedParticlePositions.Num() > 0;
}

bool UMetaAgentParticleRuntime::IsPatternReady(const FString& ImagePath) const
{
	if (ImagePath.IsEmpty())
	{
		return false;
	}

	const FMetaAgentImageMaskBuildParams Params = MetaAgentImageMask::MakeBuildParams(
		ImagePath,
		PatternConfig.Shape,
		FMath::Max(1, LatestSnapshot.ExportedParticleCount));
	return FMetaAgentParticleShapeCache::IsMaskReady(Params);
}

bool UMetaAgentParticleRuntime::ApplyPatternAsset(UMetaAgentParticlePatternAsset* PatternAsset)
{
	if (!PatternAsset)
	{
		return false;
	}

	PatternConfig = PatternAsset->PatternConfig;
	if (PatternAsset->bOverrideShape)
	{
		PatternConfig.Shape = PatternAsset->ShapeOverride;
	}
	ActiveFormCurve = PatternAsset->FormCurve;
	ActiveReturnCurve = PatternAsset->ReturnCurve;
	ActiveHoldPulseAmplitude = PatternAsset->HoldPulseAmplitude;
	PatternRuntime.ActivePatternTags = PatternAsset->PatternTags;

	if (!PatternAsset->SourceImagePathOverride.IsEmpty())
	{
		PatternShapeContext.SourceImagePath = PatternAsset->SourceImagePathOverride;
	}

	if (PatternAsset->SourceImageTexture.ToSoftObjectPath().IsValid())
	{
		if (UTexture2D* LoadedTexture = PatternAsset->SourceImageTexture.LoadSynchronous())
		{
			PatternShapeContext.SourceTexture = LoadedTexture;
			PatternShapeContext.bHasResolvedImage = true;
		}
	}

	return true;
}

bool UMetaAgentParticleRuntime::BeginPatternStart()
{
	PatternRuntime.bAwaitingAsyncMask = false;
	PatternRuntime.ActiveConfig = PatternConfig;
	const FGameplayTagContainer AssetPatternTags = PatternRuntime.ActivePatternTags;
	PatternRuntime.ActivePatternTags.Reset();
	PatternRuntime.ActivePatternTags.AppendTags(AssetPatternTags);
	PatternRuntime.ActivePatternTags.AddTag(MetaAgentParticleTags::Pattern_Active);
	if (PatternConfig.Shape.ShapeType == EMetaAgentParticlePatternShape::ImageSilhouette)
	{
		PatternRuntime.ActivePatternTags.AddTag(MetaAgentParticleTags::Pattern_ImageReveal);
	}
	PatternRuntime.BaselineWorldPositions = LatestSnapshot.ExportedParticlePositions;

	if (PatternShapeContext.BaselineWorldPositions.Num() <= 0)
	{
		PatternShapeContext.BaselineWorldPositions = PatternRuntime.BaselineWorldPositions;
	}

	if (CachedWorld.IsValid())
	{
		PatternShapeContext.World = CachedWorld.Get();
	}

	FormingSteeringBlendElapsedSeconds = 0.0f;

	if (!BuildPatternTargets())
	{
		if (PatternRuntime.bAwaitingAsyncMask)
		{
			EnterPatternState(EMetaAgentParticlePatternState::Preparing);
			PatternRuntime.Phase = 0.0f;
			bLoggedPatternStart = false;
			UE_LOG(LogMetaAgent, Log, TEXT("ParticleRuntime: pattern preparing image mask asynchronously."));
			return true;
		}

		UE_LOG(LogMetaAgent, Warning, TEXT("ParticleRuntime: failed to build pattern targets."));
		PatternRuntime.ActivePatternTags.Reset();
		return false;
	}

	EnterPatternState(EMetaAgentParticlePatternState::Forming);
	PatternRuntime.Phase = 0.0f;
	bLoggedPatternStart = false;

	UE_LOG(LogMetaAgent, Log,
		TEXT("ParticleRuntime: pattern started requested=%s resolved=%s preset=%s particles=%d timings form=%.1fs hold=%.1fs return=%.1fs | %s"),
		*PatternRuntime.ActiveConfig.Shape.GetShapeDisplayName(),
		*PatternRuntime.ActiveConfig.Shape.GetShapeDisplayName(),
		*PatternRuntime.ActiveConfig.GetPresetDisplayName(),
		PatternRuntime.BaselineWorldPositions.Num(),
		PatternRuntime.ActiveConfig.FormDurationSeconds,
		PatternRuntime.ActiveConfig.HoldDurationSeconds,
		PatternRuntime.ActiveConfig.ReturnDurationSeconds,
		*PatternRuntime.ShapeDebugInfo);

	return true;
}

void UMetaAgentParticleRuntime::TryStartQueuedPattern()
{
	if (PatternRuntime.State != EMetaAgentParticlePatternState::Idle || PendingPatternAssets.Num() <= 0)
	{
		return;
	}

	UMetaAgentParticlePatternAsset* NextAsset = PendingPatternAssets[0];
	PendingPatternAssets.RemoveAt(0);
	if (NextAsset)
	{
		RequestPatternStart(NextAsset);
	}
}

void UMetaAgentParticleRuntime::CompletePatternRun()
{
	const EMetaAgentParticlePatternState PreviousState = PatternRuntime.State;
	ResetPatternRuntime();
	if (PreviousState != EMetaAgentParticlePatternState::Idle)
	{
		OnPatternCompleted.Broadcast();
	}
	TryStartQueuedPattern();
}

bool UMetaAgentParticleRuntime::IsPatternActive() const
{
	return PatternRuntime.State != EMetaAgentParticlePatternState::Idle;
}

FString UMetaAgentParticleRuntime::GetPatternStateDisplayName() const
{
	return PatternStateToString(PatternRuntime.State);
}

void UMetaAgentParticleRuntime::SetPatternShapeContext(const FMetaAgentParticleShapeContext& ShapeContext)
{
	PatternShapeContext = ShapeContext;
}

FString UMetaAgentParticleRuntime::BuildPatternShapeText() const
{
	const FMetaAgentParticleShapeDefinition& DisplayShape =
		PatternRuntime.State == EMetaAgentParticlePatternState::Idle
			? PatternConfig.Shape
			: PatternRuntime.ActiveConfig.Shape;

	const EMetaAgentParticlePatternShape DisplayShapeType =
		PatternRuntime.State == EMetaAgentParticlePatternState::Idle
			? DisplayShape.ShapeType
			: PatternRuntime.ActiveShape;

	const FString ShapeName = DisplayShape.GetShapeDisplayName();

	const bool bImageLoaded = PatternShapeContext.bHasResolvedImage || PatternShapeContext.SourceTexture != nullptr;

	if (PatternRuntime.State == EMetaAgentParticlePatternState::Idle)
	{
		return FString::Printf(
			TEXT("Pattern Shape: %s | Sampling=%s | Res=%dpx | ImageLoaded=%s"),
			*ShapeName,
			*DisplayShape.GetImageSamplingDisplayName(),
			DisplayShape.SampleResolution,
			bImageLoaded ? TEXT("TRUE") : TEXT("FALSE"));
	}

	if (PatternRuntime.State == EMetaAgentParticlePatternState::Preparing)
	{
		return FString::Printf(
			TEXT("Pattern Shape: %s | Preparing async mask @ %dpx | %s"),
			*ShapeName,
			DisplayShape.SampleResolution,
			*PatternRuntime.ShapeDebugInfo);
	}

	return FString::Printf(
		TEXT("Pattern Shape: %s | ImageLoaded=%s | %s"),
		*ShapeName,
		bImageLoaded ? TEXT("TRUE") : TEXT("FALSE"),
		*PatternRuntime.ShapeDebugInfo);
}

FString UMetaAgentParticleRuntime::BuildPatternTimingsText() const
{
	const FMetaAgentParticlePatternConfig& DisplayConfig =
		PatternRuntime.State == EMetaAgentParticlePatternState::Idle
			? PatternConfig
			: PatternRuntime.ActiveConfig;

	return FString::Printf(
		TEXT("Pattern Preset: %s | Form=%.1fs Hold=%.1fs Return=%.1fs"),
		*DisplayConfig.GetPresetDisplayName(),
		DisplayConfig.FormDurationSeconds,
		DisplayConfig.HoldDurationSeconds,
		DisplayConfig.ReturnDurationSeconds);
}

FString UMetaAgentParticleRuntime::BuildPatternStatusText() const
{
	const int32 ParticleCount = PatternRuntime.BaselineWorldPositions.Num();
	const FString TagsSuffix = PatternRuntime.ActivePatternTags.IsEmpty()
		? FString()
		: FString::Printf(TEXT(" | Tags: %s"), *PatternRuntime.ActivePatternTags.ToStringSimple());

	if (PatternRuntime.State == EMetaAgentParticlePatternState::Idle)
	{
		return FString::Printf(
			TEXT("Pattern State: Idle | Phase: 0.00 | Queue: %d | Particles: %d%s"),
			PendingPatternAssets.Num(),
			LatestSnapshot.ExportedParticleCount,
			*TagsSuffix);
	}

	const float StateDuration = GetActiveStateDurationSeconds();
	const float TimeRemaining = GetActiveStateTimeRemainingSeconds();

	return FString::Printf(
		TEXT("Pattern State: %s | Phase: %.2f | In-state: %.2fs / %.1fs (%.1fs left) | Particles: %d%s"),
		*GetPatternStateDisplayName(),
		PatternRuntime.Phase,
		PatternRuntime.StateElapsedSeconds,
		StateDuration,
		TimeRemaining,
		ParticleCount,
		*TagsSuffix);
}

void UMetaAgentParticleRuntime::ApplyPatternConfig(const FMetaAgentParticlePatternConfig& Config)
{
	PatternConfig = Config;
}

void UMetaAgentParticleRuntime::SetPatternTimings(
	const float FormDurationSeconds,
	const float HoldDurationSeconds,
	const float ReturnDurationSeconds)
{
	PatternConfig.FormDurationSeconds = FMath::Max(0.1f, FormDurationSeconds);
	PatternConfig.HoldDurationSeconds = FMath::Max(0.0f, HoldDurationSeconds);
	PatternConfig.ReturnDurationSeconds = FMath::Max(0.1f, ReturnDurationSeconds);
	PatternConfig.ActivePreset = EMetaAgentParticlePatternPreset::Custom;
}

void UMetaAgentParticleRuntime::ApplyPatternPreset(const EMetaAgentParticlePatternPreset Preset)
{
	PatternConfig.ApplyPreset(Preset);
}

float UMetaAgentParticleRuntime::GetActiveStateDurationSeconds() const
{
	const FMetaAgentParticlePatternConfig& Timings = GetTimingConfigForTick();

	switch (PatternRuntime.State)
	{
	case EMetaAgentParticlePatternState::Preparing:
		return 60.0f;
	case EMetaAgentParticlePatternState::Forming:
		return FMath::Max(0.1f, Timings.FormDurationSeconds);
	case EMetaAgentParticlePatternState::Holding:
		return FMath::Max(0.0f, Timings.HoldDurationSeconds);
	case EMetaAgentParticlePatternState::Returning:
		return FMath::Max(0.1f, Timings.ReturnDurationSeconds);
	case EMetaAgentParticlePatternState::Idle:
	default:
		return 0.0f;
	}
}

float UMetaAgentParticleRuntime::GetActiveStateTimeRemainingSeconds() const
{
	if (PatternRuntime.State == EMetaAgentParticlePatternState::Idle)
	{
		return 0.0f;
	}

	return FMath::Max(0.0f, GetActiveStateDurationSeconds() - PatternRuntime.StateElapsedSeconds);
}

void UMetaAgentParticleRuntime::ResetPatternRuntime()
{
	PatternRuntime = FMetaAgentParticlePatternRuntime();
	ActiveFormCurve = nullptr;
	ActiveReturnCurve = nullptr;
	ActiveHoldPulseAmplitude = 0.0f;
	FormingSteeringBlendElapsedSeconds = 0.0f;
	bLoggedPatternStart = false;
	LastAppliedWorldPositions.Reset();
	LiveSimWorldPositions.Reset();
}

void UMetaAgentParticleRuntime::EnterPatternState(const EMetaAgentParticlePatternState NewState)
{
	const EMetaAgentParticlePatternState PreviousState = PatternRuntime.State;
	PatternRuntime.State = NewState;
	PatternRuntime.StateElapsedSeconds = 0.0f;

	if (NewState == EMetaAgentParticlePatternState::Holding)
	{
		RefreshTrajectoryBaselineAtHoldStart();
	}
	else if (NewState == EMetaAgentParticlePatternState::Returning)
	{
		PatternRuntime.Phase = 1.0f;
		BeginReturnFromHold();
	}

	OnPatternStateEntered.Broadcast(NewState, PreviousState);
}

bool UMetaAgentParticleRuntime::BuildPatternTargets()
{
	FMetaAgentParticleShapeContext BuildContext = PatternShapeContext;
	if (BuildContext.BaselineWorldPositions.Num() <= 0)
	{
		BuildContext.BaselineWorldPositions = PatternRuntime.BaselineWorldPositions;
	}

	if (!BuildContext.World && CachedWorld.IsValid())
	{
		BuildContext.World = CachedWorld.Get();
	}
	const FMetaAgentParticleShapeBuildResult BuildResult = FMetaAgentParticleShapeBuilder::BuildPatternTargets(
		PatternRuntime.ActiveConfig,
		BuildContext);

	PatternRuntime.bAwaitingAsyncMask = BuildResult.bAwaitingAsyncMask;

	if (!BuildResult.bSuccess || BuildResult.PatternWorldTargets.Num() <= 0)
	{
		PatternRuntime.PatternColumns = 0;
		PatternRuntime.PatternCenter = FVector::ZeroVector;
		PatternRuntime.PatternWorldTargets.Reset();
		PatternRuntime.ShapeDebugInfo = BuildResult.DebugInfo;
		return false;
	}

	PatternRuntime.PatternWorldTargets = BuildResult.PatternWorldTargets;
	PatternRuntime.PatternColumns = BuildResult.PatternColumns;
	PatternRuntime.PatternCenter = BuildResult.PatternCenter;
	PatternRuntime.ActiveShape = BuildResult.ResolvedShape;
	PatternRuntime.ActiveShapeFrame = BuildResult.ShapeFrame;
	PatternRuntime.ShapeDebugInfo = BuildResult.DebugInfo;
	return true;
}

float UMetaAgentParticleRuntime::EvaluatePhaseForState(
	const EMetaAgentParticlePatternState State,
	const float NormalizedTimeInState) const
{
	const float ClampedTime = FMath::Clamp(NormalizedTimeInState, 0.0f, 1.0f);

	if (State == EMetaAgentParticlePatternState::Forming && ActiveFormCurve)
	{
		return FMath::Clamp(ActiveFormCurve->GetFloatValue(ClampedTime), 0.0f, 1.0f);
	}

	if (State == EMetaAgentParticlePatternState::Returning && ActiveReturnCurve)
	{
		return FMath::Clamp(1.0f - ActiveReturnCurve->GetFloatValue(ClampedTime), 0.0f, 1.0f);
	}

	if (State == EMetaAgentParticlePatternState::Returning)
	{
		return 1.0f - SmoothStep01(ClampedTime);
	}

	return SmoothStep01(ClampedTime);
}

float UMetaAgentParticleRuntime::ComputeActuationBlendAlpha() const
{
	if (PatternRuntime.State == EMetaAgentParticlePatternState::Holding)
	{
		return 1.0f;
	}

	return FMath::Clamp(PatternRuntime.Phase, 0.0f, 1.0f);
}

const FMetaAgentParticlePatternConfig& UMetaAgentParticleRuntime::GetTimingConfigForTick() const
{
	return PatternRuntime.State == EMetaAgentParticlePatternState::Idle
		? PatternConfig
		: PatternRuntime.ActiveConfig;
}

void UMetaAgentParticleRuntime::TickPatternRuntime(const float DeltaTimeSeconds)
{
	if (PatternRuntime.State == EMetaAgentParticlePatternState::Idle)
	{
		return;
	}

	PatternRuntime.StateElapsedSeconds += FMath::Max(0.0f, DeltaTimeSeconds);

	const FMetaAgentParticlePatternConfig& Timings = PatternRuntime.ActiveConfig;

	switch (PatternRuntime.State)
	{
	case EMetaAgentParticlePatternState::Preparing:
	{
		if (BuildPatternTargets())
		{
			EnterPatternState(EMetaAgentParticlePatternState::Forming);
			PatternRuntime.Phase = 0.0f;
			bLoggedPatternStart = false;

			const FString ResolvedShapeName = PatternRuntime.ActiveConfig.Shape.GetShapeDisplayName();

			UE_LOG(LogMetaAgent, Log,
				TEXT("ParticleRuntime: pattern started after async mask build requested=%s resolved=%s particles=%d | %s"),
				*PatternRuntime.ActiveConfig.Shape.GetShapeDisplayName(),
				*ResolvedShapeName,
				PatternRuntime.BaselineWorldPositions.Num(),
				*PatternRuntime.ShapeDebugInfo);
		}
		else if (!PatternRuntime.bAwaitingAsyncMask && PatternRuntime.StateElapsedSeconds > 0.25f)
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("ParticleRuntime: async mask build failed (%s)."),
				*PatternRuntime.ShapeDebugInfo);
			CompletePatternRun();
		}
		else if (PatternRuntime.StateElapsedSeconds > 60.0f)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("ParticleRuntime: async mask build timed out after 60s."));
			CompletePatternRun();
		}
		break;
	}
	case EMetaAgentParticlePatternState::Forming:
	{
		FormingSteeringBlendElapsedSeconds += FMath::Max(0.0f, DeltaTimeSeconds);
		const float FormDuration = FMath::Max(0.1f, Timings.FormDurationSeconds);
		PatternRuntime.Phase = EvaluatePhaseForState(
			EMetaAgentParticlePatternState::Forming,
			PatternRuntime.StateElapsedSeconds / FormDuration);
		if (PatternRuntime.StateElapsedSeconds >= FormDuration)
		{
			EnterPatternState(EMetaAgentParticlePatternState::Holding);
			PatternRuntime.Phase = 1.0f;
		}
		break;
	}
	case EMetaAgentParticlePatternState::Holding:
	{
		PatternRuntime.Phase = 1.0f;
		if (PatternRuntime.StateElapsedSeconds >= FMath::Max(0.0f, Timings.HoldDurationSeconds))
		{
			EnterPatternState(EMetaAgentParticlePatternState::Returning);
		}
		break;
	}
	case EMetaAgentParticlePatternState::Returning:
	{
		const float ReturnDuration = FMath::Max(0.1f, Timings.ReturnDurationSeconds);
		PatternRuntime.Phase = EvaluatePhaseForState(
			EMetaAgentParticlePatternState::Returning,
			PatternRuntime.StateElapsedSeconds / ReturnDuration);

		if (PatternRuntime.StateElapsedSeconds >= ReturnDuration)
		{
			CompletePatternRun();
		}
		break;
	}
	default:
		break;
	}
}

void UMetaAgentParticleRuntime::ApplyPatternActuation()
{
	if (PatternRuntime.BaselineWorldPositions.Num() <= 0 || PatternRuntime.PatternWorldTargets.Num() <= 0)
	{
		return;
	}

	const bool bReturning = PatternRuntime.State == EMetaAgentParticlePatternState::Returning;
	float BlendAlpha = ComputeActuationBlendAlpha();
	if (bReturning)
	{
		CaptureLiveSimPositions();
		if (LiveSimWorldPositions.Num() > 0)
		{
			PatternRuntime.ReturnRestPositions = LiveSimWorldPositions;
		}
		else if (PatternRuntime.ReturnRestPositions.Num() <= 0)
		{
			PatternRuntime.ReturnRestPositions = PatternRuntime.TrajectoryWorldPositions;
		}

		BlendAlpha = FMath::Clamp(PatternRuntime.Phase, 0.0f, 1.0f);
	}

	const float HoldPulseScale = PatternRuntime.State == EMetaAgentParticlePatternState::Holding
		? (1.0f + FMath::Sin(PatternRuntime.StateElapsedSeconds * 4.0f) * ActiveHoldPulseAmplitude)
		: 1.0f;

	FMetaAgentParticleActuationRequest Request;
	Request.BaselineWorldPositions = &PatternRuntime.BaselineWorldPositions;
	Request.PatternWorldTargets = &PatternRuntime.PatternWorldTargets;
	Request.ParticleBlocks = &LatestSnapshot.ParticleBlocks;
	Request.TrackedComponents = TrackedNiagaraComponents;
	Request.BlendAlpha = BlendAlpha;
	Request.HoldPulseScale = HoldPulseScale;
	Request.PatternCenter = PatternRuntime.PatternCenter;
	Request.bPatternActive = true;
	Request.bUseReturnHoldBlend = bReturning;
	if (bReturning)
	{
		Request.ReturnHoldPositions = &PatternRuntime.ReturnHoldPositions;
		Request.ReturnRestPositions = &PatternRuntime.ReturnRestPositions;
	}
	else if (PatternRuntime.State == EMetaAgentParticlePatternState::Forming
		&& LatestSnapshot.bSteeringTargetEnabled
		&& FormingSteeringBlendDurationSeconds > KINDA_SMALL_NUMBER
		&& LatestSnapshot.SuggestedSteeringDirections.Num() > 0)
	{
		Request.FormingSteeringWeight = FMath::Clamp(
			1.0f - (FormingSteeringBlendElapsedSeconds / FormingSteeringBlendDurationSeconds),
			0.0f,
			1.0f);
		if (Request.FormingSteeringWeight > KINDA_SMALL_NUMBER)
		{
			Request.FormingSteeringOffsets = &LatestSnapshot.SuggestedSteeringDirections;
		}
	}

	const EMetaAgentParticleActuationMode EffectiveMode =
		FMetaAgentParticleActuation::ResolveEffectiveMode(ActuationMode);

	IMetaAgentParticleActuator& ParameterActuator =
		FMetaAgentParticleActuation::GetActuator(EMetaAgentParticleActuationMode::Parameters);

	if (EffectiveMode == EMetaAgentParticleActuationMode::Parameters)
	{
		ParameterActuator.ApplyParameters(Request);
		return;
	}

	IMetaAgentParticleActuator& DirectActuator =
		FMetaAgentParticleActuation::GetActuator(EMetaAgentParticleActuationMode::Direct);

	TArray<FVector> AppliedWorldPositions;
	if (DirectActuator.ApplyPhase(Request, AppliedWorldPositions) > 0)
	{
		LastAppliedWorldPositions = AppliedWorldPositions;

		AActor* SourceActor = nullptr;
		FName SourceComponentName = NAME_None;
		if (UNiagaraComponent* FirstComponent = TrackedNiagaraComponents.Num() > 0 ? TrackedNiagaraComponents[0].Get() : nullptr)
		{
			SourceActor = FirstComponent->GetOwner();
			SourceComponentName = FirstComponent->GetFName();
		}

		SubmitAggregatedParticleCapture(
			AppliedWorldPositions,
			LatestSnapshot.ParticleBlocks,
			SourceActor ? SourceActor->GetFName() : NAME_None,
			SourceComponentName);

		if (!bLoggedPatternStart)
		{
			bLoggedPatternStart = true;
			UE_LOG(LogMetaAgent, Log,
				TEXT("ParticleRuntime: pattern actuation writing %d particle position(s), state=%s phase=%.2f liveReturn=%s."),
				AppliedWorldPositions.Num(),
				*GetPatternStateDisplayName(),
				PatternRuntime.Phase,
				bReturning ? TEXT("yes") : TEXT("no"));
		}
	}

	if (EffectiveMode == EMetaAgentParticleActuationMode::Direct)
	{
		return;
	}

	ParameterActuator.ApplyParameters(Request);
}
