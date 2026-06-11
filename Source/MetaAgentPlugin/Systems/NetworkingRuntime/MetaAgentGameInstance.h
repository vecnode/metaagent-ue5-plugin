// Copyright (c) vecnode 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "HttpRequestHandler.h"
#include "HttpRouteHandle.h"
#include "MetaAgentGameInstance.generated.h"

class AMetaAgentCharacter;
class IHttpRouter;
struct FHttpServerRequest;

/**
 * Singleton Game Instance for the MetaAgent project.
 *
 * Persists across level transitions and provides a single, globally accessible
 * reference to the active player character.  Any C++ class that has a world
 * context object can reach it via UMetaAgentGameInstance::Get(this).
 *
 * --- Editor setup required ---
 * Project Settings â†’ Maps & Modes â†’ Game Instance Class = MetaAgentGameInstance
 */
UCLASS(Config=Game)
class UMetaAgentGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:

	/**
	 * The Blueprint class used to spawn the player character.
	 * Assign BP_character1 here in the Editor (or via Project Settings default pawn).
	 * Exposed to Blueprints so level designers can swap MetaAgent without recompiling.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Character")
	TSubclassOf<AMetaAgentCharacter> PlayerCharacterClass;

	/**
	 * Live reference to the currently possessed player character.
	 * Populated automatically from AMetaAgentMHPlayer::BeginPlay().
	 * Read-only from Blueprints to prevent accidental replacement.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Character")
	TObjectPtr<AMetaAgentCharacter> ActivePlayerCharacter;

	/**
	 * Global accessor.  Returns the game instance cast to this type.
	 * Returns nullptr if the world context is invalid or the Game Instance
	 * class has not been set to UMetaAgentGameInstance in Project Settings.
	 *
	 * Usage from any UObject:
	 *   if (UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
	 *       GI->ActivePlayerCharacter->DoSomething();
	 */
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

	bool HandleHealthRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleEchoRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleNotifyRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	FString BuildPlatformUrl() const;
	void StartLocalHttpServer();
	void StopLocalHttpServer();

	TSharedPtr<IHttpRouter> LocalHttpRouter;
	TArray<FHttpRouteHandle> RouteHandles;
};

