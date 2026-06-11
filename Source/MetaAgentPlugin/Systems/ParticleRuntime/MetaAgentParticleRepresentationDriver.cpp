// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleRepresentationDriver.h"

#include "NiagaraComponent.h"
#include "Systems/ParticleRuntime/MetaAgentNiagaraSystemProfile.h"
#include "Systems/ParticleRuntime/MetaAgentNiagaraTargetData.h"
#include "Systems/ParticleRuntime/MetaAgentParticleActuation.h"

namespace MetaAgentRepresentationDriverInternal
{
	static const FName DirectDriverId(TEXT("DirectPosition"));
	static const FName ParameterDriverId(TEXT("NiagaraParameters"));

	void PushTargetArrays(
		UNiagaraComponent& NiagaraComponent,
		const UMetaAgentNiagaraSystemProfile* Profile,
		const FMetaAgentParticleRepresentationFrame& Frame,
		UMetaAgentNiagaraTargetData* SharedTargetData)
	{
		if (!Profile || !Profile->HasCapability(EMetaAgentNiagaraDriverCapability::TargetArrayUpload))
		{
			return;
		}

		const int32 TargetCount = Frame.PatternWorldTargets.Num();
		if (!Profile->TargetCountParameterName.IsNone())
		{
			NiagaraComponent.SetVariableInt(Profile->TargetCountParameterName, TargetCount);
		}

		if (!Profile->TargetDataParameterName.IsNone() && SharedTargetData)
		{
			NiagaraComponent.SetVariableObject(Profile->TargetDataParameterName, SharedTargetData);
		}
	}

	class FMetaAgentDirectPositionRepresentationDriver final : public IMetaAgentParticleRepresentationDriver
	{
	public:
		virtual FName GetDriverId() const override { return DirectDriverId; }
		virtual EMetaAgentParticleActuationMode GetActuationMode() const override
		{
			return EMetaAgentParticleActuationMode::Direct;
		}

		virtual bool SupportsComponent(
			const UNiagaraComponent& NiagaraComponent,
			const UMetaAgentNiagaraSystemProfile* Profile) const override
		{
			return NiagaraComponent.IsActive()
				&& (!Profile || Profile->HasCapability(EMetaAgentNiagaraDriverCapability::DirectPositionWrite));
		}

		virtual int32 ApplyFrame(
			const FMetaAgentParticleRepresentationFrame& Frame,
			FMetaAgentParticleActuationRequest& Request,
			const UMetaAgentNiagaraSystemProfile* Profile,
			TArray<FVector>& OutAppliedWorldPositions) const override
		{
			return FMetaAgentParticleActuation::ApplyDirect(Request, OutAppliedWorldPositions);
		}
	};

	class FMetaAgentParameterRepresentationDriver final : public IMetaAgentParticleRepresentationDriver
	{
	public:
		virtual FName GetDriverId() const override { return ParameterDriverId; }
		virtual EMetaAgentParticleActuationMode GetActuationMode() const override
		{
			return EMetaAgentParticleActuationMode::Parameters;
		}

		virtual bool SupportsComponent(
			const UNiagaraComponent& NiagaraComponent,
			const UMetaAgentNiagaraSystemProfile* Profile) const override
		{
			if (!NiagaraComponent.IsActive())
			{
				return false;
			}

			if (!Profile)
			{
				return true;
			}

			FString MissingParameter;
			return Profile->ValidateComponent(const_cast<UNiagaraComponent*>(&NiagaraComponent), MissingParameter);
		}

		virtual int32 ApplyFrame(
			const FMetaAgentParticleRepresentationFrame& Frame,
			FMetaAgentParticleActuationRequest& Request,
			const UMetaAgentNiagaraSystemProfile* Profile,
			TArray<FVector>& OutAppliedWorldPositions) const override
		{
			(void)OutAppliedWorldPositions;
			FMetaAgentParticleActuation::ApplyParameters(Request);
			return 0;
		}
	};

