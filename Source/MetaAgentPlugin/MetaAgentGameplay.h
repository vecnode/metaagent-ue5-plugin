// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "HttpRequestHandler.h"
#include "HttpRouteHandle.h"
#include "Logging/LogMacros.h"
#include "UObject/SoftObjectPtr.h"
#include "MetaAgentGameplay.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class AMetaAgentHUD;
class UBehaviorTree;
class UBlackboardData;
struct FInputActionValue;

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

UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Meta Agent Main Actor"))
class METAAGENTPLUGIN_API AMetaAgentMainActor : public AActor
{
	GENERATED_BODY()

public:
	AMetaAgentMainActor();

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MetaAgent|Control")
	bool bActive = true;

	UFUNCTION(CallInEditor, BlueprintCallable, Category="MetaAgent|Control")
	void ActivateAgent();

	UFUNCTION(CallInEditor, BlueprintCallable, Category="MetaAgent|Control")
	void DeactivateAgent();

	UFUNCTION(CallInEditor, BlueprintCallable, Category="MetaAgent|Control")
	void ToggleAgentActive();
};

/**
 *  A simple player-controllable third person character
 *  Implements a controllable orbiting camera
 */
UCLASS(abstract)
class AMetaAgentCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

	/** Optional soft reference fallback for jump input action asset. */
	UPROPERTY(EditDefaultsOnly, Category="Input|Defaults")
	TSoftObjectPtr<UInputAction> JumpActionAsset;

	/** Optional soft reference fallback for move input action asset. */
	UPROPERTY(EditDefaultsOnly, Category="Input|Defaults")
	TSoftObjectPtr<UInputAction> MoveActionAsset;

	/** Optional soft reference fallback for look input action asset. */
	UPROPERTY(EditDefaultsOnly, Category="Input|Defaults")
	TSoftObjectPtr<UInputAction> LookActionAsset;

	/** Optional soft reference fallback for mouse look input action asset. */
	UPROPERTY(EditDefaultsOnly, Category="Input|Defaults")
	TSoftObjectPtr<UInputAction> MouseLookActionAsset;

public:

	/** Constructor */
	AMetaAgentCharacter();	

protected:

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	/** Prints hello world and the character world position when the H key is pressed. */
	void PrintHelloWorld();

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

/** MetaHuman player pawn that adds C++ movement fallback and GI registration. */
UCLASS()
class AMetaAgentMHPlayer : public AMetaAgentCharacter
{
	GENERATED_BODY()

public:

	AMetaAgentMHPlayer();

protected:

	virtual void BeginPlay() override;
};

/** Writes a random reachable location to the PatrolLocation blackboard key. */
UCLASS()
class UMetaAgentBTTask_SetRandomPatrolPoint : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UMetaAgentBTTask_SetRandomPatrolPoint();

	UPROPERTY(EditAnywhere, Category = "AI|Patrol", meta=(ClampMin="100.0"))
	float SearchRadius = 1200.0f;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

/**
 * Input-driven move task that walks toward PatrolLocation using AddMovementInput.
 * This preserves player-like locomotion animation while still allowing collision blocking.
 */
UCLASS()
class UMetaAgentBTTask_MoveToPatrolPoint : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UMetaAgentBTTask_MoveToPatrolPoint();

	UPROPERTY(EditAnywhere, Category = "AI|Patrol", meta=(ClampMin="10.0"))
	float AcceptableDistance = 75.0f;

	UPROPERTY(EditAnywhere, Category = "AI|Patrol", meta=(ClampMin="0.1"))
	float MaxMoveSeconds = 8.0f;

	UPROPERTY(EditAnywhere, Category = "AI|Patrol", meta=(ClampMin="0.1"))
	float StuckTimeoutSeconds = 1.0f;

	UPROPERTY(EditAnywhere, Category = "AI|Patrol", meta=(ClampMin="0.1"))
	float MoveInputScale = 1.0f;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual uint16 GetInstanceMemorySize() const override;

private:
	struct FMoveToPatrolMemory
	{
		float ElapsedSeconds = 0.0f;
		float StuckSeconds = 0.0f;
		FVector LastLocation = FVector::ZeroVector;
	};
};

/**
 * AI controller that builds and runs a runtime behavior tree:
 * Pick random reachable point -> Move To -> Wait (2-5s) -> Repeat.
 */
