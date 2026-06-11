// Based on Unreal Engine template code.
// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"
#include "Core/MetaAgent.h"
#include "MetaAgentPluginSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "MovieSceneCapture.h"
#include "MovieSceneCaptureModule.h"
#include "Protocols/AudioCaptureProtocol.h"
#include "Protocols/VideoCaptureProtocol.h"
#include "Slate/SceneViewport.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#endif

namespace MetaAgentRecording
{
	static const FName SceneViewportTypeName(TEXT("SceneViewport"));

	static TSharedPtr<FSceneViewport> ToSharedSceneViewport(FSceneViewport* SceneViewport)
	{
		if (!SceneViewport)
		{
			return nullptr;
		}

		if (const UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
		{
			if (GameEngine->SceneViewport.IsValid() && GameEngine->SceneViewport.Get() == SceneViewport)
			{
				return GameEngine->SceneViewport;
			}
		}

		// Viewport lifetime is owned by the active game/PIE session.
		return TSharedPtr<FSceneViewport>(SceneViewport, [](FSceneViewport*) {});
	}

	static FSceneViewport* ResolveRawSceneViewport(const AMetaAgentPlayerController& Controller)
	{
		if (const UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
		{
			if (GameEngine->SceneViewport.IsValid())
			{
				return GameEngine->SceneViewport.Get();
			}
		}

		if (const ULocalPlayer* LocalPlayer = Controller.GetLocalPlayer())
		{
			if (UGameViewportClient* ViewportClient = LocalPlayer->ViewportClient)
			{
				if (FSceneViewport* SceneViewport = ViewportClient->GetGameViewport())
				{
					return SceneViewport;
				}
			}
		}

		if (const UWorld* World = Controller.GetWorld())
		{
			if (UGameViewportClient* ViewportClient = World->GetGameViewport())
			{
				if (FSceneViewport* SceneViewport = ViewportClient->GetGameViewport())
				{
					return SceneViewport;
				}
			}
		}

		if (GEngine && GEngine->GameViewport)
		{
			if (FSceneViewport* SceneViewport = GEngine->GameViewport->GetGameViewport())
			{
				return SceneViewport;
			}
		}

#if WITH_EDITOR
		if (UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine))
		{
			if (FViewport* PieViewport = EditorEngine->GetPIEViewport())
			{
				if (PieViewport->GetViewportType() == SceneViewportTypeName)
				{
					return static_cast<FSceneViewport*>(PieViewport);
				}
			}
		}
#endif

		return nullptr;
	}

	static TSharedPtr<FSceneViewport> ResolveLocalSceneViewport(const AMetaAgentPlayerController& Controller)
	{
		return ToSharedSceneViewport(ResolveRawSceneViewport(Controller));
	}
}

void AMetaAgentPlayerController::HandleReportRecordingStatusPressed()
{
	if (!IsModularRuntimeEnabled(EMetaAgentModularRuntime::Recording))
	{
		return;
	}

	ReportRuntimeCaptureStatus();
}

void AMetaAgentPlayerController::HandleToggleRecordingPressed()
{
	if (!IsLocalPlayerController() || !IsModularRuntimeEnabled(EMetaAgentModularRuntime::Recording))
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

	const UMetaAgentPluginSettings* PluginSettings = GetDefault<UMetaAgentPluginSettings>();
	if (!PluginSettings || !PluginSettings->bEnableRecordingSystems)
	{
		UE_LOG(LogMetaAgent, Warning, TEXT("RecordingRuntime: recording systems are disabled in plugin settings."));
		return;
	}

	const TSharedPtr<FSceneViewport> SceneViewport = MetaAgentRecording::ResolveLocalSceneViewport(*this);
	if (!SceneViewport.IsValid())
	{
		UE_LOG(LogMetaAgent, Error,
			TEXT("RecordingRuntime: no scene viewport available for capture (world='%s' localPlayer=%s gameViewport=%s)."),
			*GetNameSafe(GetWorld()),
			GetLocalPlayer() ? TEXT("yes") : TEXT("no"),
			(GEngine && GEngine->GameViewport) ? TEXT("yes") : TEXT("no"));
		return;
	}

	Recording.ActiveMovieSceneCapture = nullptr;
	Recording.RuntimeCapturedFrameCount = 0;
	Recording.ActiveCaptureWidth = 0;
	Recording.ActiveCaptureHeight = 0;
	Recording.RuntimeCaptureOutputDirectory = TEXT("");

	const FString SessionSuffix = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	Recording.RuntimeCaptureOutputDirectory = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Renders") / FString::Printf(TEXT("Capture_%s"), *SessionSuffix));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.CreateDirectoryTree(*Recording.RuntimeCaptureOutputDirectory))
	{
		UE_LOG(LogMetaAgent, Error, TEXT("RecordingRuntime: failed to create capture directory '%s'."), *Recording.RuntimeCaptureOutputDirectory);
		return;
	}

	if (Recording.CaptureWidth > 0 && Recording.CaptureHeight > 0)
	{
		SceneViewport->SetViewportSize(Recording.CaptureWidth, Recording.CaptureHeight);
	}

	FIntPoint CaptureResolution = SceneViewport->GetSize();
	if (CaptureResolution == FIntPoint::ZeroValue)
	{
		CaptureResolution = FIntPoint(GSystemResolution.ResX, GSystemResolution.ResY);
	}

	UMovieSceneCapture* Capture = NewObject<UMovieSceneCapture>(GetTransientPackage());
	if (!Capture)
	{
		UE_LOG(LogMetaAgent, Error, TEXT("RecordingRuntime: failed to create Movie Scene Capture object."));
		return;
	}

	const int32 CaptureFps = FMath::Clamp(FMath::RoundToInt(Recording.CaptureFps), 1, 120);
	Capture->Settings.OutputDirectory.Path = Recording.RuntimeCaptureOutputDirectory;
	Capture->Settings.OutputFormat = TEXT("Capture_{world}_{time}");
	Capture->Settings.bOverwriteExisting = true;
	Capture->Settings.bUseCustomFrameRate = true;
	Capture->Settings.CustomFrameRate = FFrameRate(CaptureFps, 1);
	Capture->Settings.Resolution.ResX = CaptureResolution.X;
	Capture->Settings.Resolution.ResY = CaptureResolution.Y;
	Capture->Settings.bCinematicMode = false;
	Capture->Settings.bCinematicEngineScalability = false;
	Capture->Settings.bShowHUD = false;
	Capture->Settings.bEnableTextureStreaming = true;
	Capture->Settings.MovieExtension = TEXT(".avi");
	Capture->SetImageCaptureProtocolType(UVideoCaptureProtocol::StaticClass());
	Capture->SetAudioCaptureProtocolType(UNullAudioCaptureProtocol::StaticClass());

	Capture->Initialize(SceneViewport);

	if (UVideoCaptureProtocol* VideoProtocol = Cast<UVideoCaptureProtocol>(Capture->GetImageCaptureProtocol()))
	{
		VideoProtocol->bUseCompression = Recording.bUseVideoCompression;
		VideoProtocol->CompressionQuality = Recording.VideoCompressionQuality;
	}

	Capture->StartCapture();

	Recording.ActiveMovieSceneCapture = Capture;
	Recording.bTakeRecordingActive = true;
	Recording.ActiveCaptureWidth = CaptureResolution.X;
	Recording.ActiveCaptureHeight = CaptureResolution.Y;
	Recording.RenderStatusText = FString::Printf(
		TEXT("Video capture active (%dx%d @ %d FPS)"),
		Recording.ActiveCaptureWidth,
		Recording.ActiveCaptureHeight,
		CaptureFps);
	Recording.RenderStatusColor = FColor::Cyan;

	UE_LOG(LogMetaAgent, Log,
		TEXT("RecordingRuntime: video capture started. Output='%s' Res=%dx%d FPS=%d Compression=%s Quality=%0.0f"),
		*Recording.RuntimeCaptureOutputDirectory,
		Recording.ActiveCaptureWidth,
		Recording.ActiveCaptureHeight,
		CaptureFps,
		Recording.bUseVideoCompression ? TEXT("On") : TEXT("Off"),
		Recording.VideoCompressionQuality);
	UpdateRecordingStatusHud();
}

void AMetaAgentPlayerController::StopAutopilotTakeRecording()
{
	if (!Recording.bTakeRecordingActive || !IsLocalPlayerController())
	{
		return;
	}

	if (Recording.ActiveMovieSceneCapture)
	{
		Recording.RuntimeCapturedFrameCount = Recording.ActiveMovieSceneCapture->GetMetrics().Frame;
		IMovieSceneCaptureModule::Get().DestroyMovieSceneCapture(Recording.ActiveMovieSceneCapture->GetHandle());
		Recording.ActiveMovieSceneCapture = nullptr;
	}

	Recording.bTakeRecordingActive = false;
	Recording.RenderStatusText = FString::Printf(
		TEXT("Video capture stopped (%d frames)"),
		Recording.RuntimeCapturedFrameCount);
	Recording.RenderStatusColor = FColor::Silver;

	UE_LOG(LogMetaAgent, Log,
		TEXT("RecordingRuntime: video capture stopped. Frames=%d Output='%s'"),
		Recording.RuntimeCapturedFrameCount,
		*Recording.RuntimeCaptureOutputDirectory);
	UpdateRecordingStatusHud();
}

void AMetaAgentPlayerController::ReportRuntimeCaptureStatus()
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
			TEXT("Video saved (%d frames)"),
			Recording.RuntimeCapturedFrameCount);
		Recording.RenderStatusColor = FColor::Green;
		UpdateRecordingStatusHud();

		UE_LOG(LogMetaAgent, Log,
			TEXT("RecordingRuntime: video saved to '%s' (%d frames)."),
			*Recording.RuntimeCaptureOutputDirectory,
			Recording.RuntimeCapturedFrameCount);
		return;
	}

	UE_LOG(LogMetaAgent, Warning, TEXT("RecordingRuntime: no captured video available yet. Press J to start/stop capture first."));
}

