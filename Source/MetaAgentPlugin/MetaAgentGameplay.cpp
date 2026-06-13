// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "MetaAgentGameplay.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BrainComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "MetaAgentPlugin.h"
#include "MetaAgentGameplay.h"
#include "MetaAgentPlayerController.h"
#include "MetaAgentHUD.h"
#include "MetaAgentPlugin.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Engine/Canvas.h"
#include "HttpModule.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "MetaAgentParticleControl.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// ===== MetaAgentGameMode.cpp =====
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


// ===== MetaAgentCharacter.cpp =====
namespace
{
	UInputAction* ResolveInputActionWithFallback(
		UInputAction* ExistingAction,
		const TSoftObjectPtr<UInputAction>& SoftReference,
		const TCHAR* LegacyPath,
		const TCHAR* ActionLabel,
		const UObject* Owner)
	{
		if (ExistingAction)
		{
			return ExistingAction;
		}

		if (!SoftReference.IsNull())
		{
			if (UInputAction* LoadedFromSoftRef = SoftReference.LoadSynchronous())
			{
				return LoadedFromSoftRef;
			}

			UE_LOG(LogMetaAgent, Warning,
				TEXT("'%s' failed to load soft input action '%s' for %s."),
				*GetNameSafe(Owner),
				*SoftReference.ToString(),
				ActionLabel);
		}

		if (LegacyPath)
		{
			if (UInputAction* LoadedFromLegacyPath = Cast<UInputAction>(StaticLoadObject(UInputAction::StaticClass(), nullptr, LegacyPath)))
			{
				return LoadedFromLegacyPath;
			}
		}

		UE_LOG(LogMetaAgent, Warning,
			TEXT("'%s' has no valid input action for %s. Configure asset references in Blueprint/Class Defaults."),
			*GetNameSafe(Owner),
			ActionLabel);

		return nullptr;
	}
}

AMetaAgentCharacter::AMetaAgentCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 300.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 350.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AMetaAgentCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!JumpAction)
	{
		JumpAction = ResolveInputActionWithFallback(
			JumpAction,
			JumpActionAsset,
			TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"),
			TEXT("JumpAction"),
			this);
	}

	if (!MoveAction)
	{
		MoveAction = ResolveInputActionWithFallback(
			MoveAction,
			MoveActionAsset,
			TEXT("/Game/Input/Actions/IA_Move.IA_Move"),
			TEXT("MoveAction"),
			this);
	}

	if (!LookAction)
	{
		LookAction = ResolveInputActionWithFallback(
			LookAction,
			LookActionAsset,
			TEXT("/Game/Input/Actions/IA_Look.IA_Look"),
			TEXT("LookAction"),
			this);
	}

	if (!MouseLookAction)
	{
		MouseLookAction = ResolveInputActionWithFallback(
			MouseLookAction,
			MouseLookActionAsset,
			TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"),
			TEXT("MouseLookAction"),
			this);
	}

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		if (JumpAction)
		{
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		}

		// Moving
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMetaAgentCharacter::Move);
		}

		if (MouseLookAction)
		{
			EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMetaAgentCharacter::Look);
		}

		// Looking
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMetaAgentCharacter::Look);
		}

		if (!MoveAction || (!LookAction && !MouseLookAction))
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("'%s' has missing input actions (Move/Look). Check BP defaults or /Game/Input/Actions assets."),
				*GetNameSafe(this));
		}

	}
	else
	{
		UE_LOG(LogMetaAgent, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AMetaAgentCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AMetaAgentCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AMetaAgentCharacter::DoMove(float Right, float Forward)
{
	if (const AMetaAgentPlayerController* MetaAgentController = Cast<AMetaAgentPlayerController>(GetController()))
	{
		if (!MetaAgentController->IsModularRuntimeEnabled(EMetaAgentModularRuntime::CharacterInput))
		{
			return;
		}
	}

	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AMetaAgentCharacter::DoLook(float Yaw, float Pitch)
{
	if (const AMetaAgentPlayerController* MetaAgentController = Cast<AMetaAgentPlayerController>(GetController()))
	{
		if (!MetaAgentController->IsModularRuntimeEnabled(EMetaAgentModularRuntime::CharacterInput))
		{
			return;
		}
	}

	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AMetaAgentCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AMetaAgentCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

void AMetaAgentCharacter::PrintHelloWorld()
{
	const FVector CharacterLocation = GetActorLocation();
	const FString PositionText = FString::Printf(
		TEXT("Character Position: X=%.2f Y=%.2f Z=%.2f"),
		CharacterLocation.X,
		CharacterLocation.Y,
		CharacterLocation.Z);

	UE_LOG(LogMetaAgent, Log, TEXT("Hello World!"));
	UE_LOG(LogMetaAgent, Log, TEXT("%s"), *PositionText);

	if (const UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
	{
		const FString ServerStatus = GI->GetLocalHttpServerStatusText();
		UE_LOG(LogMetaAgent, Log, TEXT("%s"), *ServerStatus);
	}

}

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
}


// ===== MetaAgentMHPlayer.cpp =====
AMetaAgentMHPlayer::AMetaAgentMHPlayer()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AMetaAgentMHPlayer::BeginPlay()
{
	Super::BeginPlay();

	if (UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
	{
		GI->ActivePlayerCharacter = this;
	}
}



// ===== MetaAgentBTTasks.cpp =====
namespace
{
	constexpr double NoNavWarningCooldownSeconds = 2.0;
	double GLastNoNavWarningTimeSeconds = -1000.0;

	FVector MakeFallbackRandomPoint(const FVector& Origin, const float Radius)
	{
		const FVector RandomDir3D = FMath::VRand();
		const FVector2D RandomDir2D = FVector2D(RandomDir3D.X, RandomDir3D.Y).GetSafeNormal();
		const float Distance = FMath::FRandRange(Radius * 0.35f, Radius);
		return Origin + FVector(RandomDir2D.X * Distance, RandomDir2D.Y * Distance, 0.0f);
	}
}

UMetaAgentBTTask_SetRandomPatrolPoint::UMetaAgentBTTask_SetRandomPatrolPoint()
{
	NodeName = TEXT("Choose Random Patrol Point");
	BlackboardKey.SelectedKeyName = TEXT("PatrolLocation");
}

EBTNodeResult::Type UMetaAgentBTTask_SetRandomPatrolPoint::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		return EBTNodeResult::Failed;
	}

	APawn* ControlledPawn = AIController->GetPawn();
	UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if (!ControlledPawn || !BlackboardComp)
	{
		return EBTNodeResult::Failed;
	}

	const FVector Origin = ControlledPawn->GetActorLocation();
	const UWorld* World = ControlledPawn->GetWorld();
	FNavLocation RandomLocation;

	if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(ControlledPawn->GetWorld()))
	{
		FNavLocation ProjectedOrigin;
		const bool bProjectedToNav = NavSystem->ProjectPointToNavigation(
			Origin,
			ProjectedOrigin,
			FVector(5000.0f, 5000.0f, 2000.0f));

		const FVector SamplingOrigin = bProjectedToNav ? ProjectedOrigin.Location : Origin;

		if (NavSystem->GetRandomReachablePointInRadius(SamplingOrigin, SearchRadius, RandomLocation))
		{
			BlackboardComp->SetValueAsVector(GetSelectedBlackboardKey(), RandomLocation.Location);
			return EBTNodeResult::Succeeded;
		}

		if (bProjectedToNav)
		{
			BlackboardComp->SetValueAsVector(GetSelectedBlackboardKey(), ProjectedOrigin.Location);
			return EBTNodeResult::Succeeded;
		}

		const FVector FallbackPoint = MakeFallbackRandomPoint(Origin, SearchRadius);
		BlackboardComp->SetValueAsVector(GetSelectedBlackboardKey(), FallbackPoint);

		const double Now = World ? World->GetTimeSeconds() : 0.0;
		if ((Now - GLastNoNavWarningTimeSeconds) >= NoNavWarningCooldownSeconds)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("WanderAI: no reachable nav point found (origin=%s radius=%.1f). Using non-nav fallback patrol point."),
				*Origin.ToCompactString(),
				SearchRadius);
			GLastNoNavWarningTimeSeconds = Now;
		}

		return EBTNodeResult::Succeeded;
	}

	const FVector FallbackPoint = MakeFallbackRandomPoint(Origin, SearchRadius);
	BlackboardComp->SetValueAsVector(GetSelectedBlackboardKey(), FallbackPoint);

	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if ((Now - GLastNoNavWarningTimeSeconds) >= NoNavWarningCooldownSeconds)
	{
		UE_LOG(LogTemp, Warning, TEXT("WanderAI: NavigationSystem missing; using non-nav fallback patrol point."));
		GLastNoNavWarningTimeSeconds = Now;
	}

	return EBTNodeResult::Succeeded;
}

UMetaAgentBTTask_MoveToPatrolPoint::UMetaAgentBTTask_MoveToPatrolPoint()
{
	NodeName = TEXT("Walk To Patrol Location");
	BlackboardKey.SelectedKeyName = TEXT("PatrolLocation");
	bNotifyTick = true;
}

