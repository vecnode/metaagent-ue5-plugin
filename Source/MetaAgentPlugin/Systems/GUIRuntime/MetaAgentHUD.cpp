// Copyright (c) vecnode 2026. All Rights Reserved.

#include "Systems/GUIRuntime/MetaAgentHUD.h"
#include "Systems/GUIRuntime/MetaAgentRuntimePanelTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

namespace
{
	constexpr float PanelX = 24.0f;
	constexpr float PanelY = 24.0f;
	constexpr float PanelPadding = 10.0f;
	constexpr float SectionGap = 8.0f;
	constexpr float HeaderHeight = 24.0f;
	constexpr float RowHeight = 22.0f;
	constexpr float StatusLineHeight = 18.0f;
	constexpr float KeyColumnWidth = 72.0f;
	constexpr float ToggleButtonWidth = 72.0f;
	constexpr float CollapseButtonWidth = 24.0f;
	constexpr float PanelMinWidth = 460.0f;
	constexpr float TextScale = 1.0f;

	void AddClickRegion(
		TArray<FMetaAgentHUDClickRegion>& Regions,
		const FName ActionId,
		const float X,
		const float Y,
		const float W,
		const float H)
	{
		if (ActionId.IsNone() || W <= 0.0f || H <= 0.0f)
		{
			return;
		}

		FMetaAgentHUDClickRegion Region;
		Region.ActionId = ActionId;
		Region.X = X;
		Region.Y = Y;
		Region.W = W;
		Region.H = H;
		Regions.Add(Region);
	}

	float MeasureTextWidth(UCanvas* Canvas, const UFont* Font, const FString& Text)
	{
		float SizeX = 0.0f;
		float SizeY = 0.0f;
		Canvas->StrLen(Font, Text, SizeX, SizeY);
		return SizeX * TextScale;
	}
}

void AMetaAgentHUD::SetRuntimePanelVisible(const bool bVisible)
{
	bRuntimePanelVisible = bVisible;
}

void AMetaAgentHUD::SetRuntimePanelSections(const TArray<FMetaAgentGUIRuntimeSection>& InSections)
{
	RuntimePanelSections = InSections;
}

