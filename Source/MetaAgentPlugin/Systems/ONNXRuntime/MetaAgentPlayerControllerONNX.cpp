// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Gameplay/Controllers/MetaAgentPlayerController.h"

#include "Core/MetaAgent.h"
#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/ONNXRuntime/MetaAgentONNXRuntime.h"

namespace
{
	const FName ONNXStatusKey = TEXT("ONNXRuntime");

	const TCHAR* ToRuntimeStateLabel(const EMetaAgentONNXRuntimeState RuntimeState)
	{
		switch (RuntimeState)
		{
		case EMetaAgentONNXRuntimeState::Ready:
			return TEXT("Ready");
		case EMetaAgentONNXRuntimeState::Loading:
			return TEXT("Loading");
		case EMetaAgentONNXRuntimeState::Loaded:
			return TEXT("Loaded");
		case EMetaAgentONNXRuntimeState::Generating:
			return TEXT("Generating");
		case EMetaAgentONNXRuntimeState::Error:
			return TEXT("Error");
		default:
			return TEXT("Unknown");
		}
	}
}

void AMetaAgentPlayerController::HandleLoadONNXPipelinePressed()
{
	if (!IsMetaAgentRuntimeActive() || !IsLocalPlayerController())
	{
		return;
	}

	FMetaAgentONNXRuntime::RunLoadPipelineSequence(*this, ONNX);
	UpdateONNXStatusHud();
	ApplyGUIHelpPanelState();
}

void AMetaAgentPlayerController::HandleGenerateONNXImagePressed()
{
	if (!IsMetaAgentRuntimeActive() || !IsLocalPlayerController())
	{
		return;
	}

	FMetaAgentONNXRuntime::RunGenerateImageSequence(*this, ONNX);
	UpdateONNXStatusHud();
	ApplyGUIHelpPanelState();
}

void AMetaAgentPlayerController::UpdateONNXStatusHud()
{
	if (AMetaAgentHUD* MetaAgentHUD = GetHUD<AMetaAgentHUD>())
	{
		const FColor StatusColor = (ONNX.RuntimeState == EMetaAgentONNXRuntimeState::Error)
			? FColor::Red
			: ((ONNX.RuntimeState == EMetaAgentONNXRuntimeState::Generating) ? FColor::Cyan : FColor::Green);

		const FString RuntimeText = FString::Printf(
			TEXT("ONNX: %s | %s"),
			ToRuntimeStateLabel(ONNX.RuntimeState),
			*ONNX.LastStatus);

		MetaAgentHUD->SetStatusLine(ONNXStatusKey, RuntimeText, StatusColor);
	}
}

TArray<FString> AMetaAgentPlayerController::BuildONNXRuntimePanelLines() const
{
	TArray<FString> Lines;
	Lines.Add(TEXT("ONNX Runtime"));
	Lines.Add(FString::Printf(TEXT("State         : %s"), ToRuntimeStateLabel(ONNX.RuntimeState)));
	Lines.Add(FString::Printf(TEXT("Runtime       : %s"), ONNX.PreferredRuntimeName.IsEmpty() ? TEXT("Auto") : *ONNX.PreferredRuntimeName));
	Lines.Add(FString::Printf(TEXT("Model Root    : %s"), ONNX.ModelRootPath.IsEmpty() ? TEXT("(unset)") : *ONNX.ModelRootPath));
	Lines.Add(FString::Printf(TEXT("Loaded Model  : %s"), ONNX.LastLoadedModelPath.IsEmpty() ? TEXT("(none)") : *ONNX.LastLoadedModelPath));
	Lines.Add(FString::Printf(TEXT("Prompt        : %s"), ONNX.Prompt.IsEmpty() ? TEXT("(empty)") : *ONNX.Prompt));
	Lines.Add(FString::Printf(TEXT("Negative      : %s"), ONNX.NegativePrompt.IsEmpty() ? TEXT("(empty)") : *ONNX.NegativePrompt));
	Lines.Add(FString::Printf(TEXT("Resolution    : %dx%d"), ONNX.TargetWidth, ONNX.TargetHeight));
	Lines.Add(FString::Printf(TEXT("Steps / CFG   : %d / %.1f"), ONNX.StepCount, ONNX.CFGScale));
	Lines.Add(FString::Printf(TEXT("Seed          : %d"), ONNX.Seed));
	Lines.Add(FString::Printf(TEXT("Output        : %s"), ONNX.LastOutputImagePath.IsEmpty() ? TEXT("(none)") : *ONNX.LastOutputImagePath));
	Lines.Add(FString::Printf(TEXT("Status        : %s"), ONNX.LastStatus.IsEmpty() ? TEXT("idle") : *ONNX.LastStatus));
	Lines.Add(TEXT("Keys          : K=Load model path, P=Generate image"));
	return Lines;
}

void AMetaAgentPlayerController::SetONNXModelRootPath(const FString& InModelRootPath)
{
	ONNX.ModelRootPath = InModelRootPath;
	ONNX.RuntimeState = EMetaAgentONNXRuntimeState::Ready;
	ONNX.LastStatus = TEXT("ONNX model path updated");
	UpdateONNXStatusHud();
	ApplyGUIHelpPanelState();

	UE_LOG(LogMetaAgent, Log, TEXT("ONNXRuntime: model root path set to '%s'."), *ONNX.ModelRootPath);
}
