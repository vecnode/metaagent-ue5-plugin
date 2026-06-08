// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticlePatternAsset.h"

FPrimaryAssetId UMetaAgentParticlePatternAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MetaAgentParticlePattern"), GetFName());
}