UCLASS()
class AMetaAgentWanderAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMetaAgentWanderAIController();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	void BuildRuntimeBehaviorTree();
	void StartRuntimeBehaviorTree();

	UPROPERTY(EditAnywhere, Category = "AI|Patrol", meta=(ClampMin="100.0"))
	float PatrolRadius = 50000.0f;

	UPROPERTY(EditAnywhere, Category = "AI|Patrol", meta=(ClampMin="0.1"))
	float WaitMinSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category = "AI|Patrol", meta=(ClampMin="0.1"))
	float WaitMaxSeconds = 5.0f;

	UPROPERTY()
	TObjectPtr<UBlackboardData> RuntimeBlackboard = nullptr;

	UPROPERTY()
	TObjectPtr<UBehaviorTree> RuntimeBehaviorTree = nullptr;
};

struct FMetaAgentInputFallbackState;
struct FMetaAgentMovementDiagnosticsState;

class FMetaAgentCharacterRuntime
{
public:
	static void RunPossessedCharacterBootstrapSequence(
		ACharacter* PossessedCharacter,
		FMetaAgentMovementDiagnosticsState& MovementDiagnostics,
		const FMetaAgentInputFallbackState& InputFallback);
};

class IHttpRouter;
struct FHttpServerRequest;
struct FMetaAgentHostSessionSnapshot;

/**
 * Singleton Game Instance for the MetaAgent project.
 *
 * Persists across level transitions and provides a single, globally accessible
 * reference to the active player character.
 */
UCLASS(Config=Game)
class UMetaAgentGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Character")
	TSubclassOf<AMetaAgentCharacter> PlayerCharacterClass;

	UPROPERTY(BlueprintReadOnly, Category="Character")
	TObjectPtr<AMetaAgentCharacter> ActivePlayerCharacter;

	static UMetaAgentGameInstance* Get(const UObject* WorldContextObject);

	virtual void Init() override;
	virtual void Shutdown() override;

	UFUNCTION(BlueprintCallable, Category="Networking|HTTP Server")
	FString GetLocalHttpServerStatusText() const;

	UFUNCTION(BlueprintCallable, Category="Networking|Runtime")
	TArray<FString> GetNetworkingRuntimePanelLines() const;

	UFUNCTION(BlueprintCallable, Category="Networking|Platform")
	void SendEventToPlatform(const FString& EventName, const FString& Message, const FString& SourceOverride = TEXT(""));

	UFUNCTION(BlueprintCallable, Category="Networking|Runtime")
	void StartNetworkingRuntime();

	UFUNCTION(BlueprintCallable, Category="Networking|Runtime")
	void StopNetworkingRuntime();

	UPROPERTY(Config, EditAnywhere, Category="Networking|HTTP Server")
	bool bEnableLocalHttpServer = true;

	UPROPERTY(Config, EditAnywhere, Category="Networking|HTTP Server", meta=(ClampMin="1024", ClampMax="65535"))
	int32 LocalHttpServerPort = 30080;

	UPROPERTY(Config, EditAnywhere, Category="Networking|Platform")
	bool bEnablePlatformForwarding = true;

	UPROPERTY(Config, EditAnywhere, Category="Networking|Platform")
	FString PlatformBaseUrl = TEXT("http://127.0.0.1:8000");

	UPROPERTY(Config, EditAnywhere, Category="Networking|Platform")
	FString PlatformEventEndpoint = TEXT("/api/unreal/event");

	UPROPERTY(Config, EditAnywhere, Category="Networking|Platform")
	FString PlatformSessionId;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Networking|Runtime")
	bool bNetworkingRuntimeServerEnabled = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Networking|Runtime")
	bool bNetworkingRuntimeRouterBound = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Networking|Runtime")
	bool bNetworkingRuntimeListenersStarted = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Networking|Runtime")
	int32 NetworkingRuntimePort = 0;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Networking|Runtime")
	FString NetworkingRuntimeLastPlatformEvent = TEXT("none");

	UPROPERTY(Transient, BlueprintReadOnly, Category="Networking|Runtime")
	FString NetworkingRuntimeLastPlatformResult = TEXT("idle");

	UPROPERTY(Transient, BlueprintReadOnly, Category="Networking|Runtime")
	FString NetworkingRuntimeLastNotifyMessage = TEXT("none");

	UPROPERTY(Transient, BlueprintReadOnly, Category="Networking|Runtime")
	FString NetworkingRuntimeLastError = TEXT("none");

	UPROPERTY(Transient, BlueprintReadOnly, Category="Networking|Runtime")
	FString NetworkingRuntimeLastSendUtc = TEXT("n/a");

	UPROPERTY(Transient, BlueprintReadOnly, Category="Networking|Runtime")
	FString NetworkingRuntimeLastReceiveUtc = TEXT("n/a");

private:
	FMetaAgentHostSessionSnapshot BuildHostSessionSnapshot(bool bRouterBound) const;
	void ApplyNotifyMessage(const FString& NotifyMessage);
	FString BuildPlatformUrl() const;
	void StartLocalHttpServer();
	void StopLocalHttpServer();
};
