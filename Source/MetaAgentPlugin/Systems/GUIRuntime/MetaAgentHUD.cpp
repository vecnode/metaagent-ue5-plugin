// Copyright (c) vecnode 2026. All Rights Reserved.

#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

void AMetaAgentHUD::SetHelpPanelVisible(const bool bVisible)
{
	bHelpPanelVisible = bVisible;
}

void AMetaAgentHUD::SetHelpPanelLines(const TArray<FString>& InLines)
{
	HelpPanelLines = InLines;
}

void AMetaAgentHUD::SetStatusLine(FName Key, const FString& Message, FColor Color)
{
	if (Key.IsNone())
	{
		return;
	}

	if (Message.IsEmpty())
	{
		ClearStatusLine(Key);
		return;
	}

	for (FMetaAgentHUDStatusLine& Line : StatusLines)
	{
		if (Line.Key == Key)
		{
			Line.Text = Message;
			Line.Color = Color;
			return;
		}
	}

	FMetaAgentHUDStatusLine& NewLine = StatusLines.AddDefaulted_GetRef();
	NewLine.Key = Key;
	NewLine.Text = Message;
	NewLine.Color = Color;
}

void AMetaAgentHUD::ClearStatusLine(FName Key)
{
	if (Key.IsNone())
	{
		return;
	}

	for (int32 Index = StatusLines.Num() - 1; Index >= 0; --Index)
	{
		if (StatusLines[Index].Key == Key)
		{
			StatusLines.RemoveAt(Index);
		}
	}
}

void AMetaAgentHUD::AddTransientMessage(const FString& Message, FColor Color, float DurationSeconds)
{
	if (Message.IsEmpty())
	{
		return;
	}

	FMetaAgentHUDMessage& Entry = MessageQueue.AddDefaulted_GetRef();
	Entry.Text = Message;
	Entry.Color = Color;
	Entry.TimeRemaining = FMath::Max(DurationSeconds, 0.1f);
}

void AMetaAgentHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const float DeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;

	for (int32 Index = MessageQueue.Num() - 1; Index >= 0; --Index)
	{
		MessageQueue[Index].TimeRemaining -= DeltaSeconds;
		if (MessageQueue[Index].TimeRemaining <= 0.0f)
		{
			MessageQueue.RemoveAt(Index);
		}
	}

	float DrawY = 60.0f;
	for (const FMetaAgentHUDMessage& Message : MessageQueue)
	{
		if (!Message.Text.IsEmpty())
		{
			DrawText(Message.Text, Message.Color, 40.0f, DrawY, GEngine ? GEngine->GetSmallFont() : nullptr, 1.2f, false);
			DrawY += 22.0f;
		}
	}

	if (StatusLines.Num() > 0)
	{
		const UFont* StatusFont = GEngine ? GEngine->GetSmallFont() : nullptr;
		const float StatusScale = 1.0f;
		const float PanelPadding = 8.0f;
		const float PanelLineHeight = 20.0f;
		const float PanelMinWidth = 320.0f;

		float MaxTextWidth = 0.0f;
		for (const FMetaAgentHUDStatusLine& Line : StatusLines)
		{
			float SizeX = 0.0f;
			float SizeY = 0.0f;
			Canvas->StrLen(StatusFont, Line.Text, SizeX, SizeY);
			MaxTextWidth = FMath::Max(MaxTextWidth, SizeX * StatusScale);
		}

		const float PanelWidth = FMath::Max(PanelMinWidth, MaxTextWidth + (PanelPadding * 2.0f));
		const float PanelHeight = (StatusLines.Num() * PanelLineHeight) + (PanelPadding * 2.0f);
		const float PanelX = Canvas->ClipX - PanelWidth - 24.0f;
		const float PanelY = 24.0f;

		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), PanelX, PanelY, PanelWidth, PanelHeight);

		float StatusY = PanelY + PanelPadding;
		for (const FMetaAgentHUDStatusLine& Line : StatusLines)
		{
			DrawText(Line.Text, Line.Color, PanelX + PanelPadding, StatusY, const_cast<UFont*>(StatusFont), StatusScale, false);
			StatusY += PanelLineHeight;
		}
	}

	if (!bHelpPanelVisible || HelpPanelLines.Num() == 0)
	{
		return;
	}

	const UFont* HelpFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	const float HelpScale = 1.0f;
	const float HelpPadding = 10.0f;
	const float HelpLineHeight = 20.0f;
	const FString HelpTitle = TEXT("MetaAgent Controls (H to Hide)");

	float HelpMaxTextWidth = 0.0f;
	float HelpTitleWidth = 0.0f;
	float HelpTitleHeight = 0.0f;
	Canvas->StrLen(HelpFont, HelpTitle, HelpTitleWidth, HelpTitleHeight);
	HelpMaxTextWidth = HelpTitleWidth * HelpScale;

	for (const FString& Line : HelpPanelLines)
	{
		float LineWidth = 0.0f;
		float LineHeight = 0.0f;
		Canvas->StrLen(HelpFont, Line, LineWidth, LineHeight);
		HelpMaxTextWidth = FMath::Max(HelpMaxTextWidth, LineWidth * HelpScale);
	}

	const float HelpPanelWidth = FMath::Max(420.0f, HelpMaxTextWidth + (HelpPadding * 2.0f));
	const float HelpPanelHeight = ((HelpPanelLines.Num() + 1) * HelpLineHeight) + (HelpPadding * 2.0f);
	const float HelpPanelX = 24.0f;
	const float HelpPanelY = 24.0f;

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.52f), HelpPanelX, HelpPanelY, HelpPanelWidth, HelpPanelHeight);
	DrawText(HelpTitle, FColor::Cyan, HelpPanelX + HelpPadding, HelpPanelY + HelpPadding, const_cast<UFont*>(HelpFont), HelpScale, false);

	float HelpY = HelpPanelY + HelpPadding + HelpLineHeight;
	for (const FString& Line : HelpPanelLines)
	{
		DrawText(Line, FColor::White, HelpPanelX + HelpPadding, HelpY, const_cast<UFont*>(HelpFont), HelpScale, false);
		HelpY += HelpLineHeight;
	}
}

