// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "MetaAgentParticleRuntime.h"

#include "MetaAgentPlugin.h"
#include "MetaAgentTypeBridge.h"
#include "MetaAgentParticleControl.h"
#include "MetaAgentParticleShapes.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "NiagaraComponent.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraTypes.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "metaagent/particle/actuation_math.hpp"
#include "metaagent/particle/forming_solver.hpp"
#include "metaagent/particle/representation_actuation.hpp"

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
		case EMetaAgentParticlePatternState::Anticipating: return TEXT("Anticipating");
		case EMetaAgentParticlePatternState::Forming: return TEXT("Forming");
		case EMetaAgentParticlePatternState::Holding: return TEXT("Holding");
		case EMetaAgentParticlePatternState::Returning: return TEXT("Returning");
		case EMetaAgentParticlePatternState::Dissipating: return TEXT("Dissipating");
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
	if (PatternRuntime.ReturnHoldPositions.Num() <= 0)
	{
		if (LastAppliedWorldPositions.Num() > 0)
		{
			PatternRuntime.ReturnHoldPositions = LastAppliedWorldPositions;
		}
		else
		{
			PatternRuntime.ReturnHoldPositions = PatternRuntime.PatternWorldTargets;
		}
	}

	const int32 ParticleCount = PatternRuntime.ReturnHoldPositions.Num();
	PatternRuntime.ReturnRestPositions.Reset();
	PatternRuntime.ReturnRestPositions.Reserve(ParticleCount);

	// Freeze a stable idle rest target once. Per-tick live reads fight Direct buffer writes and flicker.
	for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
	{
		if (PatternRuntime.IdleBaselineWorldPositions.IsValidIndex(ParticleIndex))
		{
			PatternRuntime.ReturnRestPositions.Add(PatternRuntime.IdleBaselineWorldPositions[ParticleIndex]);
		}
		else if (PatternRuntime.BaselineWorldPositions.IsValidIndex(ParticleIndex))
		{
			PatternRuntime.ReturnRestPositions.Add(PatternRuntime.BaselineWorldPositions[ParticleIndex]);
		}
		else if (PatternRuntime.TrajectoryWorldPositions.IsValidIndex(ParticleIndex))
		{
			PatternRuntime.ReturnRestPositions.Add(PatternRuntime.TrajectoryWorldPositions[ParticleIndex]);
		}
		else
		{
			PatternRuntime.ReturnRestPositions.Add(PatternRuntime.ReturnHoldPositions[ParticleIndex]);
		}
	}

	UE_LOG(LogMetaAgent, Verbose,
		TEXT("ParticleRuntime: return started hold=%d rest=%d (frozen targets)."),
		PatternRuntime.ReturnHoldPositions.Num(),
		PatternRuntime.ReturnRestPositions.Num());
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

void UMetaAgentParticleRuntime::PruneStaleTrackedNiagaraComponents()
{
	for (int32 Index = TrackedNiagaraComponents.Num() - 1; Index >= 0; --Index)
	{
		UNiagaraComponent* NiagaraComponent = TrackedNiagaraComponents[Index].Get();
		if (!IsValid(NiagaraComponent) || !NiagaraComponent->IsActive())
		{
			TrackedNiagaraComponents.RemoveAtSwap(Index);
		}
	}
}

