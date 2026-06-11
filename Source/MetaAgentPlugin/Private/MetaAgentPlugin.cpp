#include "MetaAgentPlugin.h"

#include "GameMapsSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Systems/ParticleRuntime/MetaAgentParticleFormingSolver.h"
#include "Systems/ParticleRuntime/MetaAgentParticleRepresentationDriver.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeRegistry.h"
#include "Systems/ParticleRuntime/MetaAgentParticleTransitionGraph.h"

DEFINE_LOG_CATEGORY(LogMetaAgentPlugin);

#define LOCTEXT_NAMESPACE "FMetaAgentPluginModule"

void FMetaAgentPluginModule::StartupModule()
{
	UE_LOG(LogMetaAgentPlugin, Log, TEXT("MetaAgentPlugin module startup."));

	FMetaAgentParticleShapeRegistry::RegisterDefaults();
	FMetaAgentParticleFormingSolverRegistry::RegisterDefaults();
	FMetaAgentParticleTransitionGraph::RegisterDefaults();
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

IMPLEMENT_MODULE(FMetaAgentPluginModule, MetaAgentPlugin)
