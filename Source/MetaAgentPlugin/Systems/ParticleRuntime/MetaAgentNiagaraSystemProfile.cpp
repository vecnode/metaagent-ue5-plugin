// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentNiagaraSystemProfile.h"

#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"

namespace MetaAgentNiagaraProfileInternal
{
	static const FName PatternPhaseParameterName(TEXT("MetaAgentPatternPhase"));
	static const FName PatternCenterParameterName(TEXT("MetaAgentPatternCenter"));
	static const FName PatternActiveParameterName(TEXT("MetaAgentPatternActive"));
	static const FName PatternHoldScaleParameterName(TEXT("MetaAgentPatternHoldScale"));
	static const FName PatternDissipateActiveParameterName(TEXT("MetaAgentPatternDissipateActive"));
	static const FName PatternDissipateVisibilityParameterName(TEXT("MetaAgentPatternDissipateVisibility"));
	static const FName FormingModeParameterName(TEXT("MetaAgentFormingMode"));
	static const FName FormingArcLiftParameterName(TEXT("MetaAgentFormingArcLift"));
	static const FName FormingSpiralTurnsParameterName(TEXT("MetaAgentFormingSpiralTurns"));
}

bool UMetaAgentNiagaraSystemProfile::HasCapability(const EMetaAgentNiagaraDriverCapability Capability) const
{
	return (Capabilities & static_cast<int32>(Capability)) != 0;
}

bool UMetaAgentNiagaraSystemProfile::ComponentExposesUserParameter(
	const UNiagaraComponent& NiagaraComponent,
	const FName ParameterName)
{
	if (ParameterName.IsNone())
	{
		return false;
	}

	const UNiagaraSystem* NiagaraSystem = NiagaraComponent.GetAsset();
	if (!NiagaraSystem)
	{
		return false;
	}

	TArray<FNiagaraVariable> ExposedParameters;
	NiagaraSystem->GetExposedParameters().GetParameters(ExposedParameters);
	for (const FNiagaraVariable& ExposedParameter : ExposedParameters)
	{
		if (ExposedParameter.GetName() == ParameterName)
		{
			return true;
		}
	}
	return false;
}

bool UMetaAgentNiagaraSystemProfile::ValidateComponent(
	UNiagaraComponent* NiagaraComponent,
	FString& OutMissingParameter) const
{
	if (!NiagaraComponent)
	{
		OutMissingParameter = TEXT("<null component>");
		return false;
	}

	using namespace MetaAgentNiagaraProfileInternal;

	TArray<FName> ParametersToCheck = RequiredUserParameters;
	if (ParametersToCheck.Num() <= 0)
	{
		ParametersToCheck = {
			PatternPhaseParameterName,
			PatternCenterParameterName,
			PatternActiveParameterName,
			PatternHoldScaleParameterName,
			FormingModeParameterName
		};
	}

	for (const FName ParameterName : ParametersToCheck)
	{
		if (!ComponentExposesUserParameter(*NiagaraComponent, ParameterName))
		{
			OutMissingParameter = ParameterName.ToString();
			return false;
		}
	}

	if (HasCapability(EMetaAgentNiagaraDriverCapability::TargetArrayUpload)
		&& !TargetCountParameterName.IsNone()
		&& !ComponentExposesUserParameter(*NiagaraComponent, TargetCountParameterName))
	{
		OutMissingParameter = TargetCountParameterName.ToString();
		return false;
	}

	OutMissingParameter.Reset();
	return true;
}

const UMetaAgentNiagaraSystemProfile* UMetaAgentNiagaraSystemProfile::GetDefaultProfile()
{
	static TObjectPtr<UMetaAgentNiagaraSystemProfile> DefaultProfile;
	if (!DefaultProfile)
	{
		DefaultProfile = NewObject<UMetaAgentNiagaraSystemProfile>(
			GetTransientPackage(),
			NAME_None,
			RF_Transient | RF_Public);
		DefaultProfile->DisplayName = FText::FromString(TEXT("MetaAgent Default Niagara Profile"));
		DefaultProfile->RequiredUserParameters = {
			MetaAgentNiagaraProfileInternal::PatternPhaseParameterName,
			MetaAgentNiagaraProfileInternal::PatternCenterParameterName,
			MetaAgentNiagaraProfileInternal::PatternActiveParameterName,
			MetaAgentNiagaraProfileInternal::PatternHoldScaleParameterName,
			MetaAgentNiagaraProfileInternal::FormingModeParameterName
		};
	}
	return DefaultProfile;
}
