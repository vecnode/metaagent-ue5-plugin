// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/CharacterRuntime/MetaAgentCharacterRuntime.h"

#include "Core/MetaAgent.h"
#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpectatorPawn.h"
#include "Engine/World.h"

namespace
{
	UAnimationAsset* GetEmergencyIdleAssetForBootstrap()
	{
		static UAnimationAsset* IdleAsset = nullptr;
		if (!IdleAsset)
		{
			IdleAsset = Cast<UAnimationAsset>(StaticLoadObject(UAnimationAsset::StaticClass(), nullptr,
				TEXT("/MetaAgentPlugin/Animations/Loco/MTN_N_Idle.MTN_N_Idle")));
		}

		return IdleAsset;
	}

	UAnimationAsset* GetEmergencyWalkAssetForBootstrap()
	{
		static UAnimationAsset* WalkAsset = nullptr;
		if (!WalkAsset)
		{
			WalkAsset = Cast<UAnimationAsset>(StaticLoadObject(UAnimationAsset::StaticClass(), nullptr,
				TEXT("/MetaAgentPlugin/Animations/Loco/MTN_N_Walk_InPlace.MTN_N_Walk_InPlace")));
		}

		return WalkAsset;
	}

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
				Out.RightChopInline(SecondUnderscore + 1, EAllowShrinking::No);
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

