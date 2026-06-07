// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleRuntime.h"

#include "NiagaraComponent.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraTypes.h"
#include "Core/MetaAgent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

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
		case EMetaAgentParticlePatternState::Forming: return TEXT("Forming");
		case EMetaAgentParticlePatternState::Holding: return TEXT("Holding");
		case EMetaAgentParticlePatternState::Returning: return TEXT("Returning");
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

	bool FindPositionFloatComponents(
		const FNiagaraDataSetCompiledData& CompiledData,
		int32& OutFloatComponentStart,
		int32& OutNumFloatComponents)
	{
		const int32 VariableIndex = CompiledData.Variables.IndexOfByPredicate(
			[](const FNiagaraVariable& Variable)
			{
				return Variable.GetName() == MetaAgentParticleCapture::PositionAttributeName;
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
		const TArray<FVector>& BaselineWorldPositions,
		const TArray<FVector>& PatternWorldTargets,
		const float BlendAlpha)
	{
		int32 FloatComponentStart = INDEX_NONE;
		int32 NumFloatComponents = 0;
		if (!FindPositionFloatComponents(CompiledData, FloatComponentStart, NumFloatComponents))
		{
			return false;
		}

		const uint32 NumInstances = DataBuffer.GetNumInstances();
		if (NumInstances == 0 || BaselineWorldPositions.Num() == 0 || PatternWorldTargets.Num() == 0)
		{
			return false;
		}

		const int32 MaxParticleIndex = FMath::Min3(
			static_cast<int32>(NumInstances),
			BaselineWorldPositions.Num(),
			PatternWorldTargets.Num()) - 1;

		for (int32 ParticleIndex = 0; ParticleIndex <= MaxParticleIndex; ++ParticleIndex)
		{
			const FVector DesiredWorld = FMath::Lerp(
				BaselineWorldPositions[ParticleIndex],
				PatternWorldTargets[ParticleIndex],
				BlendAlpha);

			const FNiagaraPosition SimPosition = WorldToNiagaraPosition(
				DesiredWorld,
				EmitterInstance,
				NiagaraComponent,
				SystemInstance);

			*DataBuffer.GetInstancePtrFloat(FloatComponentStart + 0, ParticleIndex) = SimPosition.X;
			*DataBuffer.GetInstancePtrFloat(FloatComponentStart + 1, ParticleIndex) = SimPosition.Y;
			*DataBuffer.GetInstancePtrFloat(FloatComponentStart + 2, ParticleIndex) = SimPosition.Z;
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

	void CaptureEmitterPositions(
		FNiagaraEmitterInstance& EmitterInstance,
		const UNiagaraComponent& NiagaraComponent,
		const FNiagaraSystemInstance& SystemInstance,
		TArray<FVector>& OutPositions)
	{
		if (FNiagaraComputeExecutionContext* GPUContext = EmitterInstance.GetGPUContext())
		{
#if WITH_EDITOR
			if (FNiagaraDataSet* GPUDataSet = GPUContext->MainDataSet)
			{
				FScopedNiagaraDataSetGPUReadback ScopedGPUReadback;
				ScopedGPUReadback.ReadbackData(SystemInstance.GetComputeDispatchInterface(), GPUDataSet);
				if (ScopedGPUReadback.GetNumInstances() > 0)
				{
					ExtractPositionsFromDataSet(*GPUDataSet, EmitterInstance, NiagaraComponent, SystemInstance, OutPositions);
				}
			}
#endif
			return;
		}

		ExtractPositionsFromDataSet(EmitterInstance.GetParticleData(), EmitterInstance, NiagaraComponent, SystemInstance, OutPositions);
	}

	void ApplyPatternToEmitter(
		FNiagaraEmitterInstance& EmitterInstance,
		UNiagaraComponent& NiagaraComponent,
		FNiagaraSystemInstance& SystemInstance,
		const TArray<FVector>& BaselineWorldPositions,
		const TArray<FVector>& PatternWorldTargets,
		const float BlendAlpha,
		TArray<FVector>& OutAppliedWorldPositions)
	{
		const FNiagaraDataSetCompiledData& CompiledData = EmitterInstance.GetParticleData().GetCompiledData();

		if (FNiagaraComputeExecutionContext* GPUContext = EmitterInstance.GetGPUContext())
		{
#if WITH_EDITOR
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
					BaselineWorldPositions,
					PatternWorldTargets,
					BlendAlpha))
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
				FlushRenderingCommands();

				ExtractPositionsFromDataSet(*GPUDataSet, EmitterInstance, NiagaraComponent, SystemInstance, OutAppliedWorldPositions);
			}
#endif
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
			BaselineWorldPositions,
			PatternWorldTargets,
			BlendAlpha))
		{
			return;
		}

		ExtractPositionsFromDataSet(EmitterInstance.GetParticleData(), EmitterInstance, NiagaraComponent, SystemInstance, OutAppliedWorldPositions);
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

	DiscoveryFrameCounter++;
	const int32 DiscoveryInterval = FMath::Max(1, DiscoveryEveryNFrames);
	if (DiscoveryFrameCounter >= DiscoveryInterval)
	{
		DiscoveryFrameCounter = 0;
		DiscoverNiagaraComponents(false);
	}

	BuildComponentSnapshot();

	TickPatternRuntime(DeltaTimeSeconds);

	if (PatternRuntime.State != EMetaAgentParticlePatternState::Idle)
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
		const FName SourceActorName = OwnerActor ? OwnerActor->GetFName() : NAME_None;
		const FName SourceComponentName = NiagaraComponent->GetFName();

		SubmitExportedParticlePositions(CapturedPositions, SourceActorName, SourceComponentName);

		if (!bLoggedDirectCaptureSuccess)
		{
			bLoggedDirectCaptureSuccess = true;
			UE_LOG(LogMetaAgent, Log,
				TEXT("ParticleRuntime: direct C++ capture read %d position(s) from component '%s' (emitters report %d particle(s))."),
				CapturedPositions.Num(),
				*SourceComponentName.ToString(),
				TotalReportedParticles);
		}
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
	if (PatternRuntime.State != EMetaAgentParticlePatternState::Idle)
	{
		return false;
	}

	if (LatestSnapshot.ExportedParticlePositions.Num() <= 0)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("ParticleRuntime: cannot start square pattern without captured particle positions."));
		return false;
	}

	PatternRuntime.BaselineWorldPositions = LatestSnapshot.ExportedParticlePositions;
	BuildSquarePatternTargets();

	PatternRuntime.State = EMetaAgentParticlePatternState::Forming;
	PatternRuntime.Phase = 0.0f;
	PatternRuntime.StateElapsedSeconds = 0.0f;
	bLoggedPatternStart = false;

	UE_LOG(LogMetaAgent, Log,
		TEXT("ParticleRuntime: square pattern started with %d particle(s), grid columns=%d, spacing=%.1f."),
		PatternRuntime.BaselineWorldPositions.Num(),
		PatternRuntime.PatternColumns,
		PatternGridSpacingCm);

	return true;
}

