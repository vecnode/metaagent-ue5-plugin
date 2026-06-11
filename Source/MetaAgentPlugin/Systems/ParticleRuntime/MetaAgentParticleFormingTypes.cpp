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
	default:
		Mode = EMetaAgentParticleFormingMode::DirectLerp;
		break;
	}
}
