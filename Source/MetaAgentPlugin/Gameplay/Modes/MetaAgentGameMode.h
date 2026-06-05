// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/SoftObjectPtr.h"
#include "MetaAgentGameMode.generated.h"

/**
 *  Runtime game mode that possesses an existing placed character in the level.
 *  Falls back to spawning a new pawn only when no placed character is found.
 */
UCLASS()
class AMetaAgentGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	AMetaAgentGameMode();

protected:

	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void BeginPlay() override;
	void EnsureAutoNavMeshBounds();

	/** Default player pawn blueprint class to spawn and possess. */
	UPROPERTY(EditDefaultsOnly, Config, Category = "Player")
	TSoftClassPtr<APawn> DefaultPlayerPawnClass = TSoftClassPtr<APawn>(FSoftObjectPath(TEXT("/MetaAgentPlugin/BP_MH_PlayerChar.BP_MH_PlayerChar_C")));

	/** If true, spawn a large NavMeshBoundsVolume automatically at runtime. */
	UPROPERTY(EditDefaultsOnly, Config, Category = "Navigation")
	bool bAutoCreateNavMeshBounds = false;

	/** Preferred NavMeshBoundsVolume actor name/label to use and rebuild (for example: MAIN_NAV_MESH). */
	UPROPERTY(EditDefaultsOnly, Config, Category = "Navigation")
	FString PreferredNavMeshBoundsName = TEXT("MAIN_NAV_MESH");

	/** Half-extent in X/Y for the auto-created nav bounds volume (in Unreal units). */
	UPROPERTY(EditDefaultsOnly, Config, Category = "Navigation", meta = (ClampMin = "1000.0"))
	float AutoNavMeshExtentXY = 500000.0f;

	/** Half-extent in Z for the auto-created nav bounds volume (in Unreal units). */
	UPROPERTY(EditDefaultsOnly, Config, Category = "Navigation", meta = (ClampMin = "500.0"))
	float AutoNavMeshExtentZ = 100000.0f;
};




