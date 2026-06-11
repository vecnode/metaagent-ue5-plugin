// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleRepresentationTypes.h"

EMetaAgentRepresentationMacroPhase FMetaAgentParticleRepresentationMapping::MacroPhaseFromPatternState(
	const EMetaAgentParticlePatternState State)
{
	switch (State)
	{
	case EMetaAgentParticlePatternState::Preparing:
	case EMetaAgentParticlePatternState::Anticipating:
		return EMetaAgentRepresentationMacroPhase::Prepare;
	case EMetaAgentParticlePatternState::Forming:
		return EMetaAgentRepresentationMacroPhase::Express;
	case EMetaAgentParticlePatternState::Holding:
		return EMetaAgentRepresentationMacroPhase::Sustain;
	case EMetaAgentParticlePatternState::Returning:
	case EMetaAgentParticlePatternState::Dissipating:
		return EMetaAgentRepresentationMacroPhase::Release;
	case EMetaAgentParticlePatternState::Idle:
	default:
		return EMetaAgentRepresentationMacroPhase::Idle;
	}
}

FString FMetaAgentParticleRepresentationMapping::GetMacroPhaseDisplayName(
	const EMetaAgentRepresentationMacroPhase MacroPhase)
{
	switch (MacroPhase)
	{
	case EMetaAgentRepresentationMacroPhase::Prepare:
		return TEXT("Prepare");
	case EMetaAgentRepresentationMacroPhase::Express:
		return TEXT("Express");
	case EMetaAgentRepresentationMacroPhase::Sustain:
		return TEXT("Sustain");
	case EMetaAgentRepresentationMacroPhase::Release:
		return TEXT("Release");
	case EMetaAgentRepresentationMacroPhase::Idle:
	default:
		return TEXT("Idle");
	}
}
