#include "MetaAgentPlugin.h"

#include "GameMapsSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "metaagent/initialize.hpp"
#include "MetaAgentParticleControl.h"
#include "MetaAgentGameplay.h"
#include "MetaAgentHUD.h"
#include "MetaAgentParticleShapes.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "HttpModule.h"
#include "Host/MetaAgentHostSession.h"
#include "Host/MetaAgentHttpBridge.h"
#include "metaagent/session/status.hpp"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogMetaAgentPlugin);
DEFINE_LOG_CATEGORY(LogMetaAgent)

bool GMetaAgentRuntimeActive = true;

namespace MetaAgentParticleTags
{
	UE_DEFINE_GAMEPLAY_TAG(Pattern_Active, TEXT("MetaAgent.Pattern.Active"));
	UE_DEFINE_GAMEPLAY_TAG(Pattern_Blocked, TEXT("MetaAgent.Pattern.Blocked"));
	UE_DEFINE_GAMEPLAY_TAG(Pattern_ImageReveal, TEXT("MetaAgent.Pattern.ImageReveal"));
	UE_DEFINE_GAMEPLAY_TAG(Pattern_Queued, TEXT("MetaAgent.Pattern.Queued"));
}

#define LOCTEXT_NAMESPACE "FMetaAgentPluginModule"

void FMetaAgentPluginModule::StartupModule()
{
	UE_LOG(LogMetaAgentPlugin, Log, TEXT("MetaAgentPlugin module startup."));

	metaagent::initialize_defaults();
	FMetaAgentParticleShapeRegistry::RegisterDefaults();
	FMetaAgentParticleRepresentationDriverRegistry::RegisterDefaults();

	const FString DesiredGameMode = TEXT("/Script/MetaAgentPlugin.MetaAgentGameMode");
	const FString CurrentDefaultGameMode = UGameMapsSettings::GetGlobalDefaultGameMode();
	bool bUpdatedAnySetting = false;

	if (!CurrentDefaultGameMode.Equals(DesiredGameMode, ESearchCase::CaseSensitive))
	{
		UGameMapsSettings::SetGlobalDefaultGameMode(DesiredGameMode);
		bUpdatedAnySetting = true;
	}

	if (GConfig)
	{
		FString CurrentServerGameMode;
		GConfig->GetString(
			TEXT("/Script/EngineSettings.GameMapsSettings"),
			TEXT("GlobalDefaultServerGameMode"),
			CurrentServerGameMode,
			GEngineIni);

		if (!CurrentServerGameMode.Equals(DesiredGameMode, ESearchCase::CaseSensitive))
		{
			GConfig->SetString(
				TEXT("/Script/EngineSettings.GameMapsSettings"),
				TEXT("GlobalDefaultServerGameMode"),
				*DesiredGameMode,
				GEngineIni);
			bUpdatedAnySetting = true;
		}
	}

	if (bUpdatedAnySetting)
	{
		if (GConfig)
		{
			GConfig->Flush(false, GEngineIni);
		}

		UE_LOG(LogMetaAgentPlugin, Warning,
			TEXT("MetaAgentPlugin updated Maps & Modes defaults: GlobalDefaultGameMode -> %s"),
			*DesiredGameMode);
	}
}

void FMetaAgentPluginModule::ShutdownModule()
{
	UE_LOG(LogMetaAgentPlugin, Log, TEXT("MetaAgentPlugin module shutdown."));
}

#undef LOCTEXT_NAMESPACE

UMetaAgentPluginSettings::UMetaAgentPluginSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("MetaAgentPlugin");
}

FName UMetaAgentPluginSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

