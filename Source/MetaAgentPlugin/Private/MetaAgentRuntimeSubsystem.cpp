#include "MetaAgentRuntimeSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "HttpModule.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "EngineUtils.h"
#include "IHttpRouter.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Core/MetaAgent.h"
#include "MetaAgentMainActor.h"
#include "MetaAgentPlugin.h"
#include "MetaAgentPluginSettings.h"
#include "Modules/ModuleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"
#include "UI/HUD/MetaAgentHUD.h"

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
				Out.RightChopInline(SecondUnderscore + 1, false);
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

			Out.LeftInline(LastUnderscore, false);
		}

		if (Out.EndsWith(TEXT("_C")))
		{
			Out.LeftChopInline(2, false);
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
	StopLocalHttpServer();

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
	UE_LOG(
		LogMetaAgentPlugin,
		Log,
		TEXT("MetaAgent startup hook fired. FeatureFlags: Input=%s Camera=%s AI=%s Networking=%s Recording=%s UI=%s"),
		(Settings && Settings->bEnableInputSystems) ? TEXT("On") : TEXT("Off"),
		(Settings && Settings->bEnableCameraSystems) ? TEXT("On") : TEXT("Off"),
		(Settings && Settings->bEnableAISystems) ? TEXT("On") : TEXT("Off"),
		(Settings && Settings->bEnableNetworkingSystems) ? TEXT("On") : TEXT("Off"),
		(Settings && Settings->bEnableRecordingSystems) ? TEXT("On") : TEXT("Off"),
		(Settings && Settings->bEnableUISystems) ? TEXT("On") : TEXT("Off"));

	bool bRequestedActive = Settings && Settings->bActive;
	for (TActorIterator<AMetaAgentMainActor> It(&InWorld); It; ++It)
	{
		if (AMetaAgentMainActor* MainActor = *It)
		{
			bRequestedActive = MainActor->bActive;
			break;
		}
	}

	GMetaAgentRuntimeActive = bRequestedActive;
	UE_LOG(LogMetaAgentPlugin, Log, TEXT("MetaAgent runtime global active state: %s"), GMetaAgentRuntimeActive ? TEXT("ACTIVE") : TEXT("INACTIVE"));

	if (GMetaAgentRuntimeActive)
	{
		// Possession fallback for projects still running a non-plugin GameMode.
		ScheduleAutoPossessPreferredPawn(&InWorld, 20);
	}

	// Startup orchestration point for runtime systems.
}

void UMetaAgentRuntimeSubsystem::ScheduleAutoPossessPreferredPawn(UWorld* InWorld, int32 AttemptsRemaining)
{
	if (!InWorld || AttemptsRemaining <= 0)
	{
		return;
	}

	TWeakObjectPtr<UWorld> WeakWorld(InWorld);
	InWorld->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, WeakWorld, AttemptsRemaining]()
	{
		if (!WeakWorld.IsValid())
		{
			return;
		}

		UWorld* World = WeakWorld.Get();
		const bool bLogFailure = (AttemptsRemaining <= 1);
		const bool bFinished = TryAutoPossessPreferredPawn(*World, bLogFailure);
		if (!bFinished)
		{
			ScheduleAutoPossessPreferredPawn(World, AttemptsRemaining - 1);
		}
	}));
}