			Out.LeftInline(LastUnderscore, EAllowShrinking::No);
		}

		if (Out.EndsWith(TEXT("_C")))
		{
			Out.LeftChopInline(2, EAllowShrinking::No);
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
		return IsValid(Pawn) && !Pawn->IsA<ASpectatorPawn>();
	}

	bool CanControllerPossessPlacedPawn(const APawn* Pawn, const AController* RequestingController)
	{
		if (!IsValid(Pawn))
		{
			return false;
		}

		const AController* ExistingController = Pawn->GetController();
		if (!ExistingController || ExistingController == RequestingController)
		{
			return true;
		}

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

	struct FPlacedPawnSequenceContext
	{
		AController* Controller = nullptr;
		UWorld* World = nullptr;
		const FMetaAgentPlacedPawnSelectionConfig* Config = nullptr;
		UClass* PreferredClass = nullptr;
		APawn* ExistingPawn = nullptr;
		APawn* NamedPawn = nullptr;
		APawn* PreferredClassPawn = nullptr;
		APawn* FirstUsablePawn = nullptr;
		APawn* SelectedPawn = nullptr;
		AActor* MatchingNonPawnActor = nullptr;
		int32 NamedPawnMatchCount = 0;
		bool bShouldSpawnFallback = false;
		bool bStopSequence = false;
	};

	struct FBootstrapSequenceContext
	{
		ACharacter* Character = nullptr;
		USkeletalMeshComponent* PrimaryMesh = nullptr;
		FMetaAgentMovementDiagnosticsState* MovementDiagnostics = nullptr;
		const FMetaAgentInputFallbackState* InputFallback = nullptr;
		USkeletalMeshComponent* RecoveryMesh = nullptr;
		AActor* RecoveryOwner = nullptr;
		USkeletalMeshComponent* DrivingBodyMesh = nullptr;
		bool bNeedsMeshRecovery = false;
		bool bNeedsAnimRecovery = false;
		bool bRecoveredFromWorldSearch = false;
		bool bHasRecoverySource = false;
		bool bCanInitializeAnimBlueprint = false;
		bool bUsingCrowdFallbackClass = false;
		TArray<AActor*> AttachedActors;
		TInlineComponentArray<UMeshComponent*> SourceMeshes;
		TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes;
		TMap<const USceneComponent*, USceneComponent*> SourceToRecoveredSceneMap;
	};

	void Step01_ValidateControllerAndWorld(FPlacedPawnSequenceContext& Context)
	{
		if (!Context.Controller || !Context.World || !Context.Config)
		{
			Context.bStopSequence = true;
		}
	}

	void Step02_CaptureExistingPawn(FPlacedPawnSequenceContext& Context)
	{
		Context.ExistingPawn = Context.Controller ? Context.Controller->GetPawn() : nullptr;
	}

	void Step03_KeepExistingNonSpectatorPawn(FPlacedPawnSequenceContext& Context)
	{
		if (Context.ExistingPawn && !Context.ExistingPawn->IsA<ASpectatorPawn>())
		{
			UE_LOG(LogMetaAgent, Log,
				TEXT("MetaAgentGameMode: Controller '%s' already has pawn '%s'. Keeping current possession."),
				*GetNameSafe(Context.Controller),
				*Context.ExistingPawn->GetName());
			Context.bStopSequence = true;
		}
	}

	void Step04_LoadPreferredPlacedPawnClass(FPlacedPawnSequenceContext& Context)
	{
		if (!Context.Config->PreferredPlacedPawnClass.IsNull())
		{
			Context.PreferredClass = Context.Config->PreferredPlacedPawnClass.LoadSynchronous();
		}
	}

	void Step05_InitializeSelectionSlots(FPlacedPawnSequenceContext& Context)
	{
		Context.NamedPawn = nullptr;
		Context.PreferredClassPawn = nullptr;
		Context.FirstUsablePawn = nullptr;
		Context.SelectedPawn = nullptr;
		Context.NamedPawnMatchCount = 0;
	}

	void Step06_ScanPlacedPawns(FPlacedPawnSequenceContext& Context)
	{
		for (TActorIterator<APawn> It(Context.World); It; ++It)
		{
			APawn* Candidate = *It;
			if (!IsSelectablePlacedPawn(Candidate))
			{
				continue;
			}

			const bool bCanPossessCandidate = CanControllerPossessPlacedPawn(Candidate, Context.Controller);
			if (!bCanPossessCandidate)
			{
				continue;
			}

			if (MatchesPreferredName(Candidate, Context.Config->PreferredPlacedPawnName))
			{
				++Context.NamedPawnMatchCount;
				if (!Context.NamedPawn)
				{
					Context.NamedPawn = Candidate;
				}
			}

			if (!Context.PreferredClassPawn && Context.PreferredClass && Candidate->IsA(Context.PreferredClass))
			{
				Context.PreferredClassPawn = Candidate;
			}

			if (!Context.FirstUsablePawn)
			{
				Context.FirstUsablePawn = Candidate;
			}
		}
	}

	void Step07_ResolveStrictNameMode(FPlacedPawnSequenceContext& Context)
	{
		if (!Context.Config->bRequireExactPreferredPawnName || Context.Config->PreferredPlacedPawnName.IsEmpty())
		{
			Context.SelectedPawn = Context.NamedPawn ? Context.NamedPawn : (Context.PreferredClassPawn ? Context.PreferredClassPawn : Context.FirstUsablePawn);
			Context.bStopSequence = true;
		}
	}

	void Step08_ResolveUniqueNamedMatch(FPlacedPawnSequenceContext& Context)
	{
		if (!Context.bStopSequence && Context.NamedPawnMatchCount == 1)
		{
			Context.SelectedPawn = Context.NamedPawn;
			Context.bStopSequence = true;
		}
	}

	void Step09_HandleAmbiguousNamedMatch(FPlacedPawnSequenceContext& Context)
	{
		if (!Context.bStopSequence && Context.NamedPawnMatchCount > 1 && Context.Config->bRequireUniquePreferredPawnName)
		{
			UE_LOG(LogMetaAgent, Error,
				TEXT("MetaAgentGameMode: Preferred pawn name '%s' is ambiguous (%d matches). Rename to a unique actor name, or disable unique-name enforcement."),
				*Context.Config->PreferredPlacedPawnName,
				Context.NamedPawnMatchCount);
			Context.bShouldSpawnFallback = Context.Config->bAllowSpawnFallback;
			if (Context.bShouldSpawnFallback)
			{
				UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgentGameMode: Ambiguous preferred name. Using spawn fallback due to configuration."));
			}
			Context.bStopSequence = true;
		}
	}

	void Step10_FindMatchingNonPawnActor(FPlacedPawnSequenceContext& Context)
	{
		if (Context.bStopSequence || Context.Config->PreferredPlacedPawnName.IsEmpty())
		{
			return;
		}

		for (TActorIterator<AActor> It(Context.World); It; ++It)
		{
			AActor* CandidateActor = *It;
			if (!CandidateActor || Cast<APawn>(CandidateActor))
			{
				continue;
			}

			if (IsFlexibleNameMatch(CandidateActor->GetName(), Context.Config->PreferredPlacedPawnName))
			{
				Context.MatchingNonPawnActor = CandidateActor;
				return;
			}

#if WITH_EDITOR
			if (IsFlexibleNameMatch(CandidateActor->GetActorLabel(), Context.Config->PreferredPlacedPawnName))
			{
				Context.MatchingNonPawnActor = CandidateActor;
				return;
			}
#endif
		}
	}

	void Step11_LogMissingStrictNameActorMismatch(FPlacedPawnSequenceContext& Context)
	{
		if (Context.bStopSequence || !Context.MatchingNonPawnActor)
		{
			return;
		}

		UE_LOG(LogMetaAgent, Error,
			TEXT("MetaAgentGameMode: Found actor '%s' for preferred name '%s', but class '%s' is not a Pawn/Character and cannot be possessed."),
			*GetNameSafe(Context.MatchingNonPawnActor),
			*Context.Config->PreferredPlacedPawnName,
			*GetNameSafe(Context.MatchingNonPawnActor->GetClass()));
	}

	void Step12_LogMissingStrictNamePawn(FPlacedPawnSequenceContext& Context)
	{
		if (Context.bStopSequence)
		{
			return;
		}

		UE_LOG(LogMetaAgent, Error,
			TEXT("MetaAgentGameMode: Required preferred pawn '%s' was not found. Place a pawn/character with this exact name, or disable strict name requirement."),
			*Context.Config->PreferredPlacedPawnName);
	}

	void Step13_DecideSpawnFallbackForMissingStrictName(FPlacedPawnSequenceContext& Context)
	{
		if (Context.bStopSequence)
		{
			return;
		}

		Context.bShouldSpawnFallback = Context.Config->bAllowSpawnFallback;
		if (Context.bShouldSpawnFallback)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgentGameMode: Required preferred name missing. Using spawn fallback due to configuration."));
		}
		Context.bStopSequence = true;
	}

	void Step14_PossessSelectedPlacedPawn(FPlacedPawnSequenceContext& Context)
	{
		if (!Context.SelectedPawn)
		{
			return;
		}

		Context.Controller->Possess(Context.SelectedPawn);
	}

	void Step15_LogSelectedPlacedPawn(FPlacedPawnSequenceContext& Context)
	{
		if (!Context.SelectedPawn)
		{
			return;
		}

		UE_LOG(LogMetaAgent, Log,
			TEXT("MetaAgentGameMode: Possessed placed pawn '%s' (%s). Name='%s' (matches=%d) ClassFilter=%s"),
			*Context.SelectedPawn->GetName(),
			*Context.SelectedPawn->GetClass()->GetName(),
			*Context.Config->PreferredPlacedPawnName,
			Context.NamedPawnMatchCount,
			*GetNameSafe(Context.PreferredClass));
	}

	void Step16_DecideSpawnFallbackForNoSelection(FPlacedPawnSequenceContext& Context)
	{
		if (Context.SelectedPawn || Context.bShouldSpawnFallback)
		{
			return;
		}

		if (Context.Config->bAllowSpawnFallback)
		{
			UE_LOG(LogMetaAgent, Log,
				TEXT("MetaAgentGameMode: No placed pawn found (Name='%s'). Falling back to normal spawn."),
				*Context.Config->PreferredPlacedPawnName);
			Context.bShouldSpawnFallback = true;
		}
	}

	void Step17_LogNoSelectionError(FPlacedPawnSequenceContext& Context)
	{
		if (Context.SelectedPawn || Context.bShouldSpawnFallback)
		{
			return;
		}

		UE_LOG(LogMetaAgent, Error,
			TEXT("MetaAgentGameMode: No placed pawn found (Name='%s'). Place a Pawn/Character with that name, or enable spawn fallback."),
			*Context.Config->PreferredPlacedPawnName);
	}

	void Step18_FinalizePossessionSequence(FPlacedPawnSequenceContext& Context)
	{
		Context.bStopSequence = true;
	}

	void Step19_ValidateCharacter(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character || !Context.MovementDiagnostics || !Context.InputFallback)
		{
			return;
		}

		Context.PrimaryMesh = Context.Character->GetMesh();
	}

	void Step20_ValidatePrimaryMesh(FBootstrapSequenceContext& Context)
	{
		if (!Context.PrimaryMesh)
		{
			return;
		}

		Context.bNeedsMeshRecovery = (Context.PrimaryMesh->GetSkeletalMeshAsset() == nullptr);
		Context.bNeedsAnimRecovery = (Context.PrimaryMesh->GetAnimClass() == nullptr && Context.PrimaryMesh->GetAnimInstance() == nullptr);
	}

	void Step21_DetermineRecoveryNeed(FBootstrapSequenceContext& Context)
	{
		Context.bHasRecoverySource = !(Context.bNeedsMeshRecovery || Context.bNeedsAnimRecovery);
	}

	void Step22_SearchAttachedRecoveryMesh(FBootstrapSequenceContext& Context)
	{
		if (Context.bHasRecoverySource || !Context.Character)
		{
			return;
		}

		Context.Character->GetAttachedActors(Context.AttachedActors, true, true);
		for (AActor* AttachedActor : Context.AttachedActors)
		{
			if (!AttachedActor)
			{
				continue;
			}

			TInlineComponentArray<USkeletalMeshComponent*> AttachedMeshes;
			AttachedActor->GetComponents(AttachedMeshes);
			for (USkeletalMeshComponent* AttachedMesh : AttachedMeshes)
			{
				if (AttachedMesh && AttachedMesh->GetSkeletalMeshAsset())
				{
					Context.RecoveryMesh = AttachedMesh;
					Context.RecoveryOwner = AttachedActor;
					Context.bHasRecoverySource = true;
					return;
				}
			}
		}
	}

	void Step23_SearchWorldRecoveryMesh(FBootstrapSequenceContext& Context)
	{
		if (Context.bHasRecoverySource || !Context.Character || !Context.Character->GetWorld())
		{
			return;
		}

		for (TActorIterator<AActor> It(Context.Character->GetWorld()); It; ++It)
		{
			AActor* CandidateActor = *It;
			if (!CandidateActor || CandidateActor == Context.Character)
			{
				continue;
			}

			const FString CandidateName = CandidateActor->GetName();
			const bool bLikelyMetaHumanVisual =
				CandidateName.Contains(TEXT("MetaHuman"), ESearchCase::IgnoreCase) ||
				CandidateName.Contains(TEXT("Visual"), ESearchCase::IgnoreCase);
			if (!bLikelyMetaHumanVisual)
			{
				continue;
			}

			TInlineComponentArray<USkeletalMeshComponent*> CandidateMeshes;
			CandidateActor->GetComponents(CandidateMeshes);
			for (USkeletalMeshComponent* CandidateMesh : CandidateMeshes)
			{
				if (CandidateMesh && CandidateMesh->GetSkeletalMeshAsset())
				{
					Context.RecoveryMesh = CandidateMesh;
					Context.RecoveryOwner = CandidateActor;
					Context.bRecoveredFromWorldSearch = true;
					Context.bHasRecoverySource = true;
					return;
				}
			}
		}
	}

	void Step24_ApplyRecoveredSkeletalMesh(FBootstrapSequenceContext& Context)
	{
		if (!Context.bHasRecoverySource || !Context.RecoveryMesh || !Context.PrimaryMesh)
		{
			return;
		}

		if (Context.PrimaryMesh->GetSkeletalMeshAsset() == nullptr)
		{
			Context.PrimaryMesh->SetSkeletalMesh(Context.RecoveryMesh->GetSkeletalMeshAsset());
		}
	}

	void Step25_ApplyRecoveredAnimClassDirectly(FBootstrapSequenceContext& Context)
	{
		if (!Context.bHasRecoverySource || !Context.RecoveryMesh || !Context.PrimaryMesh || Context.PrimaryMesh->GetAnimClass())
		{
			return;
		}

		if (Context.RecoveryMesh->GetAnimClass())
		{
			Context.PrimaryMesh->SetAnimInstanceClass(Context.RecoveryMesh->GetAnimClass());
		}
		else if (UAnimInstance* RecoveryAnimInstance = Context.RecoveryMesh->GetAnimInstance())
		{
			Context.PrimaryMesh->SetAnimInstanceClass(RecoveryAnimInstance->GetClass());
		}
	}

	void Step26_ApplyRecoveredTransformAndVisibility(FBootstrapSequenceContext& Context)
	{
		if (!Context.bHasRecoverySource || !Context.RecoveryMesh || !Context.PrimaryMesh)
		{
			return;
		}

		Context.PrimaryMesh->SetRelativeLocation(Context.RecoveryMesh->GetRelativeLocation());
		Context.PrimaryMesh->SetRelativeRotation(Context.RecoveryMesh->GetRelativeRotation());
		Context.PrimaryMesh->SetRelativeScale3D(Context.RecoveryMesh->GetRelativeScale3D());
		Context.PrimaryMesh->SetVisibility(true, true);
		Context.PrimaryMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Context.PrimaryMesh->SetGenerateOverlapEvents(false);
	}

	void Step27_LogRecoverySource(FBootstrapSequenceContext& Context)
	{
		if (Context.bHasRecoverySource && Context.RecoveryMesh && Context.RecoveryOwner)
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("BodyRecovery: '%s' recovered primary mesh '%s' from %s actor '%s' component '%s'."),
				*GetNameSafe(Context.Character),
				*GetNameSafe(Context.PrimaryMesh ? Context.PrimaryMesh->GetSkeletalMeshAsset() : nullptr),
				Context.bRecoveredFromWorldSearch ? TEXT("world") : TEXT("attached"),
				*GetNameSafe(Context.RecoveryOwner),
				*GetNameSafe(Context.RecoveryMesh));
		}
		else if (Context.PrimaryMesh && (Context.bNeedsMeshRecovery || Context.bNeedsAnimRecovery))
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("BodyRecovery: '%s' mesh '%s' has no SkeletalMesh and no attached/world visual actor with a valid skeletal mesh was found. Configure CharacterMesh0 SkeletalMesh + AnimClass in BP_MH_PlayerChar."),
				*GetNameSafe(Context.Character),
				*GetNameSafe(Context.PrimaryMesh));
		}
	}

	void Step28_ApplyRuntimeOrientationOffset(FBootstrapSequenceContext& Context)
	{
		if (!Context.RecoveryOwner || !Context.RecoveryMesh || !Context.PrimaryMesh)
		{
			return;
		}

		Context.PrimaryMesh->SetRelativeRotation(FRotator(
			Context.RecoveryMesh->GetRelativeRotation().Pitch,
			Context.RecoveryMesh->GetRelativeRotation().Yaw - 90.0f,
			Context.RecoveryMesh->GetRelativeRotation().Roll));
		Context.PrimaryMesh->SetRelativeLocation(Context.RecoveryMesh->GetRelativeLocation() + FVector(0.0f, 0.0f, -94.0f));
	}

	void Step29_TryRecoveryOwnerAnimClass(FBootstrapSequenceContext& Context)
	{
		if (!Context.RecoveryMesh || !Context.PrimaryMesh || Context.PrimaryMesh->GetAnimClass())
		{
			return;
		}

		Context.PrimaryMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		if (Context.RecoveryMesh->GetAnimClass())
		{
			Context.PrimaryMesh->SetAnimInstanceClass(Context.RecoveryMesh->GetAnimClass());
		}
	}

	void Step30_TryRecoveryOwnerAnimInstanceClass(FBootstrapSequenceContext& Context)
	{
		if (!Context.RecoveryMesh || !Context.PrimaryMesh || Context.PrimaryMesh->GetAnimClass())
		{
			return;
		}

		if (UAnimInstance* RecoveryAnimInstance = Context.RecoveryMesh->GetAnimInstance())
		{
			Context.PrimaryMesh->SetAnimInstanceClass(RecoveryAnimInstance->GetClass());
		}
	}

	void Step31_TryRecoveryOwnerCDOMatchingMesh(FBootstrapSequenceContext& Context)
	{
		if (!Context.RecoveryOwner || !Context.PrimaryMesh || Context.PrimaryMesh->GetAnimClass())
		{
			return;
		}

		if (AActor* RecoveryCDO = Cast<AActor>(Context.RecoveryOwner->GetClass()->GetDefaultObject()))
		{
			TInlineComponentArray<USkeletalMeshComponent*> DefaultMeshes;
			RecoveryCDO->GetComponents(DefaultMeshes);
			const USkeleton* PrimarySkeleton =
				Context.PrimaryMesh->GetSkeletalMeshAsset() ? Context.PrimaryMesh->GetSkeletalMeshAsset()->GetSkeleton() : nullptr;

			for (USkeletalMeshComponent* DefaultMeshComp : DefaultMeshes)
			{
				if (!DefaultMeshComp)
				{
					continue;
				}

				if (DefaultMeshComp->GetFName() == Context.RecoveryMesh->GetFName() && DefaultMeshComp->GetAnimClass())
				{
					Context.PrimaryMesh->SetAnimInstanceClass(DefaultMeshComp->GetAnimClass());
					return;
				}

				if (PrimarySkeleton && DefaultMeshComp->GetAnimClass() && DefaultMeshComp->GetSkeletalMeshAsset()
					&& DefaultMeshComp->GetSkeletalMeshAsset()->GetSkeleton() == PrimarySkeleton)
				{
					Context.PrimaryMesh->SetAnimInstanceClass(DefaultMeshComp->GetAnimClass());
					return;
				}
			}
		}
	}

	void Step32_TryPossessedCharacterCDOAnimClass(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character || !Context.PrimaryMesh || Context.PrimaryMesh->GetAnimClass())
		{
			return;
		}

		if (ACharacter* PossessedCDOCharacter = Cast<ACharacter>(Context.Character->GetClass()->GetDefaultObject()))
		{
			if (USkeletalMeshComponent* PossessedCDOMesh = PossessedCDOCharacter->GetMesh())
			{
				if (PossessedCDOMesh->GetAnimClass())
				{
					Context.PrimaryMesh->SetAnimInstanceClass(PossessedCDOMesh->GetAnimClass());
				}
			}
		}
	}

	void Step33_TryWorldSkeletonMatchedAnimClass(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character || !Context.PrimaryMesh || Context.PrimaryMesh->GetAnimClass() || !Context.PrimaryMesh->GetSkeletalMeshAsset())
		{
			return;
		}

		const USkeleton* PrimarySkeleton = Context.PrimaryMesh->GetSkeletalMeshAsset()->GetSkeleton();
		if (!PrimarySkeleton)
		{
			return;
		}

		for (TActorIterator<AActor> It(Context.Character->GetWorld()); It; ++It)
		{
			AActor* CandidateActor = *It;
			if (!CandidateActor)
			{
				continue;
			}

			TInlineComponentArray<USkeletalMeshComponent*> CandidateMeshes;
			CandidateActor->GetComponents(CandidateMeshes);
			for (USkeletalMeshComponent* CandidateMesh : CandidateMeshes)
			{
				if (!CandidateMesh || !CandidateMesh->GetSkeletalMeshAsset())
				{
					continue;
				}

				const USkeleton* CandidateSkeleton = CandidateMesh->GetSkeletalMeshAsset()->GetSkeleton();
				if (!CandidateSkeleton || CandidateSkeleton != PrimarySkeleton)
				{
					continue;
				}

				if (CandidateMesh->GetAnimClass())
				{
					Context.PrimaryMesh->SetAnimInstanceClass(CandidateMesh->GetAnimClass());
					return;
				}

				if (UAnimInstance* CandidateAnimInstance = CandidateMesh->GetAnimInstance())
				{
					Context.PrimaryMesh->SetAnimInstanceClass(CandidateAnimInstance->GetClass());
					return;
				}
			}
		}
	}

	void Step34_TryPluginLocalFallbackAnimClasses(FBootstrapSequenceContext& Context)
	{
		if (!Context.PrimaryMesh || Context.PrimaryMesh->GetAnimClass())
		{
			return;
		}

		static const TCHAR* FallbackAnimClassPaths[] =
		{
			TEXT("/MetaAgentPlugin/External/CitySampleCrowd/Character/Male/Rig/m_tal_nrw_animbp.m_tal_nrw_animbp_C"),
			TEXT("/MetaAgentPlugin/External/CitySampleCrowd/Character/Female/Rig/f_tal_nrw_animbp.f_tal_nrw_animbp_C"),
			TEXT("/MetaAgentPlugin/External/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C")
		};

		for (const TCHAR* AnimClassPath : FallbackAnimClassPaths)
		{
			if (!AnimClassPath)
			{
				continue;
			}

			if (UClass* LoadedAnimClass = StaticLoadClass(UAnimInstance::StaticClass(), nullptr, AnimClassPath))
			{
				Context.PrimaryMesh->SetAnimInstanceClass(LoadedAnimClass);
				UE_LOG(LogMetaAgent, Warning,
					TEXT("BodyRecovery: Loaded fallback AnimClass '%s' for primary mesh '%s'."),
					AnimClassPath,
					*GetNameSafe(Context.PrimaryMesh));
				return;
			}
		}
	}

	void Step35_InitializeResolvedAnimBlueprint(FBootstrapSequenceContext& Context)
	{
		if (!Context.PrimaryMesh || !Context.PrimaryMesh->GetAnimClass())
		{
			return;
		}

		Context.PrimaryMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Context.PrimaryMesh->InitAnim(true);
		Context.bCanInitializeAnimBlueprint = true;
	}

	void Step36_LogAnimResolutionStatus(FBootstrapSequenceContext& Context)
	{
		if (!Context.PrimaryMesh)
		{
			return;
		}

		if (Context.bCanInitializeAnimBlueprint)
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("BodyRecovery: Applied animation class '%s' to primary mesh '%s'."),
				*GetNameSafe(Context.PrimaryMesh->GetAnimClass()),
				*GetNameSafe(Context.PrimaryMesh));
		}
		else if (Context.bHasRecoverySource)
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("BodyRecovery: Unable to resolve an AnimClass for primary mesh '%s'."),
				*GetNameSafe(Context.PrimaryMesh));
		}
	}

	void Step37_GatherSourceMeshesForFollowerRecovery(FBootstrapSequenceContext& Context)
	{
		if (!Context.RecoveryOwner)
		{
			return;
		}

		Context.RecoveryOwner->GetComponents(Context.SourceMeshes);
	}

	void Step38_DuplicateMissingFollowerComponents(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character || !Context.RecoveryMesh)
		{
			return;
		}

		for (UMeshComponent* SourceMeshComp : Context.SourceMeshes)
		{
			if (!SourceMeshComp || SourceMeshComp == Context.RecoveryMesh)
			{
				continue;
			}

			if (USkeletalMeshComponent* SourceSkeletal = Cast<USkeletalMeshComponent>(SourceMeshComp))
			{
				if (!SourceSkeletal->GetSkeletalMeshAsset())
				{
					continue;
				}
			}

			const FString RecoveredName = FString::Printf(TEXT("MetaAgentRecovered_%s"), *SourceMeshComp->GetName());
			UMeshComponent* RecoveredComp = FindObject<UMeshComponent>(Context.Character, *RecoveredName);
			if (!RecoveredComp)
			{
				RecoveredComp = DuplicateObject<UMeshComponent>(SourceMeshComp, Context.Character, *RecoveredName);
				if (!RecoveredComp)
				{
					continue;
				}

				Context.Character->AddInstanceComponent(RecoveredComp);
				RecoveredComp->SetVisibility(true, true);
				RecoveredComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				RecoveredComp->SetGenerateOverlapEvents(false);
				RecoveredComp->RegisterComponent();

				UE_LOG(LogMetaAgent, Warning,
					TEXT("BodyRecovery: Added follower component '%s' class '%s' from source '%s' on actor '%s'."),
					*GetNameSafe(RecoveredComp),
					*GetNameSafe(RecoveredComp->GetClass()),
					*GetNameSafe(SourceMeshComp),
					*GetNameSafe(Context.RecoveryOwner));
			}
		}
	}

	void Step39_RecordRecoveredSceneMap(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character || !Context.RecoveryMesh)
		{
			return;
		}

		for (UMeshComponent* SourceMeshComp : Context.SourceMeshes)
		{
			if (!SourceMeshComp || SourceMeshComp == Context.RecoveryMesh)
			{
				continue;
			}

			const FString RecoveredName = FString::Printf(TEXT("MetaAgentRecovered_%s"), *SourceMeshComp->GetName());
			UMeshComponent* RecoveredComp = FindObject<UMeshComponent>(Context.Character, *RecoveredName);
			if (!RecoveredComp)
			{
				continue;
			}

			if (const USceneComponent* SourceScene = Cast<USceneComponent>(SourceMeshComp))
			{
				if (USceneComponent* RecoveredScene = Cast<USceneComponent>(RecoveredComp))
				{
					Context.SourceToRecoveredSceneMap.FindOrAdd(SourceScene) = RecoveredScene;
				}
			}
		}
	}

	void Step40_RestoreFollowerAttachments(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character || !Context.PrimaryMesh || !Context.RecoveryMesh)
		{
			return;
		}

		for (UMeshComponent* SourceMeshComp : Context.SourceMeshes)
		{
			if (!SourceMeshComp || SourceMeshComp == Context.RecoveryMesh)
			{
				continue;
			}

			const FString RecoveredName = FString::Printf(TEXT("MetaAgentRecovered_%s"), *SourceMeshComp->GetName());
			UMeshComponent* RecoveredComp = FindObject<UMeshComponent>(Context.Character, *RecoveredName);
			if (!RecoveredComp)
			{
				continue;
			}

			if (USceneComponent* RecoveredScene = Cast<USceneComponent>(RecoveredComp))
			{
				if (const USceneComponent* SourceScene = Cast<USceneComponent>(SourceMeshComp))
				{
					const USceneComponent* SourceParent = SourceScene->GetAttachParent();
					USceneComponent* TargetParent = nullptr;

					if (SourceParent == Context.RecoveryMesh)
					{
						TargetParent = Context.PrimaryMesh;
					}
					else if (SourceParent)
					{
						if (USceneComponent* const* FoundParent = Context.SourceToRecoveredSceneMap.Find(SourceParent))
						{
							TargetParent = *FoundParent;
						}
					}

					if (!TargetParent)
					{
						TargetParent = Context.PrimaryMesh;
					}

					if (RecoveredScene->GetAttachParent() != TargetParent)
					{
						RecoveredScene->AttachToComponent(TargetParent, FAttachmentTransformRules::KeepRelativeTransform);
					}
				}
			}
		}
	}

	void Step41_RestoreFollowerRelativeTransforms(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character || !Context.RecoveryMesh)
		{
			return;
		}

		for (UMeshComponent* SourceMeshComp : Context.SourceMeshes)
		{
			if (!SourceMeshComp || SourceMeshComp == Context.RecoveryMesh)
			{
				continue;
			}

			const FString RecoveredName = FString::Printf(TEXT("MetaAgentRecovered_%s"), *SourceMeshComp->GetName());
			UMeshComponent* RecoveredComp = FindObject<UMeshComponent>(Context.Character, *RecoveredName);
			if (!RecoveredComp)
			{
				continue;
			}

			if (USceneComponent* RecoveredScene = Cast<USceneComponent>(RecoveredComp))
			{
				if (const USceneComponent* SourceScene = Cast<USceneComponent>(SourceMeshComp))
				{
					RecoveredScene->SetRelativeTransform(SourceScene->GetRelativeTransform());
				}
			}
		}
	}

	void Step42_RebuildFollowerLeaderPoseLinks(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character || !Context.PrimaryMesh || !Context.RecoveryMesh)
		{
			return;
		}

		for (UMeshComponent* SourceMeshComp : Context.SourceMeshes)
		{
			if (!SourceMeshComp || SourceMeshComp == Context.RecoveryMesh)
			{
				continue;
			}

			const FString RecoveredName = FString::Printf(TEXT("MetaAgentRecovered_%s"), *SourceMeshComp->GetName());
			UMeshComponent* RecoveredComp = FindObject<UMeshComponent>(Context.Character, *RecoveredName);
			if (USkinnedMeshComponent* RecoveredSkinned = Cast<USkinnedMeshComponent>(RecoveredComp))
			{
				RecoveredSkinned->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				RecoveredSkinned->bEnableUpdateRateOptimizations = false;

				if (USkinnedMeshComponent* SourceSkinned = Cast<USkinnedMeshComponent>(SourceMeshComp))
				{
					if (USkinnedMeshComponent* SourceLeader = SourceSkinned->LeaderPoseComponent.Get())
					{
						USkinnedMeshComponent* TargetLeader = nullptr;
						if (SourceLeader == Context.RecoveryMesh)
						{
							TargetLeader = Context.PrimaryMesh;
						}
						else
						{
							const FString LeaderRecoveredName = FString::Printf(TEXT("MetaAgentRecovered_%s"), *SourceLeader->GetName());
							TargetLeader = FindObject<USkinnedMeshComponent>(Context.Character, *LeaderRecoveredName);
						}

						if (TargetLeader)
						{
							RecoveredSkinned->SetLeaderPoseComponent(TargetLeader, true, true);
						}
					}
				}
			}
		}
	}

	void Step43_GatherSkeletalMeshes(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character)
		{
			return;
		}

		Context.Character->GetComponents(Context.SkeletalMeshes);
	}

	void Step44_ResolveDrivingBodyMesh(FBootstrapSequenceContext& Context)
	{
		for (USkeletalMeshComponent* MeshComp : Context.SkeletalMeshes)
		{
			if (!MeshComp || !MeshComp->GetSkeletalMeshAsset())
			{
				continue;
			}

			if (MeshComp->GetAnimInstance())
			{
				Context.DrivingBodyMesh = MeshComp;
				if (MeshComp->GetFName() == TEXT("Body"))
				{
					return;
				}
			}
		}
	}

	void Step45_ForceDrivingMeshTickSettings(FBootstrapSequenceContext& Context)
	{
		if (!Context.DrivingBodyMesh)
		{
			return;
		}

		Context.DrivingBodyMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Context.DrivingBodyMesh->bEnableUpdateRateOptimizations = false;
	}

	void Step46_ForceNoCollisionOnMeshes(FBootstrapSequenceContext& Context)
	{
		for (USkeletalMeshComponent* MeshComp : Context.SkeletalMeshes)
		{
			if (!MeshComp)
			{
				continue;
			}

			if (MeshComp->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("MovementGuard: '%s' mesh '%s' had collision enabled (%d); forcing NoCollision to avoid movement drag."),
					*GetNameSafe(Context.Character),
					*GetNameSafe(MeshComp),
					static_cast<int32>(MeshComp->GetCollisionEnabled()));
				MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}

	void Step47_DisableMeshOverlapEvents(FBootstrapSequenceContext& Context)
	{
		for (USkeletalMeshComponent* MeshComp : Context.SkeletalMeshes)
		{
			if (MeshComp && MeshComp->GetGenerateOverlapEvents())
			{
				MeshComp->SetGenerateOverlapEvents(false);
			}
		}
	}

	void Step48_RebindMeshLeaderPoseFollowers(FBootstrapSequenceContext& Context)
	{
		if (!Context.DrivingBodyMesh)
		{
			return;
		}

		for (USkeletalMeshComponent* MeshComp : Context.SkeletalMeshes)
		{
			if (!MeshComp || MeshComp == Context.DrivingBodyMesh || !MeshComp->GetSkeletalMeshAsset() || !Context.DrivingBodyMesh->GetSkeletalMeshAsset())
			{
				continue;
			}

			const USkeleton* MeshSkeleton = MeshComp->GetSkeletalMeshAsset()->GetSkeleton();
			const USkeleton* BodySkeleton = Context.DrivingBodyMesh->GetSkeletalMeshAsset()->GetSkeleton();
			const bool bSharesBodySkeleton = (MeshSkeleton && BodySkeleton && MeshSkeleton == BodySkeleton);
			if (bSharesBodySkeleton && MeshComp->GetAnimInstance() == nullptr)
			{
				MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				MeshComp->bEnableUpdateRateOptimizations = false;
				MeshComp->SetLeaderPoseComponent(Context.DrivingBodyMesh, true, true);
				UE_LOG(LogMetaAgent, Warning,
					TEXT("MovementGuard: '%s' mesh '%s' now follows '%s' via LeaderPose (same skeleton, no anim instance, follower ticks pose)."),
					*GetNameSafe(Context.Character),
					*GetNameSafe(MeshComp),
					*GetNameSafe(Context.DrivingBodyMesh));
			}
		}
	}

	void Step49_ClampMovementSpeedGuard(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character || !Context.InputFallback)
		{
			return;
		}

		if (UCharacterMovementComponent* MovementComp = Context.Character->GetCharacterMovement())
		{
			if (MovementComp->MaxWalkSpeed < 1.0f)
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("MovementGuard: '%s' MaxWalkSpeed was %.2f; clamping to fallback walk speed %.2f."),
					*GetNameSafe(Context.Character),
					MovementComp->MaxWalkSpeed,
					Context.InputFallback->WalkSpeed);
				MovementComp->MaxWalkSpeed = FMath::Max(1.0f, Context.InputFallback->WalkSpeed);
			}
		}
	}

	void Step50_ClampMovementAccelerationGuard(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character)
		{
			return;
		}

		if (UCharacterMovementComponent* MovementComp = Context.Character->GetCharacterMovement())
		{
			if (MovementComp->MaxAcceleration < 500.0f)
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("MovementGuard: '%s' MaxAcceleration was %.2f; clamping to 2048.00."),
					*GetNameSafe(Context.Character),
					MovementComp->MaxAcceleration);
				MovementComp->MaxAcceleration = 2048.0f;
			}
		}
	}

	void Step51_PreloadFallbackLocomotionAssets(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character)
		{
			return;
		}

		if (USkeletalMeshComponent* PossessedPrimaryMesh = Context.Character->GetMesh())
		{
			if (PossessedPrimaryMesh->GetAnimClass())
			{
				Context.bUsingCrowdFallbackClass =
					PossessedPrimaryMesh->GetAnimClass()->GetName().Contains(TEXT("tal_nrw_animbp"), ESearchCase::IgnoreCase);
				if (Context.bUsingCrowdFallbackClass)
				{
					GetEmergencyIdleAssetForBootstrap();
					GetEmergencyWalkAssetForBootstrap();
				}
			}
		}
	}

	void Step52_ActivateImmediateCrowdFallbackLocomotion(FBootstrapSequenceContext& Context)
	{
		if (!Context.Character || !Context.PrimaryMesh || !Context.MovementDiagnostics || !Context.bUsingCrowdFallbackClass)
		{
			return;
		}

		UAnimationAsset* IdleAsset = GetEmergencyIdleAssetForBootstrap();
		Context.MovementDiagnostics->bAutoFallbackActivated = true;
		Context.MovementDiagnostics->MovingWithoutPoseChangeSeconds = Context.MovementDiagnostics->AutoFallbackStallSeconds;

		if (IdleAsset)
		{
			if (Context.PrimaryMesh->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
			{
				Context.PrimaryMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			}
			Context.PrimaryMesh->PlayAnimation(IdleAsset, true);
		}

		UE_LOG(LogMetaAgent, Warning,
			TEXT("AnimFallback: '%s' crowd AnimBP path hardened; emergency single-node fallback activated immediately on possess."),
			*GetNameSafe(Context.Character));
	}
}