void UMetaAgentParticleRuntime::BuildComponentSnapshot()
{
	LatestSnapshot.TrackedComponents.Reset();
	PruneStaleTrackedNiagaraComponents();

	for (int32 Index = TrackedNiagaraComponents.Num() - 1; Index >= 0; --Index)
	{
		UNiagaraComponent* NiagaraComponent = TrackedNiagaraComponents[Index].Get();
		if (!IsValid(NiagaraComponent))
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

	return DispatchPatternTransition(EMetaAgentPatternTransitionTrigger::Cancel, bSkipReturn);
}

bool UMetaAgentParticleRuntime::RequestPatternMorph()
{
	if (PatternRuntime.State != EMetaAgentParticlePatternState::Holding)
	{
		return false;
	}

	(void)BuildPatternTargets();

	if (PatternRuntime.PatternWorldTargets.Num() <= 0)
	{
		return false;
	}

	if (LastAppliedWorldPositions.Num() > 0)
	{
		PatternRuntime.BaselineWorldPositions = LastAppliedWorldPositions;
	}
	else if (PatternRuntime.TrajectoryWorldPositions.Num() > 0)
	{
		PatternRuntime.BaselineWorldPositions = PatternRuntime.TrajectoryWorldPositions;
	}

	return DispatchPatternTransition(EMetaAgentPatternTransitionTrigger::Morph);
}

bool UMetaAgentParticleRuntime::RequestSkipHold()
{
	if (PatternRuntime.State != EMetaAgentParticlePatternState::Holding)
	{
		return false;
	}

	return BeginConfiguredReturn();
}

bool UMetaAgentParticleRuntime::BeginConfiguredReturn()
{
	const FMetaAgentParticleReturnSettings& ReturnSettings =
		PatternRuntime.State == EMetaAgentParticlePatternState::Idle
			? PatternConfig.Return
			: PatternRuntime.ActiveConfig.Return;

	if (ReturnSettings.Mode == EMetaAgentParticleReturnMode::DissipateToCenter)
	{
		return RequestDissipateToCenter();
	}

	EnterPatternState(EMetaAgentParticlePatternState::Returning);
	return true;
}

void UMetaAgentParticleRuntime::BeginDissipateToCenter()
{
	if (LastAppliedWorldPositions.Num() > 0)
	{
		PatternRuntime.DissipateStartPositions = LastAppliedWorldPositions;
	}
	else if (PatternRuntime.ReturnHoldPositions.Num() > 0)
	{
		PatternRuntime.DissipateStartPositions = PatternRuntime.ReturnHoldPositions;
	}
	else if (PatternRuntime.PatternWorldTargets.Num() > 0)
	{
		PatternRuntime.DissipateStartPositions = PatternRuntime.PatternWorldTargets;
	}
	else
	{
		PatternRuntime.DissipateStartPositions = PatternRuntime.BaselineWorldPositions;
	}

	UE_LOG(LogMetaAgent, Verbose,
		TEXT("ParticleRuntime: dissipate started from %d particle(s) toward center %s."),
		PatternRuntime.DissipateStartPositions.Num(),
		*PatternRuntime.PatternCenter.ToString());
}

bool UMetaAgentParticleRuntime::RequestDissipateToCenter()
{
	switch (PatternRuntime.State)
	{
	case EMetaAgentParticlePatternState::Forming:
	case EMetaAgentParticlePatternState::Holding:
	case EMetaAgentParticlePatternState::Returning:
		break;
	default:
		return false;
	}

	BeginDissipateToCenter();
	if (PatternRuntime.DissipateStartPositions.Num() <= 0)
	{
		return false;
	}

	EnterPatternState(EMetaAgentParticlePatternState::Dissipating);
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
	if (PatternAsset->HoldPulseAmplitude > 0.0f)
	{
		PatternConfig.HoldPulseAmplitude = PatternAsset->HoldPulseAmplitude;
	}
	if (PatternAsset->HoldPulseFrequencyHz > 0.0f)
	{
		PatternConfig.HoldPulseFrequencyHz = PatternAsset->HoldPulseFrequencyHz;
	}
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
	PatternRuntime.IdleBaselineWorldPositions = PatternRuntime.BaselineWorldPositions;

	if (PatternShapeContext.BaselineWorldPositions.Num() <= 0)
	{
		PatternShapeContext.BaselineWorldPositions = PatternRuntime.BaselineWorldPositions;
	}

	if (CachedWorld.IsValid())
	{
		PatternShapeContext.World = CachedWorld.Get();
	}

	FormingSteeringBlendElapsedSeconds = 0.0f;
	PatternRuntime.PatternWorldTargets = PatternRuntime.BaselineWorldPositions;
	PatternRuntime.PatternColumns = 0;
	PatternRuntime.PatternCenter = FVector::ZeroVector;
	for (const FVector& Position : PatternRuntime.BaselineWorldPositions)
	{
		PatternRuntime.PatternCenter += Position;
	}
	if (PatternRuntime.BaselineWorldPositions.Num() > 0)
	{
		PatternRuntime.PatternCenter /= static_cast<float>(PatternRuntime.BaselineWorldPositions.Num());
	}

	const bool bTargetsReady = BuildPatternTargets();
	if (!bTargetsReady && !PatternRuntime.bAwaitingAsyncMask)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("ParticleRuntime: failed to build pattern targets."));
		PatternRuntime.ActivePatternTags.Reset();
		return false;
	}

	if (bTargetsReady)
	{
		PatternRuntime.ShapeDebugInfo = FString::Printf(
			TEXT("Anticipating toward %s"),
			*PatternRuntime.ActiveConfig.Shape.GetShapeDisplayName());
	}
	else
	{
		PatternRuntime.PatternWorldTargets = PatternRuntime.BaselineWorldPositions;
		PatternRuntime.ShapeDebugInfo = FString::Printf(
			TEXT("Anticipating while loading mask @ %dpx"),
			PatternRuntime.ActiveConfig.Shape.SampleResolution);
		UE_LOG(LogMetaAgent, Log, TEXT("ParticleRuntime: anticipating while image mask builds asynchronously."));
	}

	bLoggedPatternStart = false;
	EnterPatternState(EMetaAgentParticlePatternState::Anticipating);
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
	bManualPatternStateAdvance = true;
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

