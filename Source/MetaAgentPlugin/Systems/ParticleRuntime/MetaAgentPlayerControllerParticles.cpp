// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"

#include "Core/MetaAgent.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/ParticleRuntime/MetaAgentImagePreviewRuntime.h"
#include "Systems/ParticleRuntime/MetaAgentParticleGameplayTags.h"
#include "Systems/ParticleRuntime/MetaAgentParticlePatternAsset.h"
#include "Systems/ParticleRuntime/MetaAgentParticleRuntime.h"
#include "Systems/ParticleRuntime/MetaAgentParticleShapeBuilder.h"

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
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Preset Normal|Slow|Dramatic|Sculpt"));
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
		else if (PresetName == TEXT("sculpt"))
		{
			Preset = EMetaAgentParticlePatternPreset::Sculpt;
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
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.Shape SquareGrid|ImageSilhouette|RandomParallelepiped|Box"));
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
		else if (ShapeName == TEXT("randomparallelepiped") || ShapeName == TEXT("randombox") || ShapeName == TEXT("box"))
		{
			Controller->SetParticlePatternShape(EMetaAgentParticlePatternShape::RandomParallelepiped);
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
		TEXT("Set particle pattern shape: SquareGrid or ImageSilhouette."),
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
			UE_LOG(LogMetaAgent, Warning, TEXT("Usage: MetaAgent.Pattern.ImageSampling Gray|Fill|Sobel"));
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
		else if (ModeName == TEXT("fill") || ModeName == TEXT("filled") || ModeName == TEXT("filledsilhouette"))
		{
			Controller->SetParticlePatternImageSamplingMode(EMetaAgentParticleImageSamplingMode::FilledSilhouette);
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

	void ExecRandomBox()
	{
		AMetaAgentPlayerController* Controller = ResolveLocalMetaAgentController();
		if (!Controller)
		{
			UE_LOG(LogMetaAgent, Warning, TEXT("MetaAgent.Pattern.Box: no local MetaAgent player controller found."));
			return;
		}

		if (Controller->PlayRandomBoxParticlePattern())
		{
			ShowTransientPatternMessage(
				Controller,
				FString::Printf(
					TEXT("Random box sculpt started (%s | %s)."),
					*Controller->GetParticlePatternShapeText(),
					*Controller->GetParticlePatternStatusText()),
				FColor::Green);
		}
		else
		{
			ShowTransientPatternMessage(
				Controller,
				TEXT("Random box pattern unavailable (busy or no captured particles)."),
				FColor::Orange);
		}
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

	static FAutoConsoleCommand MetaAgentPatternBoxCmd(
		TEXT("MetaAgent.Pattern.Box"),
		TEXT("Play random 3D box sculpt pattern (same as C key)."),
		FConsoleCommandDelegate::CreateStatic(&ExecRandomBox));

	static FAutoConsoleCommand MetaAgentPatternImageSamplingCmd(
		TEXT("MetaAgent.Pattern.ImageSampling"),
		TEXT("Set image sampling: Gray (default), Fill, or Sobel (edges)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetImageSampling));

	static FAutoConsoleCommand MetaAgentPatternEdgeThresholdCmd(
		TEXT("MetaAgent.Pattern.EdgeThreshold"),
		TEXT("Set Sobel edge magnitude threshold (0.01-1). Lower = denser outlines."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecSetEdgeThreshold));
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
	ParticleRuntime->ForceCaptureParticles();
	PrepareParticlePatternShapeContext();

	if (!StartParticlePattern())
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
		const bool bPreparing = ParticleRuntime->GetPatternState() == EMetaAgentParticlePatternState::Preparing;
		MetaAgentHUD->AddTransientMessage(
			bPreparing
				? FString::Printf(
					TEXT("Preparing image shape at full resolution (%s). Game stays responsive."),
					*ParticleRuntime->BuildPatternStatusText())
				: FString::Printf(
					TEXT("Particle pattern started (%s | %s)."),
					*ParticleRuntime->BuildPatternShapeText(),
					*ParticleRuntime->BuildPatternStatusText()),
			bPreparing ? FColor::Yellow : FColor::Cyan,
			bPreparing ? 4.0f : 3.0f);
	}
}

void AMetaAgentPlayerController::HandleRandomBoxPatternPressed()
{
	if (!IsMetaAgentRuntimeActive())
	{
		return;
	}

	if (PlayRandomBoxParticlePattern())
	{
		if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
		{
			MetaAgentHUD->AddTransientMessage(
				FString::Printf(
					TEXT("Random box sculpt started (%s | %s)."),
					*GetParticlePatternShapeText(),
					*GetParticlePatternStatusText()),
				FColor::Green,
				3.0f);
		}
	}
	else if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
	{
		MetaAgentHUD->AddTransientMessage(
			TEXT("Random box pattern unavailable (busy or no captured particles)."),
			FColor::Orange,
			2.5f);
	}
}

bool AMetaAgentPlayerController::PlayRandomBoxParticlePattern()
{
	if (!ParticleRuntime)
	{
		RefreshParticleRuntimeTracking();
	}

	if (!ParticleRuntime)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("ParticleRuntime: random box request ignored because runtime is not initialized."));
		return false;
	}

	ApplyParticlePatternPreset(EMetaAgentParticlePatternPreset::Sculpt);
	SetParticlePatternShape(EMetaAgentParticlePatternShape::RandomParallelepiped);
	ParticlePatternConfig.Shape.AssignmentMode = EMetaAgentParticleShapeAssignmentMode::NearestNeighbor;
	ParticlePatternConfig.Shape.bOrientShapeToView = true;
	ParticlePatternConfig.Shape.BoxRandomSeed = 0;
	SyncParticlePatternConfigToRuntime();

	ParticleRuntime->ForceCaptureParticles();
	PrepareParticlePatternShapeContext();
	return StartParticlePattern();
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
	return StartParticlePattern();
}

bool AMetaAgentPlayerController::StartParticlePattern()
{
	if (BlockedPatternTags.HasTag(MetaAgentParticleTags::Pattern_Blocked))
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("ParticleRuntime: pattern start blocked by BlockedPatternTags."));
		return false;
	}

	if (!ParticleRuntime)
	{
		RefreshParticleRuntimeTracking();
	}

	if (!ParticleRuntime)
	{
		return false;
	}

	SyncParticlePatternConfigToRuntime();
	ParticleRuntime->ForceCaptureParticles();
	PrepareParticlePatternShapeContext();

	if (DefaultParticlePatternAsset)
	{
		return ParticleRuntime->RequestPatternStart(DefaultParticlePatternAsset);
	}

	return ParticleRuntime->StartPattern();
}