	void PushTargetPayloadToComponents(
		const FMetaAgentParticleActuationRequest& Request,
		const UMetaAgentNiagaraSystemProfile* Profile,
		const FMetaAgentParticleRepresentationFrame& Frame,
		UMetaAgentNiagaraTargetData* SharedTargetData)
	{
		for (const TWeakObjectPtr<UNiagaraComponent>& WeakComponent : Request.TrackedComponents)
		{
			if (UNiagaraComponent* NiagaraComponent = WeakComponent.Get())
			{
				PushTargetArrays(*NiagaraComponent, Profile, Frame, SharedTargetData);
			}
		}
	}
}

TArray<TUniquePtr<IMetaAgentParticleRepresentationDriver>>&
FMetaAgentParticleRepresentationDriverRegistry::GetDrivers()
{
	static TArray<TUniquePtr<IMetaAgentParticleRepresentationDriver>> Drivers;
	return Drivers;
}

void FMetaAgentParticleRepresentationDriverRegistry::RegisterDefaults()
{
	TArray<TUniquePtr<IMetaAgentParticleRepresentationDriver>>& Drivers = GetDrivers();
	if (Drivers.Num() > 0)
	{
		return;
	}

	using namespace MetaAgentRepresentationDriverInternal;
	Drivers.Add(MakeUnique<FMetaAgentDirectPositionRepresentationDriver>());
	Drivers.Add(MakeUnique<FMetaAgentParameterRepresentationDriver>());
}

void FMetaAgentParticleRepresentationDriverRegistry::RegisterDriver(
	TUniquePtr<IMetaAgentParticleRepresentationDriver> Driver)
{
	RegisterDefaults();
	if (Driver)
	{
		GetDrivers().Add(MoveTemp(Driver));
	}
}

const IMetaAgentParticleRepresentationDriver& FMetaAgentParticleRepresentationDriverRegistry::ResolveDriver(
	const EMetaAgentParticleActuationMode EffectiveMode)
{
	RegisterDefaults();

	const EMetaAgentParticleActuationMode SanitizedMode =
		EffectiveMode == EMetaAgentParticleActuationMode::Direct
			? EMetaAgentParticleActuationMode::Direct
			: EMetaAgentParticleActuationMode::Parameters;

	for (const TUniquePtr<IMetaAgentParticleRepresentationDriver>& Driver : GetDrivers())
	{
		if (Driver && Driver->GetActuationMode() == SanitizedMode)
		{
			return *Driver;
		}
	}

	checkf(GetDrivers().Num() > 0 && GetDrivers()[0], TEXT("MetaAgent representation drivers were not registered."));
	return *GetDrivers()[0];
}