EBTNodeResult::Type UMetaAgentBTTask_MoveToPatrolPoint::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if (!AIController || !BlackboardComp)
	{
		return EBTNodeResult::Failed;
	}

	APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn)
	{
		return EBTNodeResult::Failed;
	}

	const FVector TargetLocation = BlackboardComp->GetValueAsVector(GetSelectedBlackboardKey());
	if (TargetLocation.ContainsNaN())
	{
		return EBTNodeResult::Failed;
	}

	const float Distance2D = FVector::Dist2D(ControlledPawn->GetActorLocation(), TargetLocation);
	if (Distance2D <= AcceptableDistance)
	{
		return EBTNodeResult::Succeeded;
	}

	FMoveToPatrolMemory* Memory = reinterpret_cast<FMoveToPatrolMemory*>(NodeMemory);
	Memory->ElapsedSeconds = 0.0f;
	Memory->StuckSeconds = 0.0f;
	Memory->LastLocation = ControlledPawn->GetActorLocation();

	return EBTNodeResult::InProgress;
}

void UMetaAgentBTTask_MoveToPatrolPoint::TickTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if (!AIController || !BlackboardComp)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const FVector TargetLocation = BlackboardComp->GetValueAsVector(GetSelectedBlackboardKey());
	const FVector PawnLocation = ControlledPawn->GetActorLocation();
	const FVector ToTarget = TargetLocation - PawnLocation;
	const float Distance2D = FVector::Dist2D(PawnLocation, TargetLocation);

	if (Distance2D <= AcceptableDistance)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	const FVector MoveDirection = FVector(ToTarget.X, ToTarget.Y, 0.0f).GetSafeNormal();
	if (MoveDirection.IsNearlyZero())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	AIController->SetControlRotation(MoveDirection.Rotation());
	ControlledPawn->AddMovementInput(MoveDirection, MoveInputScale);

	FMoveToPatrolMemory* Memory = reinterpret_cast<FMoveToPatrolMemory*>(NodeMemory);
	Memory->ElapsedSeconds += DeltaSeconds;

	const float TravelSinceLastTick2D = FVector::Dist2D(PawnLocation, Memory->LastLocation);
	if (TravelSinceLastTick2D < 1.0f)
	{
		Memory->StuckSeconds += DeltaSeconds;
	}
	else
	{
		Memory->StuckSeconds = 0.0f;
	}

	Memory->LastLocation = PawnLocation;

	if (Memory->StuckSeconds >= StuckTimeoutSeconds || Memory->ElapsedSeconds >= MaxMoveSeconds)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}
}

uint16 UMetaAgentBTTask_MoveToPatrolPoint::GetInstanceMemorySize() const
{
	return sizeof(FMoveToPatrolMemory);
}

// ===== MetaAgentWanderAIController.cpp =====
namespace
{
	const FName PatrolLocationKeyName(TEXT("PatrolLocation"));
}

AMetaAgentWanderAIController::AMetaAgentWanderAIController()
{
	bStartAILogicOnPossess = true;
}

void AMetaAgentWanderAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!IsValid(InPawn))
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("WanderAI: OnPossess received invalid pawn."));
		return;
	}

	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (UBrainComponent* Brain = GetBrainComponent())
	{
		Brain->StopLogic(TEXT("Reinitialize runtime behavior tree"));
	}

	BuildRuntimeBehaviorTree();
	StartRuntimeBehaviorTree();
}

void AMetaAgentWanderAIController::OnUnPossess()
{
	if (UBrainComponent* Brain = GetBrainComponent())
	{
		Brain->StopLogic(TEXT("Pawn unpossessed"));
	}

	StopMovement();
	Super::OnUnPossess();
}

void AMetaAgentWanderAIController::BuildRuntimeBehaviorTree()
{
	if (!RuntimeBlackboard)
	{
		RuntimeBlackboard = NewObject<UBlackboardData>(this, TEXT("BB_RuntimeWander"));
		RuntimeBlackboard->UpdatePersistentKey<UBlackboardKeyType_Vector>(PatrolLocationKeyName);
		RuntimeBlackboard->UpdateKeyIDs();
	}

	if (RuntimeBehaviorTree)
	{
		return;
	}

	RuntimeBehaviorTree = NewObject<UBehaviorTree>(this, TEXT("BT_RuntimeWander"));
	RuntimeBehaviorTree->BlackboardAsset = RuntimeBlackboard;

	UBTComposite_Selector* RootSelector = NewObject<UBTComposite_Selector>(RuntimeBehaviorTree, TEXT("RootSelector"));
	RuntimeBehaviorTree->RootNode = RootSelector;

	UBTComposite_Sequence* PatrolSequence = NewObject<UBTComposite_Sequence>(RootSelector, TEXT("PatrolSequence"));
	FBTCompositeChild& PatrolBranch = RootSelector->Children.AddDefaulted_GetRef();
	PatrolBranch.ChildComposite = PatrolSequence;

	UMetaAgentBTTask_SetRandomPatrolPoint* RandomPointTask = NewObject<UMetaAgentBTTask_SetRandomPatrolPoint>(PatrolSequence, TEXT("Task_SetRandomPoint"));
	RandomPointTask->SearchRadius = FMath::Max(100.0f, PatrolRadius);

	UMetaAgentBTTask_MoveToPatrolPoint* MoveToTask = NewObject<UMetaAgentBTTask_MoveToPatrolPoint>(PatrolSequence, TEXT("Task_MoveToPoint"));

	UBTTask_Wait* WaitTask = NewObject<UBTTask_Wait>(PatrolSequence, TEXT("Task_Wait"));
	const float ClampedWaitMin = FMath::Max(0.1f, WaitMinSeconds);
	const float ClampedWaitMax = FMath::Max(ClampedWaitMin, WaitMaxSeconds);
	WaitTask->WaitTime = FValueOrBBKey_Float(ClampedWaitMin);
	WaitTask->RandomDeviation = FValueOrBBKey_Float(ClampedWaitMax - ClampedWaitMin);

	FBTCompositeChild& ChildSetRandom = PatrolSequence->Children.AddDefaulted_GetRef();
	ChildSetRandom.ChildTask = RandomPointTask;

	FBTCompositeChild& ChildMoveTo = PatrolSequence->Children.AddDefaulted_GetRef();
	ChildMoveTo.ChildTask = MoveToTask;

	FBTCompositeChild& ChildWait = PatrolSequence->Children.AddDefaulted_GetRef();
	ChildWait.ChildTask = WaitTask;

	UBTTask_Wait* FallbackWaitTask = NewObject<UBTTask_Wait>(RootSelector, TEXT("Task_FallbackWait"));
	FallbackWaitTask->WaitTime = FValueOrBBKey_Float(1.0f);
	FallbackWaitTask->RandomDeviation = FValueOrBBKey_Float(0.5f);

	FBTCompositeChild& FallbackBranch = RootSelector->Children.AddDefaulted_GetRef();
	FallbackBranch.ChildTask = FallbackWaitTask;
}

void AMetaAgentWanderAIController::StartRuntimeBehaviorTree()
{
	if (!RuntimeBlackboard || !RuntimeBehaviorTree)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("WanderAI: runtime tree initialization failed (missing assets)."));
		return;
	}

	UBlackboardComponent* BlackboardComp = nullptr;
	if (!UseBlackboard(RuntimeBlackboard, BlackboardComp) || !BlackboardComp)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("WanderAI: failed to initialize blackboard."));
		return;
	}

	if (APawn* ControlledPawn = GetPawn())
	{
		BlackboardComp->SetValueAsVector(PatrolLocationKeyName, ControlledPawn->GetActorLocation());
	}

	if (!RunBehaviorTree(RuntimeBehaviorTree))
	{
		UE_LOG(LogMetaAgent, Error, TEXT("WanderAI: failed to run runtime behavior tree."));
		return;
	}

	UE_LOG(LogMetaAgent, Log,
		TEXT("WanderAI: runtime behavior tree started (radius=%.1f wait=[%.1f, %.1f])."),
		PatrolRadius,
		WaitMinSeconds,
		WaitMaxSeconds);
}


// ===== MetaAgentCameraRuntime.cpp =====
namespace
{
	void SanitizeCameraZoomState(FMetaAgentCameraZoomState& CameraZoom)
	{
		CameraZoom.MinDistance = FMath::Max(0.0f, CameraZoom.MinDistance);
		CameraZoom.MaxDistance = FMath::Max(CameraZoom.MinDistance + 1.0f, CameraZoom.MaxDistance);
		CameraZoom.MouseWheelStep = FMath::Max(1.0f, CameraZoom.MouseWheelStep);
		CameraZoom.InterpSpeed = FMath::Max(0.01f, CameraZoom.InterpSpeed);

		if (CameraZoom.DesiredDistance >= 0.0f)
		{
			CameraZoom.DesiredDistance = FMath::Clamp(CameraZoom.DesiredDistance, CameraZoom.MinDistance, CameraZoom.MaxDistance);
		}
	}

