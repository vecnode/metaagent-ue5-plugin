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
#include "Host/MetaAgentPlatformBridge.h"
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

void UMetaAgentRuntimeSubsystem::SendEventToPlatform(
	const FString& EventName,
	const FString& Message,
	const FString& SourceOverride)
{
	if (UMetaAgentGameInstance* GameInstance = Cast<UMetaAgentGameInstance>(GetGameInstance()))
	{
		GameInstance->SendEventToPlatform(EventName, Message, SourceOverride);
		return;
	}

	const metaagent::net::PlatformEndpointConfig Config =
		FMetaAgentPlatformBridge::MakeConfigFromPluginSettings();
	if (!Config.enabled)
	{
		UE_LOG(LogMetaAgent, Verbose, TEXT("Platform forwarding disabled. Event '%s' was not sent."), *EventName);
		return;
	}

	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : TEXT("unknown");
	const FString BuildLabel = GetBuildConfigurationLabel();
	FMetaAgentPlatformBridge::SendOutboundEvent(
		GetWorld(),
		Config,
		EventName,
		Message,
		SourceOverride,
		MapName,
		BuildLabel,
		nullptr);
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
