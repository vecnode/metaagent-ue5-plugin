#pragma once

#include "Modules/ModuleManager.h"
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HttpRequestHandler.h"
#include "HttpRouteHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NativeGameplayTags.h"
#include "MetaAgentPlugin.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMetaAgentPlugin, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogMetaAgent, Log, All);

namespace MetaAgentParticleTags
{
	METAAGENTPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Pattern_Active);
	METAAGENTPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Pattern_Blocked);
	METAAGENTPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Pattern_ImageReveal);
	METAAGENTPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Pattern_Queued);
}

extern bool GMetaAgentRuntimeActive;

FORCEINLINE bool IsMetaAgentRuntimeActive()
{
	return GMetaAgentRuntimeActive;
}

class FMetaAgentPluginModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

UCLASS(Config=Game, defaultconfig, meta=(DisplayName="Meta Agent Plugin"))
class METAAGENTPLUGIN_API UMetaAgentPluginSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMetaAgentPluginSettings();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category="MetaAgent|Startup")
	bool bActive = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category="MetaAgent|Feature Flags")
	bool bEnableInputSystems = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category="MetaAgent|Feature Flags")
	bool bEnableCameraSystems = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category="MetaAgent|Feature Flags")
	bool bEnableAISystems = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category="MetaAgent|Feature Flags")
	bool bEnableNetworkingSystems = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category="MetaAgent|Feature Flags")
	bool bEnableRecordingSystems = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category="MetaAgent|Feature Flags")
	bool bEnableUISystems = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category="MetaAgent|Networking|HTTP Server", meta=(ClampMin="1024", ClampMax="65535"))
	int32 LocalHttpServerPort = 30080;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category="MetaAgent|Networking|Platform")
	FString PlatformBaseUrl = TEXT("http://127.0.0.1:8000");

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category="MetaAgent|Networking|Platform")
	FString PlatformEventEndpoint = TEXT("/api/unreal/event");

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category="MetaAgent|Networking|Platform")
	FString PlatformSessionId = TEXT("characters-local");
};

UCLASS()
class METAAGENTPLUGIN_API UMetaAgentBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="MetaAgent|Runtime", meta=(WorldContext="WorldContextObject"))
	static bool IsMetaAgentRuntimeActive(const UObject* WorldContextObject);
};

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