bool UMetaAgentBlueprintLibrary::IsMetaAgentRuntimeActive(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject))
	{
		return false;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!IsValid(GameInstance))
	{
		return false;
	}

	const UMetaAgentRuntimeSubsystem* RuntimeSubsystem = GameInstance->GetSubsystem<UMetaAgentRuntimeSubsystem>();
	return IsValid(RuntimeSubsystem) && RuntimeSubsystem->IsActive();
}

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
				Out = Out.RightChop(SecondUnderscore + 1);
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

			Out = Out.Left(LastUnderscore);
		}

		if (Out.EndsWith(TEXT("_C")))
		{
			Out = Out.LeftChop(2);
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

void UMetaAgentRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UMetaAgentPluginSettings* Settings = GetDefault<UMetaAgentPluginSettings>();
	bRuntimeActive = Settings && Settings->bActive;
	GMetaAgentRuntimeActive = bRuntimeActive;

	if (!bRuntimeActive)
	{
		UE_LOG(LogMetaAgentPlugin, Log, TEXT("MetaAgent runtime is disabled in settings."));
		return;
	}

	if (UWorld* CurrentWorld = GetWorld())
	{
		HandleWorldBeginPlay(*CurrentWorld);
	}

	UE_LOG(LogMetaAgentPlugin, Log, TEXT("MetaAgent runtime subsystem initialized and active."));
}

void UMetaAgentRuntimeSubsystem::Deinitialize()
{
	FMetaAgentHttpBridge::Get().Stop();

	bRuntimeActive = false;
	Super::Deinitialize();
}

void UMetaAgentRuntimeSubsystem::HandleWorldBeginPlay(UWorld& InWorld)
{
	if (!bRuntimeActive || !InWorld.IsGameWorld())
	{
		return;
	}

	const UMetaAgentPluginSettings* Settings = GetDefault<UMetaAgentPluginSettings>();
	bool bRequestedActive = Settings && Settings->bActive;
	for (TActorIterator<AMetaAgentMainActor> It(&InWorld); It; ++It)
	{
		if (AMetaAgentMainActor* MainActor = *It)
		{
			bRequestedActive = MainActor->bActive;
			break;
		}
	}

	const FString StartupFeatureFlags = UTF8_TO_TCHAR(
		metaagent::session::build_startup_feature_flags_text(
			MetaAgentHostSession::MakeFromWorld(
				&InWorld,
				bRequestedActive,
				Settings && Settings->bEnableInputSystems,
				Settings && Settings->bEnableCameraSystems,
				Settings && Settings->bEnableAISystems,
				Settings && Settings->bEnableNetworkingSystems,
				Settings && Settings->bEnableRecordingSystems,
				Settings && Settings->bEnableUISystems,
				true,
				Settings ? Settings->LocalHttpServerPort : 0,
				Settings && Settings->bEnableNetworkingSystems,
				FMetaAgentHttpBridge::Get().IsRouterBound())
				.ToCoreSession())
			.c_str());
	UE_LOG(
		LogMetaAgentPlugin,
		Log,
		TEXT("MetaAgent startup hook fired. FeatureFlags: %s"),
		*StartupFeatureFlags);

	GMetaAgentRuntimeActive = bRequestedActive;
	UE_LOG(LogMetaAgentPlugin, Log, TEXT("MetaAgent runtime global active state: %s"), GMetaAgentRuntimeActive ? TEXT("ACTIVE") : TEXT("INACTIVE"));

	if (GMetaAgentRuntimeActive)
	{
		// Possession is now owned by MetaAgentGameMode only.
		UE_LOG(LogMetaAgentPlugin, Log,
			TEXT("MetaAgent startup: possession authority is MetaAgentGameMode (runtime auto-possess fallback disabled)."));
	}

	// Startup orchestration point for runtime systems.
}

FString UMetaAgentRuntimeSubsystem::BuildPlatformUrl() const
{
	const UMetaAgentPluginSettings* Settings = GetDefault<UMetaAgentPluginSettings>();
	if (!Settings)
	{
		return FString();
	}

	const FString Base = Settings->PlatformBaseUrl.EndsWith(TEXT("/")) ? Settings->PlatformBaseUrl.LeftChop(1) : Settings->PlatformBaseUrl;
	const FString Path = Settings->PlatformEventEndpoint.StartsWith(TEXT("/")) ? Settings->PlatformEventEndpoint : FString::Printf(TEXT("/%s"), *Settings->PlatformEventEndpoint);
	return Base + Path;
}