bool UMetaAgentRuntimeSubsystem::TryAutoPossessPreferredPawn(UWorld& InWorld, bool bLogFailure)
{
	if (!bRuntimeActive || !InWorld.IsGameWorld() || !GMetaAgentRuntimeActive)
	{
		return true;
	}

	APlayerController* PlayerController = InWorld.GetFirstPlayerController();
	if (!PlayerController)
	{
		return false;
	}

	FString PreferredPawnName = TEXT("MAIN_CHARACTER");
	bool bRequireExactPreferredPawnName = true;
	bool bRequireUniquePreferredPawnName = true;
	if (GConfig)
	{
		GConfig->GetString(TEXT("/Script/MetaAgentPlugin.MetaAgentGameMode"), TEXT("PreferredPlacedPawnName"), PreferredPawnName, GGameIni);
		GConfig->GetBool(TEXT("/Script/MetaAgentPlugin.MetaAgentGameMode"), TEXT("bRequireExactPreferredPawnName"), bRequireExactPreferredPawnName, GGameIni);
		GConfig->GetBool(TEXT("/Script/MetaAgentPlugin.MetaAgentGameMode"), TEXT("bRequireUniquePreferredPawnName"), bRequireUniquePreferredPawnName, GGameIni);
	}

	APawn* CurrentPawn = PlayerController->GetPawn();
	if (CurrentPawn && !CurrentPawn->IsA<ASpectatorPawn>()
		&& (!bRequireExactPreferredPawnName || MatchesPreferredName(CurrentPawn, PreferredPawnName)))
	{
		return true;
	}

	APawn* NamedPawn = nullptr;
	APawn* FirstUsablePawn = nullptr;
	int32 NamedPawnMatchCount = 0;

	for (TActorIterator<APawn> It(&InWorld); It; ++It)
	{
		APawn* Candidate = *It;
		if (!Candidate || Candidate->IsA<ASpectatorPawn>())
		{
			continue;
		}

		AController* ExistingController = Candidate->GetController();
		const bool bCanRepossess = (ExistingController == nullptr)
			|| (ExistingController == PlayerController)
			|| !ExistingController->IsPlayerController();
		if (!bCanRepossess)
		{
			continue;
		}

		if (!FirstUsablePawn)
		{
			FirstUsablePawn = Candidate;
		}

		if (MatchesPreferredName(Candidate, PreferredPawnName))
		{
			++NamedPawnMatchCount;
			if (!NamedPawn)
			{
				NamedPawn = Candidate;
			}
		}
	}

	APawn* TargetPawn = nullptr;
	if (bRequireExactPreferredPawnName && !PreferredPawnName.IsEmpty())
	{
		if (NamedPawnMatchCount == 1)
		{
			TargetPawn = NamedPawn;
		}
		else if (NamedPawnMatchCount > 1 && bRequireUniquePreferredPawnName)
		{
			if (bLogFailure)
			{
				UE_LOG(LogMetaAgentPlugin, Error,
					TEXT("AutoPossess fallback failed: preferred pawn name '%s' is ambiguous (%d matches)."),
					*PreferredPawnName,
					NamedPawnMatchCount);
			}
			return true;
		}
		else if (NamedPawnMatchCount == 0)
		{
			AActor* MatchingNonPawnActor = nullptr;
			for (TActorIterator<AActor> It(&InWorld); It; ++It)
			{
				AActor* CandidateActor = *It;
				if (!CandidateActor || Cast<APawn>(CandidateActor))
				{
					continue;
				}

				if (IsFlexibleNameMatch(CandidateActor->GetName(), PreferredPawnName))
				{
					MatchingNonPawnActor = CandidateActor;
					break;
				}

#if WITH_EDITOR
				if (IsFlexibleNameMatch(CandidateActor->GetActorLabel(), PreferredPawnName))
				{
					MatchingNonPawnActor = CandidateActor;
					break;
				}
#endif
			}

			if (MatchingNonPawnActor)
			{
				UE_LOG(LogMetaAgentPlugin, Error,
					TEXT("AutoPossess fallback: found actor '%s' for preferred name '%s', but class '%s' is not a Pawn/Character and cannot be possessed."),
					*GetNameSafe(MatchingNonPawnActor),
					*PreferredPawnName,
					*GetNameSafe(MatchingNonPawnActor->GetClass()));
			}

			if (bLogFailure)
			{
				UE_LOG(LogMetaAgentPlugin, Error,
					TEXT("AutoPossess fallback failed: required pawn '%s' was not found in world '%s'."),
					*PreferredPawnName,
					*InWorld.GetMapName());
			}
			return true;
		}
	}
	else
	{
		TargetPawn = NamedPawn ? NamedPawn : FirstUsablePawn;
	}

	if (!TargetPawn)
	{
		if (bLogFailure)
		{
			UE_LOG(LogMetaAgentPlugin, Error,
				TEXT("AutoPossess fallback failed: no suitable pawn found to possess in world '%s'."),
				*InWorld.GetMapName());
		}
		return true;
	}

	PlayerController->Possess(TargetPawn);
	UE_LOG(LogMetaAgentPlugin, Log,
		TEXT("AutoPossess fallback succeeded: controller '%s' now possesses '%s' (%s)."),
		*GetNameSafe(PlayerController),
		*GetNameSafe(TargetPawn),
		*GetNameSafe(TargetPawn->GetClass()));

	return true;
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
	const bool bEnabled = Settings && Settings->bEnableNetworkingSystems;
	const int32 Port = Settings ? Settings->LocalHttpServerPort : 0;
	const TCHAR* EnabledText = bEnabled ? TEXT("enabled") : TEXT("disabled");
	const TCHAR* BoundText = LocalHttpRouter.IsValid() ? TEXT("bound") : TEXT("not-bound");

	return FString::Printf(
		TEXT("HTTP %s, port=%d, router=%s, endpoints=/health,/echo,/notify"),
		EnabledText,
		Port,
		BoundText);
}