void FMetaAgentCharacterRuntime::RunPlacedPawnPossessionSequence(
	AController* NewPlayer,
	UWorld* World,
	const FMetaAgentPlacedPawnSelectionConfig& Config,
	bool& bOutShouldSpawnFallback)
{
	FPlacedPawnSequenceContext Context;
	Context.Controller = NewPlayer;
	Context.World = World;
	Context.Config = &Config;

	Step01_ValidateControllerAndWorld(Context);
	if (Context.bStopSequence) { bOutShouldSpawnFallback = false; return; }
	Step02_CaptureExistingPawn(Context);
	Step03_KeepExistingNonSpectatorPawn(Context);
	if (Context.bStopSequence) { bOutShouldSpawnFallback = false; return; }
	Step04_LoadPreferredPlacedPawnClass(Context);
	Step05_InitializeSelectionSlots(Context);
	Step06_ScanPlacedPawns(Context);
	Step07_ResolveStrictNameMode(Context);
	Step08_ResolveUniqueNamedMatch(Context);
	Step09_HandleAmbiguousNamedMatch(Context);
	if (!Context.SelectedPawn && !Context.bShouldSpawnFallback && !Context.bStopSequence)
		Step10_FindMatchingNonPawnActor(Context);
	if (!Context.SelectedPawn && !Context.bShouldSpawnFallback && !Context.bStopSequence)
		Step11_LogMissingStrictNameActorMismatch(Context);
	if (!Context.SelectedPawn && !Context.bShouldSpawnFallback && !Context.bStopSequence)
		Step12_LogMissingStrictNamePawn(Context);
	if (!Context.SelectedPawn && !Context.bShouldSpawnFallback && !Context.bStopSequence)
		Step13_DecideSpawnFallbackForMissingStrictName(Context);
	Step14_PossessSelectedPlacedPawn(Context);
	Step15_LogSelectedPlacedPawn(Context);
	Step16_DecideSpawnFallbackForNoSelection(Context);
	Step17_LogNoSelectionError(Context);
	Step18_FinalizePossessionSequence(Context);

	bOutShouldSpawnFallback = Context.bShouldSpawnFallback;
}

