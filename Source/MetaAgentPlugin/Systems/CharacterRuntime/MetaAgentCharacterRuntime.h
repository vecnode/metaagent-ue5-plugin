// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"

class ACharacter;
class AController;
class APawn;
class UWorld;

struct FMetaAgentInputFallbackState;
struct FMetaAgentMovementDiagnosticsState;

struct FMetaAgentPlacedPawnSelectionConfig
{
	TSoftClassPtr<APawn> PreferredPlacedPawnClass;
	FString PreferredPlacedPawnName = TEXT("MAIN_CHARACTER");
	bool bRequireExactPreferredPawnName = true;
	bool bRequireUniquePreferredPawnName = true;
	bool bAllowSpawnFallback = false;
};

class FMetaAgentCharacterRuntime
{
public:
	// Sequential module flow: resolves and possesses a placed pawn (or reports spawn fallback requirement).
	static void RunPlacedPawnPossessionSequence(
		AController* NewPlayer,
		UWorld* World,
		const FMetaAgentPlacedPawnSelectionConfig& Config,
		bool& bOutShouldSpawnFallback);

	// Sequential module flow: prepares possessed character mesh/anim runtime state and fallback locomotion bootstrap.
	static void RunPossessedCharacterBootstrapSequence(
		ACharacter* PossessedCharacter,
		FMetaAgentMovementDiagnosticsState& MovementDiagnostics,
		const FMetaAgentInputFallbackState& InputFallback);
};