EMetaAgentRepresentationMacroPhase UMetaAgentParticleRuntime::GetRepresentationMacroPhase() const
{
	return FMetaAgentParticleRepresentationMapping::MacroPhaseFromPatternState(PatternRuntime.State);
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
			TEXT("Pattern Shape: %s | Sampling=%s | Res=%dpx | ScatterGrid=%.1f Jitter=%.2f | Forming=%s | ImageLoaded=%s"),
			*ShapeName,
			*DisplayShape.GetImageSamplingDisplayName(),
			DisplayShape.SampleResolution,
			DisplayShape.DensityGridScale,
			DisplayShape.TargetJitterNormalized,
			*PatternConfig.Forming.GetModeDisplayName(),
			bImageLoaded ? TEXT("TRUE") : TEXT("FALSE"));
	}

	if (PatternRuntime.State == EMetaAgentParticlePatternState::Anticipating)
	{
		return FString::Printf(
			TEXT("Pattern Shape: %s | %s"),
			*ShapeName,
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
	return MetaAgentParticleCoreBridge::build_pattern_timings_text(*this);
}

FString UMetaAgentParticleRuntime::BuildPatternStatusText() const
{
	return MetaAgentParticleCoreBridge::build_pattern_status_text(*this);
}

void UMetaAgentParticleRuntime::ApplyPatternConfig(const FMetaAgentParticlePatternConfig& Config)
{
	PatternConfig = Config;
	PatternConfig.Forming.Mode = FMetaAgentParticleFormingSettings::SanitizeMode(PatternConfig.Forming.Mode);
	PatternConfig.Return.Mode = FMetaAgentParticleReturnSettings::SanitizeMode(PatternConfig.Return.Mode);

	if (PatternRuntime.State == EMetaAgentParticlePatternState::Forming
		|| PatternRuntime.State == EMetaAgentParticlePatternState::Anticipating)
	{
		PatternRuntime.ActiveConfig.Forming = PatternConfig.Forming;
	}

	if (PatternRuntime.State == EMetaAgentParticlePatternState::Returning)
	{
		PatternRuntime.ActiveConfig.Return = PatternConfig.Return;
	}

	if (PatternRuntime.State != EMetaAgentParticlePatternState::Idle
		&& PatternRuntime.State != EMetaAgentParticlePatternState::Preparing)
	{
		PatternRuntime.ActiveConfig.Shape = PatternConfig.Shape;
	}
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
	return MetaAgentParticleCoreBridge::get_active_state_duration_seconds(*this);
}

float UMetaAgentParticleRuntime::GetActiveStateTimeRemainingSeconds() const
{
	return MetaAgentParticleCoreBridge::get_active_state_time_remaining_seconds(*this);
}

void UMetaAgentParticleRuntime::ResetPatternRuntime()
{
	PatternRuntime = FMetaAgentParticlePatternRuntime();
	ActiveFormCurve = nullptr;
	ActiveReturnCurve = nullptr;
	FormingSteeringBlendElapsedSeconds = 0.0f;
	LastPatternTickDeltaSeconds = 0.0f;
	bLoggedPatternStart = false;
	LastAppliedWorldPositions.Reset();
	LiveSimWorldPositions.Reset();
}

void UMetaAgentParticleRuntime::CommitAnticipationBaselineForForming()
{
	const int32 ParticleCount = PatternRuntime.BaselineWorldPositions.Num();
	if (ParticleCount <= 0)
	{
		return;
	}

	const TArray<FVector>& IdleBaseline = PatternRuntime.IdleBaselineWorldPositions.Num() == ParticleCount
		? PatternRuntime.IdleBaselineWorldPositions
		: PatternRuntime.BaselineWorldPositions;

	PatternRuntime.AnticipationHandoffElapsedSeconds = PatternRuntime.StateElapsedSeconds;

	FMetaAgentParticleActuation::BuildAnticipationWorldPositions(
		IdleBaseline,
		PatternRuntime.PatternCenter,
		PatternRuntime.AnticipationHandoffElapsedSeconds,
		PatternRuntime.ActiveConfig.AnticipationAmplitudeCm,
		PatternRuntime.ActiveConfig.AnticipationFrequencyHz,
		PatternRuntime.BaselineWorldPositions,
		FMath::Max(0.05f, PatternRuntime.ActiveConfig.AnticipationIdleBlendDurationSeconds));

	UE_LOG(LogMetaAgent, Verbose,
		TEXT("ParticleRuntime: forming baseline committed from anticipating (%d particles, handoff=%.2fs)."),
		ParticleCount,
		PatternRuntime.AnticipationHandoffElapsedSeconds);
}

void UMetaAgentParticleRuntime::EnterPatternState(const EMetaAgentParticlePatternState NewState)
{
	const EMetaAgentParticlePatternState PreviousState = PatternRuntime.State;

	if (PreviousState == EMetaAgentParticlePatternState::Anticipating
		&& NewState == EMetaAgentParticlePatternState::Forming
		&& !bManualPatternStateAdvance)
	{
		CommitAnticipationBaselineForForming();
	}
	else if (NewState == EMetaAgentParticlePatternState::Forming)
	{
		PatternRuntime.AnticipationHandoffElapsedSeconds = -1.0f;
	}

	PatternRuntime.State = NewState;
	PatternRuntime.StateElapsedSeconds = 0.0f;

	if (NewState == EMetaAgentParticlePatternState::Anticipating)
	{
		PatternRuntime.Phase = 0.0f;
	}
	else if (NewState == EMetaAgentParticlePatternState::Forming)
	{
		PatternRuntime.Phase = 0.0f;
		FormingSteeringBlendElapsedSeconds = 0.0f;
	}
	else if (NewState == EMetaAgentParticlePatternState::Holding)
	{
		PatternRuntime.Phase = 1.0f;
		RefreshTrajectoryBaselineAtHoldStart();
	}
	else if (NewState == EMetaAgentParticlePatternState::Returning)
	{
		PatternRuntime.Phase = 1.0f;
		BeginReturnFromHold();
	}
	else if (NewState == EMetaAgentParticlePatternState::Dissipating)
	{
		PatternRuntime.Phase = 0.0f;
	}

	OnPatternStateEntered.Broadcast(NewState, PreviousState);
}

bool UMetaAgentParticleRuntime::AdvancePatternStateForward()
{
	const EMetaAgentParticlePatternState PreviousState = PatternRuntime.State;

	if (PatternRuntime.State == EMetaAgentParticlePatternState::Anticipating
		&& PatternRuntime.bAwaitingAsyncMask
		&& PatternRuntime.PatternWorldTargets.Num() <= 0
		&& PatternRuntime.CanonicalPatternWorldTargets.Num() <= 0)
	{
		BuildPatternTargets();
	}

	if (bManualPatternStateAdvance && IsPatternActive())
	{
		ApplyPatternActuation();
	}

	const bool bHandled = DispatchPatternTransition(EMetaAgentPatternTransitionTrigger::Advance);

	if (bHandled && IsPatternActive())
	{
		ApplyPatternActuation();
	}

	return bHandled || PreviousState != PatternRuntime.State;
}

bool UMetaAgentParticleRuntime::RetreatPatternStateBackward()
{
	const EMetaAgentParticlePatternState PreviousState = PatternRuntime.State;

	if (bManualPatternStateAdvance && IsPatternActive())
	{
		ApplyPatternActuation();
	}

	const bool bHandled = DispatchPatternTransition(EMetaAgentPatternTransitionTrigger::Retreat);

	if (bHandled && IsPatternActive())
	{
		ApplyPatternActuation();
	}

	return bHandled || PreviousState != PatternRuntime.State;
}

bool UMetaAgentParticleRuntime::DispatchPatternTransition(
	const EMetaAgentPatternTransitionTrigger Trigger,
	const bool bSkipReturnOnCancel)
{
	return MetaAgentParticleCoreBridge::dispatch_pattern_transition(*this, Trigger, bSkipReturnOnCancel);
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
	PatternRuntime.CanonicalPatternWorldTargets = BuildResult.PatternWorldTargets;
	PatternRuntime.PatternColumns = BuildResult.PatternColumns;
	PatternRuntime.PatternCenter = BuildResult.PatternCenter;
	PatternRuntime.ActiveShape = BuildResult.ResolvedShape;
	PatternRuntime.ActiveShapeFrame = BuildResult.ShapeFrame;
	PatternRuntime.ShapeDebugInfo = BuildResult.DebugInfo;
	return true;
}

void UMetaAgentParticleRuntime::TickPatternRuntime(const float DeltaTimeSeconds)
{
	MetaAgentParticleCoreBridge::tick_pattern_runtime(*this, DeltaTimeSeconds);
}

void UMetaAgentParticleRuntime::BuildRepresentationFrame(FMetaAgentParticleRepresentationFrame& OutFrame) const
{
	MetaAgentParticleCoreBridge::build_representation_frame(*this, OutFrame);
}

void UMetaAgentParticleRuntime::ApplyPatternRepresentation()
{
	ApplyPatternActuation();
}

void UMetaAgentParticleRuntime::RefreshPatternTargetsAfterConfigChange()
{
	if (!IsPatternActive())
	{
		return;
	}

	BuildPatternTargets();
	ApplyPatternRepresentation();
}

int32 UMetaAgentParticleRuntime::GetFocusableWorldPositions(TArray<FVector>& OutWorldPositions) const
{
	OutWorldPositions.Reset();

	if (LastAppliedWorldPositions.Num() > 0)
	{
		OutWorldPositions = LastAppliedWorldPositions;
		return OutWorldPositions.Num();
	}

	if (LatestSnapshot.ExportedParticlePositions.Num() > 0)
	{
		OutWorldPositions = LatestSnapshot.ExportedParticlePositions;
		return OutWorldPositions.Num();
	}

	if (PatternRuntime.BaselineWorldPositions.Num() <= 0)
	{
		return 0;
	}

	FMetaAgentParticleRepresentationFrame Frame;
	BuildRepresentationFrame(Frame);

	FMetaAgentParticleActuationRequest Request;
	FMetaAgentParticleRepresentationDriverRegistry::BuildActuationRequestFromFrame(Frame, Request);
	Request.ParticleBlocks = &LatestSnapshot.ParticleBlocks;

	return FMetaAgentParticleActuation::ComposeWorldPositionsFromRequest(Request, OutWorldPositions);
}

void UMetaAgentParticleRuntime::ApplyPatternActuation()
{
	PruneStaleTrackedNiagaraComponents();

	if (PatternRuntime.BaselineWorldPositions.Num() <= 0)
	{
		return;
	}

	if (PatternRuntime.PatternWorldTargets.Num() <= 0
		&& PatternRuntime.State != EMetaAgentParticlePatternState::Anticipating
		&& PatternRuntime.State != EMetaAgentParticlePatternState::Dissipating)
	{
		return;
	}

	BuildRepresentationFrame(LastRepresentationFrame);
	if (LastRepresentationFrame.Phase.AuthorityWeight <= KINDA_SMALL_NUMBER)
	{
		LastRepresentationFrame.bPatternActive = false;
		LastRepresentationFrame.Phase.BlendAlpha = 0.0f;
	}

	FMetaAgentParticleActuationRequest Request;
	FMetaAgentParticleRepresentationDriverRegistry::BuildActuationRequestFromFrame(
		LastRepresentationFrame,
		Request);
	Request.ParticleBlocks = &LatestSnapshot.ParticleBlocks;
	Request.TrackedComponents = TrackedNiagaraComponents;

	const UMetaAgentNiagaraSystemProfile* EffectiveProfile =
		NiagaraSystemProfile ? NiagaraSystemProfile : UMetaAgentNiagaraSystemProfile::GetDefaultProfile();

	if (!SharedNiagaraTargetData)
	{
		SharedNiagaraTargetData = NewObject<UMetaAgentNiagaraTargetData>(this);
	}
	SharedNiagaraTargetData->SetTargets(
		LastRepresentationFrame.PatternWorldTargets,
		LastRepresentationFrame.BaselineWorldPositions);

	TArray<FVector> AppliedWorldPositions;
	const int32 AppliedCount = FMetaAgentParticleRepresentationDriverRegistry::ApplyRepresentationFrame(
		LastRepresentationFrame,
		Request,
		EffectiveProfile,
		ActuationMode,
		ReturnReleaseAuthorityThreshold,
		SharedNiagaraTargetData,
		AppliedWorldPositions);

	if (AppliedCount > 0)
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
				TEXT("ParticleRuntime: representation driver applied %d particle position(s), macro=%s state=%s phase=%.2f."),
				AppliedWorldPositions.Num(),
				*FMetaAgentParticleRepresentationMapping::GetMacroPhaseDisplayName(LastRepresentationFrame.MacroPhase),
				*GetPatternStateDisplayName(),
				PatternRuntime.Phase);
		}
	}
}

