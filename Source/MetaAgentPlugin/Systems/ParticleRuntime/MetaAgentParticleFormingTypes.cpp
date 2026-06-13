// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleFormingTypes.h"

#include "Bridge/MetaAgentTypeBridge.h"
#include "Containers/StringConv.h"

#include "metaagent/particle/forming_types.hpp"

EMetaAgentParticleFormingMode FMetaAgentParticleFormingSettings::SanitizeMode(const EMetaAgentParticleFormingMode Mode)
{
	return MetaAgentTypeBridge::from_core_forming_mode(
		metaagent::particle::FormingSettings::sanitize_mode(MetaAgentTypeBridge::to_core_forming_mode(Mode)));
}

FString FMetaAgentParticleFormingSettings::GetModeDisplayName() const
{
	metaagent::particle::FormingSettings CoreSettings;
	MetaAgentTypeBridge::copy_forming_settings_to_core(*this, CoreSettings);
	return FString(UTF8_TO_TCHAR(CoreSettings.get_mode_display_name().c_str()));
}

void FMetaAgentParticleFormingSettings::CycleMode()
{
	metaagent::particle::FormingSettings CoreSettings;
	MetaAgentTypeBridge::copy_forming_settings_to_core(*this, CoreSettings);
	CoreSettings.cycle_mode();
	MetaAgentTypeBridge::copy_forming_settings_from_core(CoreSettings, *this);
}
