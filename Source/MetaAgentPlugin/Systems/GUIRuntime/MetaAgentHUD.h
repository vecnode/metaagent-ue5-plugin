// Copyright (c) vecnode 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Systems/GUIRuntime/MetaAgentRuntimePanelTypes.h"
#include "MetaAgentHUD.generated.h"

USTRUCT()
struct FMetaAgentHUDMessage
{
	GENERATED_BODY()

	UPROPERTY()
	FString Text;

	UPROPERTY()
	FColor Color = FColor::White;

	UPROPERTY()
	float TimeRemaining = 0.0f;
};

USTRUCT()
struct FMetaAgentHUDStatusLine
{
	GENERATED_BODY()

	UPROPERTY()
	FName Key = NAME_None;

	UPROPERTY()
	FString Text;

	UPROPERTY()
	FColor Color = FColor::White;
};

UCLASS()
class AMetaAgentHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	UFUNCTION(BlueprintCallable, Category = "HUD")
	void AddTransientMessage(const FString& Message, FColor Color = FColor::White, float DurationSeconds = 2.0f);

	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetStatusLine(FName Key, const FString& Message, FColor Color = FColor::White);

	UFUNCTION(BlueprintCallable, Category = "HUD")
	void ClearStatusLine(FName Key);

	void SetRuntimePanelVisible(bool bVisible);

	void SetRuntimePanelSections(const TArray<FMetaAgentGUIRuntimeSection>& InSections);

	bool HitTestRuntimePanelAction(float MouseX, float MouseY, FName& OutActionId) const;

private:
	void DrawRuntimePanel();

	UPROPERTY()
	TArray<FMetaAgentHUDMessage> MessageQueue;

	UPROPERTY()
	TArray<FMetaAgentHUDStatusLine> StatusLines;

	UPROPERTY()
	bool bRuntimePanelVisible = false;

	UPROPERTY()
	TArray<FMetaAgentGUIRuntimeSection> RuntimePanelSections;

	TArray<FMetaAgentHUDClickRegion> RuntimeClickRegions;
};
