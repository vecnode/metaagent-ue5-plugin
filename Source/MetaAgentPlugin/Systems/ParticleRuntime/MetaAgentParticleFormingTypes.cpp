// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleFormingTypes.h"

EMetaAgentParticleFormingMode FMetaAgentParticleFormingSettings::SanitizeMode(
	const EMetaAgentParticleFormingMode Mode)
{
	switch (Mode)
	{
	case EMetaAgentParticleFormingMode::DirectLerp:
	case EMetaAgentParticleFormingMode::ArcLift:
	case EMetaAgentParticleFormingMode::SpiralIn:
	case EMetaAgentParticleFormingMode::StaggeredWave:
	case EMetaAgentParticleFormingMode::SpringChase:
		return Mode;
	default:
		return EMetaAgentParticleFormingMode::DirectLerp;
	}
}

FString FMetaAgentParticleFormingSettings::GetModeDisplayName() const
{
	switch (SanitizeMode(Mode))
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
	default:
		return TEXT("Direct Lerp");
	}
}

void FMetaAgentParticleFormingSettings::CycleMode()
{
	switch (SanitizeMode(Mode))
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
	default:
		Mode = EMetaAgentParticleFormingMode::DirectLerp;
		break;
	}
}
