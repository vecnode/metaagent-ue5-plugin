#include "Host/MetaAgentPlatformBridge.h"

#include "MetaAgentHUD.h"
#include "MetaAgentPlugin.h"

#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

namespace
{
std::string ToCoreString(const FString& Value)
{
	const FTCHARToUTF8 Converter(*Value);
	return std::string(Converter.Get(), static_cast<size_t>(Converter.Length()));
}

FString CoreStringToFString(const metaagent::core::String& Value)
{
	return FString(UTF8_TO_TCHAR(Value.c_str()));
}

metaagent::net::PlatformEvent BuildCoreEvent(
	const FString& EventName,
	const FString& Message,
	const FString& SourceOverride,
	const FString& MapName,
	const FString& BuildLabel)
{
	metaagent::net::PlatformEvent Event;
	Event.source = ToCoreString(SourceOverride.IsEmpty() ? TEXT("unreal") : SourceOverride);
	Event.event_name = ToCoreString(EventName);
	Event.message = ToCoreString(Message);
	Event.timestamp_utc = ToCoreString(FDateTime::UtcNow().ToIso8601());
	Event.metadata.map_name = ToCoreString(MapName);
	Event.metadata.build_label = ToCoreString(BuildLabel);
	return Event;
}
}

metaagent::net::PlatformEndpointConfig FMetaAgentPlatformBridge::MakeConfigFromPluginSettings(const bool bForceEnabled)
{
	metaagent::net::PlatformEndpointConfig Config;
	const UMetaAgentPluginSettings* Settings = GetDefault<UMetaAgentPluginSettings>();
	if (!Settings)
	{
		return Config;
	}

	Config.enabled = bForceEnabled || Settings->bEnableNetworkingSystems;
	Config.base_url = ToCoreString(Settings->PlatformBaseUrl);
	Config.event_endpoint = ToCoreString(Settings->PlatformEventEndpoint);
	Config.session_id = ToCoreString(
		Settings->PlatformSessionId.IsEmpty() ? TEXT("default") : Settings->PlatformSessionId);
	return Config;
}

metaagent::net::PlatformEndpointConfig FMetaAgentPlatformBridge::MakeConfigFromGameInstance(
	const bool bEnabled,
	const FString& BaseUrl,
	const FString& EventEndpoint,
	const FString& SessionId)
{
	metaagent::net::PlatformEndpointConfig Config;
	Config.enabled = bEnabled;
	Config.base_url = ToCoreString(BaseUrl);
	Config.event_endpoint = ToCoreString(EventEndpoint);
	Config.session_id = ToCoreString(SessionId.IsEmpty() ? TEXT("default") : SessionId);
	return Config;
}

void FMetaAgentPlatformBridge::SendOutboundEvent(
	UWorld* World,
	const metaagent::net::PlatformEndpointConfig& Config,
	const FString& EventName,
	const FString& Message,
	const FString& SourceOverride,
	const FString& MapName,
	const FString& BuildLabel,
	FOnComplete OnComplete)
{
	FMetaAgentPlatformSendOutcome Outcome;
	const metaagent::net::PlatformEvent Event =
		BuildCoreEvent(EventName, Message, SourceOverride, MapName, BuildLabel);
	const metaagent::net::PlatformOutboundRequest Request =
		metaagent::net::build_platform_outbound_request(Config, Event);

	if (!Request.valid)
	{
		Outcome.DispatchError = CoreStringToFString(Request.error_message);
		UE_LOG(LogMetaAgent, Warning, TEXT("%s"), *Outcome.DispatchError);
		if (OnComplete)
		{
			OnComplete(Outcome);
		}
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(CoreStringToFString(Request.url));
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetContentAsString(CoreStringToFString(Request.body));

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[World, EventName, OnComplete = MoveTemp(OnComplete)](
			FHttpRequestPtr Request,
			FHttpResponsePtr Response,
			const bool bWasSuccessful)
		{
			FMetaAgentPlatformSendOutcome CompletedOutcome;
			CompletedOutcome.bDispatched = true;

			const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
			const metaagent::core::String ResponseBody = Response.IsValid()
				? ToCoreString(Response->GetContentAsString())
				: metaagent::core::String{};

			CompletedOutcome.Response = metaagent::net::parse_platform_event_response(
				StatusCode,
				ResponseBody,
				bWasSuccessful && Response.IsValid());

			if (!CompletedOutcome.Response.transport_ok)
			{
				UE_LOG(LogMetaAgent, Warning, TEXT("Platform event '%s' failed to send (network failure)."), *EventName);
			}
			else if (!CompletedOutcome.Response.http_success)
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("Platform event '%s' returned HTTP %d. Body: %s"),
					*EventName,
					StatusCode,
					Response.IsValid() ? *Response->GetContentAsString() : TEXT(""));
			}
			else
			{
				UE_LOG(LogMetaAgent, Log, TEXT("Platform event '%s' acknowledged [%d]."), *EventName, StatusCode);

				if (!CompletedOutcome.Response.user_message.empty())
				{
					if (World)
					{
						if (APlayerController* PC = World->GetFirstPlayerController())
						{
							if (AMetaAgentHUD* HUD = Cast<AMetaAgentHUD>(PC->GetHUD()))
							{
								const FString HudMessage = CoreStringToFString(CompletedOutcome.Response.user_message);
								const FColor Color = CompletedOutcome.Response.agent_running ? FColor::Green : FColor::Yellow;
								HUD->AddTransientMessage(HudMessage, Color, 2.0f);
							}
						}
					}
				}
			}

			if (OnComplete)
			{
				OnComplete(CompletedOutcome);
			}
		});

	if (!HttpRequest->ProcessRequest())
	{
		Outcome.DispatchError = TEXT("Failed to dispatch HTTP request.");
		UE_LOG(LogMetaAgent, Warning, TEXT("Failed to dispatch platform event '%s' request."), *EventName);
		if (OnComplete)
		{
			OnComplete(Outcome);
		}
		return;
	}

	Outcome.bDispatched = true;
}