void AMetaAgentPlayerController::UpdateRecordingCaptureStatus()
{
	if (!Recording.bTakeRecordingActive || !Recording.ActiveMovieSceneCapture)
	{
		return;
	}

	const FCachedMetrics& Metrics = Recording.ActiveMovieSceneCapture->GetMetrics();
	if (Metrics.Frame == Recording.RuntimeCapturedFrameCount)
	{
		return;
	}

	Recording.RuntimeCapturedFrameCount = Metrics.Frame;
	Recording.RenderStatusText = FString::Printf(TEXT("Recording video: %d frames"), Recording.RuntimeCapturedFrameCount);
	Recording.RenderStatusColor = FColor::Cyan;
	UpdateRecordingStatusHud();
}

void AMetaAgentPlayerController::UpdateRecordingStatusHud()
{
	const TCHAR* RecordingState = Recording.bTakeRecordingActive ? TEXT("ON") : TEXT("OFF");
	GUI.RecordingStatusLine = FString::Printf(TEXT("Recording: %s (Video/AVI) | %s"), RecordingState, *Recording.RenderStatusText);
	ApplyGUIHelpPanelState();
}

TArray<FString> AMetaAgentPlayerController::BuildRecordingRuntimePanelLines() const
{
	TArray<FString> Lines;
	Lines.Add(TEXT("Recording Runtime"));
	Lines.Add(FString::Printf(TEXT("State         : %s"), Recording.bTakeRecordingActive ? TEXT("ON") : TEXT("OFF")));
	Lines.Add(TEXT("Backend       : MovieSceneCapture (AVI video)"));
	if (Recording.ActiveCaptureWidth > 0 && Recording.ActiveCaptureHeight > 0)
	{
		Lines.Add(FString::Printf(TEXT("Resolution    : %dx%d"), Recording.ActiveCaptureWidth, Recording.ActiveCaptureHeight));
	}
	else if (Recording.CaptureWidth > 0 && Recording.CaptureHeight > 0)
	{
		Lines.Add(FString::Printf(TEXT("Resolution    : %dx%d (override)"), Recording.CaptureWidth, Recording.CaptureHeight));
	}
	else
	{
		Lines.Add(TEXT("Resolution    : Viewport"));
	}
	Lines.Add(FString::Printf(TEXT("FPS           : %0.0f"), Recording.CaptureFps));
	Lines.Add(FString::Printf(
		TEXT("Compression   : %s (%0.0f)"),
		Recording.bUseVideoCompression ? TEXT("On") : TEXT("Off"),
		Recording.VideoCompressionQuality));
	Lines.Add(FString::Printf(TEXT("Frames        : %d"), Recording.RuntimeCapturedFrameCount));
	Lines.Add(FString::Printf(TEXT("Render        : %s"), *Recording.RenderStatusText));
	Lines.Add(FString::Printf(
		TEXT("Output Dir    : %s"),
		Recording.RuntimeCaptureOutputDirectory.IsEmpty() ? TEXT("Saved/Renders") : *Recording.RuntimeCaptureOutputDirectory));
	return Lines;
}