void FMetaAgentParticleRepresentationDriverRegistry::BuildActuationRequestFromFrame(
	const FMetaAgentParticleRepresentationFrame& Frame,
	FMetaAgentParticleActuationRequest& OutRequest)
{
	OutRequest.BlendAlpha = Frame.Phase.BlendAlpha;
	OutRequest.HoldPulseScale = Frame.Phase.Emphasis;
	OutRequest.PatternCenter = Frame.PatternCenter;
	OutRequest.bPatternActive = Frame.bPatternActive;
	OutRequest.bUseReturnHoldBlend = Frame.bUseReturnHoldBlend;
	OutRequest.PatternState = Frame.PatternState;
	OutRequest.bAnticipatingMotion = Frame.bAnticipatingMotion;
	OutRequest.bDissipatingMotion = Frame.bDissipatingMotion;
	OutRequest.AnticipationElapsedSeconds = Frame.AnticipationElapsedSeconds;
	OutRequest.AnticipationAmplitudeCm = Frame.AnticipationAmplitudeCm;
	OutRequest.AnticipationFrequencyHz = Frame.AnticipationFrequencyHz;
	OutRequest.AnticipationIdleBlendDurationSeconds = Frame.AnticipationIdleBlendDurationSeconds;
	OutRequest.AnticipationHandoffElapsedSeconds = Frame.AnticipationHandoffElapsedSeconds;
	OutRequest.FormingAnticipationCarryoverDurationSeconds = Frame.FormingAnticipationCarryoverDurationSeconds;
	OutRequest.DissipateVisibility = Frame.DissipateVisibility;
	OutRequest.FormingStateElapsedSeconds = Frame.FormingStateElapsedSeconds;
	OutRequest.FormingDurationSeconds = Frame.FormingDurationSeconds;
	OutRequest.FormingDeltaTimeSeconds = Frame.FormingDeltaTimeSeconds;
	OutRequest.FormingSteeringWeight = Frame.FormingSteeringWeight;

	OutRequest.BaselineWorldPositions = &Frame.BaselineWorldPositions;
	OutRequest.PatternWorldTargets = &Frame.PatternWorldTargets;

	if (Frame.bUseReturnHoldBlend)
	{
		OutRequest.ReturnHoldPositions = &Frame.ReturnHoldPositions;
		OutRequest.ReturnRestPositions = &Frame.ReturnRestPositions;
	}

	if (Frame.bDissipatingMotion)
	{
		OutRequest.DissipateStartPositions = &Frame.DissipateStartPositions;
	}

	if (Frame.IdleBaselineWorldPositions.Num() > 0)
	{
		OutRequest.IdleBaselineWorldPositions = &Frame.IdleBaselineWorldPositions;
	}

	if (Frame.FormingSteeringOffsets.Num() > 0)
	{
		OutRequest.FormingSteeringOffsets = &Frame.FormingSteeringOffsets;
	}

	const bool bNeedsFormingSettings = Frame.bReturnUsesMotionSolver
		|| Frame.PatternState == EMetaAgentParticlePatternState::Forming
		|| Frame.PatternState == EMetaAgentParticlePatternState::Holding
		|| Frame.PatternState == EMetaAgentParticlePatternState::Returning;

	if (bNeedsFormingSettings)
	{
		OutRequest.FormingSettings = Frame.bReturnUsesMotionSolver
			? &Frame.ReturnMotionSettings
			: &Frame.FormingSettings;
	}
}

int32 FMetaAgentParticleRepresentationDriverRegistry::ApplyRepresentationFrame(
	const FMetaAgentParticleRepresentationFrame& Frame,
	FMetaAgentParticleActuationRequest& Request,
	const UMetaAgentNiagaraSystemProfile* Profile,
	const EMetaAgentParticleActuationMode ConfiguredMode,
	const float ReturnReleaseAuthorityThreshold,
	UMetaAgentNiagaraTargetData* SharedTargetData,
	TArray<FVector>& OutAppliedWorldPositions)
{
	using namespace MetaAgentRepresentationDriverInternal;

	const EMetaAgentParticleActuationMode PreferredMode = Profile
		? Profile->PreferredActuationMode
		: ConfiguredMode;

	const EMetaAgentParticleActuationMode EffectiveMode =
		FMetaAgentParticleActuation::ResolveEffectiveMode(PreferredMode);

	const IMetaAgentParticleRepresentationDriver& ParameterDriver =
		ResolveDriver(EMetaAgentParticleActuationMode::Parameters);
	const IMetaAgentParticleRepresentationDriver& DirectDriver =
		ResolveDriver(EMetaAgentParticleActuationMode::Direct);

	auto ApplyParametersPath = [&]()
	{
		ParameterDriver.ApplyFrame(Frame, Request, Profile, OutAppliedWorldPositions);
		PushTargetPayloadToComponents(Request, Profile, Frame, SharedTargetData);
	};

	if (Frame.bUseReturnHoldBlend && Frame.Phase.BlendAlpha <= ReturnReleaseAuthorityThreshold)
	{
		Request.bPatternActive = false;
		Request.BlendAlpha = 0.0f;
		ApplyParametersPath();
		return 0;
	}

	if (EffectiveMode == EMetaAgentParticleActuationMode::Parameters)
	{
		ApplyParametersPath();
		return 0;
	}

	const int32 AppliedCount = DirectDriver.ApplyFrame(Frame, Request, Profile, OutAppliedWorldPositions);

	if (ConfiguredMode == EMetaAgentParticleActuationMode::Hybrid)
	{
		ApplyParametersPath();
	}

	return AppliedCount;
}
