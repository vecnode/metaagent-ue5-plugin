#include "MetaAgentEditorModeToolkit.h"

#include "EditorModes.h"
#include "EditorModeManager.h"
#include "MetaAgentEditorMode.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MetaAgentEditorModeToolkit"

void FMetaAgentEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	ToolkitWidget =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MetaAgentPanelTitle", "MetaAgent"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("HelloWorldButtonLabel", "Hello World"))
			.OnClicked_Raw(this, &FMetaAgentEditorModeToolkit::HandleHelloWorldClicked)
		];

	FModeToolkit::Init(InitToolkitHost);
}

FName FMetaAgentEditorModeToolkit::GetToolkitFName() const
{
	return FName("MetaAgentEditorMode");
}

FText FMetaAgentEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitDisplayName", "MetaAgent");
}

FEdMode* FMetaAgentEditorModeToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode(FMetaAgentEditorMode::EM_MetaAgentEditorModeId);
}

TSharedPtr<SWidget> FMetaAgentEditorModeToolkit::GetInlineContent() const
{
	return ToolkitWidget;
}

FReply FMetaAgentEditorModeToolkit::HandleHelloWorldClicked()
{
	UE_LOG(LogTemp, Log, TEXT("Hello World"));

	FNotificationInfo NotificationInfo(LOCTEXT("HelloWorldNotification", "Hello World"));
	NotificationInfo.bFireAndForget = true;
	NotificationInfo.ExpireDuration = 2.0f;
	NotificationInfo.bUseSuccessFailIcons = false;

	FSlateNotificationManager::Get().AddNotification(NotificationInfo);

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
