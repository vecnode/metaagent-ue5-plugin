// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h"

FMetaAgentParticlePatternConfig FMetaAgentParticlePatternConfig::MakeFromPreset(const EMetaAgentParticlePatternPreset Preset)
{
	FMetaAgentParticlePatternConfig Config;
	Config.ApplyPreset(Preset);
	return Config;
}

void FMetaAgentParticlePatternConfig::ApplyPreset(const EMetaAgentParticlePatternPreset Preset)
{
	ActivePreset = Preset;

	switch (Preset)
	{
	case EMetaAgentParticlePatternPreset::Slow:
		FormDurationSeconds = 3.0f;
		HoldDurationSeconds = 1.5f;
		ReturnDurationSeconds = 3.0f;
		break;
	case EMetaAgentParticlePatternPreset::Dramatic:
		FormDurationSeconds = 4.0f;
		HoldDurationSeconds = 2.0f;
		ReturnDurationSeconds = 5.0f;
		break;
	case EMetaAgentParticlePatternPreset::Sculpt:
		FormDurationSeconds = 1.6f;
		HoldDurationSeconds = 0.4f;
		ReturnDurationSeconds = 1.2f;
		break;
	case EMetaAgentParticlePatternPreset::Normal:
		FormDurationSeconds = 1.5f;
		HoldDurationSeconds = 0.5f;
		ReturnDurationSeconds = 1.5f;
		break;
	case EMetaAgentParticlePatternPreset::Custom:
	default:
		break;
	}
}

FString FMetaAgentParticlePatternConfig::GetPresetDisplayName() const
{
	switch (ActivePreset)
	{
	case EMetaAgentParticlePatternPreset::Slow:
		return TEXT("Slow");
	case EMetaAgentParticlePatternPreset::Dramatic:
		return TEXT("Dramatic");
	case EMetaAgentParticlePatternPreset::Sculpt:
		return TEXT("Sculpt");
	case EMetaAgentParticlePatternPreset::Custom:
		return TEXT("Custom");
	case EMetaAgentParticlePatternPreset::Normal:
	default:
		return TEXT("Normal");
	}
}
