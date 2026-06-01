// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "Systems/NetworkingRuntime/MetaAgentGameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformFileManager.h"
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
#include "UnrealClient.h"
#include "UObject/UnrealType.h"

void AMetaAgentPlayerController::HandleRenderRecordedTakePressed()
{
	SubmitLastRecordedTakeToRenderQueue();
}

void AMetaAgentPlayerController::HandleToggleRecordingPressed()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (Recording.bTakeRecordingActive)
	{
		StopAutopilotTakeRecording();
	}
	else
	{
		StartAutopilotTakeRecording();
	}
}

void AMetaAgentPlayerController::StartAutopilotTakeRecording()
{
	if (Recording.bTakeRecordingActive || !IsLocalPlayerController())
	{
		return;
	}

	Recording.bTakeRecordingActive = false;
	Recording.bRecordingUsesTakeRecorder = false;
	Recording.bRuntimeFrameCaptureActive = false;
	Recording.RuntimeCaptureAccumulatedSeconds = 0.0f;
	Recording.RuntimeCapturedFrameCount = 0;
	Recording.RuntimeCaptureFrameIndex = 0;
	Recording.RuntimeCaptureOutputDirectory = TEXT("");

	const FString SessionSuffix = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	Recording.RuntimeCaptureOutputDirectory = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Renders") / FString::Printf(TEXT("HiResFrames_%s"), *SessionSuffix));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.CreateDirectoryTree(*Recording.RuntimeCaptureOutputDirectory))
	{
		UE_LOG(LogMetaAgent, Error, TEXT("Recording: failed to create runtime capture directory '%s'."), *Recording.RuntimeCaptureOutputDirectory);
		return;
	}

	Recording.bTakeRecordingActive = true;
	Recording.bRecordingUsesTakeRecorder = false;
	Recording.bRuntimeFrameCaptureActive = true;
	Recording.RenderStatusText = FString::Printf(
		TEXT("HiRes frame capture active (%dx%d @ %0.0f FPS)"),
		Recording.RuntimeCaptureWidth,
		Recording.RuntimeCaptureHeight,
		Recording.RuntimeCaptureFps);
	Recording.RenderStatusColor = FColor::Cyan;

	UE_LOG(LogMetaAgent, Log,
		TEXT("RecordingRuntime: HiRes frame capture started. Output='%s' Res=%dx%d FPS=%0.0f"),
		*Recording.RuntimeCaptureOutputDirectory,
		Recording.RuntimeCaptureWidth,
		Recording.RuntimeCaptureHeight,
		Recording.RuntimeCaptureFps);
	UpdateRecordingStatusHud();
}

void AMetaAgentPlayerController::StopAutopilotTakeRecording()
{
	if (!Recording.bTakeRecordingActive || !IsLocalPlayerController())
	{
		return;
	}
	if (Recording.bRuntimeFrameCaptureActive)
	{
		Recording.bRuntimeFrameCaptureActive = false;
		Recording.RenderStatusText = FString::Printf(
			TEXT("HiRes frame capture stopped (%d frames)"),
			Recording.RuntimeCapturedFrameCount);
		Recording.RenderStatusColor = FColor::Silver;

		UE_LOG(LogMetaAgent, Log,
			TEXT("RecordingRuntime: HiRes frame capture stopped. Frames=%d Output='%s'"),
			Recording.RuntimeCapturedFrameCount,
			*Recording.RuntimeCaptureOutputDirectory);
	}

	Recording.bTakeRecordingActive = false;
	Recording.bRecordingUsesTakeRecorder = false;
	UE_LOG(LogMetaAgent, Log, TEXT("RecordingRuntime: stop requested."));
	UpdateRecordingStatusHud();
}

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

	if (Recording.RuntimeCapturedFrameCount > 0)
	{
		Recording.RenderStatusText = FString::Printf(
			TEXT("Frames already saved (%d)"),
			Recording.RuntimeCapturedFrameCount);
		Recording.RenderStatusColor = FColor::Green;
		UpdateRecordingStatusHud();

		UE_LOG(LogMetaAgent, Log,
			TEXT("RecordingRuntime: captured frames are already saved to '%s' (%d frames)."),
			*Recording.RuntimeCaptureOutputDirectory,
			Recording.RuntimeCapturedFrameCount);
		return;
	}

	UE_LOG(LogMetaAgent, Warning, TEXT("RecordingRuntime: no captured frames available yet. Press J to start/stop capture first."));
	return;
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
	const TCHAR* BackendLabel = TEXT("HiResFrameCapture");
	const TCHAR* RecordingState = Recording.bTakeRecordingActive ? TEXT("ON") : TEXT("OFF");
	if (Recording.bRenderInProgress)
	{
		GUI.RecordingStatusLine = FString::Printf(TEXT("Recording: %s (%s) | %s"), RecordingState, BackendLabel, *Recording.RenderStatusText);
	}
	else
	{
		GUI.RecordingStatusLine = FString::Printf(TEXT("Recording: %s (%s)"), RecordingState, BackendLabel);
	}
	ApplyGUIHelpPanelState();
}

