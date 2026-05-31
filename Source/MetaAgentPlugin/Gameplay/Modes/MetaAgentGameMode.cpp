// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Modes/MetaAgentGameMode.h"
#include "UI/HUD/MetaAgentHUD.h"
#include "Gameplay/Characters/MetaAgentMHPlayer.h"
#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Systems/CharacterRuntime/MetaAgentCharacterRuntime.h"
#include "Core/MetaAgent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"

namespace
{
}

AMetaAgentGameMode::AMetaAgentGameMode()
{
	// Keep a native pawn as spawn fallback; placed actors are preferred (see RestartPlayer).
	DefaultPawnClass = AMetaAgentMHPlayer::StaticClass();
	PlayerControllerClass = AMetaAgentPlayerController::StaticClass();
	HUDClass = AMetaAgentHUD::StaticClass();
}

void AMetaAgentGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	// Always attempt possession logic for players when PIE starts.
	RestartPlayer(NewPlayer);
}

void AMetaAgentGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (!NewPlayer)
	{
		return;
	}

	FMetaAgentPlacedPawnSelectionConfig SelectionConfig;
	SelectionConfig.PreferredPlacedPawnClass = PreferredPlacedPawnClass;
	SelectionConfig.PreferredPlacedPawnName = PreferredPlacedPawnName;
	SelectionConfig.bRequireExactPreferredPawnName = bRequireExactPreferredPawnName;
	SelectionConfig.bRequireUniquePreferredPawnName = bRequireUniquePreferredPawnName;
	SelectionConfig.bAllowSpawnFallback = bAllowSpawnFallback;

	bool bShouldSpawnFallback = false;
	FMetaAgentCharacterRuntime::RunPlacedPawnPossessionSequence(
		NewPlayer,
		GetWorld(),
		SelectionConfig,
		bShouldSpawnFallback);

	if (bShouldSpawnFallback)
	{
		Super::RestartPlayer(NewPlayer);
	}
}

void AMetaAgentGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	EnsureAutoNavMeshBounds();
}

void AMetaAgentGameMode::EnsureAutoNavMeshBounds()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ANavMeshBoundsVolume* PreferredVolume = nullptr;
	ANavMeshBoundsVolume* FirstVolume = nullptr;

	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		ANavMeshBoundsVolume* Candidate = *It;
		if (!IsValid(Candidate))
		{
			continue;
		}

		if (!FirstVolume)
		{
			FirstVolume = Candidate;
		}

		if (!PreferredNavMeshBoundsName.IsEmpty())
		{
			const bool bNameMatch = Candidate->GetName().Equals(PreferredNavMeshBoundsName, ESearchCase::IgnoreCase)
				|| Candidate->GetFName().ToString().Equals(PreferredNavMeshBoundsName, ESearchCase::IgnoreCase);

			bool bLabelMatch = false;
#if WITH_EDITOR
			bLabelMatch = Candidate->GetActorLabel().Equals(PreferredNavMeshBoundsName, ESearchCase::IgnoreCase);
#endif

			if (bNameMatch || bLabelMatch)
			{
				PreferredVolume = Candidate;
				break;
			}
		}
	}

	ANavMeshBoundsVolume* SelectedVolume = PreferredVolume ? PreferredVolume : FirstVolume;
	if (SelectedVolume)
	{
		if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			NavigationSystem->OnNavigationBoundsUpdated(SelectedVolume);
			NavigationSystem->Build();

			UE_LOG(LogMetaAgent, Log,
				TEXT("MetaAgentGameMode: Using NavMeshBoundsVolume '%s' for navigation build.%s"),
				*GetNameSafe(SelectedVolume),
				PreferredVolume ? TEXT("") : TEXT(" (Preferred name not found, using first available volume.)"));
		}
		else
		{
			UE_LOG(LogMetaAgent, Error,
				TEXT("MetaAgentGameMode: NavigationSystemV1 unavailable while using existing NavMeshBoundsVolume '%s'."),
				*GetNameSafe(SelectedVolume));
		}

		return;
	}

	if (!bAutoCreateNavMeshBounds)
	{
		UE_LOG(LogMetaAgent, Warning,
			TEXT("MetaAgentGameMode: No NavMeshBoundsVolume found (preferred='%s') and auto-create is disabled."),
			*PreferredNavMeshBoundsName);
		return;
	}

	FVector SpawnLocation = FVector::ZeroVector;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			SpawnLocation = Pawn->GetActorLocation();
		}
	}

	ANavMeshBoundsVolume* NavBoundsVolume = World->SpawnActor<ANavMeshBoundsVolume>(
		ANavMeshBoundsVolume::StaticClass(),
		SpawnLocation,
		FRotator::ZeroRotator);

	if (!NavBoundsVolume)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("MetaAgentGameMode: Failed to spawn runtime NavMeshBoundsVolume."));
		return;
	}

	constexpr float DefaultBrushHalfExtent = 100.0f;
	const FVector SafeExtents(
		FMath::Max(AutoNavMeshExtentXY, 1000.0f),
		FMath::Max(AutoNavMeshExtentXY, 1000.0f),
		FMath::Max(AutoNavMeshExtentZ, 500.0f));

	NavBoundsVolume->SetActorScale3D(SafeExtents / DefaultBrushHalfExtent);

	if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		NavigationSystem->OnNavigationBoundsUpdated(NavBoundsVolume);
		NavigationSystem->Build();
		UE_LOG(LogMetaAgent, Log,
			TEXT("MetaAgentGameMode: Spawned runtime NavMeshBoundsVolume at %s with extents XY=%.0f Z=%.0f and triggered nav build."),
			*SpawnLocation.ToCompactString(),
			SafeExtents.X,
			SafeExtents.Z);
	}
	else
	{
		UE_LOG(LogMetaAgent, Error,
			TEXT("MetaAgentGameMode: NavigationSystemV1 unavailable after spawning NavMeshBoundsVolume."));
	}
}

