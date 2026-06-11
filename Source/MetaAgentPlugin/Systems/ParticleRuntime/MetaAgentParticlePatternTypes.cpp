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
		HoldPulseAmplitude = 0.05f;
		HoldPulseFrequencyHz = 0.6f;
		break;
	case EMetaAgentParticlePatternPreset::Dramatic:
		FormDurationSeconds = 4.0f;
		HoldDurationSeconds = 2.0f;
		ReturnDurationSeconds = 5.0f;
		HoldPulseAmplitude = 0.12f;
		HoldPulseFrequencyHz = 0.9f;
		break;
	case EMetaAgentParticlePatternPreset::Snappy:
		FormDurationSeconds = 0.7f;
		HoldDurationSeconds = 0.35f;
		ReturnDurationSeconds = 0.9f;
		HoldPulseAmplitude = 0.035f;
		HoldPulseFrequencyHz = 1.1f;
		Forming.Mode = EMetaAgentParticleFormingMode::StaggeredWave;
		Forming.WaveSpread = 0.35f;
		break;
	case EMetaAgentParticlePatternPreset::Dreamy:
		FormDurationSeconds = 5.0f;
		HoldDurationSeconds = 2.5f;
		ReturnDurationSeconds = 4.0f;
		HoldPulseAmplitude = 0.06f;
		HoldPulseFrequencyHz = 0.45f;
		Forming.Mode = EMetaAgentParticleFormingMode::SpringChase;
		Forming.SpringOvershoot = 0.14f;
		break;
	case EMetaAgentParticlePatternPreset::Normal:
		FormDurationSeconds = 1.5f;
		HoldDurationSeconds = 0.5f;
		ReturnDurationSeconds = 1.5f;
		HoldPulseAmplitude = 0.04f;
		HoldPulseFrequencyHz = 0.75f;
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
	case EMetaAgentParticlePatternPreset::Snappy:
		return TEXT("Snappy");
	case EMetaAgentParticlePatternPreset::Dreamy:
		return TEXT("Dreamy");
	case EMetaAgentParticlePatternPreset::Custom:
		return TEXT("Custom");
	case EMetaAgentParticlePatternPreset::Normal:
	default:
		return TEXT("Normal");
	}
}