namespace MetaAgentParticleActuationInternal
{
	static const FName PositionAttributeName(TEXT("Position"));
	static const FName PatternPhaseParameterName(TEXT("MetaAgentPatternPhase"));
	static const FName PatternCenterParameterName(TEXT("MetaAgentPatternCenter"));
	static const FName PatternActiveParameterName(TEXT("MetaAgentPatternActive"));
	static const FName PatternHoldScaleParameterName(TEXT("MetaAgentPatternHoldScale"));
	static const FName PatternDissipateVisibilityParameterName(TEXT("MetaAgentPatternDissipateVisibility"));
	static const FName PatternDissipateActiveParameterName(TEXT("MetaAgentPatternDissipateActive"));
	static const FName FormingModeParameterName(TEXT("MetaAgentFormingMode"));
	static const FName FormingArcLiftParameterName(TEXT("MetaAgentFormingArcLift"));
	static const FName FormingSpiralTurnsParameterName(TEXT("MetaAgentFormingSpiralTurns"));

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

	FNiagaraPosition WorldToNiagaraPosition(
		const FVector& WorldPosition,
		const FNiagaraEmitterInstance& EmitterInstance,
		const UNiagaraComponent& NiagaraComponent,
		const FNiagaraSystemInstance& SystemInstance)
	{
		FVector SimPosition = WorldPosition;
		if (EmitterInstance.IsLocalSpace())
		{
			SimPosition = NiagaraComponent.GetComponentTransform().InverseTransformPosition(SimPosition);
		}

		const FVector LWCOffset = FVector(SystemInstance.GetLWCTile()) * FLargeWorldRenderScalar::GetTileSize();
		SimPosition -= LWCOffset;
		return FNiagaraPosition(SimPosition);
	}