	void SanitizeCinematicCameraState(FMetaAgentCinematicCameraState& CinematicCamera)
	{
		CinematicCamera.BlendInSeconds = FMath::Max(0.0f, CinematicCamera.BlendInSeconds);
		CinematicCamera.BlendOutSeconds = FMath::Max(0.0f, CinematicCamera.BlendOutSeconds);
		CinematicCamera.PanDegrees = FMath::Clamp(CinematicCamera.PanDegrees, 1.0f, 360.0f);
		CinematicCamera.PanDurationSeconds = FMath::Max(0.1f, CinematicCamera.PanDurationSeconds);
		CinematicCamera.OscillationYawAmplitudeDegrees = FMath::Max(0.0f, CinematicCamera.OscillationYawAmplitudeDegrees);
		CinematicCamera.OrbitSpeedScale = FMath::Max(0.01f, CinematicCamera.OrbitSpeedScale);
		CinematicCamera.TurnsPerDirection = FMath::Max(1, CinematicCamera.TurnsPerDirection);
		CinematicCamera.CloseOrbitRadius = FMath::Max(60.0f, CinematicCamera.CloseOrbitRadius);
		CinematicCamera.SwayHorizontalAmplitude = FMath::Max(0.0f, CinematicCamera.SwayHorizontalAmplitude);
		CinematicCamera.SwayVerticalAmplitude = FMath::Max(0.0f, CinematicCamera.SwayVerticalAmplitude);
		CinematicCamera.SwayFrequency = FMath::Max(0.1f, CinematicCamera.SwayFrequency);
	}
}

const TCHAR* FMetaAgentCameraRuntime::GetCinematicStyleLabel(const EMetaAgentCinematicCameraStyle Style)
{
	switch (Style)
	{
	case EMetaAgentCinematicCameraStyle::OscillatingHold:
	default:
		return TEXT("OscillatingHold");
	}
}

void FMetaAgentCameraRuntime::RunEnvironmentZoomSequence(
	AMetaAgentPlayerController& Controller,
	const float DeltaTime,
	FMetaAgentCameraZoomState& CameraZoom)
{
	SanitizeCameraZoomState(CameraZoom);

	if (!Controller.IsLocalPlayerController())
	{
		return;
	}

	// Consume discrete mouse wheel input
	bool bConsumedDiscreteWheelInput = false;
	if (Controller.WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		CameraZoom.DesiredDistance -= CameraZoom.MouseWheelStep;
		bConsumedDiscreteWheelInput = true;
	}
	if (Controller.WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		CameraZoom.DesiredDistance += CameraZoom.MouseWheelStep;
		bConsumedDiscreteWheelInput = true;
	}

	// Consume analog wheel-axis input if discrete was not used
	if (!bConsumedDiscreteWheelInput)
	{
		const float WheelAxis = Controller.GetInputAnalogKeyState(EKeys::MouseWheelAxis);
		if (!FMath::IsNearlyZero(WheelAxis, KINDA_SMALL_NUMBER))
		{
			CameraZoom.DesiredDistance -= WheelAxis * CameraZoom.MouseWheelStep;
		}
	}

	// Clamp zoom distance to bounds
	CameraZoom.DesiredDistance = FMath::Clamp(CameraZoom.DesiredDistance, CameraZoom.MinDistance, CameraZoom.MaxDistance);
}

void FMetaAgentCameraRuntime::RunToggleCinematicCameraSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentCinematicCameraState& CinematicCamera)
{
	if (!Controller.IsLocalPlayerController())
	{
		return;
	}

	// Calculate a default focus location at the scene origin
	const FVector DefaultFocusLocation = FVector(0.0f, 0.0f, 100.0f);

	if (CinematicCamera.bModeEnabled)
	{
		RunDisableCinematicCameraSequence(Controller, CinematicCamera);
	}
	else
	{
		RunEnableCinematicCameraSequence(Controller, CinematicCamera, DefaultFocusLocation);
	}
}

void FMetaAgentCameraRuntime::RunEnableCinematicCameraSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentCinematicCameraState& CinematicCamera,
	FVector TargetFocusLocation)
{
	if (CinematicCamera.bModeEnabled)
	{
		return;
	}

	SanitizeCinematicCameraState(CinematicCamera);

	UWorld* World = Controller.GetWorld();
	if (!World || !Controller.PlayerCameraManager)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("CinematicCamera: missing world or player camera manager. Cannot enable cinematic camera."));
		return;
	}

	// Clean up invalid runtime camera actor
	if (CinematicCamera.RuntimeCameraActor && !IsValid(CinematicCamera.RuntimeCameraActor))
	{
		CinematicCamera.RuntimeCameraActor = nullptr;
	}

	// Destroy if world mismatch
	if (CinematicCamera.RuntimeCameraActor && CinematicCamera.RuntimeCameraActor->GetWorld() != World)
	{
		CinematicCamera.RuntimeCameraActor->Destroy();
		CinematicCamera.RuntimeCameraActor = nullptr;
	}

	// Spawn new runtime camera actor if needed
	if (!CinematicCamera.RuntimeCameraActor || !IsValid(CinematicCamera.RuntimeCameraActor))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CinematicCamera.RuntimeCameraActor = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(),
			Controller.PlayerCameraManager->GetCameraLocation(),
			Controller.PlayerCameraManager->GetCameraRotation(),
			SpawnParams);
	}

	if (!CinematicCamera.RuntimeCameraActor)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("CinematicCamera: failed to spawn runtime camera actor."));
		return;
	}

	// Initialize cinematic camera state
	CinematicCamera.PreViewTarget = Controller.GetViewTarget();
	CinematicCamera.PanElapsedSeconds = 0.0f;
	CinematicCamera.SwayPhaseOffset = FMath::FRandRange(0.0f, 2.0f * PI);
	CinematicCamera.OrbitAccumulatedYawDegrees = 0.0f;
	CinematicCamera.OrbitDirectionSign = 1;
	CinematicCamera.DirectionTravelDegrees = 0.0f;
	CinematicCamera.CompletedTurnsThisDirection = 0;

	// Calculate initial orbit radius from current camera position
	const FVector CurrentCameraLocation = Controller.PlayerCameraManager->GetCameraLocation();
	const FVector ToCamera = CurrentCameraLocation - TargetFocusLocation;
	const FVector ToCameraXY(ToCamera.X, ToCamera.Y, 0.0f);

	CinematicCamera.OrbitRadius = FMath::Clamp(
		ToCameraXY.Size(),
		60.0f,
		5000.0f);

	if (ToCameraXY.IsNearlyZero())
	{
		CinematicCamera.OrbitRadius = CinematicCamera.CloseOrbitRadius;
		CinematicCamera.StartOrbitYawDegrees = 0.0f;
	}
	else
	{
		CinematicCamera.StartOrbitYawDegrees = ToCameraXY.Rotation().Yaw;
	}

	CinematicCamera.CameraHeightOffset = FMath::Clamp(ToCamera.Z, 30.0f, 160.0f);

	// Position runtime camera at current location
	CinematicCamera.RuntimeCameraActor->SetActorLocationAndRotation(
		CurrentCameraLocation,
		Controller.PlayerCameraManager->GetCameraRotation());

	Controller.bAutoManageActiveCameraTarget = false;
	Controller.SetViewTargetWithBlend(CinematicCamera.RuntimeCameraActor, CinematicCamera.BlendInSeconds);
	CinematicCamera.bModeEnabled = true;

	// Keep player input fully enabled during cinematic for environment exploration
	Controller.SetIgnoreMoveInput(false);
	Controller.SetIgnoreLookInput(false);

	UE_LOG(LogMetaAgent, Log,
		TEXT("CinematicCamera: ENABLED at focus (%.1f, %.1f, %.1f) style=%s radius=%.1f speedScale=%.2f. Press O to exit."),
		TargetFocusLocation.X, TargetFocusLocation.Y, TargetFocusLocation.Z,
		GetCinematicStyleLabel(CinematicCamera.ActiveStyle),
		CinematicCamera.OrbitRadius,
		CinematicCamera.OrbitSpeedScale);
}

void FMetaAgentCameraRuntime::RunDisableCinematicCameraSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentCinematicCameraState& CinematicCamera)
{
	if (!CinematicCamera.bModeEnabled)
	{
		return;
	}

	CinematicCamera.bModeEnabled = false;
	CinematicCamera.PanElapsedSeconds = 0.0f;
	CinematicCamera.OrbitAccumulatedYawDegrees = 0.0f;
	CinematicCamera.DirectionTravelDegrees = 0.0f;
	CinematicCamera.CompletedTurnsThisDirection = 0;

	// Restore pre-cinematic view target
	AActor* RestoreViewTarget = nullptr;
	if (CinematicCamera.PreViewTarget.IsValid())
	{
		RestoreViewTarget = CinematicCamera.PreViewTarget.Get();
	}

	if (RestoreViewTarget)
	{
		Controller.SetViewTargetWithBlend(RestoreViewTarget, CinematicCamera.BlendOutSeconds);
	}

	CinematicCamera.PreViewTarget.Reset();

	UE_LOG(LogMetaAgent, Log, TEXT("CinematicCamera: DISABLED. Restored normal camera."));
}

