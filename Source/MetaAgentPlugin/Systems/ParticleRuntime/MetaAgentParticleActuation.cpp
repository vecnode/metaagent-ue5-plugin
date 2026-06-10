// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleActuation.h"

#include "NiagaraComponent.h"
#include "Systems/ParticleRuntime/MetaAgentParticleActuatorTypes.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraDataSet.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraTypes.h"
#include "Engine/World.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

namespace MetaAgentParticleActuationInternal
{
	static const FName PositionAttributeName(TEXT("Position"));
	static const FName PatternPhaseParameterName(TEXT("MetaAgentPatternPhase"));
	static const FName PatternCenterParameterName(TEXT("MetaAgentPatternCenter"));
	static const FName PatternActiveParameterName(TEXT("MetaAgentPatternActive"));
	static const FName PatternHoldScaleParameterName(TEXT("MetaAgentPatternHoldScale"));

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
		const TArray<FVector>& BaselineWorldPositions,
		const TArray<FVector>& PatternWorldTargets,
		const int32 GlobalStartIndex,
		const int32 LocalParticleCount,
		const float BlendAlpha,
		const bool bUseReturnHoldBlend,
		const TArray<FVector>* ReturnHoldPositions,
		const TArray<FVector>* ReturnRestPositions,
		const TArray<FVector>* FormingSteeringOffsets,
		const float FormingSteeringWeight)
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

		const bool bReturnBlend = bUseReturnHoldBlend
			&& ReturnHoldPositions != nullptr
			&& ReturnHoldPositions->Num() > 0
			&& ReturnRestPositions != nullptr
			&& ReturnRestPositions->Num() > 0;

		for (int32 LocalIndex = 0; LocalIndex <= MaxLocalIndex; ++LocalIndex)
		{
			const int32 GlobalIndex = GlobalStartIndex + LocalIndex;

			FVector DesiredWorld = FVector::ZeroVector;
			if (PatternWorldTargets.IsValidIndex(GlobalIndex))
			{
				DesiredWorld = PatternWorldTargets[GlobalIndex];
			}
			if (bReturnBlend)
			{
				if (!ReturnHoldPositions->IsValidIndex(GlobalIndex)
					|| !ReturnRestPositions->IsValidIndex(GlobalIndex))
				{
					continue;
				}

				const FVector HoldPos = (*ReturnHoldPositions)[GlobalIndex];
				const FVector RestPos = (*ReturnRestPositions)[GlobalIndex];
				DesiredWorld = FMath::Lerp(RestPos, HoldPos, BlendAlpha);
			}
			else
			{
				if (!BaselineWorldPositions.IsValidIndex(GlobalIndex)
					|| !PatternWorldTargets.IsValidIndex(GlobalIndex))
				{
					continue;
				}

				DesiredWorld = FMath::Lerp(
					BaselineWorldPositions[GlobalIndex],
					PatternWorldTargets[GlobalIndex],
					BlendAlpha);

				if (FormingSteeringWeight > KINDA_SMALL_NUMBER
					&& FormingSteeringOffsets != nullptr
					&& FormingSteeringOffsets->IsValidIndex(GlobalIndex))
				{
					DesiredWorld += (*FormingSteeringOffsets)[GlobalIndex] * FormingSteeringWeight * (1.0f - BlendAlpha);
				}
			}

			const FNiagaraPosition SimPosition = WorldToNiagaraPosition(
				DesiredWorld,
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
					*Request.BaselineWorldPositions,
					*Request.PatternWorldTargets,
					GlobalStartIndex,
					LocalParticleCount,
					Request.BlendAlpha,
					Request.bUseReturnHoldBlend,
					Request.ReturnHoldPositions,
					Request.ReturnRestPositions,
					Request.FormingSteeringOffsets,
					Request.FormingSteeringWeight))
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
			*Request.BaselineWorldPositions,
			*Request.PatternWorldTargets,
			GlobalStartIndex,
			LocalParticleCount,
			Request.BlendAlpha,
			Request.bUseReturnHoldBlend,
			Request.ReturnHoldPositions,
			Request.ReturnRestPositions,
			Request.FormingSteeringOffsets,
			Request.FormingSteeringWeight))
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
	switch (ConfiguredMode)
	{
	case EMetaAgentParticleActuationMode::Direct:
		return EMetaAgentParticleActuationMode::Direct;
	case EMetaAgentParticleActuationMode::Parameters:
		return EMetaAgentParticleActuationMode::Parameters;
	case EMetaAgentParticleActuationMode::Hybrid:
#if WITH_EDITOR
		return EMetaAgentParticleActuationMode::Direct;
#else
		return EMetaAgentParticleActuationMode::Parameters;
#endif
	default:
		return EMetaAgentParticleActuationMode::Direct;
	}
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
	}
}