void UMetaAgentRuntimeSubsystem::SendEventToPlatform(const FString& EventName, const FString& Message, const FString& SourceOverride)
{
	const UMetaAgentPluginSettings* Settings = GetDefault<UMetaAgentPluginSettings>();
	if (!Settings || !Settings->bEnableNetworkingSystems)
	{
		UE_LOG(LogMetaAgent, Verbose, TEXT("Platform forwarding disabled. Event '%s' was not sent."), *EventName);
		return;
	}

	const FString RequestUrl = BuildPlatformUrl();
	if (RequestUrl.IsEmpty())
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("Platform forwarding URL is empty. Configure PlatformBaseUrl/PlatformEventEndpoint."));
		return;
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("source"), SourceOverride.IsEmpty() ? TEXT("unreal") : SourceOverride);
	Payload->SetStringField(TEXT("event"), EventName);
	Payload->SetStringField(TEXT("message"), Message);
	Payload->SetStringField(TEXT("session_id"), Settings->PlatformSessionId.IsEmpty() ? TEXT("default") : Settings->PlatformSessionId);
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
			if (!bWasSuccessful)
			{
				UE_LOG(LogMetaAgent, Warning, TEXT("Platform event '%s' failed to send (network failure)."), *EventName);
				return;
			}

			if (!Response.IsValid())
			{
				UE_LOG(LogMetaAgent, Warning, TEXT("Platform event '%s' received no HTTP response."), *EventName);
				return;
			}

			const int32 StatusCode = Response->GetResponseCode();
			if (StatusCode >= 200 && StatusCode < 300)
			{
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

				FString HudMessage;
				if (!AgentAction.IsEmpty())
				{
					HudMessage = FString::Printf(
						TEXT("Agent %s (%s)"),
						*AgentAction.ToUpper(),
						bAgentRunning ? TEXT("RUNNING") : TEXT("STOPPED"));
				}
				else if (ResponseJson.IsValid())
				{
					HudMessage = FString::Printf(TEXT("Agent %s"), bAgentRunning ? TEXT("RUNNING") : TEXT("STOPPED"));
				}

				if (!HudMessage.IsEmpty())
				{
					if (UWorld* World = GetWorld())
					{
						if (APlayerController* PC = World->GetFirstPlayerController())
						{
							if (AMetaAgentHUD* HUD = Cast<AMetaAgentHUD>(PC->GetHUD()))
							{
								HUD->AddTransientMessage(HudMessage, bAgentRunning ? FColor::Green : FColor::Yellow, 2.0f);
							}
						}
					}
				}
			}
			else
			{
				UE_LOG(LogMetaAgent, Warning,
					TEXT("Platform event '%s' returned HTTP %d. Body: %s"),
					*EventName,
					StatusCode,
					*Response->GetContentAsString());
			}
		});

	if (!HttpRequest->ProcessRequest())
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("Failed to dispatch platform event '%s' request."), *EventName);
	}
}

FString UMetaAgentRuntimeSubsystem::GetLocalHttpServerStatusText() const
{
	const UMetaAgentPluginSettings* Settings = GetDefault<UMetaAgentPluginSettings>();
	const FMetaAgentHostSessionSnapshot Snapshot = MetaAgentHostSession::MakeFromWorld(
		GetWorld(),
		bRuntimeActive,
		Settings && Settings->bEnableInputSystems,
		Settings && Settings->bEnableCameraSystems,
		Settings && Settings->bEnableAISystems,
		Settings && Settings->bEnableNetworkingSystems,
		Settings && Settings->bEnableRecordingSystems,
		Settings && Settings->bEnableUISystems,
		true,
		Settings ? Settings->LocalHttpServerPort : 0,
		Settings && Settings->bEnableNetworkingSystems,
		FMetaAgentHttpBridge::Get().IsRouterBound());
	return FMetaAgentHttpBridge::Get().GetStatusText(Snapshot);
}

IMPLEMENT_MODULE(FMetaAgentPluginModule, MetaAgentPlugin)
