#pragma once

#include "CoreMinimal.h"
#include "net/platform_client.hpp"

class UWorld;

struct FMetaAgentPlatformSendOutcome
{
	bool bDispatched = false;
	FString DispatchError;
	metaagent::net::PlatformEventResponse Response;
};

class FMetaAgentPlatformBridge
{
public:
	using FOnComplete = TFunction<void(const FMetaAgentPlatformSendOutcome& Outcome)>;

	static void SendOutboundEvent(
		UWorld* World,
		const metaagent::net::PlatformEndpointConfig& Config,
		const FString& EventName,
		const FString& Message,
		const FString& SourceOverride,
		const FString& MapName,
		const FString& BuildLabel,
		FOnComplete OnComplete);

	static metaagent::net::PlatformEndpointConfig MakeConfigFromPluginSettings(bool bForceEnabled = false);

	static metaagent::net::PlatformEndpointConfig MakeConfigFromGameInstance(
		bool bEnabled,
		const FString& BaseUrl,
		const FString& EventEndpoint,
		const FString& SessionId);
};