bool AMetaAgentHUD::HitTestRuntimePanelAction(const float MouseX, const float MouseY, FName& OutActionId) const
{
	for (int32 Index = RuntimeClickRegions.Num() - 1; Index >= 0; --Index)
	{
		const FMetaAgentHUDClickRegion& Region = RuntimeClickRegions[Index];
		if (Region.Contains(MouseX, MouseY))
		{
			OutActionId = Region.ActionId;
			return true;
		}
	}

	OutActionId = NAME_None;
	return false;
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

void AMetaAgentHUD::DrawRuntimePanel()
{
	if (!bRuntimePanelVisible || RuntimePanelSections.Num() == 0 || !Canvas)
	{
		RuntimeClickRegions.Reset();
		return;
	}

	const UFont* PanelFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	RuntimeClickRegions.Reset();

	const FString PanelTitle = TEXT("MetaAgent Controls (Q to hide, click rows)");

	float MaxContentWidth = MeasureTextWidth(Canvas, PanelFont, PanelTitle);
	for (const FMetaAgentGUIRuntimeSection& Section : RuntimePanelSections)
	{
		MaxContentWidth = FMath::Max(
			MaxContentWidth,
			CollapseButtonWidth + MeasureTextWidth(Canvas, PanelFont, Section.Title) + ToggleButtonWidth + 24.0f);

		if (!Section.bSectionExpanded)
		{
			continue;
		}

		for (const FMetaAgentGUIActionRow& Row : Section.ActionRows)
		{
			const FString RowText = FString::Printf(TEXT("%s  %s"), *Row.KeyLabel, *Row.Description);
			MaxContentWidth = FMath::Max(MaxContentWidth, KeyColumnWidth + MeasureTextWidth(Canvas, PanelFont, RowText));
		}
		for (const FString& StatusLine : Section.StatusLines)
		{
			MaxContentWidth = FMath::Max(MaxContentWidth, MeasureTextWidth(Canvas, PanelFont, StatusLine));
		}
	}

	const float PanelWidth = FMath::Max(PanelMinWidth, MaxContentWidth + (PanelPadding * 2.0f));

	float TotalHeight = PanelPadding + HeaderHeight;
	for (const FMetaAgentGUIRuntimeSection& Section : RuntimePanelSections)
	{
		TotalHeight += HeaderHeight + SectionGap;
		if (Section.bSectionExpanded)
		{
			TotalHeight += Section.ActionRows.Num() * RowHeight;
			TotalHeight += Section.StatusLines.Num() * StatusLineHeight;
		}
		TotalHeight += SectionGap;
	}
	TotalHeight += PanelPadding;

	const float PanelHeight = FMath::Min(TotalHeight, Canvas->ClipY - (PanelY * 2.0f));

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f), PanelX, PanelY, PanelWidth, PanelHeight);
	DrawText(PanelTitle, FColor::Cyan, PanelX + PanelPadding, PanelY + PanelPadding, const_cast<UFont*>(PanelFont), TextScale, false);

	float DrawY = PanelY + PanelPadding + HeaderHeight;

	for (const FMetaAgentGUIRuntimeSection& Section : RuntimePanelSections)
	{
		const FColor HeaderColor = Section.bRuntimeEnabled ? FColor::Cyan : FColor(160, 160, 160);
		const bool bHasSectionBody = Section.ActionRows.Num() > 0 || Section.StatusLines.Num() > 0;
		const float CollapseX = PanelX + PanelPadding;
		const float CollapseY = DrawY - 2.0f;
		const FString CollapseLabel = Section.bSectionExpanded ? TEXT("v") : TEXT(">");

		if (bHasSectionBody)
		{
			DrawRect(
				FLinearColor(0.18f, 0.18f, 0.22f, 0.82f),
				CollapseX,
				CollapseY,
				CollapseButtonWidth,
				HeaderHeight - 2.0f);
			DrawText(
				CollapseLabel,
				FColor::White,
				CollapseX + 8.0f,
				DrawY,
				const_cast<UFont*>(PanelFont),
				TextScale,
				false);
			AddClickRegion(
				RuntimeClickRegions,
				MakeSectionExpandActionId(Section.RuntimeId),
				CollapseX,
				CollapseY,
				CollapseButtonWidth,
				HeaderHeight - 2.0f);
		}

		DrawText(
			Section.Title,
			HeaderColor,
			PanelX + PanelPadding + (bHasSectionBody ? (CollapseButtonWidth + 6.0f) : 0.0f),
			DrawY,
			const_cast<UFont*>(PanelFont),
			TextScale,
			false);

		if (!Section.bRuntimeAlwaysOn)
		{
			const FString ToggleLabel = Section.bRuntimeEnabled ? TEXT("STOP") : TEXT("START");
			const float ToggleX = PanelX + PanelWidth - PanelPadding - ToggleButtonWidth;
			const float ToggleY = DrawY - 2.0f;
			DrawRect(
				Section.bRuntimeEnabled ? FLinearColor(0.45f, 0.12f, 0.12f, 0.85f) : FLinearColor(0.12f, 0.35f, 0.12f, 0.85f),
				ToggleX,
				ToggleY,
				ToggleButtonWidth,
				HeaderHeight - 2.0f);
			DrawText(
				ToggleLabel,
				FColor::White,
				ToggleX + 14.0f,
				DrawY,
				const_cast<UFont*>(PanelFont),
				TextScale,
				false);

			const FName ToggleActionId = FName(*FString::Printf(TEXT("ToggleRuntime_%s"), *Section.RuntimeId.ToString()));
			AddClickRegion(RuntimeClickRegions, ToggleActionId, ToggleX, ToggleY, ToggleButtonWidth, HeaderHeight - 2.0f);
		}
		else
		{
			const float AlwaysOnX = PanelX + PanelWidth - PanelPadding - ToggleButtonWidth;
			DrawText(TEXT("ALWAYS ON"), FColor::Silver, AlwaysOnX, DrawY, const_cast<UFont*>(PanelFont), TextScale, false);
		}

		DrawY += HeaderHeight;

		if (!Section.bSectionExpanded)
		{
			DrawY += SectionGap;
			continue;
		}

		for (const FMetaAgentGUIActionRow& Row : Section.ActionRows)
		{
			const bool bRowEnabled = Section.bRuntimeEnabled && !Row.ActionId.IsNone();
			const float RowX = PanelX + PanelPadding;
			const float RowW = PanelWidth - (PanelPadding * 2.0f);

			DrawRect(
				bRowEnabled ? FLinearColor(0.14f, 0.14f, 0.18f, 0.72f) : FLinearColor(0.10f, 0.10f, 0.10f, 0.45f),
				RowX,
				DrawY,
				RowW,
				RowHeight - 2.0f);

			DrawText(
				Row.KeyLabel,
				bRowEnabled ? FColor::Yellow : FColor(120, 120, 120),
				RowX + 6.0f,
				DrawY + 2.0f,
				const_cast<UFont*>(PanelFont),
				TextScale,
				false);
			DrawText(
				Row.Description,
				bRowEnabled ? FColor::White : FColor(120, 120, 120),
				RowX + KeyColumnWidth,
				DrawY + 2.0f,
				const_cast<UFont*>(PanelFont),
				TextScale,
				false);

			if (bRowEnabled)
			{
				AddClickRegion(RuntimeClickRegions, Row.ActionId, RowX, DrawY, RowW, RowHeight - 2.0f);
			}

			DrawY += RowHeight;
		}

		for (const FString& StatusLine : Section.StatusLines)
		{
			DrawText(StatusLine, FColor(180, 220, 255), PanelX + PanelPadding + 4.0f, DrawY, const_cast<UFont*>(PanelFont), TextScale, false);
			DrawY += StatusLineHeight;
		}

		DrawY += SectionGap;
	}
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
		const float StatusPanelPadding = 8.0f;
		const float PanelLineHeight = 20.0f;
		const float StatusPanelMinWidth = 320.0f;

		float MaxTextWidth = 0.0f;
		for (const FMetaAgentHUDStatusLine& Line : StatusLines)
		{
			float SizeX = 0.0f;
			float SizeY = 0.0f;
			Canvas->StrLen(StatusFont, Line.Text, SizeX, SizeY);
			MaxTextWidth = FMath::Max(MaxTextWidth, SizeX * StatusScale);
		}

		const float StatusPanelWidth = FMath::Max(StatusPanelMinWidth, MaxTextWidth + (StatusPanelPadding * 2.0f));
		const float StatusPanelHeight = (StatusLines.Num() * PanelLineHeight) + (StatusPanelPadding * 2.0f);
		const float StatusPanelX = Canvas->ClipX - StatusPanelWidth - 24.0f;
		const float StatusPanelY = 24.0f;

		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), StatusPanelX, StatusPanelY, StatusPanelWidth, StatusPanelHeight);

		float StatusY = StatusPanelY + StatusPanelPadding;
		for (const FMetaAgentHUDStatusLine& Line : StatusLines)
		{
			DrawText(Line.Text, Line.Color, StatusPanelX + StatusPanelPadding, StatusY, const_cast<UFont*>(StatusFont), StatusScale, false);
			StatusY += PanelLineHeight;
		}
	}

	DrawRuntimePanel();
}
