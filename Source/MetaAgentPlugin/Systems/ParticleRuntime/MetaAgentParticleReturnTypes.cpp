// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleReturnTypes.h"

EMetaAgentParticleReturnMode FMetaAgentParticleReturnSettings::SanitizeMode(
	const EMetaAgentParticleReturnMode Mode)
{
	switch (Mode)
	{
	case EMetaAgentParticleReturnMode::DirectLerp:
	case EMetaAgentParticleReturnMode::ArcLift:
	case EMetaAgentParticleReturnMode::SpiralIn:
	case EMetaAgentParticleReturnMode::DissipateToCenter:
		return Mode;
	default:
		return EMetaAgentParticleReturnMode::DirectLerp;
	}
}

bool FMetaAgentParticleReturnSettings::UsesMotionSolver() const
{
	switch (SanitizeMode(Mode))
	{
	case EMetaAgentParticleReturnMode::ArcLift:
	case EMetaAgentParticleReturnMode::SpiralIn:
		return true;
	default:
		return false;
	}
}

FMetaAgentParticleFormingSettings FMetaAgentParticleReturnSettings::AsFormingSettings() const
{
	FMetaAgentParticleFormingSettings Out;
	Out.ArcLiftHeightCm = ArcLiftHeightCm;
	Out.SpiralTurns = SpiralTurns;

	switch (SanitizeMode(Mode))
	{
	case EMetaAgentParticleReturnMode::ArcLift:
		Out.Mode = EMetaAgentParticleFormingMode::ArcLift;
		break;
	case EMetaAgentParticleReturnMode::SpiralIn:
		Out.Mode = EMetaAgentParticleFormingMode::SpiralIn;
		break;
	case EMetaAgentParticleReturnMode::DirectLerp:
	case EMetaAgentParticleReturnMode::DissipateToCenter:
	default:
		Out.Mode = EMetaAgentParticleFormingMode::DirectLerp;
		break;
	}

	return Out;
}

FString FMetaAgentParticleReturnSettings::GetModeDisplayName() const
{
	switch (SanitizeMode(Mode))
	{
	case EMetaAgentParticleReturnMode::DirectLerp:
		return TEXT("Direct Lerp");
	case EMetaAgentParticleReturnMode::ArcLift:
		return TEXT("Arc Lift");
	case EMetaAgentParticleReturnMode::SpiralIn:
		return TEXT("Spiral In");
	case EMetaAgentParticleReturnMode::DissipateToCenter:
		return TEXT("Dissipate To Center");
	default:
		return TEXT("Direct Lerp");
	}
}

void FMetaAgentParticleReturnSettings::CycleMode()
{
	switch (SanitizeMode(Mode))
	{
	case EMetaAgentParticleReturnMode::DirectLerp:
		Mode = EMetaAgentParticleReturnMode::ArcLift;
		break;
	case EMetaAgentParticleReturnMode::ArcLift:
		Mode = EMetaAgentParticleReturnMode::SpiralIn;
		break;
	case EMetaAgentParticleReturnMode::SpiralIn:
		Mode = EMetaAgentParticleReturnMode::DissipateToCenter;
		break;
	case EMetaAgentParticleReturnMode::DissipateToCenter:
	default:
		Mode = EMetaAgentParticleReturnMode::DirectLerp;
		break;
	}
}