void FMetaAgentCameraRuntime::RunUpdateCinematicCameraSequence(
	AMetaAgentPlayerController& Controller,
	const float DeltaTime,
	FMetaAgentCinematicCameraState& CinematicCamera,
	FVector TargetFocusLocation)
{
	if (!CinematicCamera.bModeEnabled)
	{
		return;
	}

	SanitizeCinematicCameraState(CinematicCamera);

	if (DeltaTime <= 0.0f)
	{
		return;
	}

	if (!Controller.GetWorld() || !Controller.PlayerCameraManager)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("CinematicCamera: world/camera manager became invalid during update. Disabling cinematic camera."));
		RunDisableCinematicCameraSequence(Controller, CinematicCamera);
		return;
	}

	if (!CinematicCamera.RuntimeCameraActor || !IsValid(CinematicCamera.RuntimeCameraActor))
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("CinematicCamera: runtime camera actor was lost. Disabling cinematic camera."));
		RunDisableCinematicCameraSequence(Controller, CinematicCamera);
		return;
	}

	// Accumulate pan time
	CinematicCamera.PanElapsedSeconds += DeltaTime;
	const float Duration = FMath::Max(0.1f, CinematicCamera.PanDurationSeconds);
	float StyleTimeScale = 1.0f;
	const float TimeWithPhase = CinematicCamera.PanElapsedSeconds + CinematicCamera.SwayPhaseOffset;
	float CurrentOrbitYaw = CinematicCamera.StartOrbitYawDegrees;

	// Apply oscillating hold cinematic style (smooth sway motion)
	switch (CinematicCamera.ActiveStyle)
	{
	case EMetaAgentCinematicCameraStyle::OscillatingHold:
	default:
		{
			StyleTimeScale = 0.1f;
			const float OscillationFrequencyRadians = ((2.0f * PI) / Duration) * StyleTimeScale;
			const float YawOffset = FMath::Sin(CinematicCamera.PanElapsedSeconds * OscillationFrequencyRadians)
				* CinematicCamera.OscillationYawAmplitudeDegrees;
			CurrentOrbitYaw += YawOffset;
			break;
		}
	}

	// Apply sway motion (horizontal and vertical oscillation)
	const float BaseFrequencyRadians = (FMath::Max(0.1f, CinematicCamera.SwayFrequency) * 2.0f * PI) * StyleTimeScale;
	const float OrbitYawRadians = FMath::DegreesToRadians(CurrentOrbitYaw);
	const float HorizontalSway = FMath::Sin(TimeWithPhase * BaseFrequencyRadians) * CinematicCamera.SwayHorizontalAmplitude;
	const float VerticalSway = FMath::Sin((TimeWithPhase * BaseFrequencyRadians * 0.57f) + 1.2f) * CinematicCamera.SwayVerticalAmplitude;

	// Calculate orbital camera position
	const FVector OrbitDirection(FMath::Cos(OrbitYawRadians), FMath::Sin(OrbitYawRadians), 0.0f);
	const FVector OrbitRight(-OrbitDirection.Y, OrbitDirection.X, 0.0f);
	const FVector NewCameraLocation(
		TargetFocusLocation.X + (OrbitDirection.X * CinematicCamera.OrbitRadius) + (OrbitRight.X * HorizontalSway),
		TargetFocusLocation.Y + (OrbitDirection.Y * CinematicCamera.OrbitRadius) + (OrbitRight.Y * HorizontalSway),
		TargetFocusLocation.Z + CinematicCamera.CameraHeightOffset + VerticalSway);

	// Look at focus point
	const FRotator NewCameraRotation = (TargetFocusLocation - NewCameraLocation).Rotation();
	CinematicCamera.RuntimeCameraActor->SetActorLocationAndRotation(NewCameraLocation, NewCameraRotation);
}

// ===== MetaAgentMainActor.cpp =====
AMetaAgentMainActor::AMetaAgentMainActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AMetaAgentMainActor::BeginPlay()
{
	Super::BeginPlay();
	GMetaAgentRuntimeActive = bActive;
}

void AMetaAgentMainActor::ActivateAgent()
{
	bActive = true;
	GMetaAgentRuntimeActive = true;
	UE_LOG(LogMetaAgentPlugin, Log, TEXT("MetaAgentMainActor set to ACTIVE."));
}

void AMetaAgentMainActor::DeactivateAgent()
{
	bActive = false;
	GMetaAgentRuntimeActive = false;
	UE_LOG(LogMetaAgentPlugin, Log, TEXT("MetaAgentMainActor set to INACTIVE."));
}

void AMetaAgentMainActor::ToggleAgentActive()
{
	bActive = !bActive;
	GMetaAgentRuntimeActive = bActive;
	UE_LOG(LogMetaAgentPlugin, Log, TEXT("MetaAgentMainActor toggled to %s."), bActive ? TEXT("ACTIVE") : TEXT("INACTIVE"));
}

// ===== MetaAgentGameInstance.cpp =====
UMetaAgentGameInstance* UMetaAgentGameInstance::Get(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject))
	{
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	return Cast<UMetaAgentGameInstance>(World->GetGameInstance());
}


// ===== MetaAgentGameInstanceNetworking.cpp =====
namespace
{
	const TCHAR* GetBuildConfigurationLabel()
	{
#if UE_BUILD_SHIPPING
		return TEXT("Shipping");
#elif UE_BUILD_DEVELOPMENT
		return TEXT("Development");
#elif UE_BUILD_DEBUG
		return TEXT("Debug");
#else
		return TEXT("Other");
#endif
	}

	FString EscapeJson(const FString& InText)
	{
		FString Out = InText;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Out.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Out;
	}

	void AddBoundRoute(
		const TSharedPtr<IHttpRouter>& Router,
		TArray<FHttpRouteHandle>& Handles,
		const TCHAR* Path,
		EHttpServerRequestVerbs Verbs,
		const FHttpRequestHandler& Handler)
	{
		if (!Router.IsValid())
		{
			return;
		}

		FHttpRouteHandle Handle = Router->BindRoute(FHttpPath(Path), Verbs, Handler);
		if (Handle.IsValid())
		{
			Handles.Add(Handle);
		}
	}
}

void UMetaAgentGameInstance::Init()
{
	Super::Init();

	bNetworkingRuntimeServerEnabled = bEnableLocalHttpServer;
	bNetworkingRuntimeRouterBound = false;
	bNetworkingRuntimeListenersStarted = false;
	NetworkingRuntimePort = LocalHttpServerPort;
	NetworkingRuntimeLastPlatformEvent = TEXT("none");
	NetworkingRuntimeLastPlatformResult = TEXT("idle");
	NetworkingRuntimeLastNotifyMessage = TEXT("none");
	NetworkingRuntimeLastError = TEXT("none");
	NetworkingRuntimeLastSendUtc = TEXT("n/a");
	NetworkingRuntimeLastReceiveUtc = TEXT("n/a");

	if (!IsMetaAgentRuntimeActive())
	{
		UE_LOG(LogMetaAgent, Log, TEXT("MetaAgent runtime inactive. Skipping UMetaAgentGameInstance::Init logic."));
		return;
	}

	UE_LOG(LogMetaAgent, Log,
		TEXT("Startup: Build=%s HTTPServerEnabled=%s Port=%d (HTTP starts when Networking Runtime is enabled)"),
		GetBuildConfigurationLabel(),
		bEnableLocalHttpServer ? TEXT("true") : TEXT("false"),
		LocalHttpServerPort);
}

void UMetaAgentGameInstance::Shutdown()
{
	if (!IsMetaAgentRuntimeActive())
	{
		Super::Shutdown();
		return;
	}

	StopLocalHttpServer();
	bNetworkingRuntimeListenersStarted = false;
	bNetworkingRuntimeRouterBound = false;
	Super::Shutdown();
}

FString UMetaAgentGameInstance::BuildPlatformUrl() const
{
	const FString Base = PlatformBaseUrl.EndsWith(TEXT("/")) ? PlatformBaseUrl.LeftChop(1) : PlatformBaseUrl;
	const FString Path = PlatformEventEndpoint.StartsWith(TEXT("/")) ? PlatformEventEndpoint : FString::Printf(TEXT("/%s"), *PlatformEventEndpoint);
	return Base + Path;
}