bool UMetaAgentParticleRuntime::IsPatternActive() const
{
	return PatternRuntime.State != EMetaAgentParticlePatternState::Idle;
}

FString UMetaAgentParticleRuntime::GetPatternStateDisplayName() const
{
	return PatternStateToString(PatternRuntime.State);
}

FString UMetaAgentParticleRuntime::BuildPatternStatusText() const
{
	return FString::Printf(
		TEXT("Pattern State: %s | Phase: %.2f | Particles: %d"),
		*GetPatternStateDisplayName(),
		PatternRuntime.Phase,
		PatternRuntime.BaselineWorldPositions.Num());
}

void UMetaAgentParticleRuntime::ResetPatternRuntime()
{
	PatternRuntime = FMetaAgentParticlePatternRuntime();
	bLoggedPatternStart = false;
}

void UMetaAgentParticleRuntime::BuildSquarePatternTargets()
{
	const TArray<FVector>& Baseline = PatternRuntime.BaselineWorldPositions;
	const int32 ParticleCount = Baseline.Num();
	PatternRuntime.PatternWorldTargets.Reset();

	if (ParticleCount <= 0)
	{
		PatternRuntime.PatternColumns = 0;
		PatternRuntime.PatternCenter = FVector::ZeroVector;
		return;
	}

	PatternRuntime.PatternColumns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(ParticleCount))));
	const int32 RowCount = FMath::DivideAndRoundUp(ParticleCount, PatternRuntime.PatternColumns);

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& Position : Baseline)
	{
		Centroid += Position;
	}
	Centroid /= static_cast<float>(ParticleCount);
	PatternRuntime.PatternCenter = Centroid;

	const FVector GridOrigin = Centroid - FVector(
		(PatternRuntime.PatternColumns - 1) * PatternGridSpacingCm * 0.5f,
		(RowCount - 1) * PatternGridSpacingCm * 0.5f,
		0.0f);

	PatternRuntime.PatternWorldTargets.SetNum(ParticleCount);
	for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
	{
		const int32 Column = ParticleIndex % PatternRuntime.PatternColumns;
		const int32 Row = ParticleIndex / PatternRuntime.PatternColumns;
		PatternRuntime.PatternWorldTargets[ParticleIndex] = GridOrigin + FVector(
			Column * PatternGridSpacingCm,
			Row * PatternGridSpacingCm,
			0.0f);
	}
}

