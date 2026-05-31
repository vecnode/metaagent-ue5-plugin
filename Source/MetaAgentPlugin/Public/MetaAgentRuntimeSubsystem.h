#pragma once

#include "CoreMinimal.h"
#include "HttpRequestHandler.h"
#include "HttpRouteHandle.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MetaAgentRuntimeSubsystem.generated.h"

class IHttpRouter;
struct FHttpServerRequest;

UCLASS()
class METAAGENTPLUGIN_API UMetaAgentRuntimeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category="MetaAgent|Runtime")
	bool IsActive() const { return bRuntimeActive; }

	UFUNCTION(BlueprintCallable, Category="MetaAgent|Networking|Platform")
	void SendEventToPlatform(const FString& EventName, const FString& Message, const FString& SourceOverride = TEXT(""));

	UFUNCTION(BlueprintPure, Category="MetaAgent|Networking|HTTP Server")
	FString GetLocalHttpServerStatusText() const;

private:
	void HandleWorldBeginPlay(UWorld& InWorld);
	void StartLocalHttpServer();
	void StopLocalHttpServer();
	FString BuildPlatformUrl() const;

	bool HandleHealthRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleEchoRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleNotifyRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	bool bRuntimeActive = false;
	TSharedPtr<IHttpRouter> LocalHttpRouter;
	TArray<FHttpRouteHandle> RouteHandles;
};