void AMetaAgentPlayerController::UpdateRuntimeFrameCapture(float DeltaTime)
{
	if (!Recording.bTakeRecordingActive || !Recording.bRuntimeFrameCaptureActive)
	{
		return;
	}

	Recording.RuntimeCaptureAccumulatedSeconds += DeltaTime;
	const float CaptureIntervalSeconds = 1.0f / FMath::Max(1.0f, Recording.RuntimeCaptureFps);
	if (Recording.RuntimeCaptureAccumulatedSeconds < CaptureIntervalSeconds)
	{
		return;
	}

	Recording.RuntimeCaptureAccumulatedSeconds -= CaptureIntervalSeconds;

	const FString FrameFilename = Recording.RuntimeCaptureOutputDirectory /
		FString::Printf(TEXT("frame_%06d.png"), Recording.RuntimeCaptureFrameIndex++);
	const FString HighResShotCommand = FString::Printf(
		TEXT("HighResShot filename=\"%s\" %dx%d"),
		*FrameFilename,
		Recording.RuntimeCaptureWidth,
		Recording.RuntimeCaptureHeight);
	ConsoleCommand(HighResShotCommand, true);
	Recording.RuntimeCapturedFrameCount++;

	Recording.RenderStatusText = FString::Printf(TEXT("Frames captured: %d"), Recording.RuntimeCapturedFrameCount);
	Recording.RenderStatusColor = FColor::Cyan;
	UpdateRecordingStatusHud();
}

TArray<FString> AMetaAgentPlayerController::BuildRecordingRuntimePanelLines() const
{
	TArray<FString> Lines;
	Lines.Add(TEXT("Recording Runtime"));
	Lines.Add(FString::Printf(TEXT("State         : %s"), Recording.bTakeRecordingActive ? TEXT("ON") : TEXT("OFF")));
	Lines.Add(FString::Printf(TEXT("Backend       : %s"), Recording.bRecordingUsesTakeRecorder ? TEXT("TakeRecorder") : TEXT("FrameCapture")));
	Lines.Add(FString::Printf(TEXT("Resolution    : %dx%d"), Recording.RuntimeCaptureWidth, Recording.RuntimeCaptureHeight));
	Lines.Add(FString::Printf(TEXT("Frames        : %d"), Recording.RuntimeCapturedFrameCount));
	Lines.Add(FString::Printf(TEXT("Render        : %s"), *Recording.RenderStatusText));
	Lines.Add(FString::Printf(TEXT("Output Dir    : %s"), Recording.RuntimeCaptureOutputDirectory.IsEmpty() ? TEXT("Saved/Renders") : *Recording.RuntimeCaptureOutputDirectory));
	return Lines;
}

