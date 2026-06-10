// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleFormingTypes.h"

FString FMetaAgentParticleFormingSettings::GetModeDisplayName() const
{
	switch (Mode)
	{
	case EMetaAgentParticleFormingMode::DirectLerp:
		return TEXT("Direct Lerp");
	case EMetaAgentParticleFormingMode::ArcLift:
		return TEXT("Arc Lift");
	case EMetaAgentParticleFormingMode::SpiralIn:
		return TEXT("Spiral In");
	case EMetaAgentParticleFormingMode::StaggeredWave:
		return TEXT("Staggered Wave");
	case EMetaAgentParticleFormingMode::SpringChase:
		return TEXT("Spring Chase");
	case EMetaAgentParticleFormingMode::NiagaraForces:
		return TEXT("Niagara Forces");
	default:
		return TEXT("Unknown");
	}
}

void FMetaAgentParticleFormingSettings::CycleMode()
{
	switch (Mode)
	{
	case EMetaAgentParticleFormingMode::DirectLerp:
		Mode = EMetaAgentParticleFormingMode::ArcLift;
		break;
	case EMetaAgentParticleFormingMode::ArcLift:
		Mode = EMetaAgentParticleFormingMode::SpiralIn;
		break;
	case EMetaAgentParticleFormingMode::SpiralIn:
		Mode = EMetaAgentParticleFormingMode::StaggeredWave;
		break;
	case EMetaAgentParticleFormingMode::StaggeredWave:
		Mode = EMetaAgentParticleFormingMode::SpringChase;
		break;
	case EMetaAgentParticleFormingMode::SpringChase:
		Mode = EMetaAgentParticleFormingMode::NiagaraForces;
		break;
	case EMetaAgentParticleFormingMode::NiagaraForces:
	default:
		Mode = EMetaAgentParticleFormingMode::DirectLerp;
		break;
	}
}
