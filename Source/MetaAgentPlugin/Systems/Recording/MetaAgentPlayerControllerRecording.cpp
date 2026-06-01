// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "Systems/Runtime/MetaAgentGameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "LevelSequence.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelinePrimaryConfig.h"
#include "MoviePipelineQueueEngineSubsystem.h"
#include "MoviePipelineSetting.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Recorder/TakeRecorderSubsystem.h"
#include "TakeRecorderSettings.h"
#endif

void AMetaAgentPlayerController::HandleRenderRecordedTakePressed()
{
	SubmitLastRecordedTakeToRenderQueue();
}

void AMetaAgentPlayerController::StartAutopilotTakeRecording()
{
	if (Recording.bTakeRecordingActive || !IsLocalPlayerController())
	{
		return;
	}

#if WITH_EDITOR
	if (!GEngine)
	{
		return;
	}

	UTakeRecorderSubsystem* TakeRecorderSubsystem = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>();
	if (!TakeRecorderSubsystem)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("TakeRecorder: subsystem unavailable; recording was not started."));
		return;
	}

	if (UTakeRecorderProjectSettings* ProjectSettings = GetMutableDefault<UTakeRecorderProjectSettings>())
	{
		Recording.bPreviousTakeRecordToPossessable = ProjectSettings->Settings.bRecordToPossessable;
		Recording.bForcedTakeRecordToSpawnable = true;
		ProjectSettings->Settings.bRecordToPossessable = false;
	}

	TakeRecorderSubsystem->TakeRecorderFinished.RemoveDynamic(this, &AMetaAgentPlayerController::HandleTakeRecorderFinished);
	TakeRecorderSubsystem->TakeRecorderFinished.AddDynamic(this, &AMetaAgentPlayerController::HandleTakeRecorderFinished);

	TakeRecorderSubsystem->SetTargetSequence();
	TakeRecorderSubsystem->ClearSources();

	AActor* ActorToRecord = nullptr;
	if (APawn* ControlledPawn = GetPawn())
	{
		ActorToRecord = ControlledPawn;
	}
	else if (Autopilot.Pawn.IsValid())
	{
		ActorToRecord = Autopilot.Pawn.Get();
	}

	if (ActorToRecord)
	{
		TakeRecorderSubsystem->AddSourceForActor(ActorToRecord, true, false);
	}
	else
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("TakeRecorder: no pawn actor found to record (player pawn is null and autopilot pawn invalid)."));
	}

	if (CinematicCamera.RuntimeCameraActor && IsValid(CinematicCamera.RuntimeCameraActor))
	{
		TakeRecorderSubsystem->AddSourceForActor(CinematicCamera.RuntimeCameraActor, true, false);
	}

	if (!TakeRecorderSubsystem->StartRecording(false, true))
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("TakeRecorder: failed to start recording for autopilot take."));
		RestoreTakeRecorderRecordToPossessableSetting();
		return;
	}

	Recording.bTakeRecordingActive = true;
	UE_LOG(LogMetaAgent, Log, TEXT("TakeRecorder: recording started for autopilot take."));
	UpdateRecordingStatusHud();

#else
	UE_LOG(LogMetaAgent, Warning, TEXT("TakeRecorder: unavailable in non-editor builds."));
#endif
}

void AMetaAgentPlayerController::StopAutopilotTakeRecording()
{
	if (!Recording.bTakeRecordingActive || !IsLocalPlayerController())
	{
		return;
	}

#if WITH_EDITOR
	if (!GEngine)
	{
		Recording.bTakeRecordingActive = false;
		return;
	}

	if (UTakeRecorderSubsystem* TakeRecorderSubsystem = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>())
	{
		if (TakeRecorderSubsystem->IsRecording())
		{
			TakeRecorderSubsystem->StopRecording();
		}
		else
		{
			RestoreTakeRecorderRecordToPossessableSetting();
		}
	}
	else
	{
		RestoreTakeRecorderRecordToPossessableSetting();
	}
#endif

	Recording.bTakeRecordingActive = false;
	UE_LOG(LogMetaAgent, Log, TEXT("TakeRecorder: recording stop requested."));
	UpdateRecordingStatusHud();
}