	FVector ApplyStateEffectOffset(
		const FMetaAgentParticleActuationRequest& Request,
		const int32 GlobalIndex,
		const FVector& ComposedWorldPosition)
	{
		if (!Request.StateEffectOffsets || !Request.StateEffectOffsets->IsValidIndex(GlobalIndex))
		{
			return ComposedWorldPosition;
		}

		return ComposedWorldPosition + (*Request.StateEffectOffsets)[GlobalIndex];
	}

	bool FindPositionFloatComponents(
		const FNiagaraDataSetCompiledData& CompiledData,
		int32& OutFloatComponentStart,
		int32& OutNumFloatComponents)
	{
		const int32 VariableIndex = CompiledData.Variables.IndexOfByPredicate(
			[](const FNiagaraVariable& Variable)
			{
				return Variable.GetName() == PositionAttributeName;
			});

		if (VariableIndex == INDEX_NONE)
		{
			return false;
		}

		const FNiagaraVariableLayoutInfo& Layout = CompiledData.VariableLayouts[VariableIndex];
		OutFloatComponentStart = Layout.GetFloatComponentStart();
		OutNumFloatComponents = Layout.GetNumFloatComponents();
		return OutNumFloatComponents >= 3;
	}

	bool WriteBlendedPositionsToBuffer(
		FNiagaraDataBuffer& DataBuffer,
		const FNiagaraDataSetCompiledData& CompiledData,
		const FNiagaraEmitterInstance& EmitterInstance,
		const UNiagaraComponent& NiagaraComponent,
		const FNiagaraSystemInstance& SystemInstance,
		const FMetaAgentParticleActuationRequest& Request,
		const int32 GlobalStartIndex,
		const int32 LocalParticleCount)
	{
		int32 FloatComponentStart = INDEX_NONE;
		int32 NumFloatComponents = 0;
		if (!FindPositionFloatComponents(CompiledData, FloatComponentStart, NumFloatComponents))
		{
			return false;
		}

		const uint32 NumInstances = DataBuffer.GetNumInstances();
		if (NumInstances == 0 || LocalParticleCount <= 0)
		{
			return false;
		}

		const int32 MaxLocalIndex = FMath::Min(
			LocalParticleCount,
			static_cast<int32>(NumInstances)) - 1;

		if (!Request.BaselineWorldPositions || !Request.PatternWorldTargets)
		{
			return false;
		}

		if (!Request.BaselineWorldPositions || !Request.PatternWorldTargets)
		{
			return false;
		}

		MetaAgentTypeBridge::FMetaAgentActuationComposeScratch ComposeScratch;
		MetaAgentTypeBridge::build_actuation_compose_input(Request, ComposeScratch);

		for (int32 LocalIndex = 0; LocalIndex <= MaxLocalIndex; ++LocalIndex)
		{
			const int32 GlobalIndex = GlobalStartIndex + LocalIndex;
			const metaagent::particle::ActuationComposeResult Composed =
				metaagent::particle::ActuationMath::compose_particle_world_position(
					ComposeScratch.input,
					GlobalIndex);
			if (!Composed.valid)
			{
				continue;
			}

			const FVector WorldPosition = ApplyStateEffectOffset(
				Request,
				GlobalIndex,
				FVector(Composed.world_position.x, Composed.world_position.y, Composed.world_position.z));

			const FNiagaraPosition SimPosition = WorldToNiagaraPosition(
				WorldPosition,
				EmitterInstance,
				NiagaraComponent,
				SystemInstance);

			*DataBuffer.GetInstancePtrFloat(FloatComponentStart + 0, LocalIndex) = SimPosition.X;
			*DataBuffer.GetInstancePtrFloat(FloatComponentStart + 1, LocalIndex) = SimPosition.Y;
			*DataBuffer.GetInstancePtrFloat(FloatComponentStart + 2, LocalIndex) = SimPosition.Z;
		}

		return true;
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

	int32 ResolveBlockForComponent(
		const FMetaAgentParticleActuationRequest& Request,
		const UNiagaraComponent& NiagaraComponent,
		int32& OutGlobalStartIndex,
		int32& OutParticleCount)
	{
		OutGlobalStartIndex = 0;
		OutParticleCount = Request.BaselineWorldPositions ? Request.BaselineWorldPositions->Num() : 0;

		if (!Request.ParticleBlocks || Request.ParticleBlocks->Num() <= 0)
		{
			return 0;
		}

		const FName ComponentName = NiagaraComponent.GetFName();
		const FName ActorName = NiagaraComponent.GetOwner() ? NiagaraComponent.GetOwner()->GetFName() : NAME_None;

		for (const FMetaAgentTrackedParticleBlock& Block : *Request.ParticleBlocks)
		{
			if (Block.SourceComponentName == ComponentName
				|| (Block.SourceActorName != NAME_None && Block.SourceActorName == ActorName))
			{
				OutGlobalStartIndex = Block.GlobalStartIndex;
				OutParticleCount = Block.ParticleCount;
				return Block.ParticleCount;
			}
		}

		return OutParticleCount;
	}

	void ApplyPatternToEmitter(
		FNiagaraEmitterInstance& EmitterInstance,
		UNiagaraComponent& NiagaraComponent,
		FNiagaraSystemInstance& SystemInstance,
		const FMetaAgentParticleActuationRequest& Request,
		const int32 GlobalStartIndex,
		const int32 LocalParticleCount,
		TArray<FVector>& OutAppliedWorldPositions)
	{
		if (!Request.BaselineWorldPositions || !Request.PatternWorldTargets)
		{
			return;
		}

		const FNiagaraDataSetCompiledData& CompiledData = EmitterInstance.GetParticleData().GetCompiledData();

		if (FNiagaraComputeExecutionContext* GPUContext = EmitterInstance.GetGPUContext())
		{
			if (FNiagaraDataSet* GPUDataSet = GPUContext->MainDataSet)
			{
				FScopedNiagaraDataSetGPUReadback ScopedGPUReadback;
				ScopedGPUReadback.ReadbackData(SystemInstance.GetComputeDispatchInterface(), GPUDataSet);

				FNiagaraDataBuffer* SourceCPUBuffer = GPUDataSet->GetCurrentData();
				FNiagaraDataBuffer* DestinationGPUBuffer = GPUDataSet->GetDestinationData();
				if (!SourceCPUBuffer || !DestinationGPUBuffer || ScopedGPUReadback.GetNumInstances() == 0)
				{
					return;
				}

				if (!WriteBlendedPositionsToBuffer(
					*SourceCPUBuffer,
					CompiledData,
					EmitterInstance,
					NiagaraComponent,
					SystemInstance,
					Request,
					GlobalStartIndex,
					LocalParticleCount))
				{
					return;
				}

				TArray<FNiagaraDataBufferRef> SourceBuffers;
				SourceBuffers.Emplace(SourceCPUBuffer);

				const ERHIFeatureLevel::Type FeatureLevel = NiagaraComponent.GetWorld()
					? NiagaraComponent.GetWorld()->GetFeatureLevel()
					: GMaxRHIFeatureLevel;

				ENQUEUE_RENDER_COMMAND(MetaAgentParticlePatternPush)
				(
					[DestinationGPUBuffer, SourceBuffers, FeatureLevel](FRHICommandListImmediate& RHICmdList)
					{
						DestinationGPUBuffer->PushCPUBuffersToGPU(
							SourceBuffers,
							true,
							RHICmdList,
							FeatureLevel,
							TEXT("MetaAgentPattern"),
							true);
					}
				);

				FMetaAgentParticleActuation::ComposeWorldPositionsFromRequest(Request, OutAppliedWorldPositions);
			}

			return;
		}

		FNiagaraDataBuffer* CPUBuffer = EmitterInstance.GetParticleData().GetCurrentData();
		if (!CPUBuffer)
		{
			return;
		}

		if (!WriteBlendedPositionsToBuffer(
			*CPUBuffer,
			CompiledData,
			EmitterInstance,
			NiagaraComponent,
			SystemInstance,
			Request,
			GlobalStartIndex,
			LocalParticleCount))
		{
			return;
		}

		ExtractPositionsFromDataSet(EmitterInstance.GetParticleData(), EmitterInstance, NiagaraComponent, SystemInstance, OutAppliedWorldPositions);
	}
}

namespace
{
	class FMetaAgentDirectBufferActuator final : public IMetaAgentParticleActuator
	{
	public:
		virtual EMetaAgentParticleActuationMode GetActuationMode() const override
		{
			return EMetaAgentParticleActuationMode::Direct;
		}