void UMetaAgentParticleRuntime::TickPatternRuntime(const float DeltaTimeSeconds)
{
	if (PatternRuntime.State == EMetaAgentParticlePatternState::Idle)
	{
		return;
	}

	PatternRuntime.StateElapsedSeconds += FMath::Max(0.0f, DeltaTimeSeconds);

	switch (PatternRuntime.State)
	{
	case EMetaAgentParticlePatternState::Forming:
	{
		const float FormDuration = FMath::Max(0.1f, PatternFormDurationSeconds);
		PatternRuntime.Phase = SmoothStep01(PatternRuntime.StateElapsedSeconds / FormDuration);
		if (PatternRuntime.StateElapsedSeconds >= FormDuration)
		{
			PatternRuntime.State = EMetaAgentParticlePatternState::Holding;
			PatternRuntime.Phase = 1.0f;
			PatternRuntime.StateElapsedSeconds = 0.0f;
		}
		break;
	}
	case EMetaAgentParticlePatternState::Holding:
	{
		PatternRuntime.Phase = 1.0f;
		if (PatternRuntime.StateElapsedSeconds >= FMath::Max(0.0f, PatternHoldDurationSeconds))
		{
			PatternRuntime.State = EMetaAgentParticlePatternState::Returning;
			PatternRuntime.StateElapsedSeconds = 0.0f;
		}
		break;
	}
	case EMetaAgentParticlePatternState::Returning:
	{
		const float ReturnDuration = FMath::Max(0.1f, PatternReturnDurationSeconds);
		PatternRuntime.Phase = 1.0f - SmoothStep01(PatternRuntime.StateElapsedSeconds / ReturnDuration);
		if (PatternRuntime.StateElapsedSeconds >= ReturnDuration)
		{
			ResetPatternRuntime();
		}
		break;
	}
	default:
		break;
	}
}

void UMetaAgentParticleRuntime::ApplyPatternActuation()
{
	using namespace MetaAgentParticleCapture;

	if (PatternRuntime.BaselineWorldPositions.Num() <= 0 || PatternRuntime.PatternWorldTargets.Num() <= 0)
	{
		return;
	}

	const float BlendAlpha = FMath::Clamp(PatternRuntime.Phase, 0.0f, 1.0f);
	TArray<FVector> AppliedWorldPositions;

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
				PatternRuntime.BaselineWorldPositions,
				PatternRuntime.PatternWorldTargets,
				BlendAlpha,
				AppliedWorldPositions);
		}
	}

	if (AppliedWorldPositions.Num() > 0)
	{
		AActor* SourceActor = nullptr;
		FName SourceComponentName = NAME_None;
		if (UNiagaraComponent* FirstComponent = TrackedNiagaraComponents.Num() > 0 ? TrackedNiagaraComponents[0].Get() : nullptr)
		{
			SourceActor = FirstComponent->GetOwner();
			SourceComponentName = FirstComponent->GetFName();
		}

		SubmitExportedParticlePositions(
			AppliedWorldPositions,
			SourceActor ? SourceActor->GetFName() : NAME_None,
			SourceComponentName);

		if (!bLoggedPatternStart)
		{
			bLoggedPatternStart = true;
			UE_LOG(LogMetaAgent, Log,
				TEXT("ParticleRuntime: pattern actuation writing %d particle position(s), state=%s phase=%.2f."),
				AppliedWorldPositions.Num(),
				*GetPatternStateDisplayName(),
				PatternRuntime.Phase);
		}
	}
}