void UMetaAgentGameInstance::SendEventToPlatform(const FString& EventName, const FString& Message, const FString& SourceOverride)
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (!bEnablePlatformForwarding)
	{
		UE_LOG(LogMetaAgent, Verbose, TEXT("Platform forwarding disabled. Event '%s' was not sent."), *EventName);
		return;
	}

	const FString RequestUrl = BuildPlatformUrl();
	if (RequestUrl.IsEmpty())
	{
		NetworkingRuntimeLastPlatformEvent = EventName;
		NetworkingRuntimeLastPlatformResult = TEXT("invalid-url");
		NetworkingRuntimeLastError = TEXT("Platform forwarding URL is empty.");
		UE_LOG(LogMetaAgent, Warning, TEXT("Platform forwarding URL is empty. Configure PlatformBaseUrl/PlatformEventEndpoint."));
		return;
	}

	NetworkingRuntimeLastPlatformEvent = EventName;
	NetworkingRuntimeLastPlatformResult = TEXT("sending");
	NetworkingRuntimeLastError = TEXT("none");
	NetworkingRuntimeLastSendUtc = FDateTime::UtcNow().ToIso8601();

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("source"), SourceOverride.IsEmpty() ? TEXT("unreal") : SourceOverride);
	Payload->SetStringField(TEXT("event"), EventName);
	Payload->SetStringField(TEXT("message"), Message);
	Payload->SetStringField(TEXT("session_id"), PlatformSessionId.IsEmpty() ? TEXT("default") : PlatformSessionId);
	Payload->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());

	TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
	Metadata->SetStringField(TEXT("map"), GetWorld() ? GetWorld()->GetMapName() : TEXT("unknown"));
	Metadata->SetStringField(TEXT("build"), GetBuildConfigurationLabel());
	Payload->SetObjectField(TEXT("metadata"), Metadata);

	FString RequestBody;
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
		if (!FJsonSerializer::Serialize(Payload, Writer))
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Failed to serialize platform event payload for '%s'."), *EventName);
			return;
		}
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(RequestUrl);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetContentAsString(RequestBody);

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[this, EventName](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			NetworkingRuntimeLastPlatformEvent = EventName;
			NetworkingRuntimeLastReceiveUtc = FDateTime::UtcNow().ToIso8601();

			if (!bWasSuccessful)
			{
				NetworkingRuntimeLastPlatformResult = TEXT("network-failure");
				NetworkingRuntimeLastError = TEXT("Network failure while sending event.");
				UE_LOG(LogMetaAgent, Warning, TEXT("Platform event '%s' failed to send (network failure)."), *EventName);
				return;
			}

			if (!Response.IsValid())
			{
				NetworkingRuntimeLastPlatformResult = TEXT("no-response");
				NetworkingRuntimeLastError = TEXT("No HTTP response.");
				UE_LOG(LogMetaAgent, Warning, TEXT("Platform event '%s' received no HTTP response."), *EventName);
				return;
			}

			const int32 StatusCode = Response->GetResponseCode();
			if (StatusCode >= 200 && StatusCode < 300)
			{
				NetworkingRuntimeLastPlatformResult = FString::Printf(TEXT("ok-%d"), StatusCode);
				NetworkingRuntimeLastError = TEXT("none");
				UE_LOG(LogMetaAgent, Log, TEXT("Platform event '%s' acknowledged [%d]."), *EventName, StatusCode);

				bool bAgentRunning = false;
				FString AgentAction;
				TSharedPtr<FJsonObject> ResponseJson;
				const FString ResponseBody = Response->GetContentAsString();
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
				if (FJsonSerializer::Deserialize(Reader, ResponseJson) && ResponseJson.IsValid())
				{
					ResponseJson->TryGetBoolField(TEXT("agent_running"), bAgentRunning);
					ResponseJson->TryGetStringField(TEXT("agent_action"), AgentAction);
				}

				if (!AgentAction.IsEmpty())
				{
					NetworkingRuntimeLastNotifyMessage = FString::Printf(
						TEXT("Agent %s (%s)"),
						*AgentAction.ToUpper(),
						bAgentRunning ? TEXT("RUNNING") : TEXT("STOPPED"));
				}
				else if (ResponseJson.IsValid())
				{
					NetworkingRuntimeLastNotifyMessage = FString::Printf(TEXT("Agent %s"), bAgentRunning ? TEXT("RUNNING") : TEXT("STOPPED"));
				}
			}
			else
			{
				NetworkingRuntimeLastPlatformResult = FString::Printf(TEXT("http-%d"), StatusCode);
				NetworkingRuntimeLastError = Response->GetContentAsString();
				UE_LOG(LogMetaAgent, Warning,
					TEXT("Platform event '%s' returned HTTP %d. Body: %s"),
					*EventName,
					StatusCode,
					*Response->GetContentAsString());
			}
		});

	if (!HttpRequest->ProcessRequest())
	{
		NetworkingRuntimeLastPlatformResult = TEXT("dispatch-failed");
		NetworkingRuntimeLastError = TEXT("Failed to dispatch HTTP request.");
		UE_LOG(LogMetaAgent, Warning, TEXT("Failed to dispatch platform event '%s' request."), *EventName);
	}
}

TArray<FString> UMetaAgentGameInstance::GetNetworkingRuntimePanelLines() const
{
	TArray<FString> Lines;
	Lines.Add(TEXT("Networking Runtime"));
	Lines.Add(TEXT("--------------------------------"));
	Lines.Add(TEXT("COMMS (H/G)   : start audio / start image"));
	Lines.Add(FString::Printf(TEXT("Server Enabled : %s"), bNetworkingRuntimeServerEnabled ? TEXT("true") : TEXT("false")));
	Lines.Add(FString::Printf(TEXT("Port          : %d"), NetworkingRuntimePort));
	Lines.Add(FString::Printf(TEXT("Router Bound  : %s"), bNetworkingRuntimeRouterBound ? TEXT("true") : TEXT("false")));
	Lines.Add(FString::Printf(TEXT("Listeners     : %s"), bNetworkingRuntimeListenersStarted ? TEXT("started") : TEXT("stopped")));
	Lines.Add(FString::Printf(TEXT("Last Event    : %s"), *NetworkingRuntimeLastPlatformEvent));
	Lines.Add(FString::Printf(TEXT("Last Result   : %s"), *NetworkingRuntimeLastPlatformResult));
	Lines.Add(FString::Printf(TEXT("Last Notify   : %s"), *NetworkingRuntimeLastNotifyMessage));
	Lines.Add(FString::Printf(TEXT("Last Error    : %s"), *NetworkingRuntimeLastError));
	Lines.Add(FString::Printf(TEXT("Last Send UTC : %s"), *NetworkingRuntimeLastSendUtc));
	Lines.Add(FString::Printf(TEXT("Last Recv UTC : %s"), *NetworkingRuntimeLastReceiveUtc));
	return Lines;
}

FString UMetaAgentGameInstance::GetLocalHttpServerStatusText() const
{
	const TCHAR* EnabledText = bEnableLocalHttpServer ? TEXT("enabled") : TEXT("disabled");
	const TCHAR* BoundText = LocalHttpRouter.IsValid() ? TEXT("bound") : TEXT("not-bound");

	return FString::Printf(
		TEXT("HTTP %s, port=%d, router=%s, endpoints=/health,/echo,/notify"),
		EnabledText,
		LocalHttpServerPort,
		BoundText);
}

void UMetaAgentGameInstance::StartNetworkingRuntime()
{
	StartLocalHttpServer();
}

void UMetaAgentGameInstance::StopNetworkingRuntime()
{
	StopLocalHttpServer();
}

void UMetaAgentGameInstance::StartLocalHttpServer()
{
	bNetworkingRuntimeServerEnabled = bEnableLocalHttpServer;
	NetworkingRuntimePort = LocalHttpServerPort;
	bNetworkingRuntimeListenersStarted = false;
	bNetworkingRuntimeRouterBound = false;

	if (!bEnableLocalHttpServer)
	{
		NetworkingRuntimeLastPlatformResult = TEXT("server-disabled");
		UE_LOG(LogMetaAgent, Log, TEXT("HTTP server disabled by config."));
		return;
	}

	if (!FHttpServerModule::IsAvailable())
	{
		FModuleManager::LoadModuleChecked<FHttpServerModule>(TEXT("HTTPServer"));
	}

	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
	LocalHttpRouter = HttpServerModule.GetHttpRouter(static_cast<uint32>(LocalHttpServerPort), true);
	if (!LocalHttpRouter.IsValid())
	{
		NetworkingRuntimeLastPlatformResult = TEXT("bind-failed");
		NetworkingRuntimeLastError = FString::Printf(TEXT("HTTP server failed to bind port %d."), LocalHttpServerPort);
		UE_LOG(LogMetaAgent, Warning, TEXT("HTTP server failed to bind port %d."), LocalHttpServerPort);
		return;
	}

	bNetworkingRuntimeRouterBound = true;

	RouteHandles.Reset();

	const FHttpRequestHandler HealthHandler = FHttpRequestHandler::CreateUObject(this, &UMetaAgentGameInstance::HandleHealthRequest);
	const FHttpRequestHandler EchoHandler = FHttpRequestHandler::CreateUObject(this, &UMetaAgentGameInstance::HandleEchoRequest);
	const FHttpRequestHandler NotifyHandler = FHttpRequestHandler::CreateUObject(this, &UMetaAgentGameInstance::HandleNotifyRequest);

	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/health"), EHttpServerRequestVerbs::VERB_GET, HealthHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/health/"), EHttpServerRequestVerbs::VERB_GET, HealthHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/echo"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, EchoHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/echo/"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, EchoHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/notify"), EHttpServerRequestVerbs::VERB_POST, NotifyHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/notify/"), EHttpServerRequestVerbs::VERB_POST, NotifyHandler);

	HttpServerModule.StartAllListeners();
	bNetworkingRuntimeListenersStarted = true;
	NetworkingRuntimeLastPlatformResult = TEXT("server-running");
	NetworkingRuntimeLastError = TEXT("none");
	UE_LOG(LogMetaAgent, Log, TEXT("HTTP server listening on port %d. Endpoints: /health, /echo, /notify"), LocalHttpServerPort);
}

