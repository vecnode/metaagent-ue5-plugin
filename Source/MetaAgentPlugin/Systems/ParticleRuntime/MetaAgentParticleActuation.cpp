// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleActuation.h"

#include "Bridge/MetaAgentTypeBridge.h"
#include "metaagent/particle/actuation_math.hpp"
#include "metaagent/particle/forming_solver.hpp"
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

		const TArray<FVector>& BaselineWorldPositions = *Request.BaselineWorldPositions;
		const TArray<FVector>& PatternWorldTargets = *Request.PatternWorldTargets;
		const float BlendAlpha = Request.BlendAlpha;

		const bool bReturnBlend = Request.bUseReturnHoldBlend
			&& Request.ReturnHoldPositions != nullptr
			&& Request.ReturnHoldPositions->Num() > 0
			&& Request.ReturnRestPositions != nullptr
			&& Request.ReturnRestPositions->Num() > 0;

		const bool bUseFormingSolver = !bReturnBlend
			&& Request.PatternState == EMetaAgentParticlePatternState::Forming
			&& Request.FormingSettings != nullptr;

		const bool bAnticipatingMotion = !bReturnBlend
			&& Request.bAnticipatingMotion
			&& Request.PatternState == EMetaAgentParticlePatternState::Anticipating;

		const bool bDissipatingMotion = !bReturnBlend
			&& Request.bDissipatingMotion
			&& Request.PatternState == EMetaAgentParticlePatternState::Dissipating
			&& Request.DissipateStartPositions != nullptr;

		const int32 TotalParticleCount = BaselineWorldPositions.Num();

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
				if (!Request.ReturnHoldPositions->IsValidIndex(GlobalIndex)
					|| !Request.ReturnRestPositions->IsValidIndex(GlobalIndex))
				{
					continue;
				}

				const FVector HoldPos = (*Request.ReturnHoldPositions)[GlobalIndex];
				const FVector RestPos = (*Request.ReturnRestPositions)[GlobalIndex];

				const bool bReturnFormingSolver = Request.FormingSettings != nullptr
					&& Request.PatternState == EMetaAgentParticlePatternState::Returning;

				if (bReturnFormingSolver)
				{
					FMetaAgentParticleFormingContext FormingContext;
					FormingContext.GlobalIndex = GlobalIndex;
					FormingContext.TotalParticleCount = TotalParticleCount;
					FormingContext.Baseline = RestPos;
					FormingContext.Target = HoldPos;
					FormingContext.PatternCenter = Request.PatternCenter;
					FormingContext.BlendAlpha = BlendAlpha;
					FormingContext.StateElapsedSeconds = Request.FormingStateElapsedSeconds;
					FormingContext.FormDurationSeconds = Request.FormingDurationSeconds;
					FormingContext.DeltaTimeSeconds = Request.FormingDeltaTimeSeconds;
					FormingContext.Settings = Request.FormingSettings;

					DesiredWorld = FMetaAgentParticleActuation::SolveFormingPosition(FormingContext);
				}
				else
				{
					DesiredWorld = FMath::Lerp(RestPos, HoldPos, BlendAlpha);
				}
			}
			else
			{
				if (!BaselineWorldPositions.IsValidIndex(GlobalIndex)
					|| !PatternWorldTargets.IsValidIndex(GlobalIndex))
				{
					continue;
				}

				if (bAnticipatingMotion)
				{
					DesiredWorld = FMetaAgentParticleActuation::ComputeAnticipationWorldPosition(
						BaselineWorldPositions[GlobalIndex],
						GlobalIndex,
						Request.PatternCenter,
						Request.AnticipationElapsedSeconds,
						Request.AnticipationAmplitudeCm,
						Request.AnticipationFrequencyHz,
						Request.AnticipationIdleBlendDurationSeconds);
				}
				else if (bDissipatingMotion)
				{
					if (!Request.DissipateStartPositions->IsValidIndex(GlobalIndex))
					{
						continue;
					}

					const FVector StartPosition = (*Request.DissipateStartPositions)[GlobalIndex];
					DesiredWorld = FMath::Lerp(
						StartPosition,
						Request.PatternCenter,
						FMath::Clamp(BlendAlpha, 0.0f, 1.0f));
				}
				else if (bUseFormingSolver)
				{
					FMetaAgentParticleFormingContext FormingContext;
					FormingContext.GlobalIndex = GlobalIndex;
					FormingContext.TotalParticleCount = TotalParticleCount;
					FormingContext.Baseline = BaselineWorldPositions[GlobalIndex];
					FormingContext.Target = PatternWorldTargets[GlobalIndex];
					FormingContext.PatternCenter = Request.PatternCenter;
					FormingContext.BlendAlpha = BlendAlpha;
					FormingContext.StateElapsedSeconds = Request.FormingStateElapsedSeconds;
					FormingContext.FormDurationSeconds = Request.FormingDurationSeconds;
					FormingContext.DeltaTimeSeconds = Request.FormingDeltaTimeSeconds;
					FormingContext.Settings = Request.FormingSettings;
					FormingContext.FormingSteeringWeight = Request.FormingSteeringWeight;
					if (Request.FormingSteeringOffsets != nullptr
						&& Request.FormingSteeringOffsets->IsValidIndex(GlobalIndex))
					{
						FormingContext.FormingSteeringOffset = (*Request.FormingSteeringOffsets)[GlobalIndex];
					}

					const FVector FormingWorld =
						FMetaAgentParticleActuation::SolveFormingPosition(FormingContext);

					const bool bUseAnticipationCarryover = Request.AnticipationHandoffElapsedSeconds >= 0.0f
						&& Request.IdleBaselineWorldPositions != nullptr
						&& Request.IdleBaselineWorldPositions->IsValidIndex(GlobalIndex)
						&& Request.FormingAnticipationCarryoverDurationSeconds > KINDA_SMALL_NUMBER
						&& Request.FormingStateElapsedSeconds < Request.FormingAnticipationCarryoverDurationSeconds;

					if (bUseAnticipationCarryover)
					{
						const float CarryoverNormalized = FMath::Clamp(
							Request.FormingStateElapsedSeconds / Request.FormingAnticipationCarryoverDurationSeconds,
							0.0f,
							1.0f);
						const float FormingWeight = CarryoverNormalized * CarryoverNormalized
							* (3.0f - 2.0f * CarryoverNormalized);
						const FVector ContinuingAnticipation = FMetaAgentParticleActuation::ComputeAnticipationWorldPosition(
							(*Request.IdleBaselineWorldPositions)[GlobalIndex],
							GlobalIndex,
							Request.PatternCenter,
							Request.AnticipationHandoffElapsedSeconds + Request.FormingStateElapsedSeconds,
							Request.AnticipationAmplitudeCm,
							Request.AnticipationFrequencyHz,
							Request.AnticipationIdleBlendDurationSeconds);
						DesiredWorld = FMath::Lerp(ContinuingAnticipation, FormingWorld, FormingWeight);
					}
					else
					{
						DesiredWorld = FormingWorld;
					}
				}
				else
				{
					DesiredWorld = FMath::Lerp(
						BaselineWorldPositions[GlobalIndex],
						PatternWorldTargets[GlobalIndex],
						BlendAlpha);

					if (Request.FormingSteeringWeight > KINDA_SMALL_NUMBER
						&& Request.FormingSteeringOffsets != nullptr
						&& Request.FormingSteeringOffsets->IsValidIndex(GlobalIndex))
					{
						DesiredWorld += (*Request.FormingSteeringOffsets)[GlobalIndex]
							* Request.FormingSteeringWeight
							* (1.0f - BlendAlpha);
					}
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
