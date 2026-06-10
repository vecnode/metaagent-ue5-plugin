// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"

class AMetaAgentPlayerController;
class UInputComponent;
class UMetaAgentParticleOrchestrator;

class METAAGENTPLUGIN_API FMetaAgentParticleInputRouter
{
public:
	static void BindKeyboardInput(
		AMetaAgentPlayerController* Controller,
		UInputComponent* InputComponent,
		UMetaAgentParticleOrchestrator* Orchestrator);

	static TArray<FString> GetParticleKeyHelpLines();
};
