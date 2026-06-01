// Copyright (c) vecnode 2026. All Rights Reserved.

#include "Gameplay/Characters/MetaAgentMHPlayer.h"
#include "Systems/NetworkingRuntime/MetaAgentGameInstance.h"
#include "Core/MetaAgent.h"
#include "Components/SkeletalMeshComponent.h"

AMetaAgentMHPlayer::AMetaAgentMHPlayer()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AMetaAgentMHPlayer::BeginPlay()
{
	Super::BeginPlay();

	if (UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
	{
		GI->ActivePlayerCharacter = this;
	}
}


