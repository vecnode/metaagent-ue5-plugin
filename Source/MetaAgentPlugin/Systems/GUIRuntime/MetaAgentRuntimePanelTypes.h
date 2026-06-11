// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "MetaAgentRuntimePanelTypes.generated.h"

UENUM(BlueprintType)
enum class EMetaAgentModularRuntime : uint8
{
	GUI,
	Camera,
	AI,
	Recording,
	Networking,
	Particle,
	CharacterInput
};

namespace MetaAgentRuntimeIds
{
	static const FName GUI(TEXT("GUI"));
	static const FName Camera(TEXT("Camera"));
	static const FName AI(TEXT("AI"));
	static const FName Recording(TEXT("Recording"));
	static const FName Networking(TEXT("Networking"));
	static const FName Particle(TEXT("Particle"));
	static const FName CharacterInput(TEXT("CharacterInput"));

	static const FName ToggleRuntime(TEXT("ToggleRuntime"));
	static const FName ToggleHelpPanel(TEXT("ToggleHelpPanel"));
	static const FName QuitApplication(TEXT("QuitApplication"));
	static const FName ToggleCinematicCamera(TEXT("ToggleCinematicCamera"));
	static const FName ToggleAutopilot(TEXT("ToggleAutopilot"));
	static const FName ToggleRecording(TEXT("ToggleRecording"));
	static const FName ReportRecording(TEXT("ReportRecording"));
	static const FName StartAudio(TEXT("StartAudio"));
	static const FName StartImage(TEXT("StartImage"));
	static const FName ParticleLoadPreview(TEXT("ParticleLoadPreview"));
	static const FName ParticlePlayFullCycle(TEXT("ParticlePlayFullCycle"));
	static const FName ParticleStepBackward(TEXT("ParticleStepBackward"));
	static const FName ParticleStepForward(TEXT("ParticleStepForward"));
	static const FName ParticleSlowPreset(TEXT("ParticleSlowPreset"));
	static const FName ParticleDramaticPreset(TEXT("ParticleDramaticPreset"));
	static const FName ParticleCycleSampling(TEXT("ParticleCycleSampling"));
	static const FName ParticleCycleForming(TEXT("ParticleCycleForming"));
	static const FName ParticleCycleReturning(TEXT("ParticleCycleReturning"));
}

USTRUCT()
struct FMetaAgentGUIPreviewThumbnail
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UTexture2D> Texture = nullptr;

	UPROPERTY()
	FString Label;
};

USTRUCT()
struct FMetaAgentGUIActionRow
{
	GENERATED_BODY()

	UPROPERTY()
	FString KeyLabel;

	UPROPERTY()
	FString Description;

	UPROPERTY()
	FName ActionId = NAME_None;
};

USTRUCT()
struct FMetaAgentGUIRuntimeSection
{
	GENERATED_BODY()

	UPROPERTY()
	FName RuntimeId = NAME_None;

	UPROPERTY()
	FString Title;

	UPROPERTY()
	bool bRuntimeAlwaysOn = false;

	UPROPERTY()
	bool bRuntimeEnabled = true;

	UPROPERTY()
	TArray<FMetaAgentGUIActionRow> ActionRows;

	UPROPERTY()
	TArray<FString> StatusLines;

	UPROPERTY()
	TArray<FMetaAgentGUIPreviewThumbnail> PreviewThumbnails;

	UPROPERTY()
	bool bSectionExpanded = true;
};

struct FMetaAgentHUDClickRegion
{
	FName ActionId = NAME_None;
	float X = 0.0f;
	float Y = 0.0f;
	float W = 0.0f;
	float H = 0.0f;

	bool Contains(float MouseX, float MouseY) const
	{
		return MouseX >= X && MouseX <= (X + W) && MouseY >= Y && MouseY <= (Y + H);
	}
};

inline FName MakeSectionExpandActionId(const FName RuntimeId)
{
	return FName(*FString::Printf(TEXT("SectionExpand_%s"), *RuntimeId.ToString()));
}

inline bool ParseSectionExpandAction(const FName ActionId, FName& OutRuntimeId)
{
	static const FString Prefix = TEXT("SectionExpand_");
	const FString ActionString = ActionId.ToString();
	if (!ActionString.StartsWith(Prefix))
	{
		return false;
	}

	OutRuntimeId = FName(*ActionString.Mid(Prefix.Len()));
	return !OutRuntimeId.IsNone();
}

inline bool ParseRuntimeToggleAction(const FName ActionId, FName& OutRuntimeId)
{
	static const FString Prefix = TEXT("ToggleRuntime_");
	const FString ActionString = ActionId.ToString();
	if (!ActionString.StartsWith(Prefix))
	{
		return false;
	}

	OutRuntimeId = FName(*ActionString.Mid(Prefix.Len()));
	return !OutRuntimeId.IsNone();
}

inline bool TryMapRuntimeIdToModularRuntime(const FName RuntimeId, EMetaAgentModularRuntime& OutRuntime)
{
	if (RuntimeId == MetaAgentRuntimeIds::Camera)
	{
		OutRuntime = EMetaAgentModularRuntime::Camera;
		return true;
	}
	if (RuntimeId == MetaAgentRuntimeIds::AI)
	{
		OutRuntime = EMetaAgentModularRuntime::AI;
		return true;
	}
	if (RuntimeId == MetaAgentRuntimeIds::Recording)
	{
		OutRuntime = EMetaAgentModularRuntime::Recording;
		return true;
	}
	if (RuntimeId == MetaAgentRuntimeIds::Networking)
	{
		OutRuntime = EMetaAgentModularRuntime::Networking;
		return true;
	}
	if (RuntimeId == MetaAgentRuntimeIds::Particle)
	{
		OutRuntime = EMetaAgentModularRuntime::Particle;
		return true;
	}
	if (RuntimeId == MetaAgentRuntimeIds::CharacterInput)
	{
		OutRuntime = EMetaAgentModularRuntime::CharacterInput;
		return true;
	}

	return false;
}