bool AMetaAgentPlayerController::RequestParticlePatternStart(UMetaAgentParticlePatternAsset* PatternAsset)
{
	if (!ParticleRuntime)
	{
		return false;
	}

	SyncParticlePatternConfigToRuntime();
	ParticleRuntime->ForceCaptureParticles();
	PrepareParticlePatternShapeContext();
	return ParticleRuntime->RequestPatternStart(PatternAsset);
}

bool AMetaAgentPlayerController::RequestParticlePatternCancel(const bool bSkipReturn)
{
	return ParticleRuntime ? ParticleRuntime->RequestPatternCancel(bSkipReturn) : false;
}

bool AMetaAgentPlayerController::RequestParticleSkipHold()
{
	return ParticleRuntime ? ParticleRuntime->RequestSkipHold() : false;
}

bool AMetaAgentPlayerController::RequestParticlePatternQueue(UMetaAgentParticlePatternAsset* PatternAsset)
{
	return ParticleRuntime ? ParticleRuntime->RequestPatternQueue(PatternAsset) : false;
}

bool AMetaAgentPlayerController::CanStartParticlePattern() const
{
	return ParticleRuntime ? ParticleRuntime->CanStartPattern() : false;
}

bool AMetaAgentPlayerController::IsParticlePatternReady(const FString& ImagePath) const
{
	return ParticleRuntime ? ParticleRuntime->IsPatternReady(ImagePath) : false;
}

int32 AMetaAgentPlayerController::GetParticlePatternQueueDepth() const
{
	return ParticleRuntime ? ParticleRuntime->GetPatternQueueDepth() : 0;
}

void AMetaAgentPlayerController::BindParticleRuntimeDelegates()
{
	if (!ParticleRuntime)
	{
		return;
	}

	ParticleRuntime->OnPatternStateEntered.RemoveAll(this);
	ParticleRuntime->OnPatternCompleted.RemoveAll(this);
	ParticleRuntime->OnPatternStateEntered.AddDynamic(this, &AMetaAgentPlayerController::HandleParticlePatternStateEntered);
	ParticleRuntime->OnPatternCompleted.AddDynamic(this, &AMetaAgentPlayerController::HandleParticlePatternCompleted);
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
	ParticleRuntime->SetActuationMode(ParticleActuationMode);
}

