// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"

#include "Core/MetaAgent.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/ParticleRuntime/MetaAgentImagePreviewRuntime.h"
#include "Systems/ParticleRuntime/MetaAgentParticleGameplayTags.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternAsset.h"
#include "Systems/ParticleRuntime/MetaAgentParticleEffectTypes.h"
#include "Systems/ParticleRuntime/MetaAgentParticleInputRouter.h"
#include "Systems/ParticleRuntime/MetaAgentParticleOrchestrator.h"
#include "Systems/ParticleRuntime/MetaAgentParticleRuntime.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeBuilder.h"

namespace MetaAgentParticlePatternConsole
{
	bool AreParticlePatternTagsBlocked(
		const FGameplayTagContainer& BlockedTags,
		const FGameplayTagContainer& PatternTags)
	{
		if (BlockedTags.IsEmpty())
		{
			return false;
		}

		if (BlockedTags.HasTag(MetaAgentParticleTags::Pattern_Blocked))
		{
			return true;
		}

		return !PatternTags.IsEmpty() && BlockedTags.HasAny(PatternTags);
	}

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
		UE_LOG(LogMetaAgent, Log, TEXT("%s"), *Controller->GetParticlePatternShapeText());
		UE_LOG(LogMetaAgent, Log, TEXT("%s"), *Controller->GetParticlePatternStatusText());
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(
				TEXT("%s | %s | %s"),
				*Controller->GetParticlePatternTimingsText(),
				*Controller->GetParticlePatternShapeText(),
				*Controller->GetParticlePatternStatusText()),
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
		TEXT("Print active particle pattern timings, shape, and live state."),
		FConsoleCommandDelegate::CreateStatic(&ExecStatus));

	void ExecSetShape(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Shape SquareGrid|ImageSilhouette|SplinePath|MeshSilhouette"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const FString ShapeName = Args[0].ToLower();
		if (ShapeName == TEXT("imagesilhouette") || ShapeName == TEXT("image"))
		{
			Controller->SetParticlePatternShape(EMetaAgentParticlePatternShape::ImageSilhouette);
		}
		else if (ShapeName == TEXT("squaregrid") || ShapeName == TEXT("square") || ShapeName == TEXT("grid"))
		{
			Controller->SetParticlePatternShape(EMetaAgentParticlePatternShape::SquareGrid);
		}
		else if (ShapeName == TEXT("splinepath") || ShapeName == TEXT("spline"))
		{
			Controller->SetParticlePatternShape(EMetaAgentParticlePatternShape::SplinePath);
		}
		else if (ShapeName == TEXT("meshsilhouette") || ShapeName == TEXT("mesh"))
		{
			Controller->SetParticlePatternShape(EMetaAgentParticlePatternShape::MeshSilhouette);
		}
		else
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Shape: unknown shape '%s'."), *Args[0]);
			return;
		}

		ShowTransientPatternMessage(Controller, Controller->GetParticlePatternShapeText(), FColor::Cyan);
	}

	void ExecSetImageThreshold(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.ImageThreshold <0-1>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const float Threshold = FMath::Clamp(FCString::Atof(*Args[0]), 0.0f, 1.0f);
		Controller->SetParticlePatternImageThreshold(Threshold);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Image threshold set to %.2f."), Threshold),
			FColor::Cyan);
	}

	void ExecSetShapeWidth(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.ShapeWidth <cm>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const float WidthCm = FMath::Max(10.0f, FCString::Atof(*Args[0]));
		Controller->SetParticlePatternShapeWidth(WidthCm);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Shape width set to %.0f cm."), WidthCm),
			FColor::Cyan);
	}

	static FAutoConsoleCommand MetaAgentPatternShapeCmd(
		TEXT("MetaAgent.Pattern.Shape"),
		TEXT("Set particle pattern shape: SquareGrid, ImageSilhouette, SplinePath, or MeshSilhouette."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetShape));

	static FAutoConsoleCommand MetaAgentPatternImageThresholdCmd(
		TEXT("MetaAgent.Pattern.ImageThreshold"),
		TEXT("Set image silhouette alpha/luminance threshold (0-1)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetImageThreshold));

	static FAutoConsoleCommand MetaAgentPatternShapeWidthCmd(
		TEXT("MetaAgent.Pattern.ShapeWidth"),
		TEXT("Set image shape width in centimeters when not aligned to preview plane."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetShapeWidth));

	void ExecSetImageSampling(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.ImageSampling Gray|Sobel"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const FString ModeName = Args[0].ToLower();
		if (ModeName == TEXT("sobel") || ModeName == TEXT("edges") || ModeName == TEXT("sobeledges"))
		{
			Controller->SetParticlePatternImageSamplingMode(EMetaAgentParticleImageSamplingMode::SobelEdges);
		}
		else if (ModeName == TEXT("gray") || ModeName == TEXT("grey") || ModeName == TEXT("density")
			|| ModeName == TEXT("grayscale") || ModeName == TEXT("grayscaledensity"))
		{
			Controller->SetParticlePatternImageSamplingMode(EMetaAgentParticleImageSamplingMode::GrayscaleDensity);
		}
		else
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.ImageSampling: unknown mode '%s'."), *Args[0]);
			return;
		}

		ShowTransientPatternMessage(Controller, Controller->GetParticlePatternShapeText(), FColor::Cyan);
	}

	void ExecSetScatterGrid(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.ScatterGrid <1-16>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const float GridScale = FMath::Clamp(FCString::Atof(*Args[0]), 1.0f, 16.0f);
		Controller->SetParticlePatternDensityGridScale(GridScale);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Scatter grid scale set to %.1f. Press F then V to rebuild."), GridScale),
			FColor::Cyan);
	}

	void ExecSetForming(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("Usage: MetaAgent.Pattern.Forming Lerp|Arc|Spiral|Cycle|<0-2>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const FString ModeName = Args[0].ToLower();
		EMetaAgentParticleFormingMode NewMode = EMetaAgentParticleFormingMode::DirectLerp;

		if (ModeName == TEXT("lerp") || ModeName == TEXT("direct") || ModeName == TEXT("directlerp") || ModeName == TEXT("0"))
		{
			NewMode = EMetaAgentParticleFormingMode::DirectLerp;
		}
		else if (ModeName == TEXT("arc") || ModeName == TEXT("arclift") || ModeName == TEXT("lift") || ModeName == TEXT("1"))
		{
			NewMode = EMetaAgentParticleFormingMode::ArcLift;
		}
		else if (ModeName == TEXT("spiral") || ModeName == TEXT("spiralin") || ModeName == TEXT("2"))
		{
			NewMode = EMetaAgentParticleFormingMode::SpiralIn;
		}
		else if (ModeName == TEXT("cycle") || ModeName == TEXT("next"))
		{
			Controller->CycleParticlePatternFormingMode();
			ShowTransientPatternMessage(Controller, Controller->GetParticlePatternTimingsText(), FColor::Cyan);
			return;
		}
		else
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Forming: unknown mode '%s'."), *Args[0]);
			return;
		}

		Controller->SetParticlePatternFormingMode(NewMode);
		ShowTransientPatternMessage(Controller, Controller->GetParticlePatternTimingsText(), FColor::Cyan);
	}

	void ExecSetScatterJitter(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.ScatterJitter <0-1>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const float Jitter = FMath::Clamp(FCString::Atof(*Args[0]), 0.0f, 1.0f);
		Controller->SetParticlePatternTargetJitter(Jitter);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Scatter jitter set to %.2f. Press F then V to rebuild."), Jitter),
			FColor::Cyan);
	}

	void ExecSetEdgeThreshold(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.EdgeThreshold <0-1>"));
			return;
		}

		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const float Threshold = FMath::Clamp(FCString::Atof(*Args[0]), 0.01f, 1.0f);
		Controller->SetParticlePatternEdgeThreshold(Threshold);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Edge threshold set to %.3f."), Threshold),
			FColor::Cyan);
	}

	void ExecCancel(const TArray<FString>& Args)
	{
		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		const bool bSkipReturn = Args.Num() > 0 && Args[0].Equals(TEXT("SkipReturn"), ESearchCase::IgnoreCase);
		if (Controller->RequestParticlePatternCancel(bSkipReturn))
		{
			ShowTransientPatternMessage(Controller, TEXT("Particle pattern cancelled."), FColor::Orange);
		}
	}

	void ExecSkipHold()
	{
		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		if (Controller->RequestParticleSkipHold())
		{
			ShowTransientPatternMessage(Controller, TEXT("Particle pattern skip hold."), FColor::Cyan);
		}
	}

	void ExecReady(const TArray<FString>& Args)
	{
		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			return;
		}

		FString ImagePath;
		if (Args.Num() > 0)
		{
			ImagePath = Args[0];
		}
		else
		{
			ImagePath = Controller->GetLastLoadedPreviewImagePath();
		}

		const bool bReady = Controller->IsParticlePatternReady(ImagePath);
		ShowTransientPatternMessage(
			Controller,
			FString::Printf(TEXT("Pattern mask ready: %s"), bReady ? TEXT("TRUE") : TEXT("FALSE")),
			bReady ? FColor::Green : FColor::Orange);
	}

	static FAutoConsoleCommand MetaAgentPatternCancelCmd(
		TEXT("MetaAgent.Pattern.Cancel"),
		TEXT("Cancel active particle pattern. Optional arg: SkipReturn"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecCancel));

	static FAutoConsoleCommand MetaAgentPatternSkipHoldCmd(
		TEXT("MetaAgent.Pattern.SkipHold"),
		TEXT("Skip Holding and begin Returning immediately."),
		FConsoleCommandDelegate::CreateStatic(&ExecSkipHold));

	static FAutoConsoleCommand MetaAgentPatternReadyCmd(
		TEXT("MetaAgent.Pattern.Ready"),
		TEXT("Check whether image mask is cached. Optional: image path."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecReady));

	static FAutoConsoleCommand MetaAgentPatternImageSamplingCmd(
		TEXT("MetaAgent.Pattern.ImageSampling"),
		TEXT("Set image sampling: Gray (default) or Sobel (edges)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetImageSampling));

	static FAutoConsoleCommand MetaAgentPatternEdgeThresholdCmd(
		TEXT("MetaAgent.Pattern.EdgeThreshold"),
		TEXT("Set Sobel edge magnitude threshold (0.01-1). Lower = denser outlines."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetEdgeThreshold));

	static FAutoConsoleCommand MetaAgentPatternScatterGridCmd(
		TEXT("MetaAgent.Pattern.ScatterGrid"),
		TEXT("Stratification grid scale (1-16). Higher = particles spread across more of the image."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetScatterGrid));

	static FAutoConsoleCommand MetaAgentPatternScatterJitterCmd(
		TEXT("MetaAgent.Pattern.ScatterJitter"),
		TEXT("Per-particle scatter jitter within grid cells (0-1). Higher = more random offset."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetScatterJitter));

	static FAutoConsoleCommand MetaAgentPatternFormingCmd(
		TEXT("MetaAgent.Pattern.Forming"),
		TEXT("Set forming mode: Lerp, Arc, Spiral, or Cycle."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetForming));
}

namespace MetaAgentParticleControllerInternal
{
	void NotifyEffectResult(AMetaAgentPlayerController* Controller, const FMetaAgentParticleEffectResult& Result)
	{
		if (!Controller)
		{
			return;
		}

		if (AMetaAgentHUD* HUD = Controller->GetHUD<AMetaAgentHUD>())
		{
			const FColor Color = Result.bSuccess
				? (Result.bAwaitingAsyncPrepare ? FColor::Yellow : FColor::Cyan)
				: FColor::Orange;
			HUD->AddTransientMessage(Result.UserMessage.ToString(), Color, Result.bAwaitingAsyncPrepare ? 4.0f : 3.0f);
		}
	}

	void TriggerEffectOnController(AMetaAgentPlayerController* Controller, const FName EffectId)
	{
		if (!Controller
			|| !IsMetaAgentRuntimeActive()
			|| !Controller->IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
		{
			return;
		}

		NotifyEffectResult(Controller, Controller->TriggerParticleEffect(EffectId));
	}
}

void AMetaAgentPlayerController::EnsureParticleOrchestrator()
{
	if (ParticleOrchestrator)
	{
		return;
	}

	const TSubclassOf<UMetaAgentParticleOrchestrator> OrchestratorClass =
		ParticleOrchestratorClass
			? ParticleOrchestratorClass
			: TSubclassOf<UMetaAgentParticleOrchestrator>(UMetaAgentDefaultParticleOrchestrator::StaticClass());
	ParticleOrchestrator = NewObject<UMetaAgentParticleOrchestrator>(this, OrchestratorClass, TEXT("MetaAgentParticleOrchestrator"));
	SyncOrchestratorFromControllerDefaults();
	ParticleOrchestrator->InitializeOrchestrator(this, GetWorld());
	BindParticleRuntimeDelegates();
}

void AMetaAgentPlayerController::SyncOrchestratorFromControllerDefaults()
{
	if (!ParticleOrchestrator)
	{
		return;
	}

	ParticleOrchestrator->ApplyPatternConfig(ParticlePatternConfig);
	ParticleOrchestrator->SetActuationMode(ParticleActuationMode);
	ParticleOrchestrator->SetBlockedPatternTags(BlockedPatternTags);
	ParticleOrchestrator->SetDefaultPatternAsset(DefaultParticlePatternAsset);
}

UMetaAgentParticleRuntime* AMetaAgentPlayerController::GetParticleRuntime() const
{
	return ParticleOrchestrator ? ParticleOrchestrator->GetParticleRuntime() : nullptr;
}

FMetaAgentParticleEffectResult AMetaAgentPlayerController::TriggerParticleEffect(const FName EffectId)
{
	EnsureParticleOrchestrator();
	if (!ParticleOrchestrator)
	{
		return FMetaAgentParticleEffectResult();
	}

	const FMetaAgentParticleEffectResult Result = ParticleOrchestrator->TriggerEffect(EffectId);
	if (Result.bSuccess)
	{
		ParticlePatternConfig = ParticleOrchestrator->GetPatternConfig();
	}

	return Result;
}

void AMetaAgentPlayerController::HandleParticleLoadPreviewPressed()
{
	if (!IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
	{
		return;
	}

	EnsureParticleOrchestrator();
	if (!ParticleOrchestrator)
	{
		return;
	}

	FString Message;
	if (ParticleOrchestrator->LoadDefaultPreviewPng(Message))
	{
		if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
		{
			HUD->AddTransientMessage(Message, FColor::Green, 3.0f);
		}
		ApplyGUIHelpPanelState();
	}
	else if (AMetaAgentHUD* HUD = GetHUD<AMetaAgentHUD>())
	{
		HUD->AddTransientMessage(Message, FColor::Yellow, 2.5f);
	}
}

void AMetaAgentPlayerController::HandleParticleStepPatternBackwardPressed()
{
	if (!IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
	{
		return;
	}

	MetaAgentParticleControllerInternal::TriggerEffectOnController(
		this,
		MetaAgentParticleEffectIds::PatternStepBackward);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleStepPatternForwardPressed()
{
	if (!IsModularRuntimeEnabled(EMetaAgentModularRuntime::Particle))
	{
		return;
	}

	MetaAgentParticleControllerInternal::TriggerEffectOnController(
		this,
		MetaAgentParticleEffectIds::PatternStepForward);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleSlowPresetPressed()
{
	MetaAgentParticleControllerInternal::TriggerEffectOnController(this, MetaAgentParticleEffectIds::PresetSlow);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleDramaticPresetPressed()
{
	MetaAgentParticleControllerInternal::TriggerEffectOnController(this, MetaAgentParticleEffectIds::PresetDramatic);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleCycleSamplingPressed()
{
	MetaAgentParticleControllerInternal::TriggerEffectOnController(this, MetaAgentParticleEffectIds::CycleSampling);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

void AMetaAgentPlayerController::HandleParticleCycleFormingPressed()
{
	MetaAgentParticleControllerInternal::TriggerEffectOnController(this, MetaAgentParticleEffectIds::CycleForming);
	if (GUI.bHelpPanelVisible)
	{
		ApplyGUIHelpPanelState();
	}
}

bool AMetaAgentPlayerController::StartParticleSquarePattern()
{
	return StartParticlePattern();
}

bool AMetaAgentPlayerController::StartParticlePattern()
{
	return TriggerParticleEffect(MetaAgentParticleEffectIds::ImageReveal).bSuccess;
}

bool AMetaAgentPlayerController::RequestParticlePatternStart(UMetaAgentParticlePatternAsset* PatternAsset)
{
	EnsureParticleOrchestrator();
	if (!ParticleOrchestrator || !PatternAsset)
	{
		return false;
	}

	return ParticleOrchestrator->StartPatternWithAsset(PatternAsset).bSuccess;
}

bool AMetaAgentPlayerController::RequestParticlePatternCancel(const bool bSkipReturn)
{
	EnsureParticleOrchestrator();
	return ParticleOrchestrator ? ParticleOrchestrator->RequestPatternCancel(bSkipReturn) : false;
}

bool AMetaAgentPlayerController::RequestParticleSkipHold()
{
	EnsureParticleOrchestrator();
	return ParticleOrchestrator ? ParticleOrchestrator->RequestSkipHold() : false;
}

bool AMetaAgentPlayerController::RequestParticlePatternQueue(UMetaAgentParticlePatternAsset* PatternAsset)
{
	EnsureParticleOrchestrator();
	return ParticleOrchestrator ? ParticleOrchestrator->RequestPatternQueue(PatternAsset) : false;
}

bool AMetaAgentPlayerController::CanStartParticlePattern() const
{
	return GetParticleRuntime() ? GetParticleRuntime()->CanStartPattern() : false;
}

bool AMetaAgentPlayerController::IsParticlePatternReady(const FString& ImagePath) const
{
	return GetParticleRuntime() ? GetParticleRuntime()->IsPatternReady(ImagePath) : false;
}

int32 AMetaAgentPlayerController::GetParticlePatternQueueDepth() const
{
	return GetParticleRuntime() ? GetParticleRuntime()->GetPatternQueueDepth() : 0;
}

void AMetaAgentPlayerController::BindParticleRuntimeDelegates()
{
	UMetaAgentParticleRuntime* Runtime = GetParticleRuntime();
	if (!Runtime)
	{
		return;
	}

	Runtime->OnPatternStateEntered.RemoveAll(this);
	Runtime->OnPatternCompleted.RemoveAll(this);
	Runtime->OnPatternStateEntered.AddDynamic(this, &AMetaAgentPlayerController::HandleParticlePatternStateEntered);
	Runtime->OnPatternCompleted.AddDynamic(this, &AMetaAgentPlayerController::HandleParticlePatternCompleted);
}

void AMetaAgentPlayerController::HandleParticlePatternStateEntered(
	const EMetaAgentParticlePatternState NewState,
	const EMetaAgentParticlePatternState PreviousState)
{
	OnParticlePatternStateEntered.Broadcast(NewState, PreviousState);
}

void AMetaAgentPlayerController::HandleParticlePatternCompleted()
{
	OnParticlePatternCompleted.Broadcast();
}

FString AMetaAgentPlayerController::GetParticlePatternStatusText() const
{
	if (ParticleOrchestrator)
	{
		return ParticleOrchestrator->BuildPatternStatusText();
	}

	return TEXT("Pattern State: Idle | Phase: 0.00 | Particles: 0");
}

FString AMetaAgentPlayerController::GetParticlePatternTimingsText() const
{
	if (ParticleOrchestrator)
	{
		return ParticleOrchestrator->BuildPatternTimingsText();
	}

	return FString::Printf(
		TEXT("Pattern Preset: %s | Form=%.1fs Hold=%.1fs Return=%.1fs | Forming=%s"),
		*ParticlePatternConfig.GetPresetDisplayName(),
		ParticlePatternConfig.FormDurationSeconds,
		ParticlePatternConfig.HoldDurationSeconds,
		ParticlePatternConfig.ReturnDurationSeconds,
		*ParticlePatternConfig.Forming.GetModeDisplayName());
}

TArray<FString> AMetaAgentPlayerController::BuildParticleRuntimePanelStatusLines() const
{
	TArray<FString> Lines;
	const FMetaAgentParticlePatternConfig& Config = ParticlePatternConfig;

	Lines.Add(FString::Printf(
		TEXT("Callback=%s | Capture=%s | Particles=%d"),
		HasReceivedParticleCallback() ? TEXT("yes") : TEXT("no"),
		IsParticleCaptureActive() ? TEXT("yes") : TEXT("no"),
		GetCapturedParticleCount()));

	if (const UMetaAgentParticleRuntime* Runtime = GetParticleRuntime())
	{
		Lines.Add(FString::Printf(
			TEXT("State=%s | Phase=%.2f | Queue=%d"),
			*Runtime->GetPatternStateDisplayName(),
			Runtime->GetPatternPhase(),
			GetParticlePatternQueueDepth()));
	}
	else
	{
		Lines.Add(TEXT("State=unavailable | Phase=0.00 | Queue=0"));
	}

	Lines.Add(FString::Printf(
		TEXT("Preset=%s | Form=%.1fs | Hold=%.1fs | Return=%.1fs"),
		*Config.GetPresetDisplayName(),
		Config.FormDurationSeconds,
		Config.HoldDurationSeconds,
		Config.ReturnDurationSeconds));

	const bool bImageLoaded = ParticleOrchestrator && ParticleOrchestrator->GetPreviewTexture() != nullptr;
	Lines.Add(FString::Printf(
		TEXT("Shape=%s | Sampling=%s | Forming=%s | Image=%s"),
		*Config.Shape.GetShapeDisplayName(),
		*Config.Shape.GetImageSamplingDisplayName(),
		*Config.Forming.GetModeDisplayName(),
		bImageLoaded ? TEXT("loaded") : TEXT("none")));

	Lines.Add(FString::Printf(
		TEXT("Res=%dpx | Grid=%.1f | Jitter=%.2f"),
		Config.Shape.SampleResolution,
		Config.Shape.DensityGridScale,
		Config.Shape.TargetJitterNormalized));

	return Lines;
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
	SyncOrchestratorFromControllerDefaults();
}

bool AMetaAgentPlayerController::PrepareParticlePatternShapeContext()
{
	EnsureParticleOrchestrator();
	return ParticleOrchestrator ? ParticleOrchestrator->PrepareShapeContextForPlay() : false;
}

void AMetaAgentPlayerController::RequestParticleImageMaskBuild()
{
	PrepareParticlePatternShapeContext();
}

void AMetaAgentPlayerController::SetParticlePatternShape(const EMetaAgentParticlePatternShape ShapeType)
{
	ParticlePatternConfig.Shape.ShapeType = ShapeType;
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::SetParticlePatternImageThreshold(const float Threshold)
{
	ParticlePatternConfig.Shape.AlphaThreshold = FMath::Clamp(Threshold, 0.0f, 1.0f);
	SyncParticlePatternConfigToRuntime();
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
}

void AMetaAgentPlayerController::SetParticlePatternImageSamplingMode(
	const EMetaAgentParticleImageSamplingMode SamplingMode)
{
	ParticlePatternConfig.Shape.ImageSamplingMode =
		FMetaAgentParticleShapeDefinition::SanitizeImageSamplingMode(SamplingMode);
	SyncParticlePatternConfigToRuntime();
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
}

void AMetaAgentPlayerController::SetParticlePatternEdgeThreshold(const float Threshold)
{
	ParticlePatternConfig.Shape.EdgeThreshold = FMath::Clamp(Threshold, 0.01f, 1.0f);
	SyncParticlePatternConfigToRuntime();
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
}

void AMetaAgentPlayerController::SetParticlePatternShapeWidth(const float WidthCm)
{
	ParticlePatternConfig.Shape.ShapeWidthCm = FMath::Max(10.0f, WidthCm);
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::SetParticlePatternDensityGridScale(const float GridScale)
{
	ParticlePatternConfig.Shape.DensityGridScale = FMath::Clamp(GridScale, 1.0f, 16.0f);
	SyncParticlePatternConfigToRuntime();
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
}

void AMetaAgentPlayerController::SetParticlePatternTargetJitter(const float JitterNormalized)
{
	ParticlePatternConfig.Shape.TargetJitterNormalized = FMath::Clamp(JitterNormalized, 0.0f, 1.0f);
	SyncParticlePatternConfigToRuntime();
	FMetaAgentParticleShapeBuilder::InvalidateImageMaskCache();
}

void AMetaAgentPlayerController::SetParticlePatternFormingMode(const EMetaAgentParticleFormingMode FormingMode)
{
	ParticlePatternConfig.Forming.Mode = FMetaAgentParticleFormingSettings::SanitizeMode(FormingMode);
	SyncParticlePatternConfigToRuntime();
}

void AMetaAgentPlayerController::CycleParticlePatternFormingMode()
{
	ParticlePatternConfig.Forming.CycleMode();
	SyncParticlePatternConfigToRuntime();
}

FString AMetaAgentPlayerController::GetParticlePatternShapeText() const
{
	return ParticleOrchestrator
		? ParticleOrchestrator->BuildPatternShapeText()
		: FString::Printf(
			TEXT("Pattern Shape: %s | Sampling=%s | ImageLoaded=FALSE"),
			*ParticlePatternConfig.Shape.GetShapeDisplayName(),
			*ParticlePatternConfig.Shape.GetImageSamplingDisplayName());
}

FMetaAgentParticleShapeContext AMetaAgentPlayerController::BuildParticleShapeContext()
{
	EnsureParticleOrchestrator();
	PrepareParticlePatternShapeContext();
	return FMetaAgentParticleShapeContext();
}

bool AMetaAgentPlayerController::EnsureParticlePreviewTextureLoaded(FString& OutResolvedPath)
{
	return FMetaAgentImagePreviewRuntime::EnsurePreviewTextureLoaded(*this, OutResolvedPath);
}

UTexture2D* AMetaAgentPlayerController::GetLatestPngPreviewTexture() const
{
	return ParticleOrchestrator ? ParticleOrchestrator->GetPreviewTexture() : nullptr;
}

void AMetaAgentPlayerController::SetLatestPngPreviewTexture(UTexture2D* Texture)
{
	EnsureParticleOrchestrator();
	if (ParticleOrchestrator)
	{
		ParticleOrchestrator->SetPreviewSource(Texture, GetLastLoadedPreviewImagePath());
	}
}

FString AMetaAgentPlayerController::GetLastLoadedPreviewImagePath() const
{
	return ParticleOrchestrator ? ParticleOrchestrator->GetPreviewImagePath() : FString();
}

void AMetaAgentPlayerController::SetLastLoadedPreviewImagePath(const FString& Path)
{
	EnsureParticleOrchestrator();
	if (ParticleOrchestrator)
	{
		ParticleOrchestrator->SetPreviewSource(GetLatestPngPreviewTexture(), Path);
	}
}

UStaticMeshComponent* AMetaAgentPlayerController::GetExistingPreviewPlaneMesh() const
{
	return ParticleOrchestrator ? ParticleOrchestrator->GetCachedPreviewPlaneMesh() : nullptr;
}

void AMetaAgentPlayerController::CacheExistingPreviewPlaneMesh(UStaticMeshComponent* Mesh)
{
	EnsureParticleOrchestrator();
	if (ParticleOrchestrator)
	{
		ParticleOrchestrator->SetCachedPreviewPlaneMesh(Mesh);
	}
}
