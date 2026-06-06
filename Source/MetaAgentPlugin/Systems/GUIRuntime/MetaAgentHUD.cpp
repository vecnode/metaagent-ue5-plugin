// Copyright (c) vecnode 2026. All Rights Reserved.

#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Systems/NetworkingRuntime/MetaAgentGameInstance.h"

void AMetaAgentHUD::SetHelpPanelVisible(const bool bVisible)
{
	bHelpPanelVisible = bVisible;
}

void AMetaAgentHUD::SetHelpPanelLines(const TArray<FString>& InLines)
{
	HelpPanelLines = InLines;
}

void AMetaAgentHUD::SetNetworkingPanelVisible(const bool bVisible)
{
	bNetworkingPanelVisible = bVisible;
}

void AMetaAgentHUD::SetNetworkingPanelLines(const TArray<FString>& InLines)
{
	NetworkingPanelLines = InLines;
}

void AMetaAgentHUD::SetRecordingPanelVisible(const bool bVisible)
{
	bRecordingPanelVisible = bVisible;
}

void AMetaAgentHUD::SetRecordingPanelLines(const TArray<FString>& InLines)
{
	RecordingPanelLines = InLines;
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

	if (!bNetworkingPanelVisible)
	{
		return;
	}

	TArray<FString> EffectiveNetworkingLines = NetworkingPanelLines;
	if (const UMetaAgentGameInstance* GI = UMetaAgentGameInstance::Get(this))
	{
		EffectiveNetworkingLines = GI->GetNetworkingRuntimePanelLines();
	}

	if (EffectiveNetworkingLines.Num() == 0)
	{
		return;
	}

	const UFont* NetFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	const float NetScale = 1.0f;
	const float NetPadding = 10.0f;
	const float NetLineHeight = 20.0f;

	float NetMaxTextWidth = 0.0f;
	for (const FString& Line : EffectiveNetworkingLines)
	{
		float LineWidth = 0.0f;
		float LineHeight = 0.0f;
		Canvas->StrLen(NetFont, Line, LineWidth, LineHeight);
		NetMaxTextWidth = FMath::Max(NetMaxTextWidth, LineWidth * NetScale);
	}

	const float NetPanelWidth = FMath::Max(420.0f, NetMaxTextWidth + (NetPadding * 2.0f));
	const float NetPanelHeight = (EffectiveNetworkingLines.Num() * NetLineHeight) + (NetPadding * 2.0f);
	const float NetPanelX = 24.0f;
	const float DesiredNetPanelY = HelpPanelY + HelpPanelHeight + 12.0f;
	const float NetPanelY = FMath::Min(DesiredNetPanelY, Canvas->ClipY - NetPanelHeight - 24.0f);

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f), NetPanelX, NetPanelY, NetPanelWidth, NetPanelHeight);

	float NetY = NetPanelY + NetPadding;
	for (int32 Index = 0; Index < EffectiveNetworkingLines.Num(); ++Index)
	{
		const FColor LineColor = (Index == 0) ? FColor::Cyan : FColor::White;
		DrawText(EffectiveNetworkingLines[Index], LineColor, NetPanelX + NetPadding, NetY, const_cast<UFont*>(NetFont), NetScale, false);
		NetY += NetLineHeight;
	}

	if (!bRecordingPanelVisible || RecordingPanelLines.Num() == 0)
	{
		return;
	}

	const UFont* RecFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	const float RecScale = 1.0f;
	const float RecPadding = 10.0f;
	const float RecLineHeight = 20.0f;

	float RecMaxTextWidth = 0.0f;
	for (const FString& Line : RecordingPanelLines)
	{
		float LineWidth = 0.0f;
		float LineHeight = 0.0f;
		Canvas->StrLen(RecFont, Line, LineWidth, LineHeight);
		RecMaxTextWidth = FMath::Max(RecMaxTextWidth, LineWidth * RecScale);
	}

	const float RecPanelWidth = FMath::Max(420.0f, RecMaxTextWidth + (RecPadding * 2.0f));
	const float RecPanelHeight = (RecordingPanelLines.Num() * RecLineHeight) + (RecPadding * 2.0f);
	const float RecPanelX = 24.0f;
	const float DesiredRecPanelY = NetPanelY + NetPanelHeight + 12.0f;
	const float RecPanelY = FMath::Min(DesiredRecPanelY, Canvas->ClipY - RecPanelHeight - 24.0f);

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f), RecPanelX, RecPanelY, RecPanelWidth, RecPanelHeight);

	float RecY = RecPanelY + RecPadding;
	for (int32 Index = 0; Index < RecordingPanelLines.Num(); ++Index)
	{
		const FColor LineColor = (Index == 0) ? FColor::Green : FColor::White;
		DrawText(RecordingPanelLines[Index], LineColor, RecPanelX + RecPadding, RecY, const_cast<UFont*>(RecFont), RecScale, false);
		RecY += RecLineHeight;
	}
}