		virtual int32 ApplyPhase(
			const FMetaAgentParticleActuationRequest& Request,
			TArray<FVector>& OutAppliedWorldPositions) override
		{
			return FMetaAgentParticleActuation::ApplyDirect(Request, OutAppliedWorldPositions);
		}
	};

	class FMetaAgentNiagaraParameterActuator final : public IMetaAgentParticleActuator
	{
	public:
		virtual EMetaAgentParticleActuationMode GetActuationMode() const override
		{
			return EMetaAgentParticleActuationMode::Parameters;
		}

		virtual void ApplyParameters(const FMetaAgentParticleActuationRequest& Request) override
		{
			FMetaAgentParticleActuation::ApplyParameters(Request);
		}
	};
}

bool IMetaAgentParticleActuator::SupportsComponent(const UNiagaraComponent& NiagaraComponent) const
{
	return NiagaraComponent.IsActive();
}

int32 IMetaAgentParticleActuator::ApplyPhase(
	const FMetaAgentParticleActuationRequest& Request,
	TArray<FVector>& OutAppliedWorldPositions)
{
	return 0;
}

void IMetaAgentParticleActuator::ApplyParameters(const FMetaAgentParticleActuationRequest& Request)
{
}

void IMetaAgentParticleActuator::Reset()
{
}

FVector FMetaAgentParticleActuation::ComputeAnticipationWorldPosition(
	const FVector& IdleBaseline,
	const int32 GlobalIndex,
	const FVector& PatternCenter,
	const float AnticipationElapsedSeconds,
	const float AnticipationAmplitudeCm,
	const float AnticipationFrequencyHz,
	const float AnticipationIdleBlendDurationSeconds)
{
	const metaagent::core::Vec3 Result = metaagent::particle::ActuationMath::compute_anticipation_world_position(
		metaagent::core::Vec3(IdleBaseline.X, IdleBaseline.Y, IdleBaseline.Z),
		GlobalIndex,
		metaagent::core::Vec3(PatternCenter.X, PatternCenter.Y, PatternCenter.Z),
		AnticipationElapsedSeconds,
		AnticipationAmplitudeCm,
		AnticipationFrequencyHz,
		AnticipationIdleBlendDurationSeconds);
	return FVector(Result.x, Result.y, Result.z);
}

void FMetaAgentParticleActuation::BuildAnticipationWorldPositions(
	const TArray<FVector>& IdleBaselineWorldPositions,
	const FVector& PatternCenter,
	const float AnticipationElapsedSeconds,
	const float AnticipationAmplitudeCm,
	const float AnticipationFrequencyHz,
	TArray<FVector>& OutWorldPositions,
	const float AnticipationIdleBlendDurationSeconds)
{
	metaagent::core::Array<metaagent::core::Vec3> CoreBaselines;
	CoreBaselines.reserve(static_cast<size_t>(IdleBaselineWorldPositions.Num()));
	for (const FVector& Baseline : IdleBaselineWorldPositions)
	{
		CoreBaselines.push_back(metaagent::core::Vec3(Baseline.X, Baseline.Y, Baseline.Z));
	}

	metaagent::core::Array<metaagent::core::Vec3> CorePositions;
	metaagent::particle::ActuationMath::build_anticipation_world_positions(
		CoreBaselines,
		metaagent::core::Vec3(PatternCenter.X, PatternCenter.Y, PatternCenter.Z),
		AnticipationElapsedSeconds,
		AnticipationAmplitudeCm,
		AnticipationFrequencyHz,
		CorePositions,
		AnticipationIdleBlendDurationSeconds);

	OutWorldPositions.SetNum(static_cast<int32>(CorePositions.size()));
	for (int32 Index = 0; Index < OutWorldPositions.Num(); ++Index)
	{
		const metaagent::core::Vec3& Position = CorePositions[static_cast<size_t>(Index)];
		OutWorldPositions[Index] = FVector(Position.x, Position.y, Position.z);
	}
}

IMetaAgentParticleActuator& FMetaAgentParticleActuation::GetActuator(const EMetaAgentParticleActuationMode Mode)
{
	static FMetaAgentDirectBufferActuator DirectActuator;
	static FMetaAgentNiagaraParameterActuator ParameterActuator;

	switch (Mode)
	{
	case EMetaAgentParticleActuationMode::Parameters:
		return ParameterActuator;
	case EMetaAgentParticleActuationMode::Direct:
	case EMetaAgentParticleActuationMode::Hybrid:
	default:
		return DirectActuator;
	}
}

EMetaAgentParticleActuationMode FMetaAgentParticleActuation::ResolveEffectiveMode(
	const EMetaAgentParticleActuationMode ConfiguredMode)
{
#if WITH_EDITOR
	constexpr bool bHybridUseDirectPath = true;
#else
	constexpr bool bHybridUseDirectPath = false;
#endif
	return MetaAgentTypeBridge::from_core_actuation_mode(
		metaagent::particle::RepresentationActuationPolicy::resolve_effective_mode(
			MetaAgentTypeBridge::to_core_actuation_mode(ConfiguredMode),
			bHybridUseDirectPath));
}

int32 FMetaAgentParticleActuation::ApplyDirect(
	const FMetaAgentParticleActuationRequest& Request,
	TArray<FVector>& OutAppliedWorldPositions)
{
	using namespace MetaAgentParticleActuationInternal;

	if (!Request.BaselineWorldPositions || !Request.PatternWorldTargets)
	{
		return 0;
	}

	for (const TWeakObjectPtr<UNiagaraComponent>& WeakComponent : Request.TrackedComponents)
	{
		UNiagaraComponent* NiagaraComponent = WeakComponent.Get();
		if (!NiagaraComponent || !NiagaraComponent->IsActive())
		{
			continue;
		}

		if (NiagaraComponent->GetForceSolo() == false)
		{
			NiagaraComponent->SetForceSolo(true);
		}

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

		int32 GlobalStartIndex = 0;
		int32 LocalParticleCount = 0;
		ResolveBlockForComponent(Request, *NiagaraComponent, GlobalStartIndex, LocalParticleCount);

		for (const FNiagaraEmitterInstanceRef& EmitterRef : SystemInstance->GetEmitters())
		{
			FNiagaraEmitterInstance& EmitterInstance = EmitterRef.Get();
			if (EmitterInstance.IsDisabled() || EmitterInstance.IsComplete())
			{
				continue;
			}

			ApplyPatternToEmitter(
				EmitterInstance,
				*NiagaraComponent,
				*SystemInstance,
				Request,
				GlobalStartIndex,
				LocalParticleCount,
				OutAppliedWorldPositions);
		}
	}

	return OutAppliedWorldPositions.Num();
}

void FMetaAgentParticleActuation::ApplyParameters(const FMetaAgentParticleActuationRequest& Request)
{
	using namespace MetaAgentParticleActuationInternal;

	for (const TWeakObjectPtr<UNiagaraComponent>& WeakComponent : Request.TrackedComponents)
	{
		UNiagaraComponent* NiagaraComponent = WeakComponent.Get();
		if (!NiagaraComponent || !NiagaraComponent->IsActive())
		{
			continue;
		}

		NiagaraComponent->SetVariableFloat(PatternPhaseParameterName, Request.BlendAlpha);
		NiagaraComponent->SetVariableVec3(PatternCenterParameterName, Request.PatternCenter);
		NiagaraComponent->SetVariableBool(PatternActiveParameterName, Request.bPatternActive);
		NiagaraComponent->SetVariableFloat(PatternHoldScaleParameterName, Request.HoldPulseScale);
		NiagaraComponent->SetVariableBool(PatternDissipateActiveParameterName, Request.bDissipatingMotion);
		NiagaraComponent->SetVariableFloat(
			PatternDissipateVisibilityParameterName,
			Request.bDissipatingMotion ? Request.DissipateVisibility : 1.0f);

		const FMetaAgentParticleFormingSettings& FormingSettings = Request.FormingSettings
			? *Request.FormingSettings
			: FMetaAgentParticleFormingSettings();

		NiagaraComponent->SetVariableInt(
			FormingModeParameterName,
			static_cast<int32>(FMetaAgentParticleFormingSettings::SanitizeMode(FormingSettings.Mode)));
		NiagaraComponent->SetVariableFloat(FormingArcLiftParameterName, FormingSettings.ArcLiftHeightCm);
		NiagaraComponent->SetVariableFloat(FormingSpiralTurnsParameterName, FormingSettings.SpiralTurns);
	}
}

int32 FMetaAgentParticleActuation::ComposeWorldPositionsFromRequest(
	const FMetaAgentParticleActuationRequest& Request,
	TArray<FVector>& OutAppliedWorldPositions)
{
	if (!Request.BaselineWorldPositions || Request.BaselineWorldPositions->Num() <= 0)
	{
		OutAppliedWorldPositions.Reset();
		return 0;
	}

	MetaAgentTypeBridge::FMetaAgentActuationComposeScratch ComposeScratch;
	MetaAgentTypeBridge::build_actuation_compose_input(Request, ComposeScratch);

	const int32 ParticleCount = Request.BaselineWorldPositions->Num();
	OutAppliedWorldPositions.Reset();
	OutAppliedWorldPositions.Reserve(ParticleCount);

	int32 AppliedCount = 0;
	for (int32 GlobalIndex = 0; GlobalIndex < ParticleCount; ++GlobalIndex)
	{
		const metaagent::particle::ActuationComposeResult Composed =
			metaagent::particle::ActuationMath::compose_particle_world_position(
				ComposeScratch.input,
				GlobalIndex);
		if (Composed.valid)
		{
			OutAppliedWorldPositions.Add(MetaAgentParticleActuationInternal::ApplyStateEffectOffset(
				Request,
				GlobalIndex,
				FVector(Composed.world_position.x, Composed.world_position.y, Composed.world_position.z)));
		}
		else
		{
			OutAppliedWorldPositions.Add((*Request.BaselineWorldPositions)[GlobalIndex]);
		}
		++AppliedCount;
	}

	return AppliedCount;
}

FVector FMetaAgentParticleActuation::SolveFormingPosition(FMetaAgentParticleFormingContext& Context)
{
	metaagent::particle::FormingSettings CoreSettings;
	if (Context.Settings)
	{
		MetaAgentTypeBridge::copy_forming_settings_to_core(*Context.Settings, CoreSettings);
	}

	metaagent::particle::FormingContext CoreContext;
	CoreContext.global_index = Context.GlobalIndex;
	CoreContext.total_particle_count = Context.TotalParticleCount;
	CoreContext.baseline = metaagent::core::Vec3(Context.Baseline.X, Context.Baseline.Y, Context.Baseline.Z);
	CoreContext.target = metaagent::core::Vec3(Context.Target.X, Context.Target.Y, Context.Target.Z);
	CoreContext.pattern_center = metaagent::core::Vec3(
		Context.PatternCenter.X,
		Context.PatternCenter.Y,
		Context.PatternCenter.Z);
	CoreContext.blend_alpha = Context.BlendAlpha;
	CoreContext.state_elapsed_seconds = Context.StateElapsedSeconds;
	CoreContext.form_duration_seconds = Context.FormDurationSeconds;
	CoreContext.delta_time_seconds = Context.DeltaTimeSeconds;
	CoreContext.forming_steering_weight = Context.FormingSteeringWeight;
	CoreContext.forming_steering_offset = metaagent::core::Vec3(
		Context.FormingSteeringOffset.X,
		Context.FormingSteeringOffset.Y,
		Context.FormingSteeringOffset.Z);
	CoreContext.settings = Context.Settings ? &CoreSettings : nullptr;

	const metaagent::core::Vec3 Result =
		metaagent::particle::FormingSolverRegistry::solve_forming_position(CoreContext);
	return FVector(Result.x, Result.y, Result.z);
}
