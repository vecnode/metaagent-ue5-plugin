// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.


#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "AIController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Core/MetaAgent.h"
#include "UI/HUD/MetaAgentHUD.h"
#include "Systems/Runtime/MetaAgentGameInstance.h"
#include "Gameplay/AI/MetaAgentWanderAIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LevelSequence.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelinePrimaryConfig.h"
#include "MoviePipelineQueueEngineSubsystem.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelineSetting.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SVirtualJoystick.h"

#if WITH_EDITOR
#include "Recorder/TakeRecorderSubsystem.h"
#include "TakeRecorderSettings.h"
#endif

namespace
{
	UInputMappingContext* ResolveMappingContextWithFallback(
		const TSoftObjectPtr<UInputMappingContext>& SoftReference,
		const TCHAR* LegacyPath,
		const TCHAR* ContextLabel,
		const UObject* Owner)
	{
		if (!SoftReference.IsNull())
		{
			if (UInputMappingContext* LoadedFromSoftRef = SoftReference.LoadSynchronous())
			{
				return LoadedFromSoftRef;
			}

			UE_LOG(LogMetaAgent, Warning,
				TEXT("'%s' failed to load soft mapping context '%s' for %s."),
				*GetNameSafe(Owner),
				*SoftReference.ToString(),
				ContextLabel);
		}

		if (LegacyPath)
		{
			if (UInputMappingContext* LoadedFromLegacyPath = Cast<UInputMappingContext>(StaticLoadObject(UInputMappingContext::StaticClass(), nullptr, LegacyPath)))
			{
				return LoadedFromLegacyPath;
			}
		}

		UE_LOG(LogMetaAgent, Warning,
			TEXT("'%s' has no valid mapping context for %s. Configure asset references in Blueprint/Class Defaults."),
			*GetNameSafe(Owner),
			ContextLabel);

		return nullptr;
	}
}

void AMetaAgentPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (!InPawn)
	{
		return;
	}

	ResetMovementDiagnosticsState();
	ConfigureCameraForPawn(InPawn);

	if (ACharacter* PossessedCharacter = Cast<ACharacter>(InPawn))
	{
		if (USkeletalMeshComponent* PrimaryMesh = PossessedCharacter->GetMesh())
		{
			const bool bNeedsMeshRecovery = (PrimaryMesh->GetSkeletalMeshAsset() == nullptr);
			const bool bNeedsAnimRecovery = (PrimaryMesh->GetAnimClass() == nullptr && PrimaryMesh->GetAnimInstance() == nullptr);

			if (bNeedsMeshRecovery || bNeedsAnimRecovery)
			{
				USkeletalMeshComponent* RecoveryMesh = nullptr;
				AActor* RecoveryOwner = nullptr;
				bool bRecoveredFromWorldSearch = false;

				TArray<AActor*> AttachedActors;
				PossessedCharacter->GetAttachedActors(AttachedActors, true, true);

				for (AActor* AttachedActor : AttachedActors)
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
							RecoveryMesh = AttachedMesh;
							RecoveryOwner = AttachedActor;
							break;
						}
					}

					if (RecoveryMesh)
					{
						break;
					}
				}

				if (RecoveryMesh)
				{
					if (PrimaryMesh->GetSkeletalMeshAsset() == nullptr)
					{
						PrimaryMesh->SetSkeletalMesh(RecoveryMesh->GetSkeletalMeshAsset());
					}

					if (PrimaryMesh->GetAnimClass() == nullptr)
					{
						if (RecoveryMesh->GetAnimClass())
						{
							PrimaryMesh->SetAnimInstanceClass(RecoveryMesh->GetAnimClass());
						}
						else if (UAnimInstance* RecoveryAnimInstance = RecoveryMesh->GetAnimInstance())
						{
							PrimaryMesh->SetAnimInstanceClass(RecoveryAnimInstance->GetClass());
						}
					}

					PrimaryMesh->SetRelativeLocation(RecoveryMesh->GetRelativeLocation());
					PrimaryMesh->SetRelativeRotation(RecoveryMesh->GetRelativeRotation());
					PrimaryMesh->SetRelativeScale3D(RecoveryMesh->GetRelativeScale3D());
					PrimaryMesh->SetVisibility(true, true);
					PrimaryMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					PrimaryMesh->SetGenerateOverlapEvents(false);

					UE_LOG(LogMetaAgent, Warning,
						TEXT("BodyRecovery: '%s' recovered primary mesh '%s' from attached actor '%s' component '%s'."),
						*GetNameSafe(PossessedCharacter),
						*GetNameSafe(PrimaryMesh->GetSkeletalMeshAsset()),
						*GetNameSafe(RecoveryOwner),
						*GetNameSafe(RecoveryMesh));
				}
				else
				{
					for (TActorIterator<AActor> It(GetWorld()); It; ++It)
					{
						AActor* CandidateActor = *It;
						if (!CandidateActor || CandidateActor == PossessedCharacter)
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
								RecoveryMesh = CandidateMesh;
								RecoveryOwner = CandidateActor;
								bRecoveredFromWorldSearch = true;
								break;
							}
						}

						if (RecoveryMesh)
						{
							break;
						}
					}

					if (RecoveryMesh)
					{
						if (PrimaryMesh->GetSkeletalMeshAsset() == nullptr)
						{
							PrimaryMesh->SetSkeletalMesh(RecoveryMesh->GetSkeletalMeshAsset());
						}

						if (PrimaryMesh->GetAnimClass() == nullptr)
						{
							if (RecoveryMesh->GetAnimClass())
							{
								PrimaryMesh->SetAnimInstanceClass(RecoveryMesh->GetAnimClass());
							}
							else if (UAnimInstance* RecoveryAnimInstance = RecoveryMesh->GetAnimInstance())
							{
								PrimaryMesh->SetAnimInstanceClass(RecoveryAnimInstance->GetClass());
							}
						}

						PrimaryMesh->SetRelativeLocation(RecoveryMesh->GetRelativeLocation());
						PrimaryMesh->SetRelativeRotation(RecoveryMesh->GetRelativeRotation());
						PrimaryMesh->SetRelativeScale3D(RecoveryMesh->GetRelativeScale3D());
						PrimaryMesh->SetVisibility(true, true);
						PrimaryMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						PrimaryMesh->SetGenerateOverlapEvents(false);

						if (bRecoveredFromWorldSearch)
						{
							UE_LOG(LogMetaAgent, Warning,
								TEXT("BodyRecovery: '%s' recovered primary mesh '%s' from world actor '%s' component '%s'."),
								*GetNameSafe(PossessedCharacter),
								*GetNameSafe(PrimaryMesh->GetSkeletalMeshAsset()),
								*GetNameSafe(RecoveryOwner),
								*GetNameSafe(RecoveryMesh));
						}
					}
					else
					{
						UE_LOG(LogMetaAgent, Warning,
							TEXT("BodyRecovery: '%s' mesh '%s' has no SkeletalMesh and no attached/world visual actor with a valid skeletal mesh was found. Configure CharacterMesh0 SkeletalMesh + AnimClass in BP_MH_PlayerChar."),
							*GetNameSafe(PossessedCharacter),
							*GetNameSafe(PrimaryMesh));
					}
				}

				if (RecoveryOwner && RecoveryMesh)
				{
					// Requested tuning: face front and sit slightly lower on the ground.
					PrimaryMesh->SetRelativeRotation(FRotator(
						RecoveryMesh->GetRelativeRotation().Pitch,
						RecoveryMesh->GetRelativeRotation().Yaw - 90.0f,
						RecoveryMesh->GetRelativeRotation().Roll));
					PrimaryMesh->SetRelativeLocation(RecoveryMesh->GetRelativeLocation() + FVector(0.0f, 0.0f, -94.0f));

					if (PrimaryMesh->GetAnimClass() == nullptr)
					{
						PrimaryMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);

						if (RecoveryMesh->GetAnimClass())
						{
							PrimaryMesh->SetAnimInstanceClass(RecoveryMesh->GetAnimClass());
						}
						else if (UAnimInstance* RecoveryAnimInstance = RecoveryMesh->GetAnimInstance())
						{
							PrimaryMesh->SetAnimInstanceClass(RecoveryAnimInstance->GetClass());
						}

						if (PrimaryMesh->GetAnimClass() == nullptr)
						{
							if (AActor* RecoveryCDO = Cast<AActor>(RecoveryOwner->GetClass()->GetDefaultObject()))
							{
								TInlineComponentArray<USkeletalMeshComponent*> DefaultMeshes;
								RecoveryCDO->GetComponents(DefaultMeshes);
								const USkeleton* PrimarySkeleton =
									PrimaryMesh->GetSkeletalMeshAsset() ? PrimaryMesh->GetSkeletalMeshAsset()->GetSkeleton() : nullptr;

								for (USkeletalMeshComponent* DefaultMeshComp : DefaultMeshes)
								{
									if (!DefaultMeshComp)
									{
										continue;
									}

									if (DefaultMeshComp->GetFName() == RecoveryMesh->GetFName() && DefaultMeshComp->GetAnimClass())
									{
										PrimaryMesh->SetAnimInstanceClass(DefaultMeshComp->GetAnimClass());
										break;
									}

									if (PrimarySkeleton && DefaultMeshComp->GetAnimClass() &&
										DefaultMeshComp->GetSkeletalMeshAsset() &&
										DefaultMeshComp->GetSkeletalMeshAsset()->GetSkeleton() == PrimarySkeleton)
									{
										PrimaryMesh->SetAnimInstanceClass(DefaultMeshComp->GetAnimClass());
										break;
									}
								}
							}
						}

						if (PrimaryMesh->GetAnimClass() == nullptr)
						{
							if (ACharacter* PossessedCDOCharacter = Cast<ACharacter>(PossessedCharacter->GetClass()->GetDefaultObject()))
							{
								if (USkeletalMeshComponent* PossessedCDOMesh = PossessedCDOCharacter->GetMesh())
								{
									if (PossessedCDOMesh->GetAnimClass())
									{
										PrimaryMesh->SetAnimInstanceClass(PossessedCDOMesh->GetAnimClass());
									}
								}
							}
						}

						if (PrimaryMesh->GetAnimClass() == nullptr && PrimaryMesh->GetSkeletalMeshAsset())
						{
							const USkeleton* PrimarySkeleton = PrimaryMesh->GetSkeletalMeshAsset()->GetSkeleton();
							if (PrimarySkeleton)
							{
								for (TActorIterator<AActor> It(GetWorld()); It; ++It)
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
											PrimaryMesh->SetAnimInstanceClass(CandidateMesh->GetAnimClass());
											break;
										}

										if (UAnimInstance* CandidateAnimInstance = CandidateMesh->GetAnimInstance())
										{
											PrimaryMesh->SetAnimInstanceClass(CandidateAnimInstance->GetClass());
											break;
										}
									}

									if (PrimaryMesh->GetAnimClass())
									{
										break;
									}
								}
							}
						}

						if (PrimaryMesh->GetAnimClass() == nullptr)
						{
							static const TCHAR* FallbackAnimClassPaths[] =
							{
								TEXT("/Game/CitySampleCrowd/Character/Male/Rig/m_tal_nrw_animbp.m_tal_nrw_animbp_C"),
								TEXT("/Game/CitySampleCrowd/Character/Female/Rig/f_tal_nrw_animbp.f_tal_nrw_animbp_C"),
								TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C")
							};

							for (const TCHAR* AnimClassPath : FallbackAnimClassPaths)
							{
								if (!AnimClassPath)
								{
									continue;
								}

								if (UClass* LoadedAnimClass = StaticLoadClass(UAnimInstance::StaticClass(), nullptr, AnimClassPath))
								{
									PrimaryMesh->SetAnimInstanceClass(LoadedAnimClass);
									UE_LOG(LogMetaAgent, Warning,
										TEXT("BodyRecovery: Loaded fallback AnimClass '%s' for primary mesh '%s'."),
										AnimClassPath,
										*GetNameSafe(PrimaryMesh));
									break;
								}
							}
						}

						if (PrimaryMesh->GetAnimClass())
						{
							PrimaryMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
							PrimaryMesh->InitAnim(true);
							UE_LOG(LogMetaAgent, Warning,
								TEXT("BodyRecovery: Applied animation class '%s' to primary mesh '%s'."),
								*GetNameSafe(PrimaryMesh->GetAnimClass()),
								*GetNameSafe(PrimaryMesh));
						}
						else
						{
							UE_LOG(LogMetaAgent, Warning,
								TEXT("BodyRecovery: Unable to resolve an AnimClass for primary mesh '%s'."),
								*GetNameSafe(PrimaryMesh));
						}
					}

					TInlineComponentArray<UMeshComponent*> SourceMeshes;
					RecoveryOwner->GetComponents(SourceMeshes);
					TMap<const USceneComponent*, USceneComponent*> SourceToRecoveredSceneMap;

					// First pass: create/find recovered components.

					for (UMeshComponent* SourceMeshComp : SourceMeshes)
					{
						if (!SourceMeshComp || SourceMeshComp == RecoveryMesh)
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
						UMeshComponent* RecoveredComp = FindObject<UMeshComponent>(PossessedCharacter, *RecoveredName);
						if (!RecoveredComp)
						{
							RecoveredComp = DuplicateObject<UMeshComponent>(SourceMeshComp, PossessedCharacter, *RecoveredName);
							if (!RecoveredComp)
							{
								continue;
							}

							PossessedCharacter->AddInstanceComponent(RecoveredComp);
							RecoveredComp->SetVisibility(true, true);
							RecoveredComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
							RecoveredComp->SetGenerateOverlapEvents(false);
							RecoveredComp->RegisterComponent();

							UE_LOG(LogMetaAgent, Warning,
								TEXT("BodyRecovery: Added follower component '%s' class '%s' from source '%s' on actor '%s'."),
								*GetNameSafe(RecoveredComp),
								*GetNameSafe(RecoveredComp->GetClass()),
								*GetNameSafe(SourceMeshComp),
								*GetNameSafe(RecoveryOwner));
						}

						if (const USceneComponent* SourceScene = Cast<USceneComponent>(SourceMeshComp))
						{
							if (USceneComponent* RecoveredScene = Cast<USceneComponent>(RecoveredComp))
							{
								SourceToRecoveredSceneMap.FindOrAdd(SourceScene) = RecoveredScene;
							}
						}
					}

					// Second pass: preserve source hierarchy and safe pose links.
					for (UMeshComponent* SourceMeshComp : SourceMeshes)
					{
						if (!SourceMeshComp || SourceMeshComp == RecoveryMesh)
						{
							continue;
						}

						const FString RecoveredName = FString::Printf(TEXT("MetaAgentRecovered_%s"), *SourceMeshComp->GetName());
						UMeshComponent* RecoveredComp = FindObject<UMeshComponent>(PossessedCharacter, *RecoveredName);
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

								if (SourceParent == RecoveryMesh)
								{
									TargetParent = PrimaryMesh;
								}
								else if (SourceParent)
								{
									if (USceneComponent* const* FoundParent = SourceToRecoveredSceneMap.Find(SourceParent))
									{
										TargetParent = *FoundParent;
									}
								}

								if (!TargetParent)
								{
									TargetParent = PrimaryMesh;
								}

								if (RecoveredScene->GetAttachParent() != TargetParent)
								{
									RecoveredScene->AttachToComponent(TargetParent, FAttachmentTransformRules::KeepRelativeTransform);
								}

								RecoveredScene->SetRelativeTransform(SourceScene->GetRelativeTransform());
							}
						}

						if (USkinnedMeshComponent* RecoveredSkinned = Cast<USkinnedMeshComponent>(RecoveredComp))
						{
							RecoveredSkinned->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
							RecoveredSkinned->bEnableUpdateRateOptimizations = false;

							if (USkinnedMeshComponent* SourceSkinned = Cast<USkinnedMeshComponent>(SourceMeshComp))
							{
								if (USkinnedMeshComponent* SourceLeader = SourceSkinned->LeaderPoseComponent.Get())
								{
									USkinnedMeshComponent* TargetLeader = nullptr;

									if (SourceLeader == RecoveryMesh)
									{
										TargetLeader = PrimaryMesh;
									}
									else
									{
										const FString LeaderRecoveredName = FString::Printf(TEXT("MetaAgentRecovered_%s"), *SourceLeader->GetName());
										TargetLeader = FindObject<USkinnedMeshComponent>(PossessedCharacter, *LeaderRecoveredName);
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
			}
		}

		TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes;
		PossessedCharacter->GetComponents(SkeletalMeshes);

		USkeletalMeshComponent* DrivingBodyMesh = nullptr;
		for (USkeletalMeshComponent* MeshComp : SkeletalMeshes)
		{
			if (!MeshComp || !MeshComp->GetSkeletalMeshAsset())
			{
				continue;
			}

			if (MeshComp->GetAnimInstance())
			{
				DrivingBodyMesh = MeshComp;
				if (MeshComp->GetFName() == TEXT("Body"))
				{
					break;
				}
			}
		}

		if (DrivingBodyMesh)
		{
			DrivingBodyMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			DrivingBodyMesh->bEnableUpdateRateOptimizations = false;
		}

		for (USkeletalMeshComponent* MeshComp : SkeletalMeshes)
		{
			if (!MeshComp)
			{
				continue;
			}

			if (MeshComp->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("MovementGuard: '%s' mesh '%s' had collision enabled (%d); forcing NoCollision to avoid movement drag."),
					*GetNameSafe(PossessedCharacter),
					*GetNameSafe(MeshComp),
					static_cast<int32>(MeshComp->GetCollisionEnabled()));
				MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}

			if (MeshComp->GetGenerateOverlapEvents())
			{
				MeshComp->SetGenerateOverlapEvents(false);
			}

			if (DrivingBodyMesh && MeshComp != DrivingBodyMesh && MeshComp->GetSkeletalMeshAsset() && DrivingBodyMesh->GetSkeletalMeshAsset())
			{
				const USkeleton* MeshSkeleton = MeshComp->GetSkeletalMeshAsset()->GetSkeleton();
				const USkeleton* BodySkeleton = DrivingBodyMesh->GetSkeletalMeshAsset()->GetSkeleton();
				const bool bSharesBodySkeleton = (MeshSkeleton && BodySkeleton && MeshSkeleton == BodySkeleton);

				if (bSharesBodySkeleton && MeshComp->GetAnimInstance() == nullptr)
				{
					MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
					MeshComp->bEnableUpdateRateOptimizations = false;
					MeshComp->SetLeaderPoseComponent(DrivingBodyMesh, true, true);
					UE_LOG(LogMetaAgent, Warning,
						TEXT("MovementGuard: '%s' mesh '%s' now follows '%s' via LeaderPose (same skeleton, no anim instance, follower ticks pose)."),
						*GetNameSafe(PossessedCharacter),
						*GetNameSafe(MeshComp),
						*GetNameSafe(DrivingBodyMesh));
				}
			}
		}

		if (UCharacterMovementComponent* MovementComp = PossessedCharacter->GetCharacterMovement())
		{
			if (MovementComp->MaxWalkSpeed < 1.0f)
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("MovementGuard: '%s' MaxWalkSpeed was %.2f; clamping to fallback walk speed %.2f."),
					*GetNameSafe(PossessedCharacter),
					MovementComp->MaxWalkSpeed,
					InputFallback.WalkSpeed);
				MovementComp->MaxWalkSpeed = FMath::Max(1.0f, InputFallback.WalkSpeed);
			}

			if (MovementComp->MaxAcceleration < 500.0f)
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("MovementGuard: '%s' MaxAcceleration was %.2f; clamping to 2048.00."),
					*GetNameSafe(PossessedCharacter),
					MovementComp->MaxAcceleration);
				MovementComp->MaxAcceleration = 2048.0f;
			}
		}
	}

	UE_LOG(LogMetaAgent, Log,
		TEXT("AMetaAgentPlayerController: Possessed '%s' (%s)."),
		*GetNameSafe(InPawn),
		*InPawn->GetClass()->GetName());
}

