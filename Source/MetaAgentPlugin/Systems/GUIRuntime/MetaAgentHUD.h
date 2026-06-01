// Copyright (c) vecnode 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
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

	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetHelpPanelVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetHelpPanelLines(const TArray<FString>& InLines);

	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetNetworkingPanelVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetNetworkingPanelLines(const TArray<FString>& InLines);

private:
	UPROPERTY()
	TArray<FMetaAgentHUDMessage> MessageQueue;

	UPROPERTY()
	TArray<FMetaAgentHUDStatusLine> StatusLines;

	UPROPERTY()
	bool bHelpPanelVisible = false;

	UPROPERTY()
	TArray<FString> HelpPanelLines;

	UPROPERTY()
	bool bNetworkingPanelVisible = false;

	UPROPERTY()
	TArray<FString> NetworkingPanelLines;
};