bool AMetaAgentPlayerController::PrepareParticlePatternShapeContext()
{
	if (!ParticleRuntime)
	{
		return false;
	}

	if (ParticlePatternConfig.Shape.ShapeType == EMetaAgentParticlePatternShape::ImageSilhouette)
	{
		FString ResolvedPath;
		if (!EnsureParticlePreviewTextureLoaded(ResolvedPath))
		{
			UE_LOG(LogMetaAgent, Warning,
				TEXT("ParticleRuntime: image silhouette requested but no preview texture is available. Falling back to square grid if needed."));
		}
	}

	const FMetaAgentParticleShapeContext ShapeContext = BuildParticleShapeContext();
	ParticleRuntime->SetPatternShapeContext(ShapeContext);
	RequestParticleImageMaskBuild();
	return true;
}

void AMetaAgentPlayerController::RequestParticleImageMaskBuild()
{
	if (!ParticleRuntime
		|| ParticlePatternConfig.Shape.ShapeType != EMetaAgentParticlePatternShape::ImageSilhouette)
	{
		return;
	}

	const FString SourceImagePath = GetLastLoadedPreviewImagePath();
	if (SourceImagePath.IsEmpty())
	{
		return;
	}

	const int32 ParticleCount = FMath::Max(ParticleRuntime->GetKnownParticleCount(), 128);
	FMetaAgentParticleShapeBuilder::RequestImageMaskBuild(
		SourceImagePath,
		ParticlePatternConfig.Shape,
		ParticleCount);
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
	ParticlePatternConfig.Shape.ImageSamplingMode = SamplingMode;
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

FString AMetaAgentPlayerController::GetParticlePatternShapeText() const
{
	const bool bImageLoaded = GetLatestPngPreviewTexture() != nullptr;

	if (ParticleRuntime && ParticleRuntime->IsPatternActive())
	{
		return ParticleRuntime->BuildPatternShapeText();
	}

	return FString::Printf(
		TEXT("Pattern Shape: %s | Sampling=%s | ImageLoaded=%s"),
		*ParticlePatternConfig.Shape.GetShapeDisplayName(),
		*ParticlePatternConfig.Shape.GetImageSamplingDisplayName(),
		bImageLoaded ? TEXT("TRUE") : TEXT("FALSE"));
}

FMetaAgentParticleShapeContext AMetaAgentPlayerController::BuildParticleShapeContext()
{
	FMetaAgentParticleShapeContext Context;

	if (ParticleRuntime)
	{
		Context.BaselineWorldPositions = ParticleRuntime->GetKnownParticlePositions();
	}

	if (UWorld* World = GetWorld())
	{
		Context.PreviewPlaneMesh = FMetaAgentImagePreviewRuntime::FindPreviewPlaneMesh(
			World,
			ExistingPreviewPlaneActorName,
			ExistingPreviewPlaneComponentName,
			GetExistingPreviewPlaneMesh());
		if (Context.PreviewPlaneMesh)
		{
			CacheExistingPreviewPlaneMesh(Context.PreviewPlaneMesh);
		}
	}

	Context.SourceTexture = GetLatestPngPreviewTexture();
	Context.SourceImagePath = GetLastLoadedPreviewImagePath();
	Context.bHasResolvedImage = Context.SourceTexture != nullptr;

	if (APlayerCameraManager* CameraManager = PlayerCameraManager)
	{
		Context.ViewOrigin = CameraManager->GetCameraLocation();
		Context.bHasViewOrigin = true;
	}

	if (UWorld* World = GetWorld())
	{
		Context.World = World;
	}

	return Context;
}

bool AMetaAgentPlayerController::EnsureParticlePreviewTextureLoaded(FString& OutResolvedPath)
{
	if (!ParticlePatternConfig.Shape.bUseLoadedPreviewTexture
		&& ParticlePatternConfig.Shape.ShapeType != EMetaAgentParticlePatternShape::ImageSilhouette)
	{
		OutResolvedPath = GetLastLoadedPreviewImagePath();
		return GetLatestPngPreviewTexture() != nullptr;
	}

	return FMetaAgentImagePreviewRuntime::EnsurePreviewTextureLoaded(*this, OutResolvedPath);
}

void AMetaAgentPlayerController::CacheExistingPreviewPlaneMesh(UStaticMeshComponent* Mesh)
{
	ExistingPreviewPlaneMesh = Mesh;
}