void FMetaAgentCharacterRuntime::RunPossessedCharacterBootstrapSequence(
	ACharacter* PossessedCharacter,
	FMetaAgentMovementDiagnosticsState& MovementDiagnostics,
	const FMetaAgentInputFallbackState& InputFallback)
{
	FBootstrapSequenceContext Context;
	Context.Character = PossessedCharacter;
	Context.MovementDiagnostics = &MovementDiagnostics;
	Context.InputFallback = &InputFallback;

	Step19_ValidateCharacter(Context);
	if (!Context.Character) { return; }
	Step20_ValidatePrimaryMesh(Context);
	if (!Context.PrimaryMesh) { return; }
	Step21_DetermineRecoveryNeed(Context);
	Step22_SearchAttachedRecoveryMesh(Context);
	Step23_SearchWorldRecoveryMesh(Context);
	Step24_ApplyRecoveredSkeletalMesh(Context);
	Step25_ApplyRecoveredAnimClassDirectly(Context);
	Step26_ApplyRecoveredTransformAndVisibility(Context);
	Step27_LogRecoverySource(Context);
	Step28_ApplyRuntimeOrientationOffset(Context);
	Step29_TryRecoveryOwnerAnimClass(Context);
	Step30_TryRecoveryOwnerAnimInstanceClass(Context);
	Step31_TryRecoveryOwnerCDOMatchingMesh(Context);
	Step32_TryPossessedCharacterCDOAnimClass(Context);
	Step33_TryWorldSkeletonMatchedAnimClass(Context);
	Step34_TryPluginLocalFallbackAnimClasses(Context);
	Step35_InitializeResolvedAnimBlueprint(Context);
	Step36_LogAnimResolutionStatus(Context);
	Step37_GatherSourceMeshesForFollowerRecovery(Context);
	Step38_DuplicateMissingFollowerComponents(Context);
	Step39_RecordRecoveredSceneMap(Context);
	Step40_RestoreFollowerAttachments(Context);
	Step41_RestoreFollowerRelativeTransforms(Context);
	Step42_RebuildFollowerLeaderPoseLinks(Context);
	Step43_GatherSkeletalMeshes(Context);
	Step44_ResolveDrivingBodyMesh(Context);
	Step45_ForceDrivingMeshTickSettings(Context);
	Step46_ForceNoCollisionOnMeshes(Context);
	Step47_DisableMeshOverlapEvents(Context);
	Step48_RebindMeshLeaderPoseFollowers(Context);
	Step49_ClampMovementSpeedGuard(Context);
	Step50_ClampMovementAccelerationGuard(Context);
	Step51_PreloadFallbackLocomotionAssets(Context);
	Step52_ActivateImmediateCrowdFallbackLocomotion(Context);
}