void UMetaAgentGameInstance::StopLocalHttpServer()
{
	if (!LocalHttpRouter.IsValid())
	{
		return;
	}

	for (FHttpRouteHandle& RouteHandle : RouteHandles)
	{
		if (RouteHandle.IsValid())
		{
			LocalHttpRouter->UnbindRoute(RouteHandle);
			RouteHandle.Reset();
		}
	}
	RouteHandles.Reset();

	LocalHttpRouter.Reset();
	bNetworkingRuntimeRouterBound = false;
	bNetworkingRuntimeListenersStarted = false;
	UE_LOG(LogMetaAgent, Log, TEXT("HTTP server routes unbound."));
}

bool UMetaAgentGameInstance::HandleHealthRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString Response = FString::Printf(
		TEXT("{\"status\":\"ok\",\"map\":\"%s\",\"build\":\"%s\"}"),
		*EscapeJson(GetWorld() ? GetWorld()->GetMapName() : FString(TEXT("unknown"))),
#if UE_BUILD_SHIPPING
		TEXT("Shipping")
#elif UE_BUILD_DEVELOPMENT
		TEXT("Development")
#else
		TEXT("Other")
#endif
	);

	TUniquePtr<FHttpServerResponse> HttpResponse = FHttpServerResponse::Create(Response, TEXT("application/json"));
	HttpResponse->Code = EHttpServerResponseCodes::Ok;
	OnComplete(MoveTemp(HttpResponse));
	return true;
}

bool UMetaAgentGameInstance::HandleEchoRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString EchoText;
	if (const FString* QueryValue = Request.QueryParams.Find(TEXT("msg")))
	{
		EchoText = *QueryValue;
	}
	else if (Request.Body.Num() > 0)
	{
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		EchoText = FString(Converter.Length(), Converter.Get());
	}

	const FString Response = FString::Printf(TEXT("{\"echo\":\"%s\"}"), *EscapeJson(EchoText));
	TUniquePtr<FHttpServerResponse> HttpResponse = FHttpServerResponse::Create(Response, TEXT("application/json"));
	HttpResponse->Code = EHttpServerResponseCodes::Ok;
	OnComplete(MoveTemp(HttpResponse));
	return true;
}

bool UMetaAgentGameInstance::HandleNotifyRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString NotifyMessage;
	FString RawBody;
	if (Request.Body.Num() > 0)
	{
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		RawBody = FString(Converter.Length(), Converter.Get());
	}

	if (!RawBody.IsEmpty())
	{
		TSharedPtr<FJsonObject> JsonObj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawBody);
		if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
		{
			JsonObj->TryGetStringField(TEXT("message"), NotifyMessage);
		}
	}

	if (NotifyMessage.IsEmpty())
	{
		NotifyMessage = RawBody.IsEmpty() ? TEXT("(no message)") : RawBody;
	}

	NetworkingRuntimeLastNotifyMessage = NotifyMessage;
	NetworkingRuntimeLastReceiveUtc = FDateTime::UtcNow().ToIso8601();

	UE_LOG(LogMetaAgent, Log, TEXT("Platform notify received: %s"), *NotifyMessage);

	const FString Response = TEXT("{\"ok\":true}");
	TUniquePtr<FHttpServerResponse> HttpResponse = FHttpServerResponse::Create(Response, TEXT("application/json"));
	HttpResponse->Code = EHttpServerResponseCodes::Ok;
	OnComplete(MoveTemp(HttpResponse));
	return true;
}

// ===== MetaAgentHUD.cpp =====
namespace
{
	constexpr float PanelX = 24.0f;
	constexpr float PanelY = 24.0f;
	constexpr float PanelPadding = 10.0f;
	constexpr float SectionGap = 8.0f;
	constexpr float HeaderHeight = 24.0f;
	constexpr float RowHeight = 22.0f;
	constexpr float StatusLineHeight = 18.0f;
	constexpr float KeyColumnWidth = 72.0f;
	constexpr float ToggleButtonWidth = 72.0f;
	constexpr float CollapseButtonWidth = 24.0f;
	constexpr float PanelMinWidth = 460.0f;
	constexpr float TextScale = 1.0f;
	constexpr float PreviewSize = 64.0f;
	constexpr float PreviewGap = 8.0f;
	constexpr float PreviewLabelHeight = 14.0f;
	constexpr float PreviewRowHeight = PreviewSize + PreviewLabelHeight + 8.0f;

	void AddClickRegion(
		TArray<FMetaAgentHUDClickRegion>& Regions,
		const FName ActionId,
		const float X,
		const float Y,
		const float W,
		const float H)
	{
		if (ActionId.IsNone() || W <= 0.0f || H <= 0.0f)
		{
			return;
		}

		FMetaAgentHUDClickRegion Region;
		Region.ActionId = ActionId;
		Region.X = X;
		Region.Y = Y;
		Region.W = W;
		Region.H = H;
		Regions.Add(Region);
	}

	float MeasureTextWidth(UCanvas* Canvas, const UFont* Font, const FString& Text)
	{
		float SizeX = 0.0f;
		float SizeY = 0.0f;
		Canvas->StrLen(Font, Text, SizeX, SizeY);
		return SizeX * TextScale;
	}
}

void AMetaAgentHUD::SetRuntimePanelVisible(const bool bVisible)
{
	bRuntimePanelVisible = bVisible;
}

void AMetaAgentHUD::SetRuntimePanelSections(const TArray<FMetaAgentGUIRuntimeSection>& InSections)
{
	RuntimePanelSections = InSections;
}

bool AMetaAgentHUD::HitTestRuntimePanelAction(const float MouseX, const float MouseY, FName& OutActionId) const
{
	for (int32 Index = RuntimeClickRegions.Num() - 1; Index >= 0; --Index)
	{
		const FMetaAgentHUDClickRegion& Region = RuntimeClickRegions[Index];
		if (Region.Contains(MouseX, MouseY))
		{
			OutActionId = Region.ActionId;
			return true;
		}
	}

	OutActionId = NAME_None;
	return false;
}

void AMetaAgentHUD::AddTransientMessage(const FString& Message, FColor Color, float DurationSeconds)
{
	if (Message.IsEmpty())
	{
		return;
	}

	FMetaAgentHUDMessage& Entry = MessageQueue.AddDefaulted_GetRef();
	Entry.Text = Message;
	Entry.Color = Color;
	Entry.TimeRemaining = FMath::Max(DurationSeconds, 0.1f);
}

void AMetaAgentHUD::SetStatusLine(FName Key, const FString& Message, FColor Color)
{
	if (Key.IsNone())
	{
		return;
	}

	if (Message.IsEmpty())
	{
		ClearStatusLine(Key);
		return;
	}

	for (FMetaAgentHUDStatusLine& Line : StatusLines)
	{
		if (Line.Key == Key)
		{
			Line.Text = Message;
			Line.Color = Color;
			return;
		}
	}

	FMetaAgentHUDStatusLine& NewLine = StatusLines.AddDefaulted_GetRef();
	NewLine.Key = Key;
	NewLine.Text = Message;
	NewLine.Color = Color;
}

void AMetaAgentHUD::ClearStatusLine(FName Key)
{
	if (Key.IsNone())
	{
		return;
	}

	for (int32 Index = StatusLines.Num() - 1; Index >= 0; --Index)
	{
		if (StatusLines[Index].Key == Key)
		{
			StatusLines.RemoveAt(Index);
		}
	}
}

