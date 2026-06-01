// Copyright (c) vecnode 2026. All Rights Reserved.

#include "Systems/NetworkingRuntime/MetaAgentGameInstance.h"
#include "Engine/World.h"

UMetaAgentGameInstance* UMetaAgentGameInstance::Get(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject))
	{
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	return Cast<UMetaAgentGameInstance>(World->GetGameInstance());
}

