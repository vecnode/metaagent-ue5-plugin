// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Modes/MetaAgentGameMode.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Gameplay/Characters/MetaAgentMHPlayer.h"
#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"

namespace
{
}

AMetaAgentGameMode::AMetaAgentGameMode()
{
	// Keep a native pawn fallback in case the configured Blueprint class fails to load.
	DefaultPawnClass = AMetaAgentMHPlayer::StaticClass();
	if (UClass* LoadedDefaultPawnClass = DefaultPlayerPawnClass.LoadSynchronous())
	{
		DefaultPawnClass = LoadedDefaultPawnClass;
	}
	else
	{
		UE_LOG(LogMetaAgent, Warning,
			TEXT("MetaAgentGameMode: Failed to load DefaultPlayerPawnClass '%s'. Falling back to '%s'."),
			*DefaultPlayerPawnClass.ToString(),
			*GetNameSafe(DefaultPawnClass));
	}

	PlayerControllerClass = AMetaAgentPlayerController::StaticClass();
	HUDClass = AMetaAgentHUD::StaticClass();
}

void AMetaAgentGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AMetaAgentGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer || !GetWorld())
	{
		Super::RestartPlayer(NewPlayer);
		return;
	}

	APawn* PawnToSpawn = nullptr;

	// Try to find an already-placed player pawn in the level (prefer BP_MH_PlayerChar)
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		APawn* CandidatePawn = *It;
		if (!CandidatePawn || CandidatePawn->IsPlayerControlled())
		{
			continue;
		}

		// Prefer the one with the expected label
		if (CandidatePawn->GetActorLabel().Contains(TEXT("MA_PlayerStartPawn")))
		{
			PawnToSpawn = CandidatePawn;
			break;
		}

		// Fall back to any BP_MH_PlayerChar instance
		if (PawnToSpawn == nullptr && CandidatePawn->IsA<APawn>())
		{
			if (CandidatePawn->GetClass()->GetName().Contains(TEXT("BP_MH_PlayerChar")))
			{
				PawnToSpawn = CandidatePawn;
			}
		}
	}

	if (PawnToSpawn)
	{
		NewPlayer->Possess(PawnToSpawn);
		return;
	}

	// If no placed pawn found, spawn the default
	Super::RestartPlayer(NewPlayer);
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