void AMetaAgentPlayerController::HandleTakeRecorderFinished(ULevelSequence* SequenceAsset)
{
#if WITH_EDITOR
	RestoreTakeRecorderRecordToPossessableSetting();
#endif

	Recording.bTakeRecordingActive = false;
	Recording.LastRecordedTake = SequenceAsset;
	UpdateRecordingStatusHud();

	UE_LOG(LogMetaAgent, Log, TEXT("TakeRecorder: finished. Sequence='%s'"), *GetNameSafe(SequenceAsset));

}

#if WITH_EDITOR
void AMetaAgentPlayerController::RestoreTakeRecorderRecordToPossessableSetting()
{
	if (!Recording.bForcedTakeRecordToSpawnable)
	{
		return;
	}

	if (UTakeRecorderProjectSettings* ProjectSettings = GetMutableDefault<UTakeRecorderProjectSettings>())
	{
		ProjectSettings->Settings.bRecordToPossessable = Recording.bPreviousTakeRecordToPossessable;
	}

	Recording.bForcedTakeRecordToSpawnable = false;
}
#endif

void AMetaAgentPlayerController::SubmitLastRecordedTakeToRenderQueue()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (Recording.bTakeRecordingActive)
	{
		StopAutopilotTakeRecording();
	}

	ULevelSequence* SequenceToRender = Recording.LastRecordedTake.Get();
	if (!SequenceToRender)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("MRQ: no recorded take available. Press J to record first."));
		return;
	}

	if (!GEngine)
	{
		return;
	}

	UMoviePipelineQueueEngineSubsystem* RenderSubsystem = GEngine->GetEngineSubsystem<UMoviePipelineQueueEngineSubsystem>();
	if (!RenderSubsystem)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("MRQ: runtime subsystem unavailable."));
		return;
	}

	if (RenderSubsystem->IsRendering())
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("MRQ: render already in progress."));
		Recording.bRenderInProgress = true;
		Recording.RenderStatusText = TEXT("Rendering: already in progress");
		Recording.RenderStatusColor = FColor::Yellow;
		UpdateRecordingStatusHud();
		return;
	}

	RenderSubsystem->OnRenderFinished.RemoveDynamic(this, &AMetaAgentPlayerController::HandleRuntimeRenderFinished);
	RenderSubsystem->OnRenderFinished.AddDynamic(this, &AMetaAgentPlayerController::HandleRuntimeRenderFinished);

	UMoviePipelineExecutorJob* RenderJob = RenderSubsystem->AllocateJob(SequenceToRender);
	if (!RenderJob)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("MRQ: failed to allocate render job."));
		return;
	}

	if (UWorld* CurrentWorld = GetWorld())
	{
		RenderJob->Map = FSoftObjectPath(CurrentWorld);
	}
	RenderJob->SetSequence(FSoftObjectPath(SequenceToRender));

	UMoviePipelinePrimaryConfig* RenderConfig = RenderJob->GetConfiguration();
	if (!RenderConfig)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("MRQ: render job has no configuration."));
		return;
	}

	UMoviePipelineOutputSetting* OutputSetting = Cast<UMoviePipelineOutputSetting>(
		RenderConfig->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
	if (!OutputSetting)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("MRQ: failed to add output setting."));
		return;
	}

	OutputSetting->OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("Renders");
	OutputSetting->FileNameFormat = TEXT("{sequence_name}_{date}_{time}_{frame_number}");
	OutputSetting->OutputResolution = Recording.bRenderAt4K ? FIntPoint(3840, 2160) : FIntPoint(2560, 1440);
	OutputSetting->bUseCustomFrameRate = true;
	OutputSetting->OutputFrameRate = FFrameRate(60, 1);

	if (UMovieScene* MovieScene = SequenceToRender->GetMovieScene())
	{
		const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
		if (DisplayRate.IsValid() && DisplayRate.Numerator > 0)
		{
			OutputSetting->OutputFrameRate = DisplayRate;
		}
	}

	if (UMoviePipelineGameOverrideSetting* GameOverrideSetting = Cast<UMoviePipelineGameOverrideSetting>(
		RenderConfig->FindOrAddSettingByClass(UMoviePipelineGameOverrideSetting::StaticClass())))
	{
		UClass* DesiredGameModeClass = nullptr;
		if (UWorld* CurrentWorld = GetWorld())
		{
			if (AGameModeBase* ActiveGameMode = CurrentWorld->GetAuthGameMode())
			{
				DesiredGameModeClass = ActiveGameMode->GetClass();
			}
			else if (AWorldSettings* WorldSettings = CurrentWorld->GetWorldSettings())
			{
				DesiredGameModeClass = WorldSettings->DefaultGameMode;
			}
		}

		if (DesiredGameModeClass)
		{
			GameOverrideSetting->SoftGameModeOverride = DesiredGameModeClass;
		}
		else
		{
			GameOverrideSetting->SoftGameModeOverride = nullptr;
		}
	}

	RenderConfig->FindOrAddSettingByClass(UMoviePipelineDeferredPassBase::StaticClass());

	UClass* Mp4OutputClass = FindObject<UClass>(nullptr, TEXT("/Script/MovieRenderPipelineMP4Encoder.MoviePipelineMP4EncoderOutput"));
	if (!Mp4OutputClass)
	{
		Mp4OutputClass = StaticLoadClass(UMoviePipelineSetting::StaticClass(), nullptr, TEXT("/Script/MovieRenderPipelineMP4Encoder.MoviePipelineMP4EncoderOutput"));
	}

	if (Mp4OutputClass)
	{
		if (UMoviePipelineSetting* Mp4Setting = RenderConfig->FindOrAddSettingByClass(Mp4OutputClass))
		{
			if (FByteProperty* RateControlProp = FindFProperty<FByteProperty>(Mp4Setting->GetClass(), TEXT("EncodingRateControl")))
			{
				RateControlProp->SetPropertyValue_InContainer(Mp4Setting, 1);
			}

			if (FIntProperty* ConstantRateFactorProp = FindFProperty<FIntProperty>(Mp4Setting->GetClass(), TEXT("ConstantRateFactor")))
			{
				ConstantRateFactorProp->SetPropertyValue_InContainer(Mp4Setting, 18);
			}

			if (FBoolProperty* IncludeAudioProp = FindFProperty<FBoolProperty>(Mp4Setting->GetClass(), TEXT("bIncludeAudio")))
			{
				IncludeAudioProp->SetPropertyValue_InContainer(Mp4Setting, true);
			}
		}
	}
	else
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("MRQ: MP4 output class unavailable. Ensure MovieRenderPipeline plugin is enabled."));
	}

	if (Recording.bRenderPngSequenceAlongsideMp4)
	{
		if (UMoviePipelineImageSequenceOutput_PNG* PngSetting = Cast<UMoviePipelineImageSequenceOutput_PNG>(
			RenderConfig->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_PNG::StaticClass())))
		{
			PngSetting->bWriteAlpha = false;
		}
	}

	RenderSubsystem->SetConfiguration(nullptr, false);
	RenderSubsystem->RenderJob(RenderJob);
	Recording.bRenderInProgress = true;
	Recording.RenderStatusText = TEXT("Rendering: starting...");
	Recording.RenderStatusColor = FColor::Yellow;
	UpdateRecordingStatusHud();

	UE_LOG(LogMetaAgent, Log,
		TEXT("MRQ: started render for '%s' at %s (%s + %s)."),
		*GetNameSafe(SequenceToRender),
		*OutputSetting->OutputResolution.ToString(),
		TEXT("H.264 MP4"),
		Recording.bRenderPngSequenceAlongsideMp4 ? TEXT("PNG") : TEXT("no PNG"));
	UE_LOG(LogMetaAgent, Log, TEXT("MRQ: output directory is '%s'"), *OutputSetting->OutputDirectory.Path);

}

void AMetaAgentPlayerController::HandleRuntimeRenderFinished(FMoviePipelineOutputData Results)
{
	Recording.bRenderInProgress = false;
	Recording.RenderStatusText = Results.bSuccess ? TEXT("Rendering: complete") : TEXT("Rendering: failed");
	Recording.RenderStatusColor = Results.bSuccess ? FColor::Green : FColor::Red;
	UpdateRecordingStatusHud();

	UE_LOG(LogMetaAgent, Log,
		TEXT("MRQ: render finished success=%s shots=%d"),
		Results.bSuccess ? TEXT("true") : TEXT("false"),
		Results.ShotData.Num());

}

void AMetaAgentPlayerController::UpdateRecordingStatusHud()
{
	GUI.RecordingStatusLine = FString::Printf(TEXT("Recording: %s"), Recording.bTakeRecordingActive ? TEXT("ON") : TEXT("OFF"));
	ApplyGUIHelpPanelState();
}

