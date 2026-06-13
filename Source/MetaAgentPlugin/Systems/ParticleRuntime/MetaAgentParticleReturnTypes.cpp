// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleReturnTypes.h"

#include "Bridge/MetaAgentTypeBridge.h"
#include "Containers/StringConv.h"
#include "Curves/CurveFloat.h"

#include "metaagent/particle/return_types.hpp"

EMetaAgentParticleReturnMode FMetaAgentParticleReturnSettings::SanitizeMode(const EMetaAgentParticleReturnMode Mode)
{
	return MetaAgentTypeBridge::from_core_return_mode(
		metaagent::particle::ReturnSettings::sanitize_mode(MetaAgentTypeBridge::to_core_return_mode(Mode)));
}

bool FMetaAgentParticleReturnSettings::UsesMotionSolver() const
{
	metaagent::particle::ReturnSettings CoreSettings;
	MetaAgentTypeBridge::copy_return_settings_to_core(*this, CoreSettings);
	return CoreSettings.uses_motion_solver();
}

FMetaAgentParticleFormingSettings FMetaAgentParticleReturnSettings::AsFormingSettings() const
{
	metaagent::particle::ReturnSettings CoreSettings;
	MetaAgentTypeBridge::copy_return_settings_to_core(*this, CoreSettings);
	const metaagent::particle::FormingSettings CoreForming = CoreSettings.as_forming_settings();

	FMetaAgentParticleFormingSettings Out;
	MetaAgentTypeBridge::copy_forming_settings_from_core(CoreForming, Out);
	return Out;
}

FString FMetaAgentParticleReturnSettings::GetModeDisplayName() const
{
	metaagent::particle::ReturnSettings CoreSettings;
	MetaAgentTypeBridge::copy_return_settings_to_core(*this, CoreSettings);
	return FString(UTF8_TO_TCHAR(CoreSettings.get_mode_display_name().c_str()));
}

const UCurveFloat* FMetaAgentParticleReturnSettings::GetReturnCurveForMode() const
{
	switch (SanitizeMode(Mode))
	{
	case EMetaAgentParticleReturnMode::ArcLift:
		return ArcLiftReturnCurve;
	case EMetaAgentParticleReturnMode::SpiralIn:
		return SpiralInReturnCurve;
	case EMetaAgentParticleReturnMode::DirectLerp:
	default:
		return DirectLerpReturnCurve;
	}
}

void FMetaAgentParticleReturnSettings::CycleMode()
{
	metaagent::particle::ReturnSettings CoreSettings;
	MetaAgentTypeBridge::copy_return_settings_to_core(*this, CoreSettings);
	CoreSettings.cycle_mode();
	MetaAgentTypeBridge::copy_return_settings_from_core(CoreSettings, *this);
}