void AMetaAgentHUD::DrawRuntimePanel()
{
	if (!bRuntimePanelVisible || RuntimePanelSections.Num() == 0 || !Canvas)
	{
		RuntimeClickRegions.Reset();
		return;
	}

	const UFont* PanelFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	RuntimeClickRegions.Reset();

	const FString PanelTitle = TEXT("MetaAgent Controls (Q to hide, click rows)");

	float MaxContentWidth = MeasureTextWidth(Canvas, PanelFont, PanelTitle);
	for (const FMetaAgentGUIRuntimeSection& Section : RuntimePanelSections)
	{
		MaxContentWidth = FMath::Max(
			MaxContentWidth,
			CollapseButtonWidth + MeasureTextWidth(Canvas, PanelFont, Section.Title) + ToggleButtonWidth + 24.0f);

		if (!Section.bSectionExpanded)
		{
			continue;
		}

		for (const FMetaAgentGUIActionRow& Row : Section.ActionRows)
		{
			const FString RowText = FString::Printf(TEXT("%s  %s"), *Row.KeyLabel, *Row.Description);
			MaxContentWidth = FMath::Max(MaxContentWidth, KeyColumnWidth + MeasureTextWidth(Canvas, PanelFont, RowText));
		}
		for (const FString& StatusLine : Section.StatusLines)
		{
			MaxContentWidth = FMath::Max(MaxContentWidth, MeasureTextWidth(Canvas, PanelFont, StatusLine));
		}
		if (Section.PreviewThumbnails.Num() > 0)
		{
			const float PreviewRowWidth =
				(Section.PreviewThumbnails.Num() * PreviewSize)
				+ (FMath::Max(0, Section.PreviewThumbnails.Num() - 1) * PreviewGap);
			MaxContentWidth = FMath::Max(MaxContentWidth, PreviewRowWidth);
		}
	}

	const float PanelWidth = FMath::Max(PanelMinWidth, MaxContentWidth + (PanelPadding * 2.0f));

	float TotalHeight = PanelPadding + HeaderHeight;
	for (const FMetaAgentGUIRuntimeSection& Section : RuntimePanelSections)
	{
		TotalHeight += HeaderHeight + SectionGap;
		if (Section.bSectionExpanded)
		{
			TotalHeight += Section.ActionRows.Num() * RowHeight;
			TotalHeight += Section.StatusLines.Num() * StatusLineHeight;
			if (Section.PreviewThumbnails.Num() > 0)
			{
				TotalHeight += PreviewRowHeight;
			}
		}
		TotalHeight += SectionGap;
	}
	TotalHeight += PanelPadding;

	const float PanelHeight = FMath::Min(TotalHeight, Canvas->ClipY - (PanelY * 2.0f));

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f), PanelX, PanelY, PanelWidth, PanelHeight);
	DrawText(PanelTitle, FColor::Cyan, PanelX + PanelPadding, PanelY + PanelPadding, const_cast<UFont*>(PanelFont), TextScale, false);

	float DrawY = PanelY + PanelPadding + HeaderHeight;

	for (const FMetaAgentGUIRuntimeSection& Section : RuntimePanelSections)
	{
		const FColor HeaderColor = Section.bRuntimeEnabled ? FColor::Cyan : FColor(160, 160, 160);
		const bool bHasSectionBody = Section.ActionRows.Num() > 0 || Section.StatusLines.Num() > 0;
		const float CollapseX = PanelX + PanelPadding;
		const float CollapseY = DrawY - 2.0f;
		const FString CollapseLabel = Section.bSectionExpanded ? TEXT("v") : TEXT(">");

		if (bHasSectionBody)
		{
			DrawRect(
				FLinearColor(0.18f, 0.18f, 0.22f, 0.82f),
				CollapseX,
				CollapseY,
				CollapseButtonWidth,
				HeaderHeight - 2.0f);
			DrawText(
				CollapseLabel,
				FColor::White,
				CollapseX + 8.0f,
				DrawY,
				const_cast<UFont*>(PanelFont),
				TextScale,
				false);
			AddClickRegion(
				RuntimeClickRegions,
				MakeSectionExpandActionId(Section.RuntimeId),
				CollapseX,
				CollapseY,
				CollapseButtonWidth,
				HeaderHeight - 2.0f);
		}

		DrawText(
			Section.Title,
			HeaderColor,
			PanelX + PanelPadding + (bHasSectionBody ? (CollapseButtonWidth + 6.0f) : 0.0f),
			DrawY,
			const_cast<UFont*>(PanelFont),
			TextScale,
			false);

		if (!Section.bRuntimeAlwaysOn)
		{
			const FString ToggleLabel = Section.bRuntimeEnabled ? TEXT("STOP") : TEXT("START");
			const float ToggleX = PanelX + PanelWidth - PanelPadding - ToggleButtonWidth;
			const float ToggleY = DrawY - 2.0f;
			DrawRect(
				Section.bRuntimeEnabled ? FLinearColor(0.45f, 0.12f, 0.12f, 0.85f) : FLinearColor(0.12f, 0.35f, 0.12f, 0.85f),
				ToggleX,
				ToggleY,
				ToggleButtonWidth,
				HeaderHeight - 2.0f);
			DrawText(
				ToggleLabel,
				FColor::White,
				ToggleX + 14.0f,
				DrawY,
				const_cast<UFont*>(PanelFont),
				TextScale,
				false);

			const FName ToggleActionId = FName(*FString::Printf(TEXT("ToggleRuntime_%s"), *Section.RuntimeId.ToString()));
			AddClickRegion(RuntimeClickRegions, ToggleActionId, ToggleX, ToggleY, ToggleButtonWidth, HeaderHeight - 2.0f);
		}
		else
		{
			const float AlwaysOnX = PanelX + PanelWidth - PanelPadding - ToggleButtonWidth;
			DrawText(TEXT("ALWAYS ON"), FColor::Silver, AlwaysOnX, DrawY, const_cast<UFont*>(PanelFont), TextScale, false);
		}

		DrawY += HeaderHeight;

		if (!Section.bSectionExpanded)
		{
			DrawY += SectionGap;
			continue;
		}

		for (const FMetaAgentGUIActionRow& Row : Section.ActionRows)
		{
			const bool bRowEnabled = Section.bRuntimeEnabled && !Row.ActionId.IsNone();
			const float RowX = PanelX + PanelPadding;
			const float RowW = PanelWidth - (PanelPadding * 2.0f);

			DrawRect(
				bRowEnabled ? FLinearColor(0.14f, 0.14f, 0.18f, 0.72f) : FLinearColor(0.10f, 0.10f, 0.10f, 0.45f),
				RowX,
				DrawY,
				RowW,
				RowHeight - 2.0f);

			DrawText(
				Row.KeyLabel,
				bRowEnabled ? FColor::Yellow : FColor(120, 120, 120),
				RowX + 6.0f,
				DrawY + 2.0f,
				const_cast<UFont*>(PanelFont),
				TextScale,
				false);
			DrawText(
				Row.Description,
				bRowEnabled ? FColor::White : FColor(120, 120, 120),
				RowX + KeyColumnWidth,
				DrawY + 2.0f,
				const_cast<UFont*>(PanelFont),
				TextScale,
				false);

			if (bRowEnabled)
			{
				AddClickRegion(RuntimeClickRegions, Row.ActionId, RowX, DrawY, RowW, RowHeight - 2.0f);
			}

			DrawY += RowHeight;
		}

		for (const FString& StatusLine : Section.StatusLines)
		{
			DrawText(StatusLine, FColor(180, 220, 255), PanelX + PanelPadding + 4.0f, DrawY, const_cast<UFont*>(PanelFont), TextScale, false);
			DrawY += StatusLineHeight;
		}

		if (Section.PreviewThumbnails.Num() > 0)
		{
			float PreviewX = PanelX + PanelPadding + 4.0f;
			const float PreviewY = DrawY + 2.0f;
			for (const FMetaAgentGUIPreviewThumbnail& Thumbnail : Section.PreviewThumbnails)
			{
				if (Thumbnail.Texture)
				{
					DrawRect(
						FLinearColor(0.08f, 0.08f, 0.10f, 0.95f),
						PreviewX - 1.0f,
						PreviewY - 1.0f,
						PreviewSize + 2.0f,
						PreviewSize + 2.0f);
					DrawTexture(
						Thumbnail.Texture,
						PreviewX,
						PreviewY,
						PreviewSize,
						PreviewSize,
						0.0f,
						0.0f,
						1.0f,
						1.0f);
					DrawText(
						Thumbnail.Label,
						FColor(200, 200, 200),
						PreviewX,
						PreviewY + PreviewSize + 2.0f,
						const_cast<UFont*>(PanelFont),
						TextScale,
						false);
				}
				PreviewX += PreviewSize + PreviewGap;
			}
			DrawY += PreviewRowHeight;
		}

		DrawY += SectionGap;
	}
}

void AMetaAgentHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const float DeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;

	for (int32 Index = MessageQueue.Num() - 1; Index >= 0; --Index)
	{
		MessageQueue[Index].TimeRemaining -= DeltaSeconds;
		if (MessageQueue[Index].TimeRemaining <= 0.0f)
		{
			MessageQueue.RemoveAt(Index);
		}
	}

	float DrawY = 60.0f;
	for (const FMetaAgentHUDMessage& Message : MessageQueue)
	{
		if (!Message.Text.IsEmpty())
		{
			DrawText(Message.Text, Message.Color, 40.0f, DrawY, GEngine ? GEngine->GetSmallFont() : nullptr, 1.2f, false);
			DrawY += 22.0f;
		}
	}

	if (StatusLines.Num() > 0)
	{
		const UFont* StatusFont = GEngine ? GEngine->GetSmallFont() : nullptr;
		const float StatusScale = 1.0f;
		const float StatusPanelPadding = 8.0f;
		const float PanelLineHeight = 20.0f;
		const float StatusPanelMinWidth = 320.0f;

		float MaxTextWidth = 0.0f;
		for (const FMetaAgentHUDStatusLine& Line : StatusLines)
		{
			float SizeX = 0.0f;
			float SizeY = 0.0f;
			Canvas->StrLen(StatusFont, Line.Text, SizeX, SizeY);
			MaxTextWidth = FMath::Max(MaxTextWidth, SizeX * StatusScale);
		}

		const float StatusPanelWidth = FMath::Max(StatusPanelMinWidth, MaxTextWidth + (StatusPanelPadding * 2.0f));
		const float StatusPanelHeight = (StatusLines.Num() * PanelLineHeight) + (StatusPanelPadding * 2.0f);
		const float StatusPanelX = Canvas->ClipX - StatusPanelWidth - 24.0f;
		const float StatusPanelY = 24.0f;

		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), StatusPanelX, StatusPanelY, StatusPanelWidth, StatusPanelHeight);

		float StatusY = StatusPanelY + StatusPanelPadding;
		for (const FMetaAgentHUDStatusLine& Line : StatusLines)
		{
			DrawText(Line.Text, Line.Color, StatusPanelX + StatusPanelPadding, StatusY, const_cast<UFont*>(StatusFont), StatusScale, false);
			StatusY += PanelLineHeight;
		}
	}

	DrawRuntimePanel();
}