void AMetaAgentPlayerController::ResetMovementDiagnosticsState()
{
	MovementDiagnostics.bLoggedMovementAnimDiagnostics = false;
	MovementDiagnostics.bMovementProbeActive = false;
	MovementDiagnostics.ProbeElapsedSeconds = 0.0f;
	MovementDiagnostics.ProbePeakSpeed2D = 0.0f;
	MovementDiagnostics.ProbePeakAcceleration2D = 0.0f;
	MovementDiagnostics.ProbeSampleCount = 0;
}

void AMetaAgentPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (!IsLocalPlayerController())
	{
		return;
	}

	if (CinematicCamera.bModeEnabled)
	{
		UpdateCinematicCamera(DeltaTime);
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn && !CinematicCamera.bModeEnabled)
	{
		return;
	}

	ApplyFallbackMovementInput(ControlledPawn);
	ApplyFallbackLookInput();
	LogMovementAnimationDiagnostics(ControlledPawn);
	UpdateMovementProbe(ControlledPawn, DeltaTime);

	if (ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn))
	{
		if (USkeletalMeshComponent* PrimaryMesh = ControlledCharacter->GetMesh())
		{
			UClass* ActiveAnimClass = PrimaryMesh->GetAnimClass();
			const bool bUsingCrowdFallbackClass =
				ActiveAnimClass && ActiveAnimClass->GetName().Contains(TEXT("tal_nrw_animbp"), ESearchCase::IgnoreCase);

			// If the crowd fallback AnimBP is present but still visually static on this pawn,
			// force a deterministic idle/walk single-node animation from the same content set.
			if (bUsingCrowdFallbackClass)
			{
				static UAnimationAsset* IdleAsset = nullptr;
				static UAnimationAsset* WalkAsset = nullptr;

				if (!IdleAsset)
				{
					IdleAsset = Cast<UAnimationAsset>(StaticLoadObject(UAnimationAsset::StaticClass(), nullptr,
						TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTN_N_Idle.MTN_N_Idle")));
				}

				if (!WalkAsset)
				{
					WalkAsset = Cast<UAnimationAsset>(StaticLoadObject(UAnimationAsset::StaticClass(), nullptr,
						TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTN_N_Walk_InPlace.MTN_N_Walk_InPlace")));
				}

				if (IdleAsset && WalkAsset)
				{
					const float Speed2D = ControlledCharacter->GetVelocity().Size2D();
					UAnimationAsset* DesiredAsset = (Speed2D > 50.0f) ? WalkAsset : IdleAsset;

					if (PrimaryMesh->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
					{
						PrimaryMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					}

					UAnimSingleNodeInstance* SingleNodeInstance = PrimaryMesh->GetSingleNodeInstance();
					UAnimationAsset* CurrentAsset = SingleNodeInstance ? SingleNodeInstance->GetAnimationAsset() : nullptr;
					if (CurrentAsset != DesiredAsset)
					{
						PrimaryMesh->PlayAnimation(DesiredAsset, true);
					}
				}
			}
		}
	}

	if (ControlledPawn && !CinematicCamera.bModeEnabled)
	{
		ApplyMouseWheelZoom(ControlledPawn, DeltaTime);
	}

	if (Recording.bRenderInProgress && GEngine)
	{
		if (UMoviePipelineQueueEngineSubsystem* RenderSubsystem = GEngine->GetEngineSubsystem<UMoviePipelineQueueEngineSubsystem>())
		{
			if (UMoviePipelineExecutorBase* ActiveExecutor = RenderSubsystem->GetActiveExecutor())
			{
				const float RenderProgress = FMath::Clamp(ActiveExecutor->GetStatusProgress(), 0.0f, 1.0f);
				Recording.RenderStatusText = FString::Printf(TEXT("Rendering: %.0f%%"), RenderProgress * 100.0f);
				Recording.RenderStatusColor = FColor::Yellow;
				UpdateRecordingStatusHud();
			}
		}
	}
}

void AMetaAgentPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (IsLocalPlayerController() && !ShouldUseTouchControls())
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
		bShowMouseCursor = false;
		SetIgnoreLookInput(false);
		SetIgnoreMoveInput(false);
	}

	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogMetaAgent, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}

	UpdateRecordingStatusHud();
}

void AMetaAgentPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (InputComponent)
	{
		if (!InputFallback.bUtilityKeysBound)
		{
			InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AMetaAgentPlayerController::HandleEscapePressed);
			InputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AMetaAgentPlayerController::HandleYPressed);
			InputComponent->BindKey(EKeys::J, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleAutopilotPressed);
			InputComponent->BindKey(EKeys::U, IE_Pressed, this, &AMetaAgentPlayerController::HandleRenderRecordedTakePressed);
			InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AMetaAgentPlayerController::HandleToggleCinematicCameraPressed);
			InputFallback.bUtilityKeysBound = true;
		}
	}

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		InputFallback.bAddedAnyMappingContext = false;

		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			if (DefaultMappingContexts.Num() == 0)
			{
				if (UInputMappingContext* DefaultContext = ResolveMappingContextWithFallback(
					DefaultMappingContextAsset,
					TEXT("/Game/Input/IMC_Default.IMC_Default"),
					TEXT("DefaultMappingContext"),
					this))
				{
					DefaultMappingContexts.Add(DefaultContext);
				}
			}

			if (MobileExcludedMappingContexts.Num() == 0)
			{
				if (UInputMappingContext* MouseContext = ResolveMappingContextWithFallback(
					MouseLookMappingContextAsset,
					TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"),
					TEXT("MouseLookMappingContext"),
					this))
				{
					MobileExcludedMappingContexts.Add(MouseContext);
				}
			}

			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				if (CurrentContext)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
					InputFallback.bAddedAnyMappingContext = true;
				}
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					if (CurrentContext)
					{
						Subsystem->AddMappingContext(CurrentContext, 0);
						InputFallback.bAddedAnyMappingContext = true;
					}
				}
			}

			if (!InputFallback.bAddedAnyMappingContext)
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("AMetaAgentPlayerController: No input mapping contexts were added. Raw keyboard/mouse fallback remains active."));
			}
		}
		else
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("AMetaAgentPlayerController: EnhancedInputLocalPlayerSubsystem unavailable. Raw keyboard/mouse fallback remains active."));
		}
	}
}

void AMetaAgentPlayerController::HandleEscapePressed()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void AMetaAgentPlayerController::HandleYPressed()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UE_LOG(LogMetaAgent, Log, TEXT("Y pressed: requesting platform agent toggle."));

	if (UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
	{
		const FString SourceLabel = GIsEditor ? TEXT("unreal-editor") : TEXT("unreal-standalone");
		GI->SendEventToPlatform(TEXT("key_pressed"), TEXT("toggle_agent"), SourceLabel);
	}
}

bool AMetaAgentPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

