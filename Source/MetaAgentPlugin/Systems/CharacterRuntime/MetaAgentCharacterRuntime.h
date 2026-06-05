// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"

class ACharacter;

struct FMetaAgentInputFallbackState;
struct FMetaAgentMovementDiagnosticsState;

class FMetaAgentCharacterRuntime
{
public:
	// Minimal runtime hook. The default character blueprint owns mesh and camera setup.
	static void RunPossessedCharacterBootstrapSequence(
		ACharacter* PossessedCharacter,
		FMetaAgentMovementDiagnosticsState& MovementDiagnostics,
		const FMetaAgentInputFallbackState& InputFallback);
};
