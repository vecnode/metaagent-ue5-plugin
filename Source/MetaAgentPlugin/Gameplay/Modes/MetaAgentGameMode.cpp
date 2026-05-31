// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Modes/MetaAgentGameMode.h"
#include "UI/HUD/MetaAgentHUD.h"
#include "Gameplay/Characters/MetaAgentMHPlayer.h"
#include "Gameplay/Controllers/MetaAgentPlayerController.h"
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
	FString NormalizeNameForMatching(const FString& InName)
	{
		FString Out = InName;
		Out.TrimStartAndEndInline();
		Out.ToUpperInline();

		if (Out.StartsWith(TEXT("UEDPIE_")))
		{
			const int32 FirstUnderscore = Out.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 0);
			const int32 SecondUnderscore = (FirstUnderscore != INDEX_NONE)
				? Out.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FirstUnderscore + 1)
				: INDEX_NONE;

			if (SecondUnderscore != INDEX_NONE && (SecondUnderscore + 1) < Out.Len())
			{
				Out.RightChopInline(SecondUnderscore + 1, false);
			}
		}

		while (true)
		{
			int32 LastUnderscore = INDEX_NONE;
			if (!Out.FindLastChar(TEXT('_'), LastUnderscore))
			{
				break;
			}

			if ((LastUnderscore + 1) >= Out.Len())
			{
				break;
			}

			bool bAllDigits = true;
			for (int32 Index = LastUnderscore + 1; Index < Out.Len(); ++Index)
			{
				if (!FChar::IsDigit(Out[Index]))
				{
					bAllDigits = false;
					break;
				}
			}

			if (!bAllDigits)
			{
				break;
			}

			Out.LeftInline(LastUnderscore, false);
		}

		if (Out.EndsWith(TEXT("_C")))
		{
			Out.LeftChopInline(2, false);
		}

		return Out;
	}

	bool IsFlexibleNameMatch(const FString& Candidate, const FString& Preferred)
	{
		const FString CandidateNorm = NormalizeNameForMatching(Candidate);
		const FString PreferredNorm = NormalizeNameForMatching(Preferred);

		if (CandidateNorm.IsEmpty() || PreferredNorm.IsEmpty())
		{
			return false;
		}

		return CandidateNorm.Equals(PreferredNorm, ESearchCase::CaseSensitive)
			|| CandidateNorm.EndsWith(PreferredNorm, ESearchCase::CaseSensitive)
			|| CandidateNorm.Contains(PreferredNorm, ESearchCase::CaseSensitive);
	}

	bool IsSelectablePlacedPawn(const APawn* Pawn)
	{
		return IsValid(Pawn)
			&& !Pawn->IsA<ASpectatorPawn>();
	}

	bool CanControllerPossessPlacedPawn(const APawn* Pawn, const AController* RequestingController)
	{
		if (!IsValid(Pawn))
		{
			return false;
		}

		const AController* ExistingController = Pawn->GetController();
		if (!ExistingController)
		{
			return true;
		}

		if (ExistingController == RequestingController)
		{
			return true;
		}

		// Allow reclaiming pawns controlled by AI/non-player controllers after BP reparenting.
		return !ExistingController->IsPlayerController();
	}

	bool MatchesPreferredName(const APawn* Pawn, const FString& PreferredName)
	{
		if (!Pawn || PreferredName.IsEmpty())
		{
			return false;
		}

		if (IsFlexibleNameMatch(Pawn->GetName(), PreferredName)
			|| IsFlexibleNameMatch(Pawn->GetFName().ToString(), PreferredName))
		{
			return true;
		}

#if WITH_EDITOR
		if (IsFlexibleNameMatch(Pawn->GetActorLabel(), PreferredName))
		{
			return true;
		}
#endif

		return false;
	}
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

	if (APawn* ExistingPawn = NewPlayer->GetPawn())
	{
		if (!ExistingPawn->IsA<ASpectatorPawn>())
		{
			UE_LOG(LogMetaAgent, Log,
				TEXT("MetaAgentGameMode: Controller '%s' already has pawn '%s'. Keeping current possession."),
				*GetNameSafe(NewPlayer),
				*ExistingPawn->GetName());
			return;
		}
	}

	// Resolve optional class filter for placed pawn selection.
	UClass* PreferredClass = nullptr;
	if (!PreferredPlacedPawnClass.IsNull())
	{
		PreferredClass = PreferredPlacedPawnClass.LoadSynchronous();
	}

	APawn* NamedPawn = nullptr;
	int32 NamedPawnMatchCount = 0;
	APawn* PreferredClassPawn = nullptr;
	APawn* FirstUsablePawn = nullptr;

	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		APawn* Candidate = *It;
		if (!IsSelectablePlacedPawn(Candidate))
		{
			continue;
		}

		const bool bCanPossessCandidate = CanControllerPossessPlacedPawn(Candidate, NewPlayer);

		if (bCanPossessCandidate && MatchesPreferredName(Candidate, PreferredPlacedPawnName))
		{
			++NamedPawnMatchCount;
			if (!NamedPawn)
			{
				NamedPawn = Candidate;
			}
		}

		if (!PreferredClassPawn && bCanPossessCandidate && PreferredClass && Candidate->IsA(PreferredClass))
		{
			PreferredClassPawn = Candidate;
		}

		if (!FirstUsablePawn && bCanPossessCandidate)
		{
			FirstUsablePawn = Candidate;
		}
	}

	APawn* SelectedPawn = nullptr;

	if (bRequireExactPreferredPawnName && !PreferredPlacedPawnName.IsEmpty())
	{
		if (NamedPawnMatchCount == 1)
		{
			SelectedPawn = NamedPawn;
		}
		else if (NamedPawnMatchCount > 1 && bRequireUniquePreferredPawnName)
		{
			UE_LOG(LogMetaAgent, Error,
				TEXT("MetaAgentGameMode: Preferred pawn name '%s' is ambiguous (%d matches). Rename to a unique actor name, or disable unique-name enforcement."),
				*PreferredPlacedPawnName,
				NamedPawnMatchCount);

			if (bAllowSpawnFallback)
			{
				UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgentGameMode: Ambiguous preferred name. Using spawn fallback due to configuration."));
				Super::RestartPlayer(NewPlayer);
			}
			return;
		}
		else
		{
			AActor* MatchingNonPawnActor = nullptr;
			if (!PreferredPlacedPawnName.IsEmpty())
			{
				for (TActorIterator<AActor> It(GetWorld()); It; ++It)
				{
					AActor* CandidateActor = *It;
					if (CandidateActor && !Cast<APawn>(CandidateActor)
						&& IsFlexibleNameMatch(CandidateActor->GetName(), PreferredPlacedPawnName))
					{
						MatchingNonPawnActor = CandidateActor;
						break;
					}

#if WITH_EDITOR
					if (CandidateActor && !Cast<APawn>(CandidateActor)
						&& IsFlexibleNameMatch(CandidateActor->GetActorLabel(), PreferredPlacedPawnName))
					{
						MatchingNonPawnActor = CandidateActor;
						break;
					}
#endif
				}
			}

			if (MatchingNonPawnActor)
			{
				UE_LOG(LogMetaAgent, Error,
					TEXT("MetaAgentGameMode: Found actor '%s' for preferred name '%s', but class '%s' is not a Pawn/Character and cannot be possessed."),
					*GetNameSafe(MatchingNonPawnActor),
					*PreferredPlacedPawnName,
					*GetNameSafe(MatchingNonPawnActor->GetClass()));
			}

			UE_LOG(LogMetaAgent, Error,
				TEXT("MetaAgentGameMode: Required preferred pawn '%s' was not found. Place a pawn/character with this exact name, or disable strict name requirement."),
				*PreferredPlacedPawnName);

			if (bAllowSpawnFallback)
			{
				UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgentGameMode: Required preferred name missing. Using spawn fallback due to configuration."));
				Super::RestartPlayer(NewPlayer);
			}
			return;
		}
	}
	else
	{
		SelectedPawn = NamedPawn
			? NamedPawn
			: (PreferredClassPawn ? PreferredClassPawn : FirstUsablePawn);
	}

	if (SelectedPawn)
	{
		NewPlayer->Possess(SelectedPawn);
		UE_LOG(LogMetaAgent, Log,
			TEXT("MetaAgentGameMode: Possessed placed pawn '%s' (%s). Name='%s' (matches=%d) ClassFilter=%s"),
			*SelectedPawn->GetName(),
			*SelectedPawn->GetClass()->GetName(),
			*PreferredPlacedPawnName,
			NamedPawnMatchCount,
			*GetNameSafe(PreferredClass));
		return;
	}

	if (bAllowSpawnFallback)
	{
		UE_LOG(LogMetaAgent, Log,
			TEXT("MetaAgentGameMode: No placed pawn found (Name='%s'). Falling back to normal spawn."),
			*PreferredPlacedPawnName);
		Super::RestartPlayer(NewPlayer);
		return;
	}

	UE_LOG(LogMetaAgent, Error,
		TEXT("MetaAgentGameMode: No placed pawn found (Name='%s'). Place a Pawn/Character with that name, or enable spawn fallback."),
		*PreferredPlacedPawnName);
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

