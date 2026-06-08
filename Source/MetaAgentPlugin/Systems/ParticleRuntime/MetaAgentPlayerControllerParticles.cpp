// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"

#include "Core/MetaAgent.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/ParticleRuntime/MetaAgentParticleRuntime.h"

namespace MetaAgentParticlePatternConsole
{
	AMetaAgentPlayerController* ResolveLocalMetaAgentController()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			UWorld* World = WorldContext.World();
			if (!World || !World->IsGameWorld())
			{
				continue;
			}

			if (APlayerController* PlayerController = World->GetFirstPlayerController())
			{
				return Cast<AMetaAgentPlayerController>(PlayerController);
			}
		}

		return nullptr;
	}

	void ShowTransientPatternMessage(AMetaAgentPlayerController* Controller, const FString& Message, const FColor Color)
	{
		if (!Controller)
		{
			return;
		}

		if (AMetaAgentHUD* MetaAgentHUD = Controller->GetHUD<AMetaAgentHUD>())
		{
			MetaAgentHUD->AddTransientMessage(Message, Color, 2.5f);
		}
	}

	void ExecSetForm(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Form <seconds>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Form: no local MetaAgent player controller found."));
			return;
		}

		const FMetaAgentParticlePatternConfig CurrentConfig = Controller->GetParticlePatternConfig();
		const float FormSeconds = FMath::Max(0.1f, FCString::Atof(*Args[0]));
		Controller->SetParticlePatternTimings(
			FormSeconds,
			CurrentConfig.HoldDurationSeconds,
			CurrentConfig.ReturnDurationSeconds);

		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Particle pattern form duration set to %.1fs."), FormSeconds),
			FColor::Cyan);
	}

	void ExecSetHold(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Hold <seconds>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Hold: no local MetaAgent player controller found."));
			return;
		}

		const FMetaAgentParticlePatternConfig CurrentConfig = Controller->GetParticlePatternConfig();
		const float HoldSeconds = FMath::Max(0.0f, FCString::Atof(*Args[0]));
		Controller->SetParticlePatternTimings(
			CurrentConfig.FormDurationSeconds,
			HoldSeconds,
			CurrentConfig.ReturnDurationSeconds);

		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Particle pattern hold duration set to %.1fs."), HoldSeconds),
			FColor::Cyan);
	}

	void ExecSetReturn(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Return <seconds>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Return: no local MetaAgent player controller found."));
			return;
		}

		const FMetaAgentParticlePatternConfig CurrentConfig = Controller->GetParticlePatternConfig();
		const float ReturnSeconds = FMath::Max(0.1f, FCString::Atof(*Args[0]));
		Controller->SetParticlePatternTimings(
			CurrentConfig.FormDurationSeconds,
			CurrentConfig.HoldDurationSeconds,
			ReturnSeconds);

		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Particle pattern return duration set to %.1fs."), ReturnSeconds),
			FColor::Cyan);
	}

	void ExecApplyPreset(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Preset Normal|Slow|Dramatic"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Preset: no local MetaAgent player controller found."));
			return;
		}

		const FString PresetName = Args[0].ToLower();
		EMetaAgentParticlePatternPreset Preset = EMetaAgentParticlePatternPreset::Normal;
		if (PresetName == TEXT("slow"))
		{
			Preset = EMetaAgentParticlePatternPreset::Slow;
		}
		else if (PresetName == TEXT("dramatic"))
		{
			Preset = EMetaAgentParticlePatternPreset::Dramatic;
		}
		else if (PresetName != TEXT("normal"))
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Preset: unknown preset '%s'."), *Args[0]);
			return;
		}

		Controller->ApplyParticlePatternPreset(Preset);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Particle pattern preset applied: %s."), *Controller->GetParticlePatternTimingsText()),
			FColor::Cyan);
	}

	void ExecStatus()
	{
		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Status: no local MetaAgent player controller found."));
			return;
		}

		UE_LOG(LogMetaAgent, Log, TEXT("%s"), *Controller->GetParticlePatternTimingsText());
		UE_LOG(LogMetaAgent, Log, TEXT("%s"), *Controller->GetParticlePatternStatusText());
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("%s | %s"), *Controller->GetParticlePatternTimingsText(), *Controller->GetParticlePatternStatusText()),
			FColor::Silver);
	}

	static FAutoConsoleCommand MetaAgentPatternFormCmd(
		TEXT("MetaAgent.Pattern.Form"),
		TEXT("Set particle pattern Forming duration in seconds."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetForm));

	static FAutoConsoleCommand MetaAgentPatternHoldCmd(
		TEXT("MetaAgent.Pattern.Hold"),
		TEXT("Set particle pattern Holding duration in seconds."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetHold));

	static FAutoConsoleCommand MetaAgentPatternReturnCmd(
		TEXT("MetaAgent.Pattern.Return"),
		TEXT("Set particle pattern Returning duration in seconds."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetReturn));

	static FAutoConsoleCommand MetaAgentPatternPresetCmd(
		TEXT("MetaAgent.Pattern.Preset"),
		TEXT("Apply particle pattern preset: Normal, Slow, or Dramatic."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecApplyPreset));

	static FAutoConsoleCommand MetaAgentPatternStatusCmd(
		TEXT("MetaAgent.Pattern.Status"),
		TEXT("Print active particle pattern timings and live state."),
		FConsoleCommandDelegate::CreateStatic(&ExecStatus));
}

