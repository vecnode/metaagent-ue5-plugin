// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/CharacterRuntime/MetaAgentCharacterRuntime.h"

#include "GameFramework/Character.h"

void FMetaAgentCharacterRuntime::RunPossessedCharacterBootstrapSequence(
	ACharacter* PossessedCharacter,
	FMetaAgentMovementDiagnosticsState& MovementDiagnostics,
	const FMetaAgentInputFallbackState& InputFallback)
{
	(void)MovementDiagnostics;
	(void)InputFallback;

	if (!PossessedCharacter)
	{
		return;
	}

	// Intentionally minimal: the default player blueprint (BP_MH_PlayerChar)
	// owns camera, mesh, and animation setup.
}
