// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"

#include "Bridge/MetaAgentTypeBridge.h"
#include "Containers/StringConv.h"

#include "metaagent/particle/pattern_types.hpp"

FMetaAgentParticlePatternConfig FMetaAgentParticlePatternConfig::MakeFromPreset(const EMetaAgentParticlePatternPreset Preset)
{
	FMetaAgentParticlePatternConfig Config;
	Config.ApplyPreset(Preset);
	return Config;
}

void FMetaAgentParticlePatternConfig::ApplyPreset(const EMetaAgentParticlePatternPreset Preset)
{
	metaagent::particle::PatternConfig CoreConfig;
	CoreConfig.apply_preset(MetaAgentTypeBridge::to_core_pattern_preset(Preset));
	MetaAgentTypeBridge::copy_pattern_config_from_core(CoreConfig, *this);
}

FString FMetaAgentParticlePatternConfig::GetPresetDisplayName() const
{
	metaagent::particle::PatternConfig CoreConfig;
	CoreConfig.active_preset = MetaAgentTypeBridge::to_core_pattern_preset(ActivePreset);
	return FString(UTF8_TO_TCHAR(CoreConfig.get_preset_display_name().c_str()));
}