void AMetaAgentPlayerController::HandleParticlePatternPressed()
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (!ParticleRuntime)
	{
		RefreshParticleRuntimeTracking();
	}

	if (!ParticleRuntime)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("ParticleRuntime: pattern request ignored because runtime is not initialized."));
		return;
	}

	SyncParticlePatternConfigToRuntime();
	ParticleRuntime->DiscoverNiagaraComponents(false);
	ParticleRuntime->ForceCaptureParticles();

	if (!ParticleRuntime->StartSquarePattern())
	{
		if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
		{
			MetaAgentHUD->AddTransientMessage(
				TEXT("Particle pattern unavailable (busy or no captured particles)."),
				FColor::Orange,
				2.5f);
		}
		return;
	}

	if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
	{
		MetaAgentHUD->AddTransientMessage(
			FString::Printf(TEXT("Particle square pattern started (%s)."), *ParticleRuntime->BuildPatternStatusText()),
			FColor::Cyan,
			3.0f);
	}
}

void AMetaAgentPlayerController::HandleParticlePatternSlowPresetPressed()
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	ApplyParticlePatternPreset(EMetaAgentParticlePatternPreset::Slow);

	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}

	if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
	{
		MetaAgentHUD->AddTransientMessage(
			FString::Printf(TEXT("Particle pattern preset: Slow (%s). Press V to play."), *GetParticlePatternTimingsText()),
			FColor::Cyan,
			2.5f);
	}
}

void AMetaAgentPlayerController::HandleParticlePatternDramaticPresetPressed()
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	ApplyParticlePatternPreset(EMetaAgentParticlePatternPreset::Dramatic);

	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}

	if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
	{
		MetaAgentHUD->AddTransientMessage(
			FString::Printf(TEXT("Particle pattern preset: Dramatic (%s). Press V to play."), *GetParticlePatternTimingsText()),
			FColor::Cyan,
			2.5f);
	}
}

bool AMetaAgentPlayerController::StartParticleSquarePattern()
{
	if (!ParticleRuntime)
	{
		return false;
	}

	SyncParticlePatternConfigToRuntime();
	ParticleRuntime->ForceCaptureParticles();
	return ParticleRuntime->StartSquarePattern();
}

FString AMetaAgentPlayerController::GetParticlePatternStatusText() const
{
	if (!ParticleRuntime)
	{
		return TEXT("Pattern State: Idle | Phase: 0.00 | Particles: 0");
	}

	return ParticleRuntime->BuildPatternStatusText();
}

FString AMetaAgentPlayerController::GetParticlePatternTimingsText() const
{
	if (!ParticleRuntime)
	{
		return FString::Printf(
			TEXT("Pattern Preset: %s | Form=%.1fs Hold=%.1fs Return=%.1fs"),
			*ParticlePatternConfig.GetPresetDisplayName(),
			ParticlePatternConfig.FormDurationSeconds,
			ParticlePatternConfig.HoldDurationSeconds,
			ParticlePatternConfig.ReturnDurationSeconds);
	}

	return ParticleRuntime->BuildPatternTimingsText();
}

void AMetaAgentPlayerController::ApplyParticlePatternPreset(const EMetaAgentParticlePatternPreset Preset)
{
	ParticlePatternConfig.ApplyPreset(Preset);
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::SetParticlePatternTimings(
	const float FormDurationSeconds,
	const float HoldDurationSeconds,
	const float ReturnDurationSeconds)
{
	ParticlePatternConfig.FormDurationSeconds = FMath::Max(0.1f, FormDurationSeconds);
	ParticlePatternConfig.HoldDurationSeconds = FMath::Max(0.0f, HoldDurationSeconds);
	ParticlePatternConfig.ReturnDurationSeconds = FMath::Max(0.1f, ReturnDurationSeconds);
	ParticlePatternConfig.ActivePreset = EMetaAgentParticlePatternPreset::Custom;
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::SyncParticlePatternConfigToRuntime()
{
	if (!ParticleRuntime)
	{
		return;
	}

	ParticleRuntime->ApplyPatternConfig(ParticlePatternConfig);
}