// ===== MetaAgentGUIRuntime.cpp =====
namespace
{
	FMetaAgentGUIActionRow MakeActionRow(const FString& KeyLabel, const FString& Description, const FName ActionId)
	{
		FMetaAgentGUIActionRow Row;
		Row.KeyLabel = KeyLabel;
		Row.Description = Description;
		Row.ActionId = ActionId;
		return Row;
	}

	FMetaAgentGUIRuntimeSection MakeSection(
		const FName RuntimeId,
		const FString& Title,
		const bool bRuntimeAlwaysOn,
		const bool bRuntimeEnabled,
		const TArray<FMetaAgentGUIActionRow>& ActionRows,
		const TArray<FString>& StatusLines = TArray<FString>())
	{
		FMetaAgentGUIRuntimeSection Section;
		Section.RuntimeId = RuntimeId;
		Section.Title = Title;
		Section.bRuntimeAlwaysOn = bRuntimeAlwaysOn;
		Section.bRuntimeEnabled = bRuntimeEnabled;
		Section.ActionRows = ActionRows;
		Section.StatusLines = StatusLines;
		return Section;
	}

	void ApplySectionExpandState(FMetaAgentGUIState& GUI, FMetaAgentGUIRuntimeSection& Section)
	{
		if (const bool* CachedExpanded = GUI.SectionExpandedStates.Find(Section.RuntimeId))
		{
			Section.bSectionExpanded = *CachedExpanded;
			return;
		}

		const bool bHasDetails = Section.StatusLines.Num() > 0 || Section.ActionRows.Num() > 0;
		Section.bSectionExpanded = Section.bRuntimeEnabled || !bHasDetails;
		GUI.SectionExpandedStates.Add(Section.RuntimeId, Section.bSectionExpanded);
	}

	void FinalizeSection(FMetaAgentGUIState& GUI, FMetaAgentGUIRuntimeSection Section)
	{
		ApplySectionExpandState(GUI, Section);
		GUI.RuntimeSections.Add(Section);
	}
}

void FMetaAgentGUIRuntime::BuildRuntimeSections(
	AMetaAgentPlayerController& Controller,
	FMetaAgentGUIState& GUI)
{
	GUI.RuntimeSections.Reset();

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("Q"), TEXT("Toggle controls panel"), MetaAgentRuntimeIds::ToggleHelpPanel));
		Rows.Add(MakeActionRow(TEXT("Esc"), TEXT("Quit application"), MetaAgentRuntimeIds::QuitApplication));
		FinalizeSection(GUI, MakeSection(
			MetaAgentRuntimeIds::GUI,
			TEXT("GUI Runtime"),
			true,
			true,
			Rows));
	}

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("W/A/S/D"), TEXT("Move"), NAME_None));
		Rows.Add(MakeActionRow(TEXT("Shift"), TEXT("Sprint modifier"), NAME_None));
		Rows.Add(MakeActionRow(TEXT("Mouse"), TEXT("Look"), NAME_None));
		GUI.bCharacterInputRuntimeEnabled = true;
		FinalizeSection(GUI, MakeSection(
			MetaAgentRuntimeIds::CharacterInput,
			TEXT("Character Input Runtime"),
			true,
			true,
			Rows));
	}

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("O"), TEXT("Toggle cinematic camera"), MetaAgentRuntimeIds::ToggleCinematicCamera));
		Rows.Add(MakeActionRow(TEXT("Wheel"), TEXT("Zoom camera distance"), NAME_None));
		FinalizeSection(GUI, MakeSection(
			MetaAgentRuntimeIds::Camera,
			TEXT("Camera Runtime"),
			false,
			GUI.bCameraRuntimeEnabled,
			Rows));
	}

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("I"), TEXT("Toggle AI autopilot"), MetaAgentRuntimeIds::ToggleAutopilot));
		FinalizeSection(GUI, MakeSection(
			MetaAgentRuntimeIds::AI,
			TEXT("AI Runtime"),
			false,
			GUI.bAIRuntimeEnabled,
			Rows));
	}

	{
		TArray<FString> StatusLines = Controller.BuildRecordingRuntimePanelLines();
		if (StatusLines.Num() > 0 && StatusLines[0] == TEXT("Recording Runtime"))
		{
			StatusLines.RemoveAt(0);
		}

		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("J"), TEXT("Toggle viewport capture"), MetaAgentRuntimeIds::ToggleRecording));
		Rows.Add(MakeActionRow(TEXT("U"), TEXT("Finalize / show capture output"), MetaAgentRuntimeIds::ReportRecording));
		FinalizeSection(GUI, MakeSection(
			MetaAgentRuntimeIds::Recording,
			TEXT("Recording Runtime"),
			false,
			GUI.bRecordingRuntimeEnabled,
			Rows,
			StatusLines));
	}

	{
		TArray<FString> StatusLines;
		if (const UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(&Controller))
		{
			StatusLines = GI->GetNetworkingRuntimePanelLines();
			if (StatusLines.Num() > 0 && StatusLines[0] == TEXT("Networking Runtime"))
			{
				StatusLines.RemoveAt(0);
			}
			if (StatusLines.Num() > 0 && StatusLines[0] == TEXT("--------------------------------"))
			{
				StatusLines.RemoveAt(0);
			}
			if (StatusLines.Num() > 0 && StatusLines[0].StartsWith(TEXT("COMMS (H/G)")))
			{
				StatusLines.RemoveAt(0);
			}
		}
		else
		{
			StatusLines.Add(TEXT("GameInstance : MetaAgentGameInstance NOT active"));
		}

		TArray<FMetaAgentGUIActionRow> Rows;
		Rows.Add(MakeActionRow(TEXT("H"), TEXT("Send HTTP 'start audio'"), MetaAgentRuntimeIds::StartAudio));
		Rows.Add(MakeActionRow(TEXT("G"), TEXT("Send HTTP 'start image'"), MetaAgentRuntimeIds::StartImage));
		FinalizeSection(GUI, MakeSection(
			MetaAgentRuntimeIds::Networking,
			TEXT("Networking Runtime"),
			false,
			GUI.bNetworkingRuntimeEnabled,
			Rows,
			StatusLines));
	}

	{
		TArray<FMetaAgentGUIActionRow> Rows;
		for (const FMetaAgentGUIActionRow& ParticleRow : FMetaAgentParticleInputRouter::GetParticleGUIActionRows())
		{
			Rows.Add(ParticleRow);
		}

		TArray<FString> StatusLines = Controller.BuildParticleRuntimePanelStatusLines();

		FMetaAgentGUIRuntimeSection ParticleSection = MakeSection(
			MetaAgentRuntimeIds::Particle,
			TEXT("Particle Runtime"),
			false,
			GUI.bParticleRuntimeEnabled,
			Rows,
			StatusLines);
		if (const UMetaAgentParticleOrchestrator* Orchestrator = Controller.GetParticleOrchestrator())
		{
			ParticleSection.PreviewThumbnails = Orchestrator->GetPanelPreviewThumbnails();
		}
		FinalizeSection(GUI, ParticleSection);
	}
}

void FMetaAgentGUIRuntime::RunApplyHelpPanelSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentGUIState& GUI)
{
	BuildRuntimeSections(Controller, GUI);

	if (AMetaAgentHUD* MetaAgentHUD = Controller.GetHUD<AMetaAgentHUD>())
	{
		MetaAgentHUD->SetRuntimePanelVisible(GUI.bHelpPanelVisible);
		MetaAgentHUD->SetRuntimePanelSections(GUI.RuntimeSections);
	}
}

void FMetaAgentGUIRuntime::RunToggleHelpPanelSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentGUIState& GUI)
{
	if (!Controller.IsLocalPlayerController())
	{
		return;
	}

	GUI.bHelpPanelVisible = !GUI.bHelpPanelVisible;
	Controller.ApplyGUIInteractionInputModeFromPanelState();
	RunApplyHelpPanelSequence(Controller, GUI);

	UE_LOG(LogMetaAgent, Log, TEXT("GUIRuntime: Help panel %s."), GUI.bHelpPanelVisible ? TEXT("ENABLED") : TEXT("DISABLED"));
}