void UMetaAgentRuntimeSubsystem::StartLocalHttpServer()
{
	const UMetaAgentPluginSettings* Settings = GetDefault<UMetaAgentPluginSettings>();
	if (!Settings || !Settings->bEnableNetworkingSystems)
	{
		UE_LOG(LogMetaAgent, Log, TEXT("HTTP server disabled by config."));
		return;
	}

	if (!FHttpServerModule::IsAvailable())
	{
		FModuleManager::LoadModuleChecked<FHttpServerModule>(TEXT("HTTPServer"));
	}

	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
	LocalHttpRouter = HttpServerModule.GetHttpRouter(static_cast<uint32>(Settings->LocalHttpServerPort), true);
	if (!LocalHttpRouter.IsValid())
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("HTTP server failed to bind port %d."), Settings->LocalHttpServerPort);
		return;
	}

	RouteHandles.Reset();

	const FHttpRequestHandler HealthHandler = FHttpRequestHandler::CreateUObject(this, &UMetaAgentRuntimeSubsystem::HandleHealthRequest);
	const FHttpRequestHandler EchoHandler = FHttpRequestHandler::CreateUObject(this, &UMetaAgentRuntimeSubsystem::HandleEchoRequest);
	const FHttpRequestHandler NotifyHandler = FHttpRequestHandler::CreateUObject(this, &UMetaAgentRuntimeSubsystem::HandleNotifyRequest);

	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/health"), EHttpServerRequestVerbs::VERB_GET, HealthHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/health/"), EHttpServerRequestVerbs::VERB_GET, HealthHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/echo"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, EchoHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/echo/"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST, EchoHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/notify"), EHttpServerRequestVerbs::VERB_POST, NotifyHandler);
	AddBoundRoute(LocalHttpRouter, RouteHandles, TEXT("/notify/"), EHttpServerRequestVerbs::VERB_POST, NotifyHandler);

	HttpServerModule.StartAllListeners();
	UE_LOG(LogMetaAgent, Log, TEXT("HTTP server listening on port %d. Endpoints: /health, /echo, /notify"), Settings->LocalHttpServerPort);
}

void UMetaAgentRuntimeSubsystem::StopLocalHttpServer()
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
	UE_LOG(LogMetaAgent, Log, TEXT("HTTP server routes unbound."));
}

bool UMetaAgentRuntimeSubsystem::HandleHealthRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
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

bool UMetaAgentRuntimeSubsystem::HandleEchoRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
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

bool UMetaAgentRuntimeSubsystem::HandleNotifyRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
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

	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (AMetaAgentHUD* HUD = Cast<AMetaAgentHUD>(PC->GetHUD()))
			{
				HUD->AddTransientMessage(NotifyMessage, FColor::Cyan, 3.0f);
			}
		}
	}

	UE_LOG(LogMetaAgent, Log, TEXT("Platform notify received: %s"), *NotifyMessage);

	const FString Response = TEXT("{\"ok\":true}");
	TUniquePtr<FHttpServerResponse> HttpResponse = FHttpServerResponse::Create(Response, TEXT("application/json"));
	HttpResponse->Code = EHttpServerResponseCodes::Ok;
	OnComplete(MoveTemp(HttpResponse));
	return true;
}

